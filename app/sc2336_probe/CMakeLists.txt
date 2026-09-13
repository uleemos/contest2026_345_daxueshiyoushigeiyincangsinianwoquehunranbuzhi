# Contest 2026 team 345 SC2336 sensor probe

if(CONFIG_LVX_USE_DEMO_CONTEST2026_345_SC2336_PROBE)
  nuttx_add_application(
    NAME sc2336_probe
    SRCS sc2336_probe_main.c sc2336_raw10.c
    STACKSIZE 2048)
endif()
