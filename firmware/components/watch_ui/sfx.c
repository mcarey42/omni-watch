/* Sound effects through the ES8311 (sfx.h; spec §10, Stage 3 Task 13). */
#include "sfx.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "bsp/esp-bsp.h"
#include "assets.h"

static const char *TAG = "sfx";

#define SFX_RATE 16000
#define SFX_CHUNK 512        /* samples per write, read straight from the flash mapping */
#define SFX_DMA_DESC 3
#define SFX_DMA_FRAMES 256   /* 3 x 256 frames: 48 ms of DMA */
#define SFX_FADE 80          /* 5 ms: a cut or a stop fades the current effect out over this */
#define SFX_IDLE_MS 2000     /* the codec (and with it the I2S channel) closes after this long idle */
#define SFX_STACK 6144     /* PSRAM; 4096 left 876 B free after the codec's creation */
#define SFX_PRIO 3

typedef enum { OP_PLAY, OP_STOP, OP_RELEASE, OP_SEQ } op_t;   /* OP_SEQ: a sequence is pending (Task 16) */
typedef struct {
    op_t op;
    const char *name;   /* the asset's (a play's) */
    const int16_t *pcm;
    uint32_t n;
} cmd_t;

/* Settings and the block: written by the event loop (sfx_config, sfx_set_blocked), read anywhere. */
static volatile bool s_on, s_blocked;
static volatile uint8_t s_vol;
static bool s_configured;   /* set (release) by the first sfx_config, after s_on and s_vol: the mirror is real */

/* Created once, lazily (C16); s_task is published last, so a non-NULL s_task means the queue and lock exist. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_creating;
static bool s_task_failed;   /* latched: a failed creation is logged once and never retried (no churn per play) */
static QueueHandle_t s_q;
static esp_pm_lock_handle_t s_pm;
static TaskHandle_t volatile s_task;

/* The sfx task's own (created and deleted there). */
static i2s_chan_handle_t s_tx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_es8311;
static esp_codec_dev_handle_t s_codec;
static bool s_open;
static int64_t s_idle_deadline;   /* open: the codec closes at this esp_timer us (2 s after the last effect's end) */
static uint8_t s_applied_vol;

/* Stats (the console's). The counters are bumped from several tasks: atomic increments (T13 M8). The rest are single
 * words written by the sfx task only, read racily. */
#define STAT_INC(x) __atomic_fetch_add(&(x), 1u, __ATOMIC_RELAXED)
#define STAT_GET(x) __atomic_load_n(&(x), __ATOMIC_RELAXED)
static uint32_t s_plays, s_cuts, s_stops, s_dropped, s_rejected, s_failures;
static volatile bool s_playing, s_pm_held;
static volatile uint32_t s_last_write_dur;
static volatile int64_t s_last_write, s_pa_low, s_pm_release, s_close;
static volatile int64_t s_effect_t0;   /* the current (last) effect's first write */
static volatile uint32_t s_effect_n;   /* its samples written */
static int32_t s_ram_task = -1, s_ram_codec = -1;

/* Play sequences (Task 16). Synchronisation:
 *  - sfx_play_seq (any task; the chime calls it on the esp_timer task) resolves the names into s_seq->stage (PSRAM,
 *    behind an atomic try-flag: no seq_t on the caller's stack, and it logs nothing there), then under s_mux copies it
 *    into s_seq->next and sets pending, and posts OP_SEQ (a wake only: the queue never carries a
 *    sequence, so its depth and its internal RAM are unchanged). The sfx task, under s_mux, copies next into play (its
 *    own copy) and clears pending. A newer sequence overwrites next; the task takes whichever is newest.
 *  - s_seq (PSRAM, the two copies) is allocated by the first sfx_play_seq and published once (CAS); s_seq_run, the
 *    player, is set there too, so the task reaches the sequence code only through that pointer: a build that never
 *    calls sfx_play_seq (the Omni) links none of it.
 *  - keep: s_keep_until (ms, wrapping, 0 none; atomic) is set by sfx_play_seq before the post, to the sequence's end
 *    plus SFX_KEEP_SLACK_MS. While it is ahead, sound on and not blocked, sfx_stop posts nothing and sfx_play_asset is
 *    dropped (counted), so neither can supersede a pending kept sequence in the newest-wins queue. A STOP that raced
 *    the keep's store anyway (sfx_stop read it just before) and coalesced the OP_SEQ away is caught where it lands: the
 *    idle task, an effect that the STOP ended (play() returns OP_STOP), a sequence without keep that it ended and a
 *    playing kept sequence all try seq_take for it, which takes a kept sequence still in its window. A play that raced
 *    the keep the same way is dropped where it lands (seq_bump at the idle task; seq_play while a kept one plays) and
 *    counted once, as dropped. The task clears it
 *    (compare-and-swap with the value its sequence set, so a newer sequence's is never cleared) when the sequence
 *    ends; a sequence never played leaves it to expire. Sound off, volume 0 and a block are levels the task re-reads
 *    before every write, as before, so they still cut a kept sequence.
 *  - s_gain is the sfx task's own: the playing sequence's gain_pct (100 otherwise, so a single effect plays at s_vol
 *    exactly as before). */
#define SFX_KEEP_SLACK_MS 1000   /* the task's start and the codec's open, before the first sample */
typedef struct {
    const int16_t *pcm;
    uint32_t n;      /* samples */
    uint32_t at;     /* the step's start, in samples from the sequence's start */
    const char *name;
} seq_step_t;
typedef struct {
    uint8_t n, gain;
    bool keep;
    uint32_t keep_until;   /* the s_keep_until this sequence set (0: none) */
    seq_step_t step[SFX_SEQ_MAX];
} seq_t;
typedef struct {
    bool pending;
    bool staging;       /* atomic: an sfx_play_seq is filling stage (a concurrent one is dropped, counted) */
    seq_t next, play;   /* next: the newest posted (under s_mux); play: the sfx task's copy */
    seq_t stage;        /* sfx_play_seq's, off the caller's stack (the esp_timer task's is small) */
} seq_buf_t;
static seq_buf_t *s_seq;                       /* atomic; NULL until the first sfx_play_seq */
static bool (*s_seq_run)(cmd_t *c);    /* atomic: the sequence player; NULL until the first sfx_play_seq */
static bool (*s_seq_bump)(bool drop);  /* atomic: a kept sequence outranks a racing play; NULL as s_seq_run */
/* The hooks are read afresh (acquire) wherever they are used, never held across a blocking call: the first
 * sfx_play_seq since boot can set them while an effect plays (fix round 2). */
#define SEQ_RUN() __atomic_load_n(&s_seq_run, __ATOMIC_ACQUIRE)
#define SEQ_BUMP() __atomic_load_n(&s_seq_bump, __ATOMIC_ACQUIRE)
static uint32_t s_keep_until;                  /* atomic */
static uint8_t s_gain = 100;                   /* the sfx task's */

static uint32_t now_ms(void)
{
    const uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    return t ? t : 1;   /* 0 means none */
}

/* a kept sequence is pending or playing (its window is ahead) */
static bool keep_active(void)
{
    const uint32_t k = __atomic_load_n(&s_keep_until, __ATOMIC_ACQUIRE);
    return k != 0 && (int32_t)(k - now_ms()) > 0;
}

static size_t internal_free(void) { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }

/* ------------------------------------------------------------ the codec (sfx task only) */
/* The codec as bsp_audio_codec_speaker_init builds it, on our own TX-only channel: the BSP's I2C bus, es8311 with the
 * amp on BSP_POWER_AMP_IO. */
static bool codec_create(void)
{
    if (s_codec) return true;
    const size_t f0 = internal_free();
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    cc.dma_desc_num = SFX_DMA_DESC;
    cc.dma_frame_num = SFX_DMA_FRAMES;
    cc.auto_clear = true;   /* silence when the writes stop */
    const i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SFX_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = BSP_I2S_LCLK, .dout = BSP_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    esp_err_t err = i2s_new_channel(&cc, &s_tx, NULL);
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_tx, &std);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel: %s", esp_err_to_name(err));
        goto fail;
    }
    audio_codec_i2s_cfg_t ic = {.port = CONFIG_BSP_I2S_NUM, .tx_handle = s_tx, .rx_handle = NULL};
    s_data_if = audio_codec_new_i2s_data(&ic);
    s_gpio_if = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t i2c = {.port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR,
                                       .bus_handle = bsp_i2c_get_handle()};
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c);
    if (!s_data_if || !s_gpio_if || !s_ctrl_if) goto fail;
    es8311_codec_cfg_t es = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    s_es8311 = es8311_codec_new(&es);
    if (!s_es8311) goto fail;
    esp_codec_dev_cfg_t dc = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_es8311, .data_if = s_data_if};
    s_codec = esp_codec_dev_new(&dc);
    if (!s_codec) goto fail;
    s_ram_codec = (int32_t)(f0 - internal_free());
    ESP_LOGI(TAG, "codec ready: channel and codec took %ld B internal", (long)s_ram_codec);
    return true;
fail:
    ESP_LOGE(TAG, "codec not created");
    if (s_es8311) audio_codec_delete_codec_if(s_es8311);
    if (s_ctrl_if) audio_codec_delete_ctrl_if(s_ctrl_if);
    if (s_gpio_if) audio_codec_delete_gpio_if(s_gpio_if);
    if (s_data_if) audio_codec_delete_data_if(s_data_if);
    if (s_tx) i2s_del_channel(s_tx);
    s_es8311 = NULL;
    s_ctrl_if = NULL;
    s_gpio_if = NULL;
    s_data_if = NULL;
    s_tx = NULL;
    return false;
}

static void pa(bool on)
{
    gpio_set_level(BSP_POWER_AMP_IO, on);
    if (!on) s_pa_low = esp_timer_get_time();
}

static bool codec_open(void)
{
    if (s_open) {
        pa(true);   /* still open from an effect less than 2 s ago: only the amp went low */
        return true;
    }
    esp_codec_dev_sample_info_t fs = {.sample_rate = SFX_RATE, .channel = 1, .bits_per_sample = 16};
    /* esp_codec_dev_open disables the channel before it sets the format (an error log on a channel never enabled), then
     * enables it, the codec and the amp: enabled here first, so that disable has something to do */
    i2s_channel_enable(s_tx);
    if (esp_codec_dev_open(s_codec, &fs) != ESP_CODEC_DEV_OK) {
        i2s_channel_disable(s_tx);
        ESP_LOGE(TAG, "codec open failed");
        return false;
    }
    s_open = true;
    s_applied_vol = 255;
    return true;
}

/* The 2 s idle close: the codec off with its amp low (esp_codec_dev_close), and the I2S channel disabled, which releases
 * the I2S driver's PM lock. esp_codec_dev_close disables the channel itself (its I2S data interface's enable(false) ->
 * i2s_channel_disable), so a second i2s_channel_disable here would only fail (INVALID_STATE) and log an error. */
static void codec_close(void)
{
    if (!s_open) return;
    esp_codec_dev_close(s_codec);
    s_open = false;
    s_close = esp_timer_get_time();
}

/* A block (provisioning): the channel and the codec are deleted, so their internal RAM is back for BLE and Wi-Fi. */
static void codec_release(void)
{
    codec_close();
    if (!s_codec) return;
    esp_codec_dev_delete(s_codec);
    audio_codec_delete_codec_if(s_es8311);
    audio_codec_delete_ctrl_if(s_ctrl_if);
    audio_codec_delete_gpio_if(s_gpio_if);
    audio_codec_delete_data_if(s_data_if);
    i2s_del_channel(s_tx);
    s_codec = NULL;
    s_es8311 = NULL;
    s_ctrl_if = NULL;
    s_gpio_if = NULL;
    s_data_if = NULL;
    s_tx = NULL;
    ESP_LOGI(TAG, "released (blocked)");
}

/* ------------------------------------------------------------ the task */
static bool take_newest(cmd_t *c, TickType_t wait)
{
    if (xQueueReceive(s_q, c, wait) != pdTRUE) return false;
    while (xQueueReceive(s_q, c, 0) == pdTRUE) {}
    return true;
}

static void write_samples(const int16_t *p, uint32_t n)
{
    uint8_t v = s_vol;
    if (s_gain < 100 && v) {   /* a sequence's gain_pct of the set volume (never 0 from a volume above 0) */
        v = (uint8_t)(((unsigned)v * s_gain + 50) / 100);
        if (v == 0) v = 1;
    }
    if (v != s_applied_vol) {   /* a volume change applies at once, between chunks */
        s_applied_vol = v;
        esp_codec_dev_set_out_vol(s_codec, s_applied_vol);
    }
    const int64_t t0 = esp_timer_get_time();
    esp_codec_dev_write(s_codec, (void *)p, (int)(n * sizeof *p));
    s_last_write = esp_timer_get_time();
    s_last_write_dur = (uint32_t)(s_last_write - t0);
    if (s_effect_n == 0) s_effect_t0 = t0;
    s_effect_n += n;
}

/* The next SFX_FADE samples (or what is left) faded to 0: a cut or a stop does not click. */
static void fade_out(const int16_t *p, uint32_t left)
{
    int16_t buf[SFX_FADE];
    const uint32_t n = left < SFX_FADE ? left : SFX_FADE;
    for (uint32_t i = 0; i < n; i++) buf[i] = (int16_t)((int32_t)p[i] * (int32_t)(n - i) / (int32_t)(n + 1));
    if (n) write_samples(buf, n);
}

/* Plays c, and whatever play cuts it, to the end; returns a stop or release that ended it (op OP_PLAY: none). Refuses
 * while blocked (T13 I1: a play that raced the block must not create the codec under the QR), and a block that lands
 * mid-effect ends it after the current write (the task then releases). */
static op_t play(cmd_t c)
{
    if (s_blocked) {
        STAT_INC(s_dropped);
        return OP_RELEASE;
    }
    if (!codec_create()) {
        STAT_INC(s_failures);
        return OP_PLAY;
    }
    esp_pm_lock_acquire(s_pm);   /* held while writing: no light sleep mid-effect */
    s_pm_held = true;
    if (!codec_open()) {
        esp_pm_lock_release(s_pm);
        s_pm_held = false;
        STAT_INC(s_failures);
        return OP_PLAY;
    }
    s_playing = true;
    s_effect_n = 0;
    ESP_LOGI(TAG, "%s (%lu samples)", c.name, (unsigned long)c.n);
    op_t ended = OP_PLAY;
    uint32_t pos = 0;
    while (pos < c.n) {
        cmd_t nc;
        if (s_blocked) {   /* checked before every write: the RELEASE command may have been coalesced away */
            fade_out(c.pcm + pos, c.n - pos);
            ended = OP_RELEASE;
            break;
        }
        if (take_newest(&nc, 0)) {
            fade_out(c.pcm + pos, c.n - pos);
            if (nc.op != OP_PLAY || s_blocked) {
                ended = nc.op == OP_PLAY ? OP_RELEASE : nc.op;
                if (nc.op == OP_STOP) STAT_INC(s_stops);
                break;
            }
            bool (*const bump)(bool) = SEQ_BUMP();   /* NULL in a build without sequences (the Omni) */
            if (bump && bump(true)) {   /* this play raced a kept sequence and coalesced its OP_SEQ away: the */
                ended = OP_SEQ;         /* play is dropped (counted once) and the sequence starts at once */
                break;
            }
            c = nc;   /* a new effect cuts this one */
            pos = 0;
            s_effect_n = 0;
            ESP_LOGI(TAG, "%s (%lu samples), cutting the last", c.name, (unsigned long)c.n);
            STAT_INC(s_cuts);
            continue;
        }
        const uint32_t len = c.n - pos < SFX_CHUNK ? c.n - pos : SFX_CHUNK;
        write_samples(c.pcm + pos, len);
        pos += len;
    }
    /* push the last samples out of the DMA (3 x 256 frames) with silence before the amp goes low */
    int16_t zero[SFX_DMA_FRAMES];
    memset(zero, 0, sizeof zero);
    const int64_t end = s_last_write;
    for (int i = 0; i < SFX_DMA_DESC; i++) esp_codec_dev_write(s_codec, zero, sizeof zero);
    s_last_write = end;
    pa(false);
    s_playing = false;
    esp_pm_lock_release(s_pm);
    s_pm_held = false;
    s_pm_release = esp_timer_get_time();
    s_idle_deadline = s_pm_release + (int64_t)SFX_IDLE_MS * 1000;
    return ended;
}

/* How long to wait for a command: until the idle close is due (an absolute deadline: a stop while idle does not
 * restart it, T13 M2), or for ever when the codec is closed. */
static TickType_t idle_wait(void)
{
    if (!s_open) return portMAX_DELAY;
    const int64_t left_us = s_idle_deadline - esp_timer_get_time();
    return left_us <= 0 ? 0 : pdMS_TO_TICKS((left_us + 999) / 1000);
}

static void sfx_task(void *arg)
{
    (void)arg;
    cmd_t c;
    bool have = false;   /* c is a command a sequence or an effect ended on, still to act on */
    for (;;) {
        if (!have) have = take_newest(&c, idle_wait());
        if (have) {
            have = false;
            /* the hooks are NULL unless sfx_play_seq was called (never in the Omni); read again after play(), which
             * blocks for the effect's length: the first sequence since boot may have set them meanwhile */
            bool (*bump)(bool) = SEQ_BUMP();
            if (c.op == OP_PLAY && bump && bump(true)) {
                c.op = OP_SEQ, have = true;   /* a play that raced a kept sequence coalesced its OP_SEQ away: dropped */
            } else if (c.op == OP_PLAY) {
                /* a sequence cut the effect; or a STOP did, which may have coalesced away a kept sequence's OP_SEQ
                 * (the stop raced sfx_play_seq's keep): run() takes that one and discards any other */
                const op_t e = play(c);
                bool (*const run)(cmd_t *) = SEQ_RUN();
                bump = SEQ_BUMP();
                if (run && (e == OP_SEQ || e == OP_STOP)) c.op = e, have = true;
                else if (bump && bump(false)) c.op = OP_SEQ, have = true;   /* a kept sequence still pending (its
                                                                              * OP_SEQ coalesced away): it plays */
            } else if (c.op != OP_RELEASE) {
                bool (*const run)(cmd_t *) = SEQ_RUN();
                if (run) have = run(&c);   /* OP_SEQ; or a STOP, which a pending kept sequence outlives */
            }
            /* otherwise a STOP or RELEASE only wakes the task */
        }
        if (s_open && esp_timer_get_time() - s_idle_deadline >= 0) codec_close();   /* 2 s idle */
        /* T13 I1: level-triggered, after every wake: whatever ended the effect or was coalesced away, a block never
         * leaves the channel and codec allocated (every block posts a command, and any command wakes the task) */
        if (s_blocked && s_codec) codec_release();
    }
}

/* The task, queue and lock: once, by the first play that is not dropped (any task; a racing second caller drops). */
/* quiet (sfx_play_seq, which may run on the small esp_timer stack): no logging here; the task logs the creation on its
 * first sequence, and a failure stays latched in sfx_debug_stat's task_failed */
static bool s_task_note;   /* the task was created quietly: seq_play logs it */
static bool ensure_task(bool quiet)
{
    if (s_task) return true;
    taskENTER_CRITICAL(&s_mux);
    const bool mine = !s_creating && !s_task_failed;
    s_creating = true;
    taskEXIT_CRITICAL(&s_mux);
    if (!mine) return false;
    const size_t f0 = internal_free();
    TaskHandle_t h = NULL;
    s_q = xQueueCreate(2, sizeof(cmd_t));
    if (s_q && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "sfx", &s_pm) == ESP_OK &&
        xTaskCreatePinnedToCoreWithCaps(sfx_task, "sfx", SFX_STACK, NULL, SFX_PRIO, &h, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM) == pdPASS) {
        s_ram_task = (int32_t)(f0 - internal_free());
        s_task_note = quiet;
        s_task = h;
        if (!quiet) ESP_LOGI(TAG, "task created: task, queue and PM lock took %ld B internal", (long)s_ram_task);
        return true;
    }
    if (!quiet) ESP_LOGE(TAG, "task not created: sound is off until a reboot");   /* once: latched below */
    if (s_pm) esp_pm_lock_delete(s_pm);
    if (s_q) vQueueDelete(s_q);
    s_pm = NULL;
    s_q = NULL;
    taskENTER_CRITICAL(&s_mux);
    s_task_failed = true;
    s_creating = false;
    taskEXIT_CRITICAL(&s_mux);
    return false;
}

/* Depth 2, the newest wins: a full queue drops its oldest command. */
static void post(op_t op, const char *name, const int16_t *pcm, uint32_t n)
{
    if (!s_task) return;
    const cmd_t c = {.op = op, .name = name, .pcm = pcm, .n = n};
    if (xQueueSend(s_q, &c, 0) == pdTRUE) return;
    cmd_t old;
    xQueueReceive(s_q, &old, 0);
    xQueueSend(s_q, &c, 0);
}

/* ------------------------------------------------------------ the API */
void sfx_config(bool on, uint8_t volume)
{
    s_on = on;
    s_vol = volume > 100 ? 100 : volume;
    __atomic_store_n(&s_configured, true, __ATOMIC_RELEASE);
    if (!on || volume == 0) sfx_stop();
}

/* The asset's data and its name as the assets table holds it (the caller's string may not outlive the play). */
static const void *find_pcm(const char *name, const char **stable, size_t *size)
{
    const char *ext = name ? strrchr(name, '.') : NULL;
    if (!ext || strcmp(ext, ".pcm") != 0 || !assets_ready()) return NULL;
    for (int i = 0; i < assets_count(); i++) {
        const void *p;
        const char *n = assets_entry(i, &p, size);
        if (strcmp(n, name) == 0) {
            *stable = n;
            return p;
        }
    }
    return NULL;
}

bool sfx_play_asset(const char *name)
{
    size_t size = 0;
    const void *p = find_pcm(name, &name, &size);
    if (p == NULL || size < 2 || size % 2) {
        STAT_INC(s_rejected);
        return false;
    }
    if (!s_on || s_vol == 0 || s_blocked || keep_active() || !ensure_task(false)) {   /* keep: a kept sequence plays */
        STAT_INC(s_dropped);
        return true;
    }
    STAT_INC(s_plays);
    post(OP_PLAY, name, p, (uint32_t)(size / 2));
    return true;
}

/* A kept sequence outlives the stop (the sleep and glance paths); sfx_config's stop for off or volume 0 is not held
 * back (the levels are already written) and the task cuts the sequence on them. */
void sfx_stop(void)
{
    if (s_on && s_vol != 0 && !s_blocked && keep_active()) return;
    post(OP_STOP, NULL, NULL, 0);
}

void sfx_config_get(bool *on, uint8_t *volume)
{
    /* each is one aligned byte the event loop writes (sfx_config): a reader racing a change may see one field old and
     * the other new for that one read, which no caller acts on for longer than a single decision */
    if (on) *on = s_on;
    if (volume) *volume = s_vol;
}

bool sfx_config_known(void)
{
    return __atomic_load_n(&s_configured, __ATOMIC_ACQUIRE);
}

/* ------------------------------------------------------------ sequences (Task 16) */
static const int16_t SILENCE[SFX_DMA_FRAMES];   /* between steps: the stream (and the amp) stays on */

/* the end of a kept sequence's window, unless a newer sequence has set its own */
static void keep_clear(const seq_t *q)
{
    uint32_t k = q->keep_until;
    if (k) __atomic_compare_exchange_n(&s_keep_until, &k, 0u, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

/* The pending sequence into the task's copy, for the command that woke the task: OP_SEQ takes it; a STOP takes a kept
 * one still in its window and discards any other (the stop won). True if s_seq->play now holds one to play. */
static bool seq_take(op_t why)
{
    seq_buf_t *b = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
    bool ok = false;
    taskENTER_CRITICAL(&s_mux);
    if (b->pending) {
        ok = why == OP_SEQ || (why == OP_STOP && b->next.keep && keep_active());
        if (ok) b->play = b->next;
        b->pending = false;
    }
    taskEXIT_CRITICAL(&s_mux);
    return ok;
}

/* A kept sequence is pending in its window although its OP_SEQ never reached the task: a play raced the keep and
 * newest-wins coalesced the OP_SEQ away. drop: that play has just reached the idle task, and is dropped, counted once
 * (its play count moves to dropped); otherwise it already played (it cut an effect). Either way the task runs the
 * sequence next. */
static bool seq_bump(bool drop)
{
    seq_buf_t *b = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
    taskENTER_CRITICAL(&s_mux);
    const bool kept = b->pending && b->next.keep;
    taskEXIT_CRITICAL(&s_mux);
    if (!kept || !keep_active()) return false;
    if (drop) {
        __atomic_fetch_sub(&s_plays, 1u, __ATOMIC_RELAXED);
        STAT_INC(s_dropped);
    }
    return true;
}

/* Plays the pending sequence (if c's command takes it, seq_take) through to its last step's end; the amp and the PM
 * lock are held throughout, and silence is written between steps. True when it ended on a command the task must still
 * act on (a play or a stop that cut a sequence without keep), left in *c. */
static bool seq_play(cmd_t *c)
{
    if (!seq_take(c->op)) return false;
    seq_t *q = &__atomic_load_n(&s_seq, __ATOMIC_ACQUIRE)->play;
    if (s_blocked || !s_on || s_vol == 0) {   /* changed since the post: the levels win */
        STAT_INC(s_dropped);
        keep_clear(q);
        return false;
    }
    if (!codec_create()) {
        STAT_INC(s_failures);
        keep_clear(q);
        return false;
    }
    esp_pm_lock_acquire(s_pm);   /* held for the whole sequence: no light sleep, no amp pop per step (C10) */
    s_pm_held = true;
    if (!codec_open()) {
        esp_pm_lock_release(s_pm);
        s_pm_held = false;
        STAT_INC(s_failures);
        keep_clear(q);
        return false;
    }
    s_playing = true;
    s_effect_n = 0;
    s_gain = q->gain;
    if (s_task_note) {   /* created quietly by sfx_play_seq: logged here, on this task's stack */
        s_task_note = false;
        ESP_LOGI(TAG, "task created: task, queue and PM lock took %ld B internal", (long)s_ram_task);
    }
    ESP_LOGI(TAG, "sequence: %u steps at %u%%%s", q->n, q->gain, q->keep ? ", kept" : "");
    bool again = false, restart = true;
    uint32_t t = 0, left = 0;   /* t: samples into the sequence; left: of the current step */
    uint8_t i = 0;              /* the next step */
    const int16_t *cur = NULL;
    for (;;) {
        if (restart) t = 0, left = 0, i = 0, cur = NULL, restart = false;
        if (s_blocked || !s_on || s_vol == 0) {   /* before every write, as an effect: a kept sequence too */
            if (left) fade_out(cur, left);
            break;
        }
        cmd_t nc;
        if (take_newest(&nc, 0)) {
            if (nc.op == OP_SEQ) {   /* a new sequence replaces this one */
                if (seq_take(OP_SEQ)) {
                    if (left) fade_out(cur, left);
                    s_gain = q->gain;
                    STAT_INC(s_cuts);
                    ESP_LOGI(TAG, "sequence: %u steps at %u%%%s, replacing the last", q->n, q->gain,
                             q->keep ? ", kept" : "");
                    restart = true;
                }
                continue;
            }
            if (q->keep && nc.op == OP_STOP) {   /* the sleep and glance paths leave it playing */
                /* a STOP that raced a newer kept sequence may have coalesced its OP_SEQ away: take that one */
                if (seq_take(OP_STOP)) {
                    if (left) fade_out(cur, left);
                    s_gain = q->gain;
                    STAT_INC(s_cuts);
                    ESP_LOGI(TAG, "sequence: %u steps at %u%%%s, replacing the last", q->n, q->gain,
                             q->keep ? ", kept" : "");   /* the sfx task's stack */
                    restart = true;
                }
                continue;
            }
            if (q->keep && nc.op == OP_PLAY) {   /* a play that raced the keep: dropped, and counted once (as dropped) */
                __atomic_fetch_sub(&s_plays, 1u, __ATOMIC_RELAXED);
                STAT_INC(s_dropped);
                continue;
            }
            if (left) fade_out(cur, left);
            if (nc.op == OP_PLAY) *c = nc, again = true;   /* a new effect cuts a sequence without keep */
            else if (nc.op == OP_STOP) {   /* back to the task with it: a kept sequence it coalesced away still plays */
                STAT_INC(s_stops);
                *c = nc, again = true;
            }
            break;
        }
        if (i < q->n && t >= q->step[i].at) {   /* the next step's offset: it cuts what is left of the last */
            if (left) {
                const uint32_t f = left < SFX_FADE ? left : SFX_FADE;
                fade_out(cur, left);
                t += f;
            }
            cur = q->step[i].pcm;
            left = q->step[i].n;
            i++;
            continue;
        }
        uint32_t len = SFX_CHUNK;
        if (i < q->n && q->step[i].at - t < len) len = q->step[i].at - t;
        if (left) {
            if (left < len) len = left;
            write_samples(cur, len);
            cur += len;
            left -= len;
        } else if (i < q->n) {
            if (len > SFX_DMA_FRAMES) len = SFX_DMA_FRAMES;
            write_samples(SILENCE, len);
        } else {
            break;   /* the last step's end */
        }
        t += len;
    }
    /* as an effect's end: the last samples out of the DMA with silence, then the amp low and the PM lock released */
    const int64_t end = s_last_write;
    for (int k = 0; k < SFX_DMA_DESC; k++) esp_codec_dev_write(s_codec, (void *)SILENCE, sizeof SILENCE);
    s_last_write = end;
    s_gain = 100;
    s_applied_vol = s_vol;   /* the codec back at the *current* volume */
    esp_codec_dev_set_out_vol(s_codec, s_applied_vol);
    pa(false);
    s_playing = false;
    esp_pm_lock_release(s_pm);
    s_pm_held = false;
    s_pm_release = esp_timer_get_time();
    s_idle_deadline = s_pm_release + (int64_t)SFX_IDLE_MS * 1000;
    keep_clear(q);
    return again;
}

/* the two copies in PSRAM: once, by the first sequence (any task; a racing second caller frees its own) */
static seq_buf_t *seq_buf(void)
{
    seq_buf_t *b = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
    if (b) return b;
    seq_buf_t *mine = heap_caps_calloc(1, sizeof *mine, MALLOC_CAP_SPIRAM);
    if (!mine) return NULL;
    if (__atomic_compare_exchange_n(&s_seq, &b, mine, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return mine;
    heap_caps_free(mine);
    return b;
}

bool sfx_play_seq(const sfx_step_t *steps, uint8_t n, uint8_t gain_pct, bool keep)
{
    if (!steps || n == 0 || n > SFX_SEQ_MAX) {
        STAT_INC(s_rejected);
        return false;
    }
    /* the names first (false for a missing one, whatever the sound), then the sequence into the PSRAM stage: no seq_t
     * on the caller's stack */
    uint32_t end_ms = 0;
    for (uint8_t i = 0; i < n; i++) {
        size_t size = 0;
        const char *name = steps[i].name;
        if (find_pcm(name, &name, &size) == NULL || size < 2 || size % 2) {
            STAT_INC(s_rejected);
            return false;
        }
        const uint32_t e = steps[i].at_ms + (uint32_t)(size / 2) / (SFX_RATE / 1000);
        if (e > end_ms) end_ms = e;
    }
    const uint8_t gain = gain_pct > 100 ? 100 : gain_pct;
    if (gain == 0 || !s_on || s_vol == 0 || s_blocked || !ensure_task(true)) {
        STAT_INC(s_dropped);
        return true;
    }
    seq_buf_t *b = seq_buf();
    if (!b) {
        STAT_INC(s_failures);
        return true;
    }
    if (__atomic_exchange_n(&b->staging, true, __ATOMIC_ACQUIRE)) {   /* another sfx_play_seq right now */
        STAT_INC(s_dropped);
        return true;
    }
    seq_t *q = &b->stage;
    memset(q, 0, sizeof *q);
    for (uint8_t i = 0; i < n; i++) {
        size_t size = 0;
        const char *name = steps[i].name;
        const void *p = find_pcm(name, &name, &size);   /* found above: the mapping never changes */
        q->step[i] = (seq_step_t){p, (uint32_t)(size / 2), (uint32_t)steps[i].at_ms * (SFX_RATE / 1000), name};
    }
    q->n = n;
    q->gain = gain;
    q->keep = keep;
    if (keep) {
        q->keep_until = now_ms() + end_ms + SFX_KEEP_SLACK_MS;
        if (q->keep_until == 0) q->keep_until = 1;
    }
    taskENTER_CRITICAL(&s_mux);
    b->next = *q;
    b->pending = true;
    __atomic_store_n(&s_keep_until, q->keep_until, __ATOMIC_RELEASE);   /* a sequence without keep ends any keep */
    taskEXIT_CRITICAL(&s_mux);
    __atomic_store_n(&b->staging, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_seq_run, seq_play, __ATOMIC_RELEASE);    /* the player first: a bump that finds a kept */
    __atomic_store_n(&s_seq_bump, seq_bump, __ATOMIC_RELEASE);   /* sequence always finds the player to run it */
    STAT_INC(s_plays);
    post(OP_SEQ, NULL, NULL, 0);
    return true;
}

/* The block is a level the task re-reads after every wake and every write (T13 I1); the RELEASE posted here only wakes
 * it, so losing it to newest-wins coalescing is harmless. */
void sfx_set_blocked(bool blocked)
{
    if (blocked == s_blocked) return;
    s_blocked = blocked;
    if (blocked) post(OP_RELEASE, NULL, NULL, 0);
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* The other side of the T13 I1 races, for `sfx race`: a STOP, or a PLAY of an asset that skips the block check (as a play
 * that read s_blocked false just before the block was set would). */
void sfx_debug_post(bool play, const char *name)
{
    size_t size = 0;
    const void *p = play ? find_pcm(name, &name, &size) : NULL;
    if (play && p) post(OP_PLAY, name, p, (uint32_t)(size / 2));
    if (!play) post(OP_STOP, NULL, NULL, 0);
}
#endif

void sfx_debug_stat(sfx_stat_t *o)
{
    memset(o, 0, sizeof *o);
    o->on = s_on;
    o->blocked = s_blocked;
    o->volume = s_vol;
    o->task = s_task != NULL;
    o->queue = s_q != NULL;
    o->pm_lock = s_pm != NULL;
    o->channel = s_tx != NULL;
    o->codec = s_codec != NULL;
    o->open = s_open;
    o->playing = s_playing;
    o->pa = (REG_READ(GPIO_OUT1_REG) >> (BSP_POWER_AMP_IO - 32)) & 1;   /* the pin's output latch (it is output only) */
    o->pm_held = s_pm_held;
    o->task_failed = s_task_failed;
    o->plays = STAT_GET(s_plays);
    o->cuts = STAT_GET(s_cuts);
    o->stops = STAT_GET(s_stops);
    o->dropped = STAT_GET(s_dropped);
    o->rejected = STAT_GET(s_rejected);
    o->failures = STAT_GET(s_failures);
    o->last_write_us = s_last_write;
    o->last_write_dur_us = s_last_write_dur;
    o->effect_samples = s_effect_n;
    o->effect_write_us = s_effect_n ? (uint32_t)(s_last_write - s_effect_t0) : 0;
    o->pa_low_us = s_pa_low;
    o->pm_release_us = s_pm_release;
    o->close_us = s_close;
    o->ram_task_b = s_ram_task;
    o->ram_codec_b = s_ram_codec;
    o->stack_free_b = s_task ? (unsigned)uxTaskGetStackHighWaterMark(s_task) : 0;
}
