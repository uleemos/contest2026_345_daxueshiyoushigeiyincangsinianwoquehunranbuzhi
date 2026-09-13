# Contest 2026 team 345 VelaFit Edge AI Suite

if(CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI)
  if(CONFIG_VELAFIT_POSE_BACKEND_TFLM)
    set(VELAFIT_TFLM_SRCS
      models/velafit_pose_tflm.cc
      models/velafit_pose_fixture.S)
  endif()

  nuttx_add_application(
    NAME velafit_ai
    PRIORITY ${CONFIG_SCHED_PRIORITY_DEFAULT}
    STACKSIZE 32768
    MODULE ${CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI}
    INCLUDE_DIRECTORIES
      include
      engine
      models
      algo
      render
      audio
      storage
      sync
      pipeline
      plan
      ${NUTTX_DIR}/arch/risc-v/src/esp32p4
      ${NUTTX_APPS_DIR}/mlearning/tflite-micro/tflite-micro
    SRCS
      engine/esp_nn_ops.c
      engine/velafit_ppa_bench.c
      engine/velafit_touch.c
      models/velafit_pose_model.c
      models/sample_pose_frames.c
      ${VELAFIT_TFLM_SRCS}
      algo/one_euro_filter.c
      algo/geometry.c
      algo/calorie_calc.c
      algo/squat_fsm.c
      algo/jumping_jack_fsm.c
      algo/pushup_fsm.c
      algo/plank_fsm.c
      algo/velafit_kws.c
      render/velafit_render.c
      audio/velafit_audio_cue.c
      storage/velafit_storage.c
      sync/velafit_sync.c
      sync/velafit_cloud_agent.c
      sync/velafit_config.c
      pipeline/velafit_pipeline.c
      plan/velafit_plan_scheduler.c
      plan/velafit_preset_plans.c
      velafit_ai_main.c
  )

  if(CONFIG_VELAFIT_POSE_BACKEND_TFLM)
    target_compile_options(apps_velafit_ai PRIVATE
      -DTFLITE_EMULATE_FLOAT
      -DTF_LITE_DISABLE_X86_NEON)
    add_dependencies(apps_velafit_ai tflite_micro)
  endif()
endif()
