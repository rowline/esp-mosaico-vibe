cmake_minimum_required(VERSION 3.16)
# ESP-Mosaico application that builds the Muse Gadget SDK firmware from
# ../muse-gadget-sdk/esp32 with the retained Vibe Mode layout, so it installs
# through `mosaico.py iris system-update` instead of `idf.py flash`.
set(MUSE_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../muse-gadget-sdk/esp32")
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
# Muse's sdkconfig.defaults names the ESP32-C5 DevKit and IDF takes the target
# from the first defaults file that sets one.
set(IDF_TARGET "esp32s31")
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS
        "${MUSE_SDK_ROOT}/sdkconfig.defaults"
        "${MUSE_SDK_ROOT}/devices/sdkconfig.esp-mosaico"
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.mosaico")
    # Optional settings for one particular network, such as a gateway override.
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local")
        list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local")
    endif()
endif()

# The SDK token stays out of the repository: it is read from a private file
# into a generated defaults file in the build directory. A new token takes
# effect after deleting build/sdkconfig.
set(MUSE_SDK_TOKEN_FILE "$ENV{HOME}/.config/muse/sdk_token" CACHE FILEPATH
    "File holding the Muse Gadgets SDK token (mgst_...)")
if(EXISTS "${MUSE_SDK_TOKEN_FILE}")
    file(READ "${MUSE_SDK_TOKEN_FILE}" muse_sdk_token)
    string(STRIP "${muse_sdk_token}" muse_sdk_token)
    file(WRITE "${CMAKE_BINARY_DIR}/sdkconfig.token"
        "CONFIG_GADGET_SDK_TOKEN=\"${muse_sdk_token}\"\n")
    unset(muse_sdk_token)
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/sdkconfig.token")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${MUSE_SDK_ROOT}/main"
    "${MUSE_SDK_ROOT}/components"
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-recovery/components/esp_mosaico_app_recovery")
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-recovery/cmake/mosaico_idf_project.cmake")
# One version for the app descriptor Muse reports and the System Update
# release (PROJECT_VERSION, a 1-64 character string). IDF derives PROJECT_VER
# from VERSION only when PROJECT_VER is not set beforehand.
file(STRINGS "${MUSE_SDK_ROOT}/version.txt" MUSE_VERSION LIMIT_COUNT 1)
project(muse_gadget VERSION ${MUSE_VERSION})
include("${MUSE_SDK_ROOT}/cmake/validate_config.cmake")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
