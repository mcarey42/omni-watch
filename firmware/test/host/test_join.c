#include "unity.h"
#include "omni/join.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define NEV 8   /* every omni_trial_ev_t */

/* ---------------------------------------------------------------- refusals */
static void test_join_refusals(void)
{
    const char *pw = "abcdefgh";   /* 8 characters: the shortest WPA2 password */
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 5, pw, 8));
    /* in order: each refusal wins over every later one */
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_READ_ONLY, omni_join_check(true, true, true, OMNI_WA_WEP, 0, "x", 1));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_BUSY, omni_join_check(false, true, true, OMNI_WA_WEP, 0, "x", 1));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PHONE, omni_join_check(false, false, true, OMNI_WA_WEP, 0, "x", 1));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_SSID, omni_join_check(false, false, false, OMNI_WA_WEP, 0, "x", 1));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_AUTH, omni_join_check(false, false, false, OMNI_WA_WEP, 5, "x", 1));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PASS, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 5, "x", 1));
    /* each refusal alone */
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_READ_ONLY, omni_join_check(true, false, false, OMNI_WA_WPA2_PSK, 5, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_BUSY, omni_join_check(false, true, false, OMNI_WA_WPA2_PSK, 5, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PHONE, omni_join_check(false, false, true, OMNI_WA_WPA2_PSK, 5, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PASS, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 5, pw, 7));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PASS, omni_join_check(false, false, false, OMNI_WA_WPA3_PSK, 5, pw, 7));
    /* every unsupported class is AUTH, never PASS */
    const uint8_t unsup[] = {OMNI_WA_WEP, OMNI_WA_ENTERPRISE, OMNI_WA_WAPI_PSK, OMNI_WA_OWE, OMNI_WA_WPA3_ENT_192,
                             OMNI_WA_DPP, OMNI_WA_WPA3_ENTERPRISE, OMNI_WA_WPA2_WPA3_ENTERPRISE,
                             OMNI_WA_WPA_ENTERPRISE, OMNI_WA_UNKNOWN, 18, 255};
    for (unsigned i = 0; i < sizeof unsup; i++)
        TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_AUTH, omni_join_check(false, false, false, unsup[i], 5, pw, 8));
    /* an open network with an empty password (and a NULL one) is OK */
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_OPEN, 5, "", 0));
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_OPEN, 5, NULL, 0));
    /* net's buffer holds 64: longer is refused even where the class takes any password; NULL with a length too */
    char big[66];
    memset(big, 'a', sizeof big);
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PASS, omni_join_check(false, false, false, OMNI_WA_OPEN, 5, big, 65));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_PASS, omni_join_check(false, false, false, OMNI_WA_OPEN, 5, NULL, 3));
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 5, big, 64));   /* 64 hex */
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 5, big, 63));
}

static void test_join_ssid_bytes(void)
{
    const char *pw = "abcdefgh";
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 1, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_OK, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 32, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_SSID, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 0, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_SSID, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 33, pw, 8));
    TEST_ASSERT_EQUAL(OMNI_JOIN_REFUSE_SSID, omni_join_check(false, false, false, OMNI_WA_WPA2_PSK, 255, pw, 8));
}

static void test_join_threshold(void)
{
    TEST_ASSERT_EQUAL(OMNI_WA_OPEN, omni_join_threshold(OMNI_WA_OPEN));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA_PSK, omni_join_threshold(OMNI_WA_WPA_PSK));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA2_PSK, omni_join_threshold(OMNI_WA_WPA2_PSK));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA_WPA2_PSK, omni_join_threshold(OMNI_WA_WPA_WPA2_PSK));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA3_PSK, omni_join_threshold(OMNI_WA_WPA3_PSK));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA2_WPA3_PSK, omni_join_threshold(OMNI_WA_WPA2_WPA3_PSK));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA3_PSK, omni_join_threshold(OMNI_WA_WPA3_EXT_PSK));
    TEST_ASSERT_EQUAL(OMNI_WA_WPA3_PSK, omni_join_threshold(OMNI_WA_WPA3_EXT_PSK_MIXED));
    /* never weaker than scanned, for every PSK and SAE mode */
    for (uint8_t m = 0; m <= OMNI_WA_UNKNOWN; m++) {
        omni_auth_class_t c = omni_netlist_auth(m);
        if (c == OMNI_AUTH_PSK) TEST_ASSERT_EQUAL(OMNI_AUTH_PSK, omni_netlist_auth(omni_join_threshold(m)));
        if (c == OMNI_AUTH_SAE) TEST_ASSERT_EQUAL(OMNI_AUTH_SAE, omni_netlist_auth(omni_join_threshold(m)));
    }
}

static void test_join_auth_reasons(void)
{
    TEST_ASSERT_TRUE(omni_join_auth_reason(2));
    TEST_ASSERT_TRUE(omni_join_auth_reason(15));
    TEST_ASSERT_TRUE(omni_join_auth_reason(202));
    TEST_ASSERT_TRUE(omni_join_auth_reason(204));
    TEST_ASSERT_FALSE(omni_join_auth_reason(201));   /* NO_AP_FOUND */
    TEST_ASSERT_FALSE(omni_join_auth_reason(8));
    TEST_ASSERT_FALSE(omni_join_auth_reason(0));
    int n = 0;
    for (int r = 0; r < 256; r++) n += omni_join_auth_reason((uint8_t)r);
    TEST_ASSERT_EQUAL(4, n);
}

/* ---------------------------------------------------------------- the trial machine */
static void test_join_writes_only_on_ip(void)
{
    /* every sequence of length 1..4 over every event */
    for (int len = 1; len <= 4; len++) {
        int total = 1;
        for (int i = 0; i < len; i++) total *= NEV;
        for (int code = 0; code < total; code++) {
            omni_trial_t t;
            omni_trial_start(&t);
            TEST_ASSERT_FALSE(t.committed);
            int c = code;
            for (int i = 0; i < len; i++) {
                omni_trial_ev_t e = (omni_trial_ev_t)(c % NEV);
                c /= NEV;
                const bool was_joining = t.phase == OMNI_TR_JOINING, was = t.committed;
                omni_trial_act_t a = omni_trial_step(&t, e);
                const bool commit_now = was_joining && e == OMNI_TE_GOT_IP;
                TEST_ASSERT_EQUAL(commit_now, a == OMNI_TA_COMMIT);
                TEST_ASSERT_EQUAL(commit_now, !was && t.committed);
                TEST_ASSERT_TRUE(!was || t.committed);   /* once committed, always committed */
            }
        }
    }
}

static void test_join_sync_fail_keeps_commit(void)
{
    omni_trial_t t;
    omni_trial_start(&t);
    TEST_ASSERT_EQUAL(OMNI_TA_COMMIT, omni_trial_step(&t, OMNI_TE_GOT_IP));
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_SYNC_FAIL));
    TEST_ASSERT_EQUAL(OMNI_TRES_SYNC, t.res);
    TEST_ASSERT_TRUE(t.committed);
    TEST_ASSERT_EQUAL(OMNI_TR_DONE, t.phase);
    /* DONE: nothing changes any more */
    for (int e = 0; e < NEV; e++) {
        TEST_ASSERT_EQUAL(OMNI_TA_CONTINUE, omni_trial_step(&t, (omni_trial_ev_t)e));
        TEST_ASSERT_EQUAL(OMNI_TRES_SYNC, t.res);
        TEST_ASSERT_TRUE(t.committed);
    }
}

static void test_join_screen_gone_continues(void)
{
    omni_trial_t t;
    omni_trial_start(&t);
    TEST_ASSERT_EQUAL(OMNI_TA_CONTINUE, omni_trial_step(&t, OMNI_TE_SCREEN_GONE));
    TEST_ASSERT_EQUAL(OMNI_TR_JOINING, t.phase);
    TEST_ASSERT_EQUAL(OMNI_TA_COMMIT, omni_trial_step(&t, OMNI_TE_GOT_IP));
    TEST_ASSERT_EQUAL(OMNI_TA_CONTINUE, omni_trial_step(&t, OMNI_TE_SCREEN_GONE));
    TEST_ASSERT_EQUAL(OMNI_TR_SYNCING, t.phase);
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_SYNC_OK));
    TEST_ASSERT_EQUAL(OMNI_TRES_OK, t.res);
    TEST_ASSERT_TRUE(t.committed);
}

static void test_join_cancel_before_ip(void)
{
    omni_trial_t t;
    omni_trial_start(&t);
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_CANCEL));
    TEST_ASSERT_EQUAL(OMNI_TRES_CANCELLED, t.res);
    TEST_ASSERT_FALSE(t.committed);
    TEST_ASSERT_EQUAL(OMNI_TA_CONTINUE, omni_trial_step(&t, OMNI_TE_GOT_IP));   /* too late: no commit after DONE */
    TEST_ASSERT_FALSE(t.committed);
    /* the other failures */
    omni_trial_start(&t);
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_FAIL_AUTH));
    TEST_ASSERT_EQUAL(OMNI_TRES_PASSWORD, t.res);
    omni_trial_start(&t);
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_FAIL_NO_AP));
    TEST_ASSERT_EQUAL(OMNI_TRES_NO_AP, t.res);
    omni_trial_start(&t);
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_TIMEOUT));
    TEST_ASSERT_EQUAL(OMNI_TRES_NO_AP, t.res);
    TEST_ASSERT_FALSE(t.committed);
}

static void test_join_cancel_after_commit(void)
{
    omni_trial_t t;
    omni_trial_start(&t);
    omni_trial_step(&t, OMNI_TE_GOT_IP);
    TEST_ASSERT_EQUAL(OMNI_TA_CONTINUE, omni_trial_step(&t, OMNI_TE_CANCEL));
    TEST_ASSERT_TRUE(t.committed);
    TEST_ASSERT_EQUAL(OMNI_TR_SYNCING, t.phase);
    TEST_ASSERT_EQUAL(OMNI_TA_END, omni_trial_step(&t, OMNI_TE_SYNC_OK));
    TEST_ASSERT_EQUAL(OMNI_TRES_OK, t.res);
}

/* ---------------------------------------------------------------- the driver call order (C1, R202) */
typedef struct {
    char log[64];
    int n;
    bool fail_up, fail_ram, fail_set, fail_flash, sntp_ok, persisted_ok;
    omni_trial_ev_t waits[8];
    int nwaits, wi;
    omni_trial_ev_t wait_ev[8];   /* what each recorded W returned */
} rec_t;

static void put(rec_t *r, char ch)
{
    TEST_ASSERT_TRUE(r->n < (int)sizeof r->log - 1);
    r->log[r->n++] = ch;
    r->log[r->n] = '\0';
}
static int op_up(void *c) { put(c, 'U'); return ((rec_t *)c)->fail_up ? -1 : 0; }
static int op_ram(void *c) { put(c, 'R'); return ((rec_t *)c)->fail_ram ? -1 : 0; }
static void op_log(void *c) { put(c, 'L'); }
static int op_set(void *c) { put(c, 'S'); return ((rec_t *)c)->fail_set ? -1 : 0; }
static omni_trial_ev_t op_wait(void *c)
{
    rec_t *r = c;
    put(r, 'W');
    omni_trial_ev_t e = r->wi < r->nwaits ? r->waits[r->wi] : OMNI_TE_TIMEOUT;
    if (r->wi < 8) r->wait_ev[r->wi] = e;
    r->wi++;
    return e;
}
static int op_flash(void *c) { put(c, 'F'); return ((rec_t *)c)->fail_flash ? -1 : 0; }
static bool op_sntp(void *c) { put(c, 'N'); return ((rec_t *)c)->sntp_ok; }
static void op_off(void *c) { put(c, 'O'); }
static bool op_persisted(void *c) { put(c, 'P'); return ((rec_t *)c)->persisted_ok; }

static const omni_join_ops_t OPS = {
    .wifi_up = op_up, .storage_ram = op_ram, .storage_ok_log = op_log, .set_config = op_set,
    .connect_wait = op_wait, .storage_flash = op_flash, .sntp = op_sntp, .wifi_off = op_off,
    .persisted = op_persisted,
};

static omni_trial_res_t drive(rec_t *r, omni_trial_t *t, bool *persisted)
{
    *persisted = true;   /* the drive must set it */
    return omni_join_drive(&OPS, r, t, persisted);
}

/* The order's invariants on one recorded run. */
static void check_order(const rec_t *r, const omni_trial_t *t, bool persisted)
{
    const char *s = r->log;
    const char *L = strchr(s, 'L'), *S1 = strchr(s, 'S');
    /* no set_config before the "storage RAM ok" log, and the log only after a successful RAM call */
    if (S1) TEST_ASSERT_TRUE(L && L < S1);
    if (L) TEST_ASSERT_EQUAL_CHAR('R', L[-1]);
    /* at most two set_config; the second only straight after a flash that follows a W which returned GOT_IP */
    const char *S2 = S1 ? strchr(S1 + 1, 'S') : NULL;
    if (S2) {
        TEST_ASSERT_NULL(strchr(S2 + 1, 'S'));
        TEST_ASSERT_EQUAL_CHAR('F', S2[-1]);
        TEST_ASSERT_EQUAL_CHAR('W', S2[-2]);
        int w = 0;
        for (const char *p = s; p < S2 - 2; p++) w += *p == 'W';
        TEST_ASSERT_EQUAL(OMNI_TE_GOT_IP, r->wait_ev[w]);
        TEST_ASSERT_TRUE(t->committed);
    } else {
        TEST_ASSERT_TRUE(!t->committed || r->fail_flash);
    }
    /* every path ends with wifi_off; once RAM storage was set, a flash comes before the last off */
    TEST_ASSERT_TRUE(r->n > 0);
    TEST_ASSERT_EQUAL_CHAR('O', s[r->n - 1]);
    if (L) {
        const char *F = strrchr(s, 'F');
        TEST_ASSERT_NOT_NULL(F);
        TEST_ASSERT_TRUE(F > L && F < s + r->n - 1);
    }
    /* the I11 check runs only after a commit, between two offs */
    const char *P = strchr(s, 'P');
    if (t->committed) {
        TEST_ASSERT_NOT_NULL(P);
        TEST_ASSERT_EQUAL_CHAR('O', P[-1]);
        TEST_ASSERT_EQUAL_CHAR('O', P[1]);
        TEST_ASSERT_EQUAL(r->persisted_ok, persisted);
    } else {
        TEST_ASSERT_NULL(P);
        TEST_ASSERT_FALSE(persisted);
    }
}

static void test_join_call_order(void)
{
    rec_t r;
    omni_trial_t t;
    bool persisted;
    /* the happy path, exactly */
    memset(&r, 0, sizeof r);
    r.sntp_ok = r.persisted_ok = true;
    r.waits[0] = OMNI_TE_GOT_IP;
    r.nwaits = 1;
    TEST_ASSERT_EQUAL(OMNI_TRES_OK, drive(&r, &t, &persisted));
    TEST_ASSERT_EQUAL_STRING("URLSWFSNOPO", r.log);
    TEST_ASSERT_TRUE(persisted);
    check_order(&r, &t, persisted);
    /* storage_ram fails: up, ram, off; no set_config at all */
    memset(&r, 0, sizeof r);
    r.fail_ram = true;
    r.waits[0] = OMNI_TE_GOT_IP;
    r.nwaits = 1;
    TEST_ASSERT_EQUAL(OMNI_TRES_NO_MEM, drive(&r, &t, &persisted));
    TEST_ASSERT_EQUAL_STRING("URO", r.log);
    TEST_ASSERT_FALSE(persisted);
    TEST_ASSERT_FALSE(t.committed);
    /* wifi_up fails: no ram, no set */
    memset(&r, 0, sizeof r);
    r.fail_up = true;
    TEST_ASSERT_EQUAL(OMNI_TRES_NO_MEM, drive(&r, &t, &persisted));
    TEST_ASSERT_EQUAL_STRING("UO", r.log);
    TEST_ASSERT_FALSE(persisted);
    /* the trial's set_config fails: NO_MEM, FLASH back, off */
    memset(&r, 0, sizeof r);
    r.fail_set = true;
    TEST_ASSERT_EQUAL(OMNI_TRES_NO_MEM, drive(&r, &t, &persisted));
    TEST_ASSERT_EQUAL_STRING("URLSFO", r.log);
    check_order(&r, &t, persisted);
    /* I11's seam: not persisted, still OK */
    memset(&r, 0, sizeof r);
    r.sntp_ok = true;
    r.waits[0] = OMNI_TE_GOT_IP;
    r.nwaits = 1;
    TEST_ASSERT_EQUAL(OMNI_TRES_OK, drive(&r, &t, &persisted));
    TEST_ASSERT_FALSE(persisted);
    TEST_ASSERT_EQUAL_STRING("URLSWFSNOPO", r.log);
    /* the sync fails after the commit: SYNC, the commit stands */
    memset(&r, 0, sizeof r);
    r.persisted_ok = true;
    r.waits[0] = OMNI_TE_GOT_IP;
    r.nwaits = 1;
    TEST_ASSERT_EQUAL(OMNI_TRES_SYNC, drive(&r, &t, &persisted));
    TEST_ASSERT_TRUE(t.committed);
    TEST_ASSERT_EQUAL_STRING("URLSWFSNOPO", r.log);
    /* FLASH storage fails at the commit: the write is skipped (it would not persist) and the I11 check reports it */
    memset(&r, 0, sizeof r);
    r.fail_flash = true;
    r.sntp_ok = true;
    r.waits[0] = OMNI_TE_GOT_IP;
    r.nwaits = 1;
    TEST_ASSERT_EQUAL(OMNI_TRES_OK, drive(&r, &t, &persisted));
    TEST_ASSERT_EQUAL_STRING("URLSWFNOPO", r.log);
    check_order(&r, &t, persisted);
    /* each failure, exactly */
    const struct { omni_trial_ev_t e; omni_trial_res_t res; } ends[] = {
        {OMNI_TE_FAIL_AUTH, OMNI_TRES_PASSWORD}, {OMNI_TE_FAIL_NO_AP, OMNI_TRES_NO_AP},
        {OMNI_TE_TIMEOUT, OMNI_TRES_NO_AP}, {OMNI_TE_CANCEL, OMNI_TRES_CANCELLED}};
    for (unsigned i = 0; i < sizeof ends / sizeof ends[0]; i++) {
        memset(&r, 0, sizeof r);
        r.waits[0] = ends[i].e;
        r.nwaits = 1;
        TEST_ASSERT_EQUAL(ends[i].res, drive(&r, &t, &persisted));
        TEST_ASSERT_EQUAL_STRING("URLSWFO", r.log);
        TEST_ASSERT_FALSE(t.committed);
        TEST_ASSERT_FALSE(persisted);
    }
    /* every sequence of connect_wait results (any event, up to 5 asked), every sntp and persisted answer, and every
     * single failing call */
    for (int code = 0; code < NEV * NEV * NEV * NEV * NEV; code++) {
        for (int v = 0; v < 4 * 5; v++) {
            memset(&r, 0, sizeof r);
            int c = code;
            for (int i = 0; i < 5; i++, c /= NEV) r.waits[i] = (omni_trial_ev_t)(c % NEV);
            r.nwaits = 5;
            r.sntp_ok = v & 1;
            r.persisted_ok = (v >> 1) & 1;
            const int f = v >> 2;   /* 0 none, 1 up, 2 ram, 3 set, 4 flash */
            r.fail_up = f == 1;
            r.fail_ram = f == 2;
            r.fail_set = f == 3;
            r.fail_flash = f == 4;
            omni_trial_res_t res = drive(&r, &t, &persisted);
            check_order(&r, &t, persisted);
            TEST_ASSERT_EQUAL(t.res, res);
            TEST_ASSERT_EQUAL(OMNI_TR_DONE, t.phase);
            TEST_ASSERT_TRUE(r.wi <= 4);   /* bounded */
            if (f >= 1 && f <= 3) TEST_ASSERT_EQUAL(OMNI_TRES_NO_MEM, res);
            if (f == 1 || f == 2) TEST_ASSERT_NULL(strchr(r.log, 'S'));
            if (f == 1) TEST_ASSERT_NULL(strchr(r.log, 'R'));
        }
    }
}

/* ---------------------------------------------------------------- Forget */
static void test_join_forget_allowed(void)
{
    TEST_ASSERT_TRUE(omni_join_forget_allowed(false, false, false));
    TEST_ASSERT_FALSE(omni_join_forget_allowed(true, false, false));
    TEST_ASSERT_FALSE(omni_join_forget_allowed(false, true, false));
    TEST_ASSERT_FALSE(omni_join_forget_allowed(false, false, true));
    for (int m = 1; m < 8; m++) TEST_ASSERT_FALSE(omni_join_forget_allowed(m & 1, m & 2, m & 4));
}

static void test_join_books_sync(void)
{
    TEST_ASSERT_TRUE(omni_join_books_sync(OMNI_TRES_OK));
    TEST_ASSERT_TRUE(omni_join_books_sync(OMNI_TRES_SYNC));
    TEST_ASSERT_FALSE(omni_join_books_sync(OMNI_TRES_PASSWORD));
    TEST_ASSERT_FALSE(omni_join_books_sync(OMNI_TRES_NO_AP));
    TEST_ASSERT_FALSE(omni_join_books_sync(OMNI_TRES_CANCELLED));
    TEST_ASSERT_FALSE(omni_join_books_sync(OMNI_TRES_NO_MEM));
    TEST_ASSERT_FALSE(omni_join_books_sync(OMNI_TRES_NONE));
}

/* ---------------------------------------------------------------- the Forget hold (C2, R202) */
/* G_HOLD ticks as the classifier sends them: 450 + 85 k ms, every one up to `to`; returns how many fired */
static int ticks(omni_forget_hold_t *h, uint16_t to, bool on_key)
{
    int fired = 0;
    for (uint32_t ms = OMNI_HOLD_FIRST_MS; ms <= to; ms += OMNI_HOLD_STEP_MS)
        fired += omni_forget_hold_step(h, G_HOLD, (uint16_t)ms, on_key);
    return fired;
}

static void test_forget_hold_tap_and_still_lift_never_fire(void)
{
    omni_forget_hold_t h;
    omni_forget_hold_reset(&h);
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_DOWN, 0, true));
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_TAP, 0, true));
    TEST_ASSERT_FALSE(h.armed);
    TEST_ASSERT_FALSE(h.fired);
    const uint16_t lifts[] = {300, 1499, 3000};
    for (unsigned i = 0; i < 3; i++) {
        omni_forget_hold_reset(&h);
        omni_forget_hold_step(&h, G_DOWN, 0, true);
        /* ticks below the still lift's age (with no tick at or past 1 500), then the lift */
        TEST_ASSERT_EQUAL(0, ticks(&h, lifts[i] < 1499 ? lifts[i] : 1499, true));
        TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_LIFT_STILL, 0, true));
        TEST_ASSERT_FALSE(h.fired);
        TEST_ASSERT_FALSE(h.armed);
        /* a stale tick after the lift (no new G_DOWN) never fires */
        TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_HOLD, lifts[i] > 1500 ? lifts[i] : 1500, true));
    }
}

static void test_forget_hold_short_holds_never_fire(void)
{
    omni_forget_hold_t h;
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    TEST_ASSERT_EQUAL(0, ticks(&h, 1499, true));   /* ticks stop below 1 500: 450 .. 1 470 */
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_HOLD, 1499, true));
    TEST_ASSERT_FALSE(h.fired);
    TEST_ASSERT_TRUE(h.armed);
}

static void test_forget_hold_started_off_key_never_fires(void)
{
    omni_forget_hold_t h;
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, false);
    TEST_ASSERT_EQUAL(0, ticks(&h, 3000, true));   /* even if the ticks are on the key */
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, false);
    TEST_ASSERT_EQUAL(0, ticks(&h, 3000, false));
    /* armed on the key, then a G_DOWN off it (a new press elsewhere) disarms */
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    omni_forget_hold_step(&h, G_DOWN, 0, false);
    TEST_ASSERT_EQUAL(0, ticks(&h, 3000, true));
    /* a tick off the key disarms for the rest of the press */
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_HOLD, 1000, false));
    TEST_ASSERT_EQUAL(0, ticks(&h, 3000, true));
}

static void test_forget_hold_drag_long_press_reset_never_fire(void)
{
    const omni_gesture_kind_t breakers[] = {G_DRAG_START, G_DRAG_MOVE, G_DRAG_END, G_LONG_PRESS, G_TAP, G_LIFT_STILL,
                                            G_SWIPE_L, G_SWIPE_R, G_SWIPE_UP, G_SWIPE_DOWN_FROM_TOP};
    for (unsigned i = 0; i < sizeof breakers / sizeof breakers[0]; i++) {
        omni_forget_hold_t h;
        omni_forget_hold_reset(&h);
        omni_forget_hold_step(&h, G_DOWN, 0, true);
        TEST_ASSERT_EQUAL(0, ticks(&h, 1000, true));
        TEST_ASSERT_FALSE(omni_forget_hold_step(&h, breakers[i], 0, true));
        TEST_ASSERT_EQUAL(0, ticks(&h, 3000, true));
    }
    /* a reset mid-hold (BOOT, a tap outside, Cancel, the timeout close the sheet) */
    omni_forget_hold_t h;
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    TEST_ASSERT_EQUAL(0, ticks(&h, 1400, true));
    omni_forget_hold_reset(&h);
    TEST_ASSERT_EQUAL(0, ticks(&h, 3000, true));
    /* G_NONE changes nothing */
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_NONE, 0, true));
    TEST_ASSERT_TRUE(h.armed);
}

static void test_forget_hold_fires_once(void)
{
    omni_forget_hold_t h;
    omni_forget_hold_reset(&h);
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_DOWN, 0, true));
    TEST_ASSERT_EQUAL(0, ticks(&h, 1499, true));   /* 450 .. 1 470 */
    /* the next tick carries the sample's age (R208): 1 525 */
    TEST_ASSERT_TRUE(omni_forget_hold_step(&h, G_HOLD, 1525, true));
    TEST_ASSERT_TRUE(h.fired);
    TEST_ASSERT_EQUAL(0, ticks(&h, 5000, true));   /* further ticks do not fire again */
    TEST_ASSERT_FALSE(omni_forget_hold_step(&h, G_HOLD, 65535, true));
    /* a new press on the key without a reset still does not fire again */
    omni_forget_hold_step(&h, G_LIFT_STILL, 0, true);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    TEST_ASSERT_EQUAL(0, ticks(&h, 3000, true));
    /* a late sample (R208): one tick carrying the current age */
    omni_forget_hold_reset(&h);
    omni_forget_hold_step(&h, G_DOWN, 0, true);
    TEST_ASSERT_TRUE(omni_forget_hold_step(&h, G_HOLD, 2400, true));
}

/* F1: the net task runs a scan or a trial only outside a phone session, and leaves busy set at its end while a session
 * runs or is queued (a CMD_PROV sent meanwhile), so no later scan or trial can be claimed in front of the session */
static void test_join_net_task_exclusion(void)
{
    TEST_ASSERT_TRUE(omni_join_admit(false));
    TEST_ASSERT_FALSE(omni_join_admit(true));
    TEST_ASSERT_FALSE(omni_join_keep_busy(false, false));
    TEST_ASSERT_TRUE(omni_join_keep_busy(true, false));
    TEST_ASSERT_TRUE(omni_join_keep_busy(false, true));
    TEST_ASSERT_TRUE(omni_join_keep_busy(true, true));
}

/* Task 12: the claimed-but-not-started gap. busy_left is keep_busy or a running or claimed scan or trial (all 16 cases) */
static void test_join_busy_left_table(void)
{
    for (int m = 0; m < 16; m++) {
        const bool prov = m & 1, queued = m & 2, running = m & 4, claimed = m & 8;
        TEST_ASSERT_EQUAL(omni_join_keep_busy(prov, queued) || running || claimed,
                          omni_join_busy_left(prov, queued, running, claimed));
    }
}

/* The sequence net.c's flags follow (a model of claim_busy, net_start_provisioning, the net task's handling): a trial
 * claimed while a CMD_PROV is queued ahead of it keeps busy through the failed start and a session that ends, so a
 * second net_try_join (claim_busy: !busy) is refused until the first trial has run */
typedef struct { bool busy, prov, running, claimed; int queued; } busy_model_t;
static bool model_claim(busy_model_t *m)
{
    if (m->busy || m->prov) return false;
    m->busy = m->claimed = true;
    return true;
}
static void model_settle(busy_model_t *m) { m->busy = omni_join_busy_left(m->prov, m->queued != 0, m->running, m->claimed); }

static void test_join_claim_survives_failed_prov_start(void)
{
    busy_model_t m = {0};
    TEST_ASSERT_TRUE(model_claim(&m));   /* the trial: busy and claimed; its command not yet in the queue */
    m.busy = true, m.queued++;           /* net_start_provisioning: CMD_PROV sent first */
    m.queued--;                          /* the net task: CMD_PROV's start fails */
    model_settle(&m);
    TEST_ASSERT_TRUE(m.busy);            /* before Task 12 busy was false here */
    TEST_ASSERT_FALSE(model_claim(&m));  /* a second trial cannot overwrite the first one's password copy */
    m.running = true, m.claimed = false; /* the net task takes the trial */
    model_settle(&m);
    TEST_ASSERT_TRUE(m.busy);
    m.running = false;                   /* its end */
    model_settle(&m);
    TEST_ASSERT_FALSE(m.busy);
    TEST_ASSERT_TRUE(model_claim(&m));
}

static void test_join_claim_survives_session_end(void)
{
    busy_model_t m = {0};
    TEST_ASSERT_TRUE(model_claim(&m));   /* a scan claimed */
    m.busy = true, m.queued++;           /* a phone session queued ahead of it */
    m.queued--, m.prov = true;           /* it starts */
    m.prov = false;                      /* and ends (a stop queued ahead of the scan): prov_end */
    model_settle(&m);
    TEST_ASSERT_TRUE(m.busy);
    m.claimed = false;                   /* the scan handled: refused (a session) or run, then ended */
    model_settle(&m);
    TEST_ASSERT_FALSE(m.busy);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_join_refusals);
    RUN_TEST(test_join_ssid_bytes);
    RUN_TEST(test_join_threshold);
    RUN_TEST(test_join_auth_reasons);
    RUN_TEST(test_join_writes_only_on_ip);
    RUN_TEST(test_join_sync_fail_keeps_commit);
    RUN_TEST(test_join_screen_gone_continues);
    RUN_TEST(test_join_cancel_before_ip);
    RUN_TEST(test_join_cancel_after_commit);
    RUN_TEST(test_join_call_order);
    RUN_TEST(test_join_forget_allowed);
    RUN_TEST(test_join_books_sync);
    RUN_TEST(test_forget_hold_tap_and_still_lift_never_fire);
    RUN_TEST(test_forget_hold_short_holds_never_fire);
    RUN_TEST(test_forget_hold_started_off_key_never_fires);
    RUN_TEST(test_forget_hold_drag_long_press_reset_never_fire);
    RUN_TEST(test_forget_hold_fires_once);
    RUN_TEST(test_join_net_task_exclusion);
    RUN_TEST(test_join_busy_left_table);
    RUN_TEST(test_join_claim_survives_failed_prov_start);
    RUN_TEST(test_join_claim_survives_session_end);
    return UNITY_END();
}
