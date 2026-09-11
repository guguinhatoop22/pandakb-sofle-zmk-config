# Copyright (c) 2026 guguinhatoop22
# SPDX-License-Identifier: MIT

if(CONFIG_SOFLE_DUAL_ROLE)
  set(ZMK_APP ${APPLICATION_SOURCE_DIR})

  zephyr_linker_sources(SECTIONS ${ZMK_APP}/include/linker/zmk-split-transport-peripheral.ld)

  # Única colisão de símbolo entre os dois papéis: o global `active_transport`,
  # que o upstream esqueceu de marcar static nos dois arquivos.
  set(DUAL_ROLE_SHIM ${CMAKE_CURRENT_BINARY_DIR}/sofle_peripheral_role_shim.c)
  file(WRITE ${DUAL_ROLE_SHIM}
       "#define active_transport sofle_peripheral_active_transport\n"
       "#include \"${ZMK_APP}/src/split/peripheral.c\"\n")

  target_sources(app PRIVATE
    ${DUAL_ROLE_SHIM}
    ${ZMK_APP}/src/split/bluetooth/peripheral.c
    ${ZMK_APP}/src/split/bluetooth/service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dual_role/dual_role.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dual_role/dual_role_wraps.c
  )
  target_include_directories(app PRIVATE ${ZMK_APP}/include ${ZMK_APP}/src/split/bluetooth)

  zephyr_ld_options(-Wl,--wrap=bt_le_adv_start -Wl,--wrap=bt_conn_auth_info_cb_register -Wl,--wrap=bt_conn_auth_cb_register)
endif()

if(CONFIG_SOFLE_SPLIT_PERIPHERAL_OPEN_ADV)
  zephyr_ld_options(-Wl,--wrap=bt_foreach_bond)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/dual_role/right_open_adv.c)
endif()
