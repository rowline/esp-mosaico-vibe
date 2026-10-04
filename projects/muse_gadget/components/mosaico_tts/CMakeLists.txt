# Muse's voice (muse_tts.h) from esp-sr's offline Chinese TTS.
idf_component_register(
    SRCS "tts_mosaico.c"
    INCLUDE_DIRS "."
    REQUIRES muse
    PRIV_REQUIRES esp_partition esp_timer)

# ESP-DL, which esp-sr requires, trips GCC 16's stricter C++ linkage
# diagnostic in code TTS doesn't use (the BSP's voice_tts example downgrades it).
idf_component_get_property(esp_dl_lib espressif__esp-dl COMPONENT_LIB)
target_compile_options(${esp_dl_lib} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-non-c-typedef-for-linkage>)

# The voice goes in the voice_data partition (partitions.csv) with each System
# Update; app-update leaves it in place.
idf_component_get_property(esp_sr_dir espressif__esp-sr COMPONENT_DIR)
set(voice_data_image "${esp_sr_dir}/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaole.dat")
add_custom_target(voice_data_image DEPENDS "${voice_data_image}")
set_property(GLOBAL APPEND PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_LABELS voice_data)
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_voice_data_IMAGE "${voice_data_image}")
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_voice_data_TARGET voice_data_image)
