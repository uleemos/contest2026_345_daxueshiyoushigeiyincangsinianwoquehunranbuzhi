############################################################################
# Contest 2026 team 345 ESP32-P4X Function EV Board
############################################################################

include $(TOPDIR)/Make.defs

CSRCS = board_boot.c

ifeq ($(CONFIG_DEV_GPIO),y)
ifneq ($(CONFIG_GPIO_LOWER_HALF),y)
CSRCS += board_gpio.c
endif
endif

ifeq ($(CONFIG_ESPRESSIF_I2C0),y)
CSRCS += board_i2c.c
endif

ifeq ($(CONFIG_ESPRESSIF_SPIFLASH_SMARTFS),y)
CSRCS += board_spiflash.c
endif

ifeq ($(CONFIG_AUDIO_ES8311),y)
CSRCS += board_audio.c
endif

ifeq ($(CONFIG_ESP32P4_MIPI_DSI),y)
CSRCS += board_display.c
endif

include $(TOPDIR)/boards/Board.mk
