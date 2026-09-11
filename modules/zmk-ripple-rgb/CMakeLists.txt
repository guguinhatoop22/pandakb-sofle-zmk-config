# Copyright (c) 2026 guguinhatoop22
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_RIPPLE_RGB)
  zephyr_library()
  zephyr_library_sources(src/ripple_rgb.c)
  zephyr_include_directories(include)

  # Extend ZMK underglow effect cycle with Ripple (effect index 4) and
  # keep a local mirror of animation_speed. Peripheral half also wraps the
  # split command handler to receive compact cross-half ripple events.
  zephyr_ld_options(
    -Wl,--wrap=zmk_rgb_underglow_calc_effect
    -Wl,--wrap=zmk_rgb_underglow_select_effect
    -Wl,--wrap=zmk_rgb_underglow_cycle_effect
    -Wl,--wrap=zmk_rgb_underglow_on
    -Wl,--wrap=zmk_rgb_underglow_off
    -Wl,--wrap=zmk_rgb_underglow_change_spd
  )
  if(CONFIG_ZMK_SPLIT AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_ld_options(-Wl,--wrap=zmk_split_transport_peripheral_command_handler)
  endif()
endif()
