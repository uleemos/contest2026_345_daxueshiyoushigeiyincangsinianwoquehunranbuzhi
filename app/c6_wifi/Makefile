############################################################################
# Contest 2026 team 345 ESP32-C6 Wi-Fi link utility
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = c6_wifi
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 32768
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_345_C6_WIFI)

MAINSRC = c6_wifi_main.c
CSRCS   = c6_mimo_tls.c

include $(APPDIR)/Application.mk
