/* SPDX-License-Identifier: Apache-2.0 */
  .section .rodata.velafit_mww_control,"a",@progbits
  .balign 16
  .global g_velafit_mww_control
g_velafit_mww_control:
  .incbin VELAFIT_MWW_MODEL_PATH
  .global g_velafit_mww_control_end
g_velafit_mww_control_end:
#ifdef VELAFIT_MWW_DEV_PATH
  .balign 16
  .global g_velafit_mww_development
g_velafit_mww_development:
  .incbin VELAFIT_MWW_DEV_PATH
  .global g_velafit_mww_development_end
g_velafit_mww_development_end:
#endif
