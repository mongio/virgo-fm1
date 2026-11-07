# Licensing

Mario Texture's VIRGO is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text
in `LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| VIRGO: `firmware/src/virgo.c`, `virgo_dsp.c`, `virgo_ui.c`, `virgo_panel.c`, `virgo_bt.c`, `virgo_smc.c`, `vgfx.c`, `midi_uart.c` changes, the editor `web/`, `tools/gen_virgo_tables.py`, `tools/gen_aafont.py`, `README.md`, `LEGGIMI.md` | Copyright (C) 2026 Mario Texture, written with the help of Claude (Anthropic) |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` (the build) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The starting point: [Dinghy](https://github.com/zvenson/dinghy) — the wiring `platform.c`, the host mode `tests/host.c` | zvenson |
| The sister firmware VIRGO grew out of: [GRANA](https://github.com/mongio/grana-fm1) (user bank, sample editor, Bluetooth glue) | Mario Texture |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Inter (Rasmus Andersson and the Inter Project Authors), rasterised into `firmware/gen/virgo_font.h` | SIL OFL 1.1 | `assets/fonts/Inter-LICENSE.txt` |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package (and inside the built editor `virgo-editor.html`, which carries the packages); no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Science

Quasi-normal mode fits: E. Berti, V. Cardoso, C. M. Will, *Phys. Rev. D* 73, 064030 (2006). Catalogue values rounded
from the GWTC catalogues of LIGO, Virgo and KAGRA. VIRGO is not affiliated with the Virgo Collaboration, EGO, LIGO or
KAGRA; the name is a tribute.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. VIRGO is not made or endorsed by them.

## VIRGO + Bluetooth

- `firmware/src/ble/`, `firmware/hal/fm1_ble.h`, `fm1_ble_rf.h`, `ble_rf_tables_v15.h` (copied to `firmware/gen/ble_rf_tables.h`):
  Optimist's BLE stack and WL82 driver (w0ts/optimist, commit 4b05f23, GPL-3.0-only OR MPL-2.0), unchanged.
- `firmware/src/virgo_bt.c`: the glue, after fm1-dock's `src/ble/hx.c` (Joakim Eriksson, GPL-3.0-only) and Optimist's
  `io/midi/midi_ble.c`, by way of GRANA.
- `firmware/src/virgo_smc.c`: the SMC-Mixer's DAW mode, after Hortator's `smc.c` (DeadActive, GPL-3.0-only).

