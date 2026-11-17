idf_component_register(SRCS "version.c" "cities.c" "cities_v1.c" "settings.c" "sun.c" "suntext.c" "globe.c" "globe_render.c" "globe_fast.c" "globe_compact.c" "globe_check.c" "timefmt.c" "sync.c" "gesture.c" "buttons.c" "wake.c" "power_policy.c" "orbit.c" "nav.c" "nav_alarm.c" "prov_payload.c" "prov_session.c" "batrec.c" "clockcal.c" "globe_motion.c" "dial.c" "vbus.c" "ring.c" "trig.c" "omni_model.c" "raster.c" "omni_dial.c" "rects.c" "lid.c" "iris.c" "reveal.c" "travel.c" "ntp.c" "brassbg.c" "band.c" "record.c" "shared.c" "local.c" "migrate.c" "boot.c" "timer.c" "tmr_ring.c" "tmr_alarm.c" "almanac.c" "alm_render.c" "alm_face.c" "worldtime.c" "tmr_jog.c" "wipe.c" "kb.c" "netlist.c" "city_find.c" "carousel_edit.c" "hold_key.c" "settings_rows.c" "join.c"
                       INCLUDE_DIRS "include")
# The LUT and shade builds are single precision with a fixed evaluation order, so the device builds the bytes the
# host tests pin: no fused multiply-add contraction (the host's -std=c99 already implies it).
set_source_files_properties(globe.c globe_render.c globe_compact.c dial.c ring.c trig.c omni_model.c raster.c omni_dial.c lid.c iris.c travel.c tmr_ring.c alm_render.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
# The one documented exception (R121/B1): brassbg.c, the brass backgrounds' fallback renderer, moved from main/ui/theme.c,
# keeps theme.c's device flags (FMA contraction allowed: madd.s), so that a fallback render reproduces the captured
# Stage 3 bytes the bg_*.bin assets hold (R115). The host's bytes are not pinned (test_brassbg only warns).
set_source_files_properties(brassbg.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=fast")
# The owner's flip A/B (CONFIG_OMNI_FLIP_MOCKUP, Kconfig.projbuild): lid.h's timeline from the mockup. PUBLIC, so every
# component that reads lid.h's constants sees the same timeline.
if(CONFIG_OMNI_FLIP_MOCKUP)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC OMNI_FLIP_MOCKUP=1)
endif()
