# zmk-ripple-rgb

Reactive **Ripple** Underglow effect for [ZMK](https://zmk.dev/) split keyboards.

When Ripple is selected (5th effect after Solid / Breathe / Spectrum), the LEDs
stay dark until you press a key. A wave then expands from that switch using
**physical PCB distances**. On a split, only a tiny BLE event (`key_id` +
`event_id`) is sent — each half renders locally (no RGB frame streaming).

Originally developed for the **PandaKB Sofle RGB**; geometry tables currently
target that PCB. Contributions for other boards are welcome.

## Features

- Uses ZMK native Underglow as single source of truth (hue / sat / bri / speed / on-off)
- Cross-half ripple over the existing split transport (`--wrap`, no ZMK core fork)
- Standalone central↔peripheral and dongle topologies
- Peak brightness and FPS configurable via Kconfig

## Add to your ZMK user config

### 1. `config/west.yml`

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: guguinhatoop22
      url-base: https://github.com/guguinhatoop22
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: v0.3   # or your pin
      import: app/west.yml
    - name: zmk-ripple-rgb
      remote: guguinhatoop22
      revision: main
  self:
    path: config
```

### 2. Board / shield `.conf`

```conf
CONFIG_ZMK_RGB_UNDERGLOW=y
CONFIG_ZMK_RIPPLE_RGB=y
CONFIG_ZMK_RIPPLE_RGB_PEAK_BRIGHTNESS=35
CONFIG_ZMK_RIPPLE_RGB_FPS=25
```

Older configs that still set `CONFIG_NICE_OLED_WIDGET_REACTIVE_RIPPLE=y` keep working
(alias selects `ZMK_RIPPLE_RGB`).

### 3. Use it

Cycle effects with `&rgb_ug RGB_EFF` until Ripple is selected. Adjust color / brightness /
speed with the usual Underglow behaviors.

## Requirements

- ZMK with `CONFIG_ZMK_RGB_UNDERGLOW`
- `chosen { zmk,underglow = &...; }` with `chain-length` matching the strip
- Split optional; without split, ripple is local-only

## Sofle geometry note

`src/ripple_rgb.c` embeds a precomputed `cross_dist[60][36]` table derived from the
Sofle RGB KiCad layout (plus a ~80 mm desk gap between halves). Right-half builds
detect `CONFIG_SHIELD_SOFLE_R` or `CONFIG_SHIELD_SOFLE_DONGLE_RIGHT`.

To port another keyboard: replace that table and the shield detection macros.

## License

MIT — see [LICENSE](LICENSE).
