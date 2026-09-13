############################################################################
# Contest 2026 team 345 SC2336 sensor probe
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = sc2336_probe
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_345_SC2336_PROBE)

MAINSRC = sc2336_probe_main.c
CSRCS  += sc2336_raw10.c

include $(APPDIR)/Application.mk
