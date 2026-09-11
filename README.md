# PandaKB Sofle RGB MX ZMK Config

[![Build](https://github.com/guguinhatoop22/pandakb-sofle-zmk-config/actions/workflows/build.yml/badge.svg)](https://github.com/guguinhatoop22/pandakb-sofle-zmk-config/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/guguinhatoop22/pandakb-sofle-zmk-config?label=latest%20firmware&color=blue)](https://github.com/guguinhatoop22/pandakb-sofle-zmk-config/releases/latest)

An advanced ZMK configuration for the **PandaKB Sofle RGB MX** split keyboard, running on the [PandaKB shield](https://github.com/PandaKBLab/zmk-for-PandaKB/tree/PandaKB_Sofle) hardware definition.

This repository extends the original work by **[AlexandrLo](https://github.com/AlexandrLo/pandakb-sofle-zmk-config)**, adding an **independent cross-half reactive RGB Ripple engine** with native Underglow binding, NVS boot synchronization, optimized split BLE packet architecture, and physical PCB geometry mapping, alongside customized keymaps, dongle support, and dual OLED animations.

---

## 🌟 Highlights

* **🌊 Cross-Half Reactive RGB Ripple Engine**:
  * Acts as an **independent 5th lighting effect** (`Effect 4` in ZMK Underglow).
  * Smooth physical wave propagation across the split halves when any key is pressed on either side.
  * Seamlessly bound to **native ZMK Underglow controls** (Hue, Saturation, Brightness, Speed, Toggle) as the **single source of truth**—no parallel state, no duplicate settings.
  * Automatic restoration of `animation_speed` from flash/NVS on boot via Zephyr Settings commit hook.
  * Ultra-lean BLE transport: propagates only `(origin_key_id, event_id)`—zero continuous RGB frames broadcast over radio, preserving bandwidth and battery life.
* **Six functional layers**: `Base`, `Symbols`, `Ruchey`, `Symbols*`, `Editing`, `Adjust`.
  * `Base`/`Symbols` is the English QWERTY pair.
  * `Ruchey`/`Symbols*` types Cyrillic via AltGr/Shift combos for the [Ruchey](https://github.com/a-projects/ruchey) layout.
  * `Adjust` switches between `Base` and `Ruchey`, and activates when `Editing` is held with `Symbols`/`Symbols*`.
* **Dual Encoders**: Left encoder controls volume; right encoder switches virtual desktops (`Ctrl+Alt+PageUp/PageDown`).
* **Intelligent Power Management**: RGB underglow automatically powers down on idle and USB disconnect, preventing battery drain and unwanted glow after host shutdown.
* **Dual OLED Displays & Dongle Support**: Animated Luna WPM graph, Pokémon animations, layer/battery indicators, and dedicated dongle firmware for both nice!nano and Prospector (Xiao BLE).

---

## ⬇️ Download Firmware

**[👉 Download the latest firmware.zip](https://github.com/guguinhatoop22/pandakb-sofle-zmk-config/releases/latest)**

Every successful build on `main` automatically republishes this rolling `latest` release with a `firmware.zip` containing all `.uf2` binaries. No local toolchain or GitHub account required—simply download, unzip, and flash your target board.

---

## 🌊 Independent Cross-Half RGB Ripple Effect

The keyboard features a custom, physics-inspired **Reactive RGB Ripple engine** engineered specifically for split keyboards running ZMK.

```
       LEFT HALF (e.g. Key Press)               RIGHT HALF
 ┌────────────────────────────────────┐   ┌────────────────────────────────────┐
 │ [Key Matrix Event]                 │   │                                    │
 │        │                           │   │                                    │
 │ [Local Ripple Trigger]             │   │                                    │
 │        │                           │   │                                    │
 │ [Split Transport Wrapper]          │   │                                    │
 │   param1 = global_sw               │   │                                    │
 │   param2 = event_id                │   │                                    │
 └─────────────────┬──────────────────┘   └─────────────────▲──────────────────┘
                   │                                        │
                   │  BLE Minimal Packet                    │
                   │  (No RGB streams, ~2 bytes payload)    │
                   └────────────────────────────────────────┘
                                            │
                         [Opposite Half Receives Event]
                                            │
                         [Computes Inter-Half PCB Distance]
                                            │
                         [Renders Wave via k_work (100 Hz)]
```

### 1. Architectural Principles

1. **Native Underglow as Single Source of Truth**:
   * The Ripple effect does **not** maintain isolated color, speed, or power variables.
   * **Hue & Saturation**: Queries `zmk_rgb_underglow_calc_hue(0)` on every animation frame. Adjusting `&rgb_ug RGB_HUI` / `&rgb_ug RGB_HUD` dynamically changes the ripple color. Decreasing saturation with `RGB_SAD` transitions the wave smoothly into pure white.
   * **Brightness**: Scales proportionally with `RGB_BRI` / `RGB_BRD`. The maximum ripple peak is capped at **35%** ($\frac{35 \times \text{brightness}}{100}$) to ensure maximum eye comfort and prevent brownouts during rapid typing.
   * **Speed**: Driven by `RGB_SPI` / `RGB_SPD` ($S \in [1..5]$). Uses an inverse-duration physical curve:
     $$\text{duration}(S) = \frac{1600}{S + 1}\text{ ms}$$
     * Speed 1: $800\text{ ms}$ (gentle, expansive wave)
     * Speed 2: $533\text{ ms}$
     * Speed 3: $400\text{ ms}$ (default)
     * Speed 4: $320\text{ ms}$
     * Speed 5: $267\text{ ms}$ (rapid, energetic wave)
   * **Toggle / Power**: Immediately honors `RGB_TOG` / `RGB_OFF`. When underglow is toggled off, any active ripple wave is instantaneously cleared and the rendering workqueue halts.

2. **NVS Settings & Boot Synchronization**:
   * Underglow settings in ZMK are persisted in non-volatile flash (NVS key `rgb/underglow/state`).
   * To prevent desynchronization on boot (e.g. user selected Speed 4 in a previous session, but firmware defaults to Speed 1), a Zephyr static settings handler (`SETTINGS_STATIC_HANDLER_DEFINE`) hooks into the `.h_commit` lifecycle.
   * Upon boot completion, the exact `animation_speed` byte stored in flash is loaded directly into the local runtime mirror without introducing any proprietary storage keys.

3. **Ultra-Lean BLE Cross-Half Protocol**:
   * Traditional wireless RGB solutions transmit full LED color frames over Bluetooth, causing packet congestion, latency, and battery drain.
   * Our implementation wraps the low-level split transport (`zmk_split_transport_peripheral_send_event` and `zmk_split_transport_peripheral_command_handler`) via GCC linker flags (`-Wl,--wrap`).
   * Only the key switch ID and an incremental event ID are transmitted. Each half independently computes wave propagation and LED rendering locally in a cooperative Zephyr workqueue (`k_work`).

4. **True Physical PCB Geometry**:
   * LED coordinates and switch positions are not approximated with arbitrary grids.
   * The wave calculations use an exact Euclidean distance lookup table pre-computed directly from the **KiCad PCB layout** (`SofleKeyboard.kicad_pcb`), incorporating an accurate physical gap (~80 mm) between the split halves.
   * Key presses on the outer column of the left half naturally travel inward, "leap" across the split gap, and continue across the right half with seamless spatial continuity.

5. **Universal Topology Support**:
   * **Standalone Split**: Left half (Central/USB) $\leftrightarrow$ Right half (Peripheral/BLE).
   * **Dongle Central**: Left (Peripheral) $\leftrightarrow$ Central Dongle (USB) $\leftrightarrow$ Right (Peripheral) with intelligent forwarding and loopback/echo suppression.

---

## 🎮 Underglow Controls Reference

The Ripple effect can be cycled into using the standard Underglow keymap behaviors:

| Behavior | Description | Ripple Effect Response |
| :--- | :--- | :--- |
| `&rgb_ug RGB_EFF` / `RGB_EFR` | Cycle Effects Forward / Backward | Selects Effect 4 (Ripple) |
| `&rgb_ug RGB_TOG` | Toggle Underglow On / Off | Instantly enables or blanks the ripple |
| `&rgb_ug RGB_HUI` / `RGB_HUD` | Increase / Decrease Hue | Changes wave color dynamically |
| `&rgb_ug RGB_SAI` / `RGB_SAD` | Increase / Decrease Saturation | Adjusts color purity (0% = pure white) |
| `&rgb_ug RGB_BRI` / `RGB_BRD` | Increase / Decrease Brightness | Adjusts peak wave luminescence (up to 35%) |
| `&rgb_ug RGB_SPI` / `RGB_SPD` | Increase / Decrease Speed | Adjusts wave velocity and lifespan ($1..5$) |

---

## 🛠️ Build Variants

This repository builds two primary architectures across eight distinct firmware targets:

### 1. Standalone (Dongle-less)
* **`Sofle_L_oled` / `Sofle_R_oled`** (`nice_nano_v2`):
  * Left half acts as BLE central and connects via USB to the host computer.
  * Left OLED displays WPM graph and system status; right OLED displays the Guguinhatop graphic.
  * Cross-half ripple broadcasts directly from Central to Peripheral and vice-versa.

### 2. Wireless Dongle Setup
* Neither half connects directly to the host; an external USB dongle acts as BLE central:
  * **`Sofle_dongle`** (`nice_nano_v2` + SSD1306 OLED):
    * Horizontal 128x32 display with Luna running animation, active layer, modifiers, and host status.
    * Pinout: `GND` $\rightarrow$ `GND`, `VCC` $\rightarrow$ `3V3`, `SDA` $\rightarrow$ `D2` (`P0.17`), `SCL` $\rightarrow$ `D3` (`P0.20`).
  * **`Sofle_dongle_prospector`** ([Prospector](https://github.com/carrefinho/prospector) adapter with Seeed Xiao nRF52840 + circular LCD).
  * **`Sofle_dongle_L` / `Sofle_dongle_R`** (`nice_nano_v2`):
    * Firmware for the left and right halves when operating with a dongle.
    * Left OLED features animated Pokémon; right OLED features Guguinhatop.
  * **`settings_reset`** builds for both `nice_nano_v2` and `seeeduino_xiao_ble` to wipe BLE bonding metadata when re-pairing.

> [!TIP]
> `config/Sofle_dongle.keymap` is a symlink to `config/Sofle.keymap`, ensuring keymap edits automatically apply across both standalone and dongle variants.

---

## ⚡ Flashing Instructions

1. Download and extract `firmware.zip` from the **[latest release](https://github.com/guguinhatoop22/pandakb-sofle-zmk-config/releases/latest)**.
2. Put the target board into bootloader mode by **double-pressing the reset button**. The board will mount as a USB storage device (`NICENANO` or `XIAO-SENSE`).
3. Drag and drop the matching `.uf2` file onto the drive:
   * **Standalone**:
     * Flash `Sofle_L_oled.uf2` to the left half.
     * Flash `Sofle_R_oled.uf2` to the right half.
   * **Dongle Setup**:
     * Flash `Sofle_dongle_L.uf2` to the left half.
     * Flash `Sofle_dongle_R.uf2` to the right half.
     * Flash `Sofle_dongle.uf2` or `Sofle_dongle_prospector.uf2` to the dongle.
     * *Note: Power on the left half first, then the right half, to ensure battery indicators order correctly.*
4. To reset Bluetooth bonds, flash `settings_reset.uf2` to the board, wait for it to restart, then re-flash the desired firmware.

---

## ⌨️ Keymap Layout

![keymap-drawer-demo-corne](keymap-drawer/Sofle.svg)

---

## 🤝 Credits & Acknowledgments

This project stands on the shoulders of the open-source keyboard community:

* **[AlexandrLo](https://github.com/AlexandrLo/pandakb-sofle-zmk-config)**: Creator of the upstream repository, original multi-layer keymap architecture, dual OLED animations (Luna & Pokémon), and dongle configurations.
* **[PandaKBLab](https://github.com/PandaKBLab/zmk-for-PandaKB/tree/PandaKB_Sofle)**: Hardware definition and shield designs for the PandaKB Sofle RGB MX.
* **[ZMK Firmware](https://zmk.dev/)**: The powerful wireless keyboard firmware powering this project.
* **[guguinhatoop22](https://github.com/guguinhatoop22/pandakb-sofle-zmk-config)**: Design and implementation of the independent cross-half RGB Ripple engine, native Underglow state binding, NVS boot speed synchronization, and split BLE transport interception.