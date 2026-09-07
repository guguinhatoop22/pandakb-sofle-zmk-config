# PandaKB Sofle RGB MX ZMK Config

My ZMK configuration for the PandaKB Sofle RGB MX keyboard, running on the
[PandaKB shield](https://github.com/PandaKBLab/zmk-for-PandaKB/tree/PandaKB_Sofle) hardware
definition. The keymap, power/lighting behavior, and build targets have all been rewritten to
my own layout and workflow.

## Highlights

* Six layers: `Base`, `Symbols`, `Ruchey`, `Symbols*`, `Editing`, `Adjust`. `Base`/`Symbols` is
  the English QWERTY pair; `Ruchey`/`Symbols*` types Cyrillic via AltGr/Shift combos for the
  [Ruchey](https://github.com/a-projects/ruchey) layout. `Adjust` switches between `Base` and
  `Ruchey`, and also activates itself whenever `Editing` is held together with either
  `Symbols` or `Symbols*`.
* The left encoder controls volume; the right encoder switches virtual desktops
  (`Ctrl+Alt+PageUp/PageDown`).
* RGB underglow turns off on idle and USB disconnect so the keyboard does not keep glowing
  after the host shuts down.

## Download Firmware

**[⬇ Download the latest firmware.zip](https://github.com/AlexandrLo/pandakb-sofle-zmk-config/releases/latest)**

Every successful build of `main` republishes this rolling `latest` release with a
`firmware.zip` containing all `.uf2` files. No GitHub account needed — unzip it and pick the
file for the device you are flashing.

## Build Variants

Two independent build variants live in this repo; you choose which firmware to flash per
device, and both can coexist:

* **Standalone (dongle-less), SSD1306 OLED** — `Sofle_L_oled` / `Sofle_R_oled`
  (`nice_nano_v2`). Each half runs `Sofle_L`/`Sofle_R`, `Sofle_L` is BLE central by default,
  and both halves show status on their OLED.
* **Dongle** — neither half is central; flash a dongle firmware as the BLE central. Two
  dongle boards are built:
  * `Sofle_dongle` — `nice_nano_v2` + a horizontal SSD1306 (same 128x32 panel as the
    halves). Wire `GND`→`GND`, `VCC`→`3V3`, `SDA`→`D2` (`P0.17`), `SCL`→`D3` (`P0.20`).
    The screen shows host output (USB/BT + profile), the active layer, held modifiers,
    and Luna (WPM from both halves).
  * `Sofle_dongle_prospector` — [Prospector](https://github.com/carrefinho/prospector)
    (Seeed XIAO nRF52840 + round LCD); shows layer, battery, and connection status.
  * `Sofle_dongle_L` / `Sofle_dongle_R` — flash to each half (`nice_nano_v2`). Left OLED
    is Luna; right OLED is the static Guguinhatop image.
  * A separate `settings_reset` build is provided for each board (`nice_nano_v2` and
    `seeeduino_xiao_ble`) so BLE bonds can be reset independently.

  After flashing, pair the **left half first, then the right half** — battery widgets
  that list both halves order themselves by pairing order.

`config/Sofle_dongle.keymap` is a symlink to `config/Sofle.keymap`, so keymap edits apply to
both variants automatically.

## Flashing

1. Download and unzip `firmware.zip` from the [`latest` release](https://github.com/AlexandrLo/pandakb-sofle-zmk-config/releases/latest).
2. Put the target board into bootloader mode by double-tapping its reset button. It shows up
   on your computer as a USB mass storage drive (`NICENANO` for `nice_nano_v2`, `XIAO-SENSE`
   for `seeeduino_xiao_ble`).
3. Drag and drop the matching `.uf2` file onto that drive. The board reboots into the new
   firmware automatically once the copy finishes.
4. Repeat per board/half:
   * Standalone: `Sofle_L_oled.uf2` → left half, `Sofle_R_oled.uf2` → right half.
   * Dongle variant: `Sofle_dongle_L.uf2` → left half, `Sofle_dongle_R.uf2` → right half,
     then either `Sofle_dongle.uf2` → the nice!nano + SSD1306 dongle or
     `Sofle_dongle_prospector.uf2` → the Prospector dongle. Flash and power on the **left
     half first, then the right half** so battery widgets order themselves correctly.

To reset BLE bonds on a board, flash its `settings_reset.uf2` (same bootloader-drive process),
let it finish rebooting once, then flash the normal firmware `.uf2` back onto it.

## Keymap

![keymap-drawer-demo-corne](keymap-drawer/Sofle.svg)