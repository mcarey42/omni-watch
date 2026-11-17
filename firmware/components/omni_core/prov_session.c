#include "omni/prov_session.h"

bool omni_prov_stop_on(omni_prov_phase_t ph, omni_prov_why_t why)
{
    (void)why; /* leaving and sleeping are treated alike: the QR is no longer in use */
    return ph != PROV_PH_JOINING;
}

bool omni_prov_expired(uint32_t started_ms, uint32_t now_ms) { return now_ms - started_ms >= OMNI_PROV_MAX_MS; }

bool omni_prov_long_timeout(omni_prov_phase_t ph) { return ph == PROV_PH_WAITING || ph == PROV_PH_PHONE; }

omni_prov_end_plan_t omni_prov_end_plan(bool snap_has_ssid, bool cred_recv, bool cred_ok, omni_prov_end_t why)
{
    (void)why;
    omni_prov_end_plan_t p = {.restore = snap_has_ssid && cred_recv && !cred_ok};
    p.provisioned = p.restore || cred_ok || snap_has_ssid;
    p.name_from_snapshot = snap_has_ssid && !cred_ok;
    return p;
}

bool omni_prov_type_instead_ok(omni_prov_phase_t ph)
{
    return ph != PROV_PH_NONE && ph != PROV_PH_DONE && omni_prov_stop_on(ph, PROV_WHY_LEFT);
}

bool omni_prov_manager_wrote(bool cred_recv_seen, bool cfg_nonempty) { return cred_recv_seen || cfg_nonempty; }

bool omni_prov_start_ok(bool fresh, bool snap_read) { return fresh && snap_read; }

bool omni_prov_end_provisioned(omni_prov_end_plan_t p, bool restore_ok, bool driver_has_ssid)
{
    return p.restore && !restore_ok ? driver_has_ssid : p.provisioned;
}
