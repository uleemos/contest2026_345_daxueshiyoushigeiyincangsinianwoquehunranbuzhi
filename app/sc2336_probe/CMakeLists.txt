# Contest 2026 team 345 SC2336 sensor probe

if(CONFIG_LVX_USE_DEMO_CONTEST2026_345_SC2336_PROBE)
  nuttx_add_application(
    NAME sc2336_probe
    INCLUDE_DIRECTORIES ../velafit_ai/include
    SRCS sc2336_probe_main.c sc2336_raw10.c sc2336_model_preview.c
    STACKSIZE 4096)
endif()
