############################################################################
# Contest 2026 team 345 VelaFit Edge AI Suite
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = velafit_ai
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 32768
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI)

CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/include
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/engine
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/models
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/algo
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/render
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/audio
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/storage
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/sync
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/pipeline
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/plan
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/esp32p4

ifneq ($(CONFIG_VELAFIT_POSE_BACKEND_TFLM),)
CXXEXT = .cc
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro
CXXFLAGS += -I$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += -I$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += -I$(APPDIR)/math/ruy/ruy
CXXFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/include
CXXFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/models
CXXFLAGS += -DTFLITE_EMULATE_FLOAT -DTF_LITE_DISABLE_X86_NEON
CXXFLAGS += -Wno-shadow -Wno-sign-compare -Wno-undef -Wno-unused-variable
CXXSRCS += models/velafit_pose_tflm.cc
ASRCS += models/velafit_pose_fixture.S
endif

CSRCS += engine/esp_nn_ops.c
CSRCS += engine/velafit_ppa_bench.c
CSRCS += engine/velafit_touch.c
CSRCS += models/velafit_pose_model.c
CSRCS += models/sample_pose_frames.c
CSRCS += algo/one_euro_filter.c
CSRCS += algo/geometry.c
CSRCS += algo/calorie_calc.c
CSRCS += algo/squat_fsm.c
CSRCS += algo/jumping_jack_fsm.c
CSRCS += algo/pushup_fsm.c
CSRCS += algo/plank_fsm.c
CSRCS += algo/velafit_kws.c
CSRCS += render/velafit_render.c
CSRCS += audio/velafit_audio_cue.c
CSRCS += storage/velafit_storage.c
CSRCS += sync/velafit_sync.c
CSRCS += sync/velafit_cloud_agent.c
CSRCS += sync/velafit_config.c
CSRCS += pipeline/velafit_pipeline.c
CSRCS += plan/velafit_plan_scheduler.c
CSRCS += plan/velafit_preset_plans.c

MAINSRC = velafit_ai_main.c

include $(APPDIR)/Application.mk
