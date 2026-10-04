# Muse board for ESP-Mosaico (CONFIG_MUSE_BOARD_HOST). WHOLE_ARCHIVE keeps
# muse_board_get(), which only the muse component references. The wide font
# for Chinese captions goes in the image; fonts/make_cjk_font.py makes it.
idf_component_register(
    SRCS "board_esp_mosaico.c"
    REQUIRES muse esp-mosaico-bsp esp_lcd esp_timer freertos
    EMBED_FILES "fonts/cjk_16.bin"
    WHOLE_ARCHIVE)
