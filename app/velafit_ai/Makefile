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
CFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_sc2336_probe
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/esp32p4

ifneq ($(CONFIG_VELAFIT_POSE_BACKEND_TFLM),)
CXXEXT = .cc
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro
CXXFLAGS += -I$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += -I$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += -I$(APPDIR)/math/ruy/ruy
CXXFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/include
CXXFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/models
CXXFLAGS += -I$(APPDIR)/packages/demos/contest2026_345_velafit_ai/algo
CXXFLAGS += -DTFLITE_EMULATE_FLOAT -DTF_LITE_DISABLE_X86_NEON
CXXFLAGS += -Wno-shadow -Wno-sign-compare -Wno-undef -Wno-unused-variable
CXXSRCS += models/velafit_pose_tflm.cc
CXXSRCS += models/velafit_pose_pie.cc
CXXSRCS += models/velafit_pose_lut.cc
CXXSRCS += models/velafit_mww_probe.cc
CXXSRCS += models/velafit_mww_frontend.cc
CXXSRCS += models/velafit_mww_stream.cc models/velafit_mww_chain_probe.cc
MWW_FRONTEND = $(APPDIR)/mlearning/tflite-micro/tflite-micro/tensorflow/lite/experimental/microfrontend/lib
MWW_C = frontend frontend_util window window_util filterbank filterbank_util noise_reduction noise_reduction_util pcan_gain_control pcan_gain_control_util log_scale log_scale_util log_lut
CSRCS += $(addprefix $(MWW_FRONTEND)/,$(addsuffix .c,$(MWW_C)))
CXXSRCS += $(addprefix $(MWW_FRONTEND)/,fft.cc fft_util.cc kiss_fft_int16.cc)
CFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro
ASRCS += models/velafit_mww_control.S
AFLAGS += -DVELAFIT_MWW_MODEL_PATH=\"$(realpath $(CURDIR))/../../.secrets/build-inputs/okay_nabu.tflite\"
ifneq ($(VELAFIT_MWW_DEV_PATH),)
AFLAGS += -DVELAFIT_MWW_DEV_PATH=\"$(VELAFIT_MWW_DEV_PATH)\"
endif
ifneq ($(VELAFIT_FIXTURE_PATH),)
AFLAGS += -DVELAFIT_FIXTURE_BUILD_PATH=\"$(VELAFIT_FIXTURE_PATH)\"
endif
ASRCS += models/velafit_pose_fixture.S
ifneq ($(VELAFIT_TINY_MODEL_PATH),)
CFLAGS += -DVELAFIT_TINY_POSE=1
CXXFLAGS += -DVELAFIT_TINY_POSE=1
CSRCS += models/velafit_tiny_pose_preprocess.c
CXXSRCS += models/velafit_tiny_pose_tflm.cc
ASRCS += models/velafit_tiny_pose_assets.S
AFLAGS += -DVELAFIT_TINY_MODEL_PATH=\"$(VELAFIT_TINY_MODEL_PATH)\"
AFLAGS += -DVELAFIT_TINY_FIXTURE_PATH=\"$(VELAFIT_TINY_FIXTURE_PATH)\"
AFLAGS += -DVELAFIT_TINY_ORACLE_PATH=\"$(VELAFIT_TINY_ORACLE_PATH)\"
endif
endif

CSRCS += engine/esp_nn_ops.c
CSRCS += engine/velafit_fpu_probe.c
CSRCS += engine/velafit_hwlp_probe.c
CSRCS += engine/velafit_pie_switch.c
CSRCS += engine/velafit_esp_nn_probe.c
CSRCS += engine/velafit_esp_nn_lock.c
ifneq ($(VELAFIT_ESP_NN_LIB),)
CFLAGS += -DVELAFIT_ESP_NN_TEST=1 -I$(VELAFIT_ESP_NN_ROOT)/include
CXXFLAGS += -DVELAFIT_ESP_NN_TEST=1 -I$(VELAFIT_ESP_NN_ROOT)/include
ifeq ($(VELAFIT_ESP_NN_V132),1)
CXXFLAGS += -DVELAFIT_ESP_NN_V132=1
CFLAGS += -DVELAFIT_ESP_NN_V132=1
endif
ifeq ($(VELAFIT_QACC_CONV),1)
CXXFLAGS += -DVELAFIT_QACC_CONV=1
CFLAGS += -DVELAFIT_QACC_CONV=1
endif
ifeq ($(VELAFIT_POSE_LUT),1)
CXXFLAGS += -DVELAFIT_POSE_LUT=1
endif
ifeq ($(VELAFIT_POSE_BINARY_LUT),1)
CXXFLAGS += -DVELAFIT_POSE_BINARY_LUT=1 -DVELAFIT_POSE_LUT=1
endif
endif
ASRCS += engine/velafit_hwlp_switch.S
CSRCS += engine/velafit_ppa_bench.c
CSRCS += engine/velafit_touch.c
CSRCS += models/velafit_pose_model.c
CSRCS += models/sample_pose_frames.c
CSRCS += algo/one_euro_filter.c
CSRCS += algo/geometry.c
CSRCS += algo/calorie_calc.c
CSRCS += algo/squat_fsm.c
CSRCS += algo/velafit_body_check.c
CSRCS += algo/jumping_jack_fsm.c
CSRCS += algo/pushup_fsm.c
CSRCS += algo/plank_fsm.c
CSRCS += algo/velafit_kws.c
CSRCS += algo/velafit_kws_acoustic.c
CSRCS += algo/velafit_voice_command.c
CSRCS += algo/velafit_resample.c
CSRCS += render/velafit_render.c
CSRCS += audio/velafit_audio_cue.c
CSRCS += storage/velafit_storage.c
CSRCS += sync/velafit_sync.c
CSRCS += sync/velafit_outbox.c
CSRCS += sync/velafit_outbox_service.c
CSRCS += sync/velafit_cloud_agent.c
CSRCS += sync/velafit_config.c
CSRCS += pipeline/velafit_pipeline.c
CSRCS += pipeline/velafit_pose_worker.c
CSRCS += pipeline/velafit_session.c
CSRCS += plan/velafit_plan_scheduler.c
CSRCS += plan/velafit_preset_plans.c

MAINSRC = velafit_ai_main.c

include $(APPDIR)/Application.mk

.PHONY: velafit_esp_nn_probe_rebuild
$(filter %velafit_esp_nn_probe.c$(SUFFIX)$(OBJEXT),$(COBJS)): velafit_esp_nn_probe_rebuild
$(filter %velafit_pose_pie.cc$(SUFFIX)$(OBJEXT) %velafit_pose_tflm.cc$(SUFFIX)$(OBJEXT),$(CXXOBJS)): velafit_esp_nn_probe_rebuild

ifeq ($(CONFIG_VELAFIT_POSE_BACKEND_TFLM),y)
.PHONY: velafit_assets_reembed
$(CAOBJS): velafit_assets_reembed
endif
