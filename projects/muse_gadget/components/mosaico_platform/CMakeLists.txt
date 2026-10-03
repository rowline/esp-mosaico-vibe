# WHOLE_ARCHIVE keeps the strong muse_gadget_platform_start() even though only
# Muse's weak reference names it.
idf_component_register(
    SRCS "mosaico_platform.c"
    REQUIRES esp_iris esp_mosaico_app_recovery
    WHOLE_ARCHIVE)
