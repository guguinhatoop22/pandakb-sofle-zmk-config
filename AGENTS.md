# AGENTS.md

This is a ZMK user config plus a local `Sofle` shield for a PandaKB Sofle RGB MX. It is not the full ZMK source tree; ZMK discovers the local shield through `zephyr/module.yml` (`board_root: .`).

## Build Targets

`build.yaml` produces exactly these GitHub Actions artifacts:

```yaml
- nice_nano_v2 + "Sofle_L nice_oled", snippet: studio-rpc-usb-uart, artifact-name: Sofle_L_oled
- nice_nano_v2 + "Sofle_R nice_oled", artifact-name: Sofle_R_oled
- nice_nano_v2 + settings_reset
- nice_nano_v2 + Sofle_dongle_left, artifact-name: Sofle_dongle_L
- nice_nano_v2 + Sofle_dongle_right, artifact-name: Sofle_dongle_R
- seeeduino_xiao_ble + "Sofle_dongle_central prospector_adapter", snippet: studio-rpc-usb-uart, artifact-name: Sofle_dongle_prospector
- seeeduino_xiao_ble + settings_reset
```

On pushes to `main`, `.github/workflows/build.yml` also republishes the rolling `latest` GitHub
Release, packing the merged `firmware` artifact into a single `firmware.zip`. The release body
lists whatever the artifact actually contains, so build target changes need no workflow edit.

The build workflow delegates to `zmkfirmware/zmk/.github/workflows/build-user-config.yml@v0.3`; dependency versions come from `config/west.yml` (`zmk` default revision `v0.3`, `zmk-nice-oled` from `mctechnology17` on `main`, `self.path: config`). The dongle build also pulls in `prospector-zmk-module` (`carrefinho`, `main`). Do not casually change revisions or remove `zephyr/module.yml`.

## Where To Change Things

- User key behavior, layers, combos, macros, and encoder actions: `config/Sofle.keymap`.
- Firmware feature flags and power/display/RGB/BLE options: `config/Sofle.conf` (standalone OLED halves) or `config/Sofle_dongle_left.conf` / `config/Sofle_dongle_right.conf` / `config/Sofle_dongle_central.conf` (dongle variant).
- Build target or artifact changes: `build.yaml`.
- ZMK/module dependency changes: `config/west.yml`.
- Matrix pins, encoder pins, OLED, RGB chain length, or transforms: `boards/shields/Sofle/` only when the task is explicitly hardware-related.
- Prospector dongle hardware/role (central vs. peripheral, mock kscan): `boards/shields/Sofle_dongle/` only when the task is explicitly dongle-hardware-related.
- ZMK Studio physical layout: keep `boards/shields/Sofle/Sofle-layouts.dtsi`, `boards/shields/Sofle/Sofle.dtsi`, and `config/Sofle.json` consistent.

There are two `Sofle.keymap` files. `config/Sofle.keymap` is the active personal layout used by builds; `boards/shields/Sofle/Sofle.keymap` is the shield default/example. Do not update the shield default for ordinary personal keymap edits unless explicitly requested.

There is also `config/Sofle_dongle.keymap`, a symlink to `config/Sofle.keymap` used by the `Sofle_dongle_central`/`_left`/`_right` shields. Edits to `config/Sofle.keymap` apply automatically — do not replace the symlink with a real file.

## Keymap Constraints

- Active layers are `BASE 0`, `GAMING 1`, `SYMBOLS 2`, `EDITING 3`, `ADJUST 4`; `ADJUST` is a conditional layer when `SYMBOLS` and `EDITING` are both active.
- `ADJUST` holds Bluetooth profile select/clear, external power toggle, RGB controls, and layer-select shortcuts (`&to GAMING`, `&to BASE`). Preserve this reachability unless the user asks otherwise.
- Current encoders: left volume via `inc_dec_kp C_VOL_DN/C_VOL_UP` on every layer; right virtual-desktop switch via `inc_dec_kp LC(LA(PG_UP)) LC(LA(PG_DN))` on every layer except `ADJUST` (which has no sensor-bindings). Not `LC(LG(...))` (Ctrl+Meta) — that clashed with Meta-drag window moving in KWin.
- Keep each `bindings = < ... >;` layer at the matrix-transform count in `boards/shields/Sofle/Sofle.dtsi` and preserve the matrix-shaped formatting.

## Hardware Model

- Split wireless `nice_nano_v2` with local shield siblings `Sofle_L` and `Sofle_R`; `Sofle_L` is central by default in `Kconfig.defconfig`.
- Both halves stack `nice_oled`; OLED support also depends on the `zmk-nice-oled` module.
- EC11 encoders are defined in shared `Sofle.dtsi` and enabled per side in `Sofle_L.overlay` / `Sofle_R.overlay`.
- Battery reporting uses `zmk,battery-nrf-vddh` in each side overlay.
- WS2812 RGB is on SPI in `Sofle.dtsi` with `chain-length = <36>`; be conservative with power-hungry changes on this wireless build.
- The Prospector dongle variant (`boards/shields/Sofle_dongle/`) uses the same PCB/pin mapping as `Sofle_L`/`Sofle_R` for its left/right peripherals, but neither half is central — `Sofle_dongle_central` (built for `seeeduino_xiao_ble`, paired with the `prospector-zmk-module`'s `prospector_adapter` shield) is. Peripherals in this variant have no OLED.
- `Sofle_dongle_central.overlay` wires an unused SPI pin to a fake `zmk,underglow` chosen node (`chain-length = <1>`, nothing physically attached) purely so `CONFIG_ZMK_RGB_UNDERGLOW` compiles on the dongle; this lets `BEHAVIOR_LOCALITY_GLOBAL` RGB behaviors (`RGB_TOG`/hue/effect) forward from the dongle to the halves' real LED strips.
- Dongle-mode peripherals (`config/Sofle_dongle_left.conf` / `_right.conf`) run `CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER=y` (cuts the WS2812 VCC rail on `RGB_TOG` so all 36 chips stop drawing quiescent current) and ZMK's default `*_LATENCY=30` (not `0`) to conserve peripheral battery; do not change these without understanding the battery-life tradeoff.

## Validation

Preferred local builds require a workspace that has the ZMK checkout at `zmk/app`; run from the config repo root or adjust `ZMK_CONFIG` accordingly:

```bash
west build -s zmk/app -b nice_nano_v2 -- -DSHIELD="Sofle_L nice_oled" -DSNIPPET=studio-rpc-usb-uart -DZMK_CONFIG="$PWD/config"
west build -s zmk/app -b nice_nano_v2 -- -DSHIELD="Sofle_R nice_oled" -DZMK_CONFIG="$PWD/config"
```

If local ZMK dependencies are unavailable, validate by inspection: `build.yaml` still includes the intended targets, keymap/devicetree braces and semicolons are balanced, every keymap layer has the transform binding count, and `.conf` lines are valid `CONFIG_NAME=value` entries.

The keymap drawing workflow watches `config/*.keymap`, `config/config_keymap-drawer.yaml`, and `.github/workflows/keymap-drawer.yaml`; it writes to `keymap-drawer/` with `amend_commit: true`.

## Repo Hygiene

- Do not add generated firmware/build outputs: `build/`, `.zmk-build/`, `*.uf2`, `*.hex`, `*.elf`, `*.map`, `*.bin`, `*.log`.
- For small key binding edits, rebuild/flash both halves if behavior should apply everywhere; for left-only Studio/snippet changes, call out that only the left artifact is affected.
- Update `README.md` and this file only for repo-shaping changes: build targets, artifact names, hardware variants, dependencies, shield structure, or build/flashing/reset instructions.
