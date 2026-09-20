# Contest 2026 team 345 VelaFit Edge AI Suite

if(CONFIG_LVX_USE_DEMO_CONTEST2026_345_VELAFIT_AI)
  if(CONFIG_VELAFIT_POSE_BACKEND_TFLM)
    set(VELAFIT_TFLM_SRCS
      models/velafit_pose_tflm.cc
      models/velafit_pose_lut.cc
      models/velafit_pose_pie.cc
      models/velafit_mww_probe.cc
      models/velafit_mww_frontend.cc
      models/velafit_mww_stream.cc
      models/velafit_mww_chain_probe.cc
      models/velafit_mww_control.S
      models/velafit_pose_fixture.S)
    set(MWW_FRONTEND ${NUTTX_APPS_DIR}/mlearning/tflite-micro/tflite-micro/tensorflow/lite/experimental/microfrontend/lib)
    foreach(name frontend frontend_util window window_util filterbank filterbank_util
        noise_reduction noise_reduction_util pcan_gain_control
        pcan_gain_control_util log_scale log_scale_util log_lut)
      list(APPEND VELAFIT_TFLM_SRCS ${MWW_FRONTEND}/${name}.c)
    endforeach()
    foreach(name fft fft_util kiss_fft_int16)
      list(APPEND VELAFIT_TFLM_SRCS ${MWW_FRONTEND}/${name}.cc)
    endforeach()
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
      ${NUTTX_APPS_DIR}/math/kissfft/kissfft
    SRCS
      engine/esp_nn_ops.c
      engine/velafit_fpu_probe.c
      engine/velafit_hwlp_probe.c
      engine/velafit_pie_switch.c
      engine/velafit_esp_nn_probe.c
      engine/velafit_esp_nn_lock.c
      engine/velafit_hwlp_switch.S
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
      algo/velafit_kws_acoustic.c
      algo/velafit_voice_command.c
      algo/velafit_resample.c
      render/velafit_render.c
      audio/velafit_audio_cue.c
      storage/velafit_storage.c
      sync/velafit_sync.c
    sync/velafit_outbox.c
    sync/velafit_outbox_service.c
      sync/velafit_cloud_agent.c
      sync/velafit_config.c
      pipeline/velafit_pipeline.c
      pipeline/velafit_pose_worker.c
      pipeline/velafit_session.c
      plan/velafit_plan_scheduler.c
      plan/velafit_preset_plans.c
      velafit_ai_main.c
  )

  if(CONFIG_VELAFIT_POSE_BACKEND_TFLM)
    get_filename_component(VELAFIT_SOURCE_REAL ${CMAKE_CURRENT_SOURCE_DIR} REALPATH)
    target_compile_definitions(apps_velafit_ai PRIVATE
      VELAFIT_MWW_MODEL_PATH="${VELAFIT_SOURCE_REAL}/../../.secrets/build-inputs/okay_nabu.tflite")
    if(DEFINED ENV{VELAFIT_FIXTURE_PATH})
      target_compile_definitions(apps_velafit_ai PRIVATE
        VELAFIT_FIXTURE_BUILD_PATH="$ENV{VELAFIT_FIXTURE_PATH}")
    endif()
    if(DEFINED ENV{VELAFIT_MWW_DEV_PATH})
      target_compile_definitions(apps_velafit_ai PRIVATE
        VELAFIT_MWW_DEV_PATH="$ENV{VELAFIT_MWW_DEV_PATH}")
    endif()
    if(DEFINED ENV{VELAFIT_TINY_MODEL_PATH})
      target_sources(apps_velafit_ai PRIVATE
        models/velafit_tiny_pose_tflm.cc
        models/velafit_tiny_pose_assets.S)
      target_compile_definitions(apps_velafit_ai PRIVATE
        VELAFIT_TINY_POSE=1
        VELAFIT_TINY_MODEL_PATH="$ENV{VELAFIT_TINY_MODEL_PATH}"
        VELAFIT_TINY_FIXTURE_PATH="$ENV{VELAFIT_TINY_FIXTURE_PATH}"
        VELAFIT_TINY_ORACLE_PATH="$ENV{VELAFIT_TINY_ORACLE_PATH}")
    endif()
    target_compile_options(apps_velafit_ai PRIVATE
      -DTFLITE_EMULATE_FLOAT
      -DTF_LITE_DISABLE_X86_NEON)
    add_dependencies(apps_velafit_ai tflite_micro)
  endif()
endif()
