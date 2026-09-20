# Contest 2026 team 345 ESP32-P4X Function EV Board
nuttx_add_library(board)
target_sources(board PRIVATE board_boot.c)

if(CONFIG_DEV_GPIO AND NOT CONFIG_GPIO_LOWER_HALF)
  target_sources(board PRIVATE board_gpio.c)
endif()

if(CONFIG_ESPRESSIF_I2C0)
  target_sources(board PRIVATE board_i2c.c)
endif()

if(CONFIG_AUDIO_ES8311)
  target_sources(board PRIVATE board_audio.c)
endif()

if(CONFIG_INPUT_TOUCHSCREEN)
  target_sources(board PRIVATE board_touch.c)
endif()

if(CONFIG_ESP32P4_MIPI_DSI)
  target_sources(board PRIVATE board_display.c)
endif()

if(CONFIG_ESP32P4_SDMMC)
  target_sources(board PRIVATE board_sdmmc.c)
endif()
