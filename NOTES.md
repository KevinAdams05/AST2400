> [!NOTE]
> An LLM was used to aid in development of this code.

# Haiku AST2400 / ASPEED Graphics Driver — Project Notes

Status: **idea / future project**, not yet started. Captured for
posterity because Kevin runs Supermicro server boards as primary
hardware and the AST2x00 family covers a huge chunk of that market.

## Hardware identification

The "onboard GPU" on most Supermicro and other enterprise server boards
is the **display block of the BMC** (Baseboard Management Controller),
not a discrete GPU. ASPEED Technology supplies the BMC silicon used by
Supermicro, Dell, HPE, Asus server line, and many others.

Family lineage:

| Chip | Vendor:Device | Era | Boards (examples) |
|---|---|---|---|
| AST2000 | `1a03:2000` rev ≤2x | ~2007-2010 | Older server boards |
| AST2300 | `1a03:2000` rev 22-2x | ~2011-2013 | Supermicro X9 |
| AST2400 | `1a03:2000` rev 30 | ~2013-2016 | Supermicro X10, X11 |
| AST2500 | `1a03:2000` rev 40-4x | ~2016-2020 | Supermicro X11SDV, H11/H12 |
| AST2600 | `1a03:2000` rev 5x | ~2020+ | Supermicro X12+, H13 |

PCI ID `1a03:2000` is shared across the family; revision differentiates
generations. All sit behind an ASPEED PCIe-to-PCI bridge (`1a03:1150`
AST1150 or similar) on x86 host systems.

### Confirmed test hardware

- **Supermicro X11SSH-LN4F** (Xeon E3-1230v5, Skylake-WS class)
  - Onboard: AST2400, `1a03:2000` rev 30
  - Discrete: AMD Cedar HD 5450 (`1002:68f9`) in PCIe slot
  - lspci dump: `~/Code/Haiku/TestHardware/Supermicro/lspci.txt`

When developing the driver, the X11SSH-LN4F is a known-good target.

## What the chip actually is

The AST2400 is a 2D-only VGA controller designed for **remote IPMI/KVM
console rendering**, not desktop use. Capabilities:

- Up to ~1920×1200 @ 60 Hz, 16/24/32 bpp
- Single CRTC, single output (VGA on back panel)
- Shared system memory framebuffer (typically 16-64 MB carved out at
  boot via BIOS-reserved range)
- Primitive 2D engine: BitBlt, solid fill, pattern fill
- Hardware cursor (small, simple)
- DPMS
- I2C/DDC controller for EDID readback
- Mode list driven by attached display's EDID, not a fixed table

It is **not** a GPU. No 3D, no shaders, no compute. Treat it as a smart
framebuffer with a small acceleration engine.

## Why this is worth doing

- A native driver replaces the VESA fallback on a huge family of server
  boards. Even though VESA already gets Haiku to boot, a native driver
  provides proper mode-setting (EDID-driven mode list, real refresh
  rates), DPMS, and possibly 2D accel.
- Wide applicability: same driver covers AST2400, AST2500, AST2600 with
  generation-specific quirks. That's thousands of board models.
- Self-contained scope. No AtomBIOS abstraction, no shared graphics
  infrastructure politics, no upstream Haiku coordination required —
  this is a clean greenfield driver.
- Good vehicle for learning mode-setting fundamentals (CRTC, PLL, EDID,
  DDC) at a level the BIOS-abstracted radeon path hides.

## Why this is mid-priority, not urgent

- BMC graphics are intrinsically not a great desktop experience. Even
  working perfectly, it's 1080p, no acceleration, no 3D. Server boards
  with a desktop on the BMC port are unusual.
- VESA framebuffer already provides "Haiku boots and is usable."
- For Kevin's daily desktop driving, the radeon_dce work matters more.
- The driver only changes the experience for users specifically on a
  server board with no discrete GPU installed.

This is the kind of project worth doing slowly in evenings, *alongside*
ongoing work, not instead of it.

## Effort estimate

For "boots to a usable Haiku desktop with proper modes" — **2-3 weeks
of evening work**:

| Phase | Effort | Notes |
|---|---|---|
| Skeleton kernel driver, PCI bind, BAR mapping | 1-2 days | Template: Haiku `src/add-ons/kernel/drivers/graphics/skeleton/` |
| EDID readback via DDC | 1-2 days | AST has its own I2C controller; Linux `ast` driver documents the sequence |
| CRTC + PLL programming for mode setting | 4-6 days | Bulk of the real work. PLL math in AST2500 Software Programming Guide (AST2400 inherits the same registers); silicon-rev deltas from Linux source |
| Framebuffer publish + accelerant boilerplate | 1-2 days | Match Haiku accelerant API |
| Mode list, sane defaults, app_server smoke test | 2-3 days | |
| Real-hardware debugging | open-ended | Always the long pole |

Optional extensions that ~double the budget:

- Hardware cursor (~3 days)
- 2D acceleration via BitBlt engine (~1 week; decent desktop perf win)
- DPMS sleep/resume (~2-3 days)
- Multi-generation support (AST2500/AST2600 differ in PLL and register
  layout) — incremental, generation-by-generation

## Reference material

For AST2400 specifically, this project's policy is to **port** code
from Linux's `drivers/gpu/drm/ast/` (not just reference it), reflecting
the GPL v2 licensing decision documented in
[`docs/STYLE_GUIDE.md`](docs/STYLE_GUIDE.md) §16. ASPEED hardware is
too sparsely documented publicly to drive blind, and the Linux driver
encodes register-sequence knowledge that doesn't exist anywhere else
in writeable form. Preserve upstream copyright lines per the style
guide when porting a file substantially intact.

### Vendor documentation (`~/Code/Syllable/RefDocs/GPU/Aspeed/`)

- **`AST2500.pdf`** — **AST2500 Software Programming Guide**, 833 pages,
  2017. Register sequences, init flows, programming model for the full
  BMC SoC. **The authoritative reference for AST2400 driver work** —
  ASPEED maintained register-level backward compatibility AST2300 →
  AST2400 → AST2500, so this doc covers the AST2400 display block,
  PLL, CRTC, I2C/DDC, and framebuffer scanout. AST2400-specific deltas
  (where they exist) come from Linux source.
- **`ast2520a2gp_datasheet.pdf`** — byte-identical duplicate of
  `AST2500.pdf` (same MD5). AST2520 is a packaging variant of AST2500;
  same silicon, same document. **Safe to delete the duplicate.**
- **`ast2600_datasheet.pdf`** — AST2600 datasheet, 1580 pages, 2022.
  Needed when extending support to AST2600. Different display block
  architecture (DCN-style); not directly applicable to AST2400/2500
  work but useful for understanding ASPEED's roadmap.

No standalone AST2400 datasheet is in the collection. ASPEED's public
developer portal may have one (registration usually required), but in
practice the AST2500 SPG plus Linux source covers everything needed.

### Source code references

- **Linux `drivers/gpu/drm/ast/`** at `/home/kevin/Code/Linux/linux/`
  — canonical reference. ~5000 lines of C. Covers AST2000 through
  AST2600 with per-silicon-rev branches. Per the style guide, port
  the algorithm, not the structure — rewrite to Haiku accelerant API,
  RAII, `TRACE()`, and Haiku types.
- **`xf86-video-ast`** X11 driver — older, simpler structure (~3000
  lines), MIT-licensed. Useful as a secondary reference for register
  sequences that Linux has refactored heavily.
- **Haiku graphics skeleton driver** —
  `~/Code/Haiku/haiku/src/add-ons/kernel/drivers/graphics/skeleton/`.
  Best starting point for the kernel driver structure.
- **Haiku `radeon_hd` / `intel_extreme`** — same project tree.
  Reference for accelerant-side patterns (mode list, EDID,
  set_display_mode, frame buffer publish) and for kernel-driver
  patterns (BAR mapping, PCI bind, ioctl dispatch).

## Implementation phasing

When ready to start, suggested phases:

### Phase 1 — Probe and inventory
- Skeleton driver that claims `1a03:2000`
- Map BARs
- Dump registers to syslog
- Confirm we can read EDID via DDC
- *Goal: prove we can talk to the chip at all*

### Phase 2 — Mode setting
- Program CRTC for a single hard-coded mode (e.g. 1024×768@60)
- Verify framebuffer activates by displaying a known pattern
- *Goal: prove we can drive the display*

### Phase 3 — EDID-driven mode list
- Parse EDID
- Build dynamic mode list from EDID + safe fallbacks
- Implement mode-set hook for accelerant
- *Goal: app_server gets a real mode list and can switch modes*

### Phase 4 — Accelerant + app_server integration
- Implement Haiku accelerant API hooks
- Publish framebuffer to app_server
- *Goal: Haiku boots to a desktop on this driver instead of VESA*

### Phase 5 (optional) — Acceleration + polish
- Hardware cursor
- 2D BitBlt acceleration
- DPMS
- AST2500 / AST2600 generation support

## Open questions for when work begins

- Should this live in the official Haiku tree or as a standalone fork
  like radeon_dce? Standalone is easier to iterate; upstream is better
  for long-term users. Probably: develop standalone, upstream once
  stable.
- Driver name in Haiku: `aspeed_gfx`? `ast`? Match Linux for
  discoverability or pick a Haiku-native name? Probably `aspeed_gfx`
  for matching Linux convention (the newer Linux DRM driver name).
- Do we need a way to coexist with a discrete GPU? On X11SSH-LN4F both
  AST2400 and Radeon HD 5450 are present. Haiku's app_server picks one
  graphics device. Need to confirm behavior when both have native
  drivers loaded.

## Status / next steps

- 2026-05-27: notes captured, no code written.
- 2026-05-28: **Phase 1 kernel driver written and builds clean.**
  - `STYLE_GUIDE.md` adapted from NimblePDF.
  - ASPEED datasheets discovered at `~/Code/Syllable/RefDocs/GPU/Aspeed/`
    (AST2500 SPG 833 pp. + AST2600 datasheet 1580 pp.); duplicate removed.
  - Driver source tree skeleton: `src/add-ons/kernel/drivers/graphics/ast/`
    + `headers/private/graphics/ast/DriverInterface.h` + Jamfile +
    `scripts/build.sh`.
  - Driver claims PCI `1a03:2000`, distinguishes AST2100–AST2600 by
    revision-id ranges (per Linux `ast_detect_chip()`), maps BAR0
    (framebuffer) + BAR1 (MMIO) on open, creates accelerant
    shared_area. `AST_DUMP_REGISTERS` ioctl available for Phase 1
    sanity diagnostic once an accelerant exists to open the device.
  - Built kernel addon: 15.8 KB at
    `~/AST2400/build/x86_64/ast` on the build server.
- **Phase 1 next:** package as `.hpkg` and verify on real hardware
  (Supermicro X11SSH-LN4F = `shredder` at 192.168.74.54, AST2400
  alongside HD 6850). Driver should bind silently — display stays on
  the radeon_hd-driven 6850, but syslog should show the `ast:` probe
  lines.
- **Phase 2 next:** accelerant skeleton — bare-minimum hook table that
  can open the device, clone the shared area, return a single fixed
  mode list (e.g. 1024×768@60), and publish a framebuffer. Goal: get
  to a state where app_server can switch the active display to the
  AST2400's BMC VGA output (with the HD 6850 disconnected for the
  test, since app_server only picks one).
