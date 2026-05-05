## LVGL Config (Important)

To use a custom config file (recommended), first, make sure the KConfig setting is disabled in sdkconfig:

```bash
CONFIG_LV_CONF_SKIP=n
```

Then, the root CMakeLists.txt needs CMake to include a config file for LVGL. They are currently in the config directory, and defined by target, e.g.:

```bash
add_compile_definitions(LV_CONF_PATH="components/atomic_lcd/config/lv_conf_s3.h")
```

Some CMake versions require filenames to have formats that cannot be used in CMakeLists.txt, so instead, you can define a directory path, and move just one target config file into that directory, e.g.:

```bash
idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-I../main" APPEND)
```

## Sanity Check

Make sure one of your chosen fonts shows up.

```c
#ifndef LV_FONT_MONTSERRAT_48
    ESP_LOGE(TAG, "Error: lv_conf.h not found!");
    return ESP_FAIL;
#endif
```





idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DLV_LVGL_H_INCLUDE_SIMPLE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-I../components/atomic_lcd/config" APPEND)
add_compile_definitions(LV_CONF_PATH="../components/atomic_lcd/config/lv_conf.h")

add_compile_options(“-DLV_CONF_PATH= ${CMAKE_SOURCE_DIR}/main/lv_conf.h”)