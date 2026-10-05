# WHOLE_ARCHIVE keeps the strong muse_gadget_platform_*() hooks even though
# only Muse's weak references name them.
idf_component_register(
    SRCS "mosaico_platform.c" "mosaico_commands.c"
    REQUIRES esp_iris esp_mosaico_app_recovery camera mosaico_camera mosaico_presence muse mosaico_tts
    PRIV_REQUIRES esp_timer
    WHOLE_ARCHIVE)
