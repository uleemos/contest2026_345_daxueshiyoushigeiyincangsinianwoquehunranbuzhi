############################################################################
# Contest 2026 team 345 SC2336 sensor probe
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = sc2336_probe velafit_demo
PRIORITY  = SCHED_PRIORITY_DEFAULT
# LCD conversion, skeleton/HUD rendering and printf formatting share this
# foreground stack.  4 KiB corrupted the task after roughly 20-30 frames once
# TinyPose/FSM locals were added.
STACKSIZE = 32768
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_345_SC2336_PROBE)

MAINSRC = sc2336_probe_main.c
CSRCS  += sc2336_raw10.c sc2336_model_preview.c
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/include
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/pipeline
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/algo
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_es8311_audio
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_c6_wifi
ifneq ($(VELAFIT_TINY_MODEL_PATH),)
CFLAGS += -DVELAFIT_TINY_POSE=1
endif

include $(APPDIR)/Application.mk
