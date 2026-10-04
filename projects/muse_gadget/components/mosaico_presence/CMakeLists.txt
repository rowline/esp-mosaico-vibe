# Presence greeting: the Interaction module's PIR and LEDs, Muse's screen.
idf_component_register(
    SRCS "mosaico_presence.c" "presence_clock.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES mosaico_module_interact muse esp_netif esp_timer freertos log)
