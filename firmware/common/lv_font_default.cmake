# LVGL's default font for every theme app (step 1, rim keyboard and Settings track, D2): font_plex_sans_500_14, which
# watch_ui links in all three (the pills), instead of Montserrat 14. lv_conf_internal.h takes both through #ifndef,
# ahead of the Kconfig choice. Included by each app's CMakeLists.txt after project.cmake and before project(), so the
# definitions reach every component, LVGL's included.
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_DEFAULT=&font_plex_sans_500_14" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_CUSTOM_DECLARE=LV_FONT_DECLARE(font_plex_sans_500_14)" APPEND)
