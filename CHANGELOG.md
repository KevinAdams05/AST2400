> [!NOTE]
> An LLM was used to aid in development of this code.

# Changelog

All notable changes to the AST2400 (unofficial) driver are recorded here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project follows [Semantic Versioning](https://semver.org/).

The 0.0.x line is the **Phase 1** (probe + bind) release series. The 0.1.x
line will open when Phase 3 actually programs a display mode.

---

## [0.0.10] — 2026-05-28

**Phase 4.0: multi-mode support.** Mode list grows from a single
hardcoded 1024×768 entry to five 4:3 VESA modes — 640×480, 800×600,
1024×768, 1280×1024, 1600×1200 — all at 60 Hz @ 32 bpp. Screen
preferences now offers real choices. No EDID readback yet (Phase 4.1).

### Added

- `kModeList[]` in `mode.cpp` — five `ast_mode_info` entries ported
  from Linux `ast_vbios.c` (`res_640x480` / `res_800x600` /
  `res_1024x768` / `res_1280x1024` / `res_1600x1200`). All use DCLK
  indices already in the table (VCLK25_175 through VCLK162).
- `kHaikuModes[]` in `accelerant.cpp` — matching `display_mode`
  entries (same array indexing as `kModeList`).
- `ast_find_mode()` and `find_mode_index()` helpers — width/height
  lookups so `SET_DISPLAY_MODE` knows which table entry to program.
- Framebuffer-size validation in `SET_DISPLAY_MODE`. Mode is rejected
  with `B_NO_MEMORY` if `width × height × 4 > framebufferSize`. On
  the AST2400's 16 MB BAR0 this allows up to ~2048×2048 single-buffer
  @ 32 bpp; the check exists mostly to guard future format/depth
  expansion.

### Changed

- `ast_program_mode_1024x768()` renamed to `ast_program_mode(const
  ast_mode_info*)`. Pitch + start-address programming now uses the
  passed-in mode's width instead of the previous hardcoded 1024×4.
- `ACCELERANT_MODE_COUNT` returns 5 (was 1).
- `GET_MODE_LIST` returns all five modes.
- `GET_FRAME_BUFFER_CONFIG` returns the current mode's stride (was
  hardcoded 1024×4).
- `GET_DISPLAY_MODE` returns the actually-active mode (tracked in
  `sCurrentModeIndex`), default 1024×768.

### Skipped

- 1920×1080@60 — needs the extended DCLK table (`VCLK148_5` is at
  index 0x14, beyond our current 0x00–0x0f) and exercises the
  `AST2500PreCatchCRT` flag path we haven't ported. Will land in
  Phase 4.2 alongside 16:9 modes.

---

## [0.0.9] — 2026-05-28

**First fully working Phase 3 release** — clean 1024×768 Haiku desktop on
the AST2400 with correct colors.

### Fixed

- **CR_A8 bit 1 cleared for 32 bpp.** Linux's `ast_set_color_reg(32)`
  sets `CR_A8 = 0x02`; doing the same on our AST2400 produces R↔B
  swapped colors (yellow → cyan, blue → magenta). Per-boot register
  dumps (added in 0.0.8) showed the VBIOS leaves bit 1 clear in 32 bpp
  mode and the same memory contents scan out with correct colors.
  Diverged from Linux here — Linux must compensate for the swap in
  `ast_post.c` silicon-init code we haven't ported. Cleared bit 1 for
  32 bpp; left the 16 bpp path at Linux's value (untested but matches
  reference). Documented inline in `mode.cpp`'s `ast_set_color_reg`.

### Added

- **`dump_color_regs()`** diagnostic helper in `mode.cpp`, called from
  `ast_program_mode_1024x768` before and after the mode-set sequence.
  Logs VGAMR / SR01-04 / GR05-06 / CR13 / CR17 / CR_A0 / CR_A3 / CR_A8 /
  CR_B0 / DAC PEL mask to syslog with "BEFORE" / "AFTER" markers. Was
  added in 0.0.8 specifically to track down the CR_A8 issue; kept
  enabled because the comparison is invaluable when porting future
  silicon-init differences. Will become a build-flag-gated debug
  dump once Phase 4 stabilises.

### Skipped versions

- **0.0.6** — attempted to fix the R↔B swap by switching the mode's
  Haiku color space from `B_RGB32_LITTLE` to `B_RGB32_BIG`. Boot
  hung at black screen with no network response; required hard
  power-cycle + safe-mode delete. Linux confirms `XRGB8888` (= Haiku
  `B_RGB32_LITTLE`) is the correct format; the issue was always
  chip-side, not host-side. Reverted in 0.0.7.
- **0.0.7** — functional revert of 0.0.6, equivalent to 0.0.5 with
  baseline restored.
- **0.0.8** — 0.0.7 + the `dump_color_regs()` diagnostic. The dumps
  it produced informed the 0.0.9 fix.

---

## [0.0.5] — 2026-05-28

**Phase 3 lands: the chip is actually programmed.** `SET_DISPLAY_MODE`
now writes real values to the AST's sequencer, CRTC, attribute,
graphics, DAC, and PLL registers instead of being a no-op stub.
On install, the display should switch to 1024×768@60 in 32 bpp —
matching what we tell app_server, and replacing the stride-mismatched
mess from 0.0.4.

### Added

- **`headers/private/graphics/ast/ast_regs.h`** — register constants
  ported from Linux `drivers/gpu/drm/ast/ast_reg.h` + flag bits from
  `ast_vbios.h` + DCLK index defines. Plus `ast_mode_info`,
  `ast_std_table`, `ast_dclk_info` struct layouts.
- **`src/add-ons/accelerants/ast/mode.cpp`** — Haiku port of:
  - `ast_set_std_reg` — VGA sequencer/CRTC/attribute/graphics
    defaults from the VBIOS standard tables (Linux
    `ast_mode.c:200-243`, `ast_tables.h:36-107`).
  - `ast_set_crtc_reg` — CRTC timing programming from a mode
    descriptor (Linux `ast_mode.c:245-356`).
  - `ast_set_offset_reg` — scanout pitch (Linux
    `ast_mode.c:358-366`).
  - `ast_set_dclk_reg` — pixel-clock PLL programming (Linux
    `ast_mode.c:368-379`).
  - `ast_set_color_reg` — color-depth selection (Linux
    `ast_mode.c:381-408`).
  - `ast_set_sync_reg` — H/V sync polarity (Linux
    `ast_mode.c:419-432`).
  - `ast_set_start_address` — scanout base address within BAR0
    framebuffer (Linux `ast_mode.c:434-444`).
  - `ast_set_vbios_mode_reg` — IPMI/iKVM mode-info hints (Linux
    `ast_mode.c:176-198`).
  - `ast_wait_for_vretrace` — bounded retrace wait so we don't
    reprogram CRTC mid-scanout.
  - **Tables**: `kStdTables[5]` (TextMode/EGA/VGA/HiC/TrueC), ported
    verbatim from Linux `ast_tables.h`. `kDclkTable[16]` (VCLK25_175
    through VCLK162), ported from Linux `ast_2000.c:158-186` — AST2400
    inherits this table per `ast_2400_init()`.
- **`accelerant.cpp`** — `set_display_mode` calls into
  `ast_program_mode_1024x768()` instead of returning a no-op `B_OK`.

### Known limitations (still)

- Only the one hardcoded 1024×768@60 mode is supported. Mode-list
  generation from EDID lands in Phase 4.
- No per-silicon-rev quirks. AST2500 / AST2600 will use the AST2400
  code path; deltas need hardware testing before being added.
- No DAC gamma table programming (sticks with whatever VBIOS left).
- No cursor support, no acceleration. Phase 5+.

---

## [0.0.4] — 2026-05-28

Fixes two Phase 2 bring-up bugs found on first install of 0.0.3.

### Fixed

- **Accelerant init failed silently** — the kernel driver created the
  shared_info, MMIO, and framebuffer areas with only
  `B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA`. The accelerant then
  failed to clone them from user space (areas weren't marked
  cloneable), `init_accelerant()` returned an error, app_server
  silently moved on to VESA. Added `B_CLONEABLE_AREA` to all three
  area creates, matching radeon_hd's pattern at
  `src/add-ons/kernel/drivers/graphics/radeon_hd/device.cpp`.
- **Accelerant logging didn't reach syslog** — the original
  `fputs(stderr)` from inside app_server's address space doesn't go
  anywhere visible. Switched to `_sPrintf()` (matching
  radeon_hd/accelerant.cpp:39), with `#undef TRACE` first to override
  `<Debug.h>`'s release-build no-op definition. `ast.accel:` lines
  now appear in syslog.

---

## [0.0.3] — 2026-05-28

Phase 2 accelerant skeleton lands. The driver now ships both a kernel
driver and an accelerant; app_server can load both halves and query the
accelerant. **Display output is still whatever VBIOS POST set up — the
accelerant stubs `SET_DISPLAY_MODE` and does not program the chip.**
That's Phase 3 work.

### Added

- **`ast.accelerant`** — userspace accelerant addon.
  - `INIT_ACCELERANT` clones the kernel driver's shared_info area into
    user space, plus clones the MMIO and framebuffer BARs for future
    Phase 3 register access.
  - `GET_ACCELERANT_DEVICE_INFO` reports the detected chip generation
    (AST2100/2200/2300/2400/2500/2600), framebuffer size, and DAC
    speed.
  - `ACCELERANT_MODE_COUNT` / `GET_MODE_LIST` expose a single
    hardcoded 1024×768@60 mode. EDID-driven mode generation is Phase 4.
  - `PROPOSE_DISPLAY_MODE` / `SET_DISPLAY_MODE` / `GET_DISPLAY_MODE`
    accept only the hardcoded mode. `SET_DISPLAY_MODE` is a no-op —
    it returns `B_OK` without programming the chip, since CRTC + PLL +
    encoder sequences are Phase 3.
  - `GET_FRAME_BUFFER_CONFIG` returns the cloned framebuffer base and
    a 1024×4-byte stride. **The framebuffer is the chip's BAR0 — the
    chip is currently scanning out whatever VBIOS programmed, so
    drawing through this buffer is likely to look wrong until Phase 3
    actually sets the matching CRTC state.**
- **Build / package scripts** updated to handle both the kernel driver
  and the accelerant in one pass.

### Known limitations

- **No real mode setting yet.** `SET_DISPLAY_MODE` doesn't touch the
  chip. Display output remains whatever VBIOS programmed (typically
  text-mode-ish 1024×768 or 800×600).
- **One hardcoded mode.** EDID-driven mode-list generation lands in
  Phase 4.
- **No EDID readback.** The chip's DDC/I2C controller isn't being
  exercised yet.

---

## [0.0.2] — 2026-05-28

First publicly-published build. Phase 1 (probe and bind) verified on real
hardware.

### Added

- **Kernel driver `ast`** for ASPEED AST2400 / AST2500 / AST2600 BMC GPUs.
  Claims PCI vendor `0x1a03`, device `0x2000`. Distinguishes AST2100
  through AST2600 generations at runtime via the PCI revision byte, per
  Linux's `ast_detect_chip()`.
- **BAR mapping on device open.** BAR0 (framebuffer) and BAR1 (MMIO
  registers) are mapped into kernel space and surfaced to the accelerant
  via the standard shared_area pattern.
- **`AST_DUMP_REGISTERS` ioctl** — Phase 1 diagnostic that dumps the
  first 64 MMIO bytes to syslog. Real register decode follows in Phase 2/3.
- **Build / package scripts** — `scripts/build.sh` overlays the source
  onto a Haiku source tree and runs jam, mirroring the RadeonHD
  packaging pattern. `scripts/package.sh` wraps the built driver as a
  `.hpkg`.

### Fixed

- **SMAP violation on first ioctl** (would have shipped as 0.0.1).
  `ast_control()` used direct pointer dereferences on the ioctl `arg`
  parameter, which is in fact a *userspace* address. On modern x86_64
  with SMAP enabled, the kernel cannot dereference user pointers
  directly — doing so triggers an immediate panic. Rewritten to use
  `user_strlcpy()` / `user_memcpy()` for all kernel↔userspace data
  transfer, matching the canonical Haiku pattern at
  `src/add-ons/kernel/drivers/graphics/radeon_hd/device.cpp:186-191`.
  Caught during Phase 1 bring-up on Supermicro X11SSH-LN4F.

### Verified

- **Supermicro X11SSH-LN4F** (Xeon E3-1230v5, AST2400 rev 0x30): driver
  loads, identifies chip and BAR layout in syslog, app_server probes
  the device cleanly, display continues via VESA fallback. See README
  for the verification syslog dump.

### Known limitations

- **No accelerant yet** — app_server cannot drive a display through this
  driver in 0.0.2. Display output is whatever VBIOS POST set up via VESA
  fallback. Phase 2 will add the accelerant skeleton.
- **No mode setting** — the driver does not touch the CRTC, PLL, encoder,
  or any other register block that would change display state. The chip
  runs whatever the BIOS programmed.
- **AST2500 / AST2600 untested** — silicon-rev detection is wired up
  per the Linux driver's revision ranges, but no AST2500 or AST2600
  hardware has been exercised. AST2400 only so far.
