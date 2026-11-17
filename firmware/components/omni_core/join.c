#include "omni/join.h"
#include "omni/kb.h"

omni_join_refuse_t omni_join_check(bool read_only, bool busy, bool phone, uint8_t authmode,
                                   uint8_t ssid_len, const char *pass, uint8_t pass_len)
{
    if (read_only) return OMNI_JOIN_REFUSE_READ_ONLY;
    if (busy) return OMNI_JOIN_REFUSE_BUSY;
    if (phone) return OMNI_JOIN_REFUSE_PHONE;
    if (ssid_len == 0 || ssid_len > 32) return OMNI_JOIN_REFUSE_SSID;
    const omni_auth_class_t cls = omni_netlist_auth(authmode);
    if (cls == OMNI_AUTH_UNSUPPORTED) return OMNI_JOIN_REFUSE_AUTH;
    if (pass_len > OMNI_JOIN_PASS_MAX || (pass == NULL && pass_len > 0)) return OMNI_JOIN_REFUSE_PASS;
    if (!omni_kb_pass_ok(pass ? pass : "", pass_len, (uint8_t)cls)) return OMNI_JOIN_REFUSE_PASS;
    return OMNI_JOIN_OK;
}

uint8_t omni_join_threshold(uint8_t m)
{
    switch (m) {
    case OMNI_WA_WPA_WPA2_PSK: return OMNI_WA_WPA_WPA2_PSK;
    case OMNI_WA_WPA3_EXT_PSK:
    case OMNI_WA_WPA3_EXT_PSK_MIXED: return OMNI_WA_WPA3_PSK;
    default: return m;
    }
}

bool omni_join_auth_reason(uint8_t r) { return r == 2 || r == 15 || r == 202 || r == 204; }

void omni_trial_start(omni_trial_t *t)
{
    t->phase = OMNI_TR_JOINING;
    t->res = OMNI_TRES_NONE;
    t->committed = false;
}

static omni_trial_act_t end(omni_trial_t *t, omni_trial_res_t r)
{
    t->phase = OMNI_TR_DONE;
    t->res = (uint8_t)r;
    return OMNI_TA_END;
}

omni_trial_act_t omni_trial_step(omni_trial_t *t, omni_trial_ev_t e)
{
    switch (t->phase) {
    case OMNI_TR_JOINING:
        switch (e) {
        case OMNI_TE_GOT_IP:
            t->committed = true;
            t->phase = OMNI_TR_SYNCING;
            return OMNI_TA_COMMIT;
        case OMNI_TE_FAIL_AUTH: return end(t, OMNI_TRES_PASSWORD);
        case OMNI_TE_FAIL_NO_AP:
        case OMNI_TE_TIMEOUT: return end(t, OMNI_TRES_NO_AP);
        case OMNI_TE_CANCEL: return end(t, OMNI_TRES_CANCELLED);
        default: return OMNI_TA_CONTINUE;
        }
    case OMNI_TR_SYNCING:
        if (e == OMNI_TE_SYNC_OK) return end(t, OMNI_TRES_OK);
        if (e == OMNI_TE_SYNC_FAIL) return end(t, OMNI_TRES_SYNC);
        return OMNI_TA_CONTINUE;
    default: return OMNI_TA_CONTINUE;
    }
}

/* connect_wait only ever returns an attempt's end; a stub (or a future op) answering anything else is asked again a
 * bounded number of times, then read as a timeout, so the drive always ends */
#define DRIVE_WAITS 4

omni_trial_res_t omni_join_drive(const omni_join_ops_t *ops, void *ctx, omni_trial_t *t, bool *persisted)
{
    *persisted = false;
    omni_trial_start(t);
    if (ops->wifi_up(ctx) != 0) {
        end(t, OMNI_TRES_NO_MEM);
        ops->wifi_off(ctx);
        return OMNI_TRES_NO_MEM;
    }
    /* C1: RAM storage first, its return checked; with any error set_config is never called (storage is still the
     * driver's FLASH default: nothing to put back) */
    if (ops->storage_ram(ctx) != 0) {
        end(t, OMNI_TRES_NO_MEM);
        ops->wifi_off(ctx);
        return OMNI_TRES_NO_MEM;
    }
    ops->storage_ok_log(ctx);
    if (ops->set_config(ctx) != 0) {
        end(t, OMNI_TRES_NO_MEM);
        ops->storage_flash(ctx);
        ops->wifi_off(ctx);
        return OMNI_TRES_NO_MEM;
    }
    omni_trial_act_t act = OMNI_TA_CONTINUE;
    for (int i = 0; act == OMNI_TA_CONTINUE; i++)
        act = omni_trial_step(t, i < DRIVE_WAITS ? ops->connect_wait(ctx) : OMNI_TE_TIMEOUT);
    if (act != OMNI_TA_COMMIT) {   /* END before an IP: nothing was written; FLASH storage back before deinit */
        ops->storage_flash(ctx);
        ops->wifi_off(ctx);
        return (omni_trial_res_t)t->res;
    }
    /* the commit: the one write of the saved network (skipped if FLASH storage could not be set: it would not persist,
     * and the I11 check below says so) */
    if (ops->storage_flash(ctx) == 0) ops->set_config(ctx);
    omni_trial_step(t, ops->sntp(ctx) ? OMNI_TE_SYNC_OK : OMNI_TE_SYNC_FAIL);
    ops->wifi_off(ctx);
    *persisted = ops->persisted(ctx);
    ops->wifi_off(ctx);
    return (omni_trial_res_t)t->res;
}

bool omni_join_forget_allowed(bool read_only, bool prov, bool trial_or_scan)
{
    return !read_only && !prov && !trial_or_scan;
}

bool omni_join_books_sync(omni_trial_res_t r) { return r == OMNI_TRES_OK || r == OMNI_TRES_SYNC; }

void omni_forget_hold_reset(omni_forget_hold_t *h)
{
    h->armed = false;
    h->fired = false;
}

bool omni_forget_hold_step(omni_forget_hold_t *h, omni_gesture_kind_t k, uint16_t hold_ms, bool on_key)
{
    switch (k) {
    case G_NONE: return false;
    case G_DOWN:
        h->armed = on_key;
        return false;
    case G_HOLD:
        if (!on_key) {
            h->armed = false;
            return false;
        }
        if (h->armed && !h->fired && hold_ms >= OMNI_FORGET_HOLD_MS) {
            h->fired = true;
            return true;
        }
        return false;
    default:   /* a tap, a still lift, a drag, a long press, a swipe: the press is over or moved */
        h->armed = false;
        return false;
    }
}

bool omni_join_admit(bool prov) { return !prov; }

bool omni_join_keep_busy(bool prov, bool prov_queued) { return prov || prov_queued; }

bool omni_join_busy_left(bool prov, bool prov_queued, bool running, bool claimed)
{
    return omni_join_keep_busy(prov, prov_queued) || running || claimed;
}
