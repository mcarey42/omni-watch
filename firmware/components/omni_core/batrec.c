#include "omni/batrec.h"
#include <string.h>

_Static_assert(sizeof(omni_bsnap_t) == 32, "batrec snapshot layout changed: bump OMNI_BATREC_VERSION");
_Static_assert(sizeof(omni_batrec_t) == 112, "batrec layout changed: bump OMNI_BATREC_VERSION");

void omni_batrec_init(omni_batrec_t *r) { memset(r, 0, sizeof *r); }

void omni_batrec_wake(omni_batrec_t *r, omni_wake_kind_t k)
{
    if (k == WAKE_LIFT) r->lift++;
    else if (k == WAKE_TOUCH) r->touch++;
    else if (k == WAKE_GLANCE) r->glance++;
}

omni_bsnap_t omni_batrec_snap(const omni_batrec_t *r, int64_t unix_s, uint32_t up_s, int pct, int mv)
{
    omni_bsnap_t s = {.unix_s = unix_s, .up_s = up_s, .boot = r->boot, .mv = (int16_t)mv, .pct = (int8_t)(pct < 0 || pct > 100 ? -1 : pct),
                      .valid = 1, .lift = r->lift, .touch = r->touch, .glance = r->glance};
    return s;
}

static void begin(omni_batrec_t *r, omni_bsnap_t now)
{
    r->start = now;
    r->last = now;
    memset(&r->end, 0, sizeof r->end);
    r->end_is_checkpoint = 0;
    r->on_battery = 1;
}

bool omni_batrec_vbus(omni_batrec_t *r, bool present, omni_bsnap_t now)
{
    if (!present && !r->on_battery) {
        begin(r, now);
        return true;
    }
    if (present && r->on_battery) {
        r->end = now;
        r->end_is_checkpoint = 0;
        r->on_battery = 0;
        return true;
    }
    return false;
}

bool omni_batrec_boot(omni_batrec_t *r, bool present, omni_bsnap_t now)
{
    r->boot++; /* RAM only: the next save carries it; snapshots of this boot are tagged with it */
    now.boot = r->boot;
    if (present && r->on_battery) { /* the replug happened while we were off: close from the checkpoint */
        r->end = r->last;
        r->end_is_checkpoint = 1;
        r->on_battery = 0;
        return true;
    }
    if (!present && !r->on_battery) { /* booted on battery without seeing the unplug */
        begin(r, now);
        return true;
    }
    return false;
}

bool omni_batrec_checkpoint_due(const omni_batrec_t *r, uint32_t up_s, uint32_t interval_s)
{
    if (!r->on_battery) return false;
    if (r->last.boot != r->boot) return true; /* the last checkpoint is from an earlier boot */
    return up_s - r->last.up_s >= interval_s;
}

bool omni_batrec_checkpoint(omni_batrec_t *r, omni_bsnap_t now, uint32_t interval_s)
{
    if (!omni_batrec_checkpoint_due(r, now.up_s, interval_s)) return false;
    r->last = now;
    return true;
}

void omni_batrec_encode(const omni_batrec_t *r, uint8_t *buf)
{
    const uint16_t ver = OMNI_BATREC_VERSION;
    memcpy(buf, &ver, sizeof ver);
    memcpy(buf + sizeof ver, r, sizeof *r);
}

static bool snap_ok(const omni_bsnap_t *s)
{
    return s->valid <= 1 && s->pct >= -1 && s->pct <= 100;
}

bool omni_batrec_decode(omni_batrec_t *r, const uint8_t *buf, size_t n)
{
    omni_batrec_t t;
    uint16_t ver;
    omni_batrec_init(r);
    if (n != OMNI_BATREC_BLOB_SIZE) return false;
    memcpy(&ver, buf, sizeof ver);
    if (ver != OMNI_BATREC_VERSION) return false;
    memcpy(&t, buf + sizeof ver, sizeof t);
    if (t.on_battery > 1 || t.end_is_checkpoint > 1 || !snap_ok(&t.start) || !snap_ok(&t.last) || !snap_ok(&t.end))
        return false;
    if (t.on_battery && !t.start.valid) return false;
    *r = t;
    return true;
}

void omni_batrec_report(const omni_batrec_t *r, omni_bsession_report_t *o)
{
    memset(o, 0, sizeof *o);
    o->hours_per_100_wakes = -1;
    o->pct_per_hour = -1;
    if (!r->start.valid) return;
    const omni_bsnap_t *e = r->on_battery ? &r->last : &r->end;
    if (!e->valid) return;
    o->have = true;
    o->in_progress = r->on_battery;
    o->partial = r->end_is_checkpoint;
    o->start_pct = r->start.pct;
    o->end_pct = e->pct;
    o->start_mv = r->start.mv;
    o->end_mv = e->mv;
    o->lift = e->lift - r->start.lift;
    o->touch = e->touch - r->start.touch;
    o->glance = e->glance - r->start.glance;
    o->wakes = o->lift + o->touch;
    if (r->start.boot == e->boot && e->up_s > r->start.up_s) o->duration_s = (int64_t)(e->up_s - r->start.up_s);
    else if (r->start.boot != e->boot && r->start.unix_s != 0 && e->unix_s > r->start.unix_s)
        o->duration_s = e->unix_s - r->start.unix_s; /* across a reboot uptime restarts: fall back to the wall clock */
    if (o->duration_s > 0) {
        o->hours = (float)o->duration_s / 3600.0f;
        if (o->wakes > 0) o->hours_per_100_wakes = o->hours * 100.0f / (float)o->wakes;
        if (r->start.pct >= 0 && e->pct >= 0) o->pct_per_hour = (float)(r->start.pct - e->pct) / o->hours;
    }
}
