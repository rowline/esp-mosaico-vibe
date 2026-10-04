idf_component_register(
    SRCS "camera_mosaico.c"
    INCLUDE_DIRS "."
    REQUIRES camera
    PRIV_REQUIRES mosaico_module_camera muse esp_mm esp_psram esp_timer)
