> [!NOTE]
> An LLM was used to aid in development of this code.

# AST2400 Coding Style Guide

AST2400 is a native Haiku graphics driver for the ASPEED AST2400 / AST2500
/ AST2600 family of integrated BMC GPUs. The codebase is C++, split
across a kernel driver and a userspace accelerant. Our style is the
**Haiku Project Coding Guidelines** verbatim, with the project-specific
deviations called out in §1.

**Authoritative base:** <https://www.haiku-os.org/development/coding-guidelines>

When this document and the Haiku guidelines disagree, **this document
wins** for AST2400 code. When this document is silent, defer to Haiku.

When in doubt, look at how the surrounding code does it. Consistency
with the immediate context outranks consistency with the project as a
whole — never make a file "stick out" from its neighbours just to match
a rule in this guide.

---

## 1. Project-specific deviations from Haiku

These are the **only** intentional differences from upstream Haiku
style. Everything else in this document is a restatement of the Haiku
rules for convenience.

### 1.1 Line length — 100-character soft cap

- **Target:** ≤ 80 columns where natural. Matches upstream Haiku and
  keeps side-by-side diffs comfortable.
- **Soft warning:** the linter warns at **100 columns**.
- **Hard cap:** none, but lines past 100 require a justification in
  code review (typically: an unbroken string literal, a URL, a long
  PCI/register define name, or a generated table that wrapping would
  actually harm).

Rationale: register-define lines and AtomBIOS-style table entries
sometimes need to be a touch wider than 80. 100 covers those without
abandoning the goal of readable narrow code.

### 1.2 Linux source is in scope as a porting reference

AST2400 is permitted to **port** code from Linux's
`drivers/gpu/drm/ast/` driver (and the older `xf86-video-ast` X11
driver). Porting means:

- Take the Linux algorithm and register sequence as the
  authoritative reference.
- Rewrite it to Haiku's accelerant / kernel-driver API, types,
  logging, and style.
- Preserve copyright lines from upstream when transferring a function
  or table essentially intact (see §16).

This is a deliberate departure from the general "reference but don't
copy" policy applied elsewhere in Kevin's Haiku work. The ASPEED
hardware is too sparsely documented publicly to drive blind; the
Linux driver is the only reliable reference for several
register sequences. License implications are addressed in §16.

---

## 2. Indentation and whitespace

- **Tabs** for indenting blocks. Editor tab width is **4** for purposes
  of computing line length and alignment.
- Wrapped lines get **at least one extra tab**, plus one more tab per
  expression nesting level.
- Namespace contents are **not indented** — they sit flush at column 0.
- **Spaces** on both sides of binary operators (`a + b`, `x == y`).
- **No space** between a C-style cast operator and its operand: `(int)x`.
- **Always a space** after a comma.
- Every file ends with a newline.
- No trailing whitespace on any line.

## 3. Naming

| Kind | Convention | Example |
|---|---|---|
| Classes, structs, types, namespaces, functions | `UpperCamelCase` | `ModeSet`, `ReadEdid` |
| Local variables | `lowerCamelCase` | `pixelClock`, `connectorIndex` |
| Member variables | `f` prefix + `UpperCamelCase` | `fRegisters`, `fFramebuffer` |
| Constants | `k` prefix + `UpperCamelCase` | `kMaxPixelClock`, `kDefaultBpp` |
| Globals | `g` prefix | `gInfo`, `gDeviceInfo` |
| Statics (file/function scope) | `s` prefix | `sChipTable` |
| Private methods | `_` prefix | `_ProgramPll`, `_ReadDpcd` |

Rules:

- No underscores in type or function names (other than the `_`
  prefix on private methods, and the lowercase-underscore form
  required by Haiku driver entry points — see §3.1 below).
- **Descriptive names always beat short ones.** No abbreviations, no
  letter-soup names, even for "obvious" things. Spell it out — the
  few extra characters pay for themselves the first time someone
  unfamiliar reads the code.
  - Variables: `connector` not `conn`, `register` not `reg` (use
    `registerOffset` etc.), `framebuffer` not `fb` (use `fFramebuffer`),
    `width` not `w`, `index` not `idx`.
  - File and class names: `ModeSetter` not `MSetter`,
    `PllProgrammer` not `PllProg`, `EdidParser` not `EdidPrs`.
  - Method names: `ProgramPll()` not `ProgPll()`,
    `ReadEdidBlock()` not `RdEdid()`.
- Exception: a few well-known abbreviations are fine when their
  full form would be noise — `id`, `dpi`, `rgb`, `min`/`max`,
  `i`/`j`/`k` for tight loop indices, `pll`, `bpp`, `crtc`,
  `edid`, `i2c`, `ddc`, `aux`, `mmio`, `bar`, `dpcd`,
  `dvo`, `tmds`, `dac`, `vga`, `lcd`, `dpms` — these are
  graphics-driver terms of art and spelling them out (e.g.
  "PhaseLockedLoop") harms readability rather than helping.
  When in doubt, spell it out.
- No articles in names — avoid `aMessage`, `theView`, `MyDraw`.
  Prefer `message`, `view`, `Draw`.
- Avoid ambiguous pairs like `SetMode` / `DoSetMode`. Pick one verb
  that says what it actually does.
- All identifiers, comments, and strings in **US English**
  ("color", not "colour").

### 3.1 Haiku driver entry-point naming exception

The Haiku kernel and accelerant subsystems specify lowercase-with-
underscores names for driver entry points (`init_driver`,
`uninit_driver`, `init_hardware`, `publish_devices`, etc.) and for the
accelerant hook table (`get_accelerant_hook`, `set_display_mode`, ...).
Use the exact names the OS expects — do not "Haiku-cgvify" them to
`InitDriver` style. This is the same exception that `radeon_hd`,
`intel_extreme`, etc. all live with.

## 4. Braces and blocks

- **Class / struct** opening brace: same line as the declaration.
- **Function** opening brace: on its own line.
- **`if` / `else` / `for` / `while` / `switch`** opening brace: same
  line as the keyword and condition.
- `else` and `else if` go on a new line, after the closing brace of
  the previous block.
- **Single-statement** `if`/`else`/`for`/`while`: omit the braces,
  put the statement on a new indented line.
- **Multi-statement** blocks: always braces.
- Empty inline functions defined inside a class definition may sit on
  a single line. Empty functions defined outside the class follow the
  standard function format (return type on its own line, brace on its
  own line).
- After an early `return` (or `break`/`continue`) inside an `if`, do
  **not** write an `else` — the `else` is dead syntax.

```cpp
status_t
AstDevice::ReadEdid(uint8 connector, uint8* buffer)
{
    if (fInitialized == false)
        return B_NO_INIT;

    status_t status = _SelectDdcBus(connector);
    if (status != B_OK)
        return status;

    // multi-statement → braces
    if (fLastConnector != connector) {
        _ResetI2cBus();
        fLastConnector = connector;
    }

    return _ReadEdidBytes(buffer, 128);
}
```

## 5. Functions

- Return type on its own line, **above** the function name.
- Opening brace on its own line, flush left.
- **Two blank lines** between function definitions.
- Long argument lists: wrap and indent the continuation by **one tab**.

```cpp
status_t
AstDevice::ProgramCrtc(uint32 width, uint32 height,
    uint32 pixelClock, uint32 colorDepth);
```

## 6. Constructor initializer lists

- Colon on its **own line**, indented one tab.
- Each initializer on its own line, indented one tab.
- Prefer initializer lists over assigning in the body — only put work
  in the body that genuinely cannot be expressed as initialization.

```cpp
AstAccelerant::AstAccelerant(int sharedAreaId, int deviceFd)
    :
    fSharedAreaId(sharedAreaId),
    fDeviceFd(deviceFd),
    fSharedInfo(NULL),
    fRegisters(NULL),
    fFramebuffer(NULL)
{
}
```

## 7. Blank lines

- **Two blank lines** between functions.
- **Two blank lines** between the include block and any subsequent
  define block, and between defines and the first variable/function.
- **One blank line** between cases in a `switch`.
- **One blank line** after the opening `#define` of a header guard.
- **Two blank lines** before the closing `#endif` of a header guard.
- No blank line between the license/copyright block and the header
  guard.

## 8. Control flow specifics

### 8.1 If / else

- Always use explicit boolean tests, never rely on implicit
  truthiness.
  - Pointers: `if (pointer != NULL)`, not `if (pointer)`.
  - Integers: `if (count != 0)`, not `if (count)`.
- Bitmasks always go in parentheses with an explicit comparison:
  `if ((flags & kMask) != 0)`.
- No assignment inside an `if` (or `while`) condition. Split it:
  ```cpp
  status_t status = entry.GetRef(&ref);
  if (status != B_OK)
      return status;
  ```
- Variable goes on the **left** of comparisons: `if (status == B_OK)`,
  never `if (B_OK == status)`. AST2400 does not use Yoda conditions.
- Do not wrap an entire `if` condition in redundant outer parentheses,
  and do not parenthesise each clause:
  `if (a == 3 && b != 4)`, not `if ((a == 3) && (b != 4))`.

### 8.2 Long conditions

When wrapping a long boolean expression, put the **logical operator at
the start** of the next line, not at the end of the previous one:

```cpp
if (device != NULL
    && device->IsInitialized()
    && device->ChipRevision() >= AST_2400) {
    // ...
}
```

### 8.3 Switch

- `case` labels are indented one tab inside the `switch`.
- The body of each case is indented one further tab.
- One blank line between cases.
- Wrap a case body in `{ }` whenever it declares its own variables.
- Always have a `default:` (even if it just `break;`s).

```cpp
switch (chip->revision) {
    case AST_2400:
    {
        uint32 pllClock = _CalcPll2400(targetMhz);
        // ...
        break;
    }

    case AST_2500:
        _ProgramPll2500(targetMhz);
        break;

    default:
        TRACE("unknown chip revision %u\n", chip->revision);
        return B_NOT_SUPPORTED;
}
```

### 8.4 Loops

- Prefer `for` over `while`-with-assignment. If you find yourself
  writing `while ((x = next()) != NULL)`, refactor to a `for` or
  pull the assignment out.
- Range-based `for` is allowed; use it when the index is not needed.

### 8.5 No `goto`. No exceptions for cleanup either — use RAII.

Note: Linux's `drivers/gpu/drm/ast/` uses `goto err_*` cleanup chains
extensively. When porting, **rewrite to RAII or early-return**; do
not bring the goto chains across. The point of porting is the
algorithm, not the control structure.

## 9. Types

### 9.1 Prefer Haiku types over raw C types

When working in Haiku-native code (the entire current codebase):

- `int32` / `uint32` instead of `int` / `unsigned`.
- `int64` / `uint64` for explicit 64-bit.
- `off_t` for file offsets.
- `size_t` / `ssize_t` for sizes.
- `phys_addr_t` for physical addresses.
- `addr_t` for pointer-sized integers (e.g. when casting between
  `void*` and integer for BAR arithmetic — see Haiku #20112 for the
  canonical pattern).
- `status_t` for error returns. **All AST2400 functions that can
  fail return `status_t`**, with `B_OK` on success.

These come from `<SupportDefs.h>`.

### 9.2 Linux driver porting

AST2400 actively ports code from Linux's `drivers/gpu/drm/ast/`. Some
ground rules:

- **Port the algorithm, not the structure.** Linux's DRM driver uses
  `drm_*` types, `goto err_*` cleanup chains, `pr_err` logging, and
  the Linux `device` / `crtc` / `connector` object model. The
  ported AST2400 code uses Haiku's accelerant API, RAII, `TRACE()`,
  and Haiku object lifetimes.
- **Cite the Linux source** in comments for non-obvious register
  sequences: `// Ported from Linux drivers/gpu/drm/ast/ast_mode.c
  ast_set_vbios_color_reg() — kernel 6.8`. The intent: a future
  reader can compare against upstream when a Linux fix lands.
- **Preserve Linux copyright lines** on files that are substantially
  ported. See §16.2.
- **Don't blindly translate Linux types to Haiku types.** Some Linux
  patterns (`mutex_lock_irqsave`, `wait_event_interruptible`) don't
  have direct Haiku equivalents and the right Haiku idiom may be a
  different shape (`acquire_sem`, condition variable, etc.) — look
  at how Haiku's existing graphics drivers handle the same need
  before introducing a new pattern.

### 9.3 Strings

- `char[N]` buffers and `snprintf` for kernel-driver code (no
  `BString` is available in kernel space).
- `BString` is available in the accelerant; prefer it over
  `char*` / `malloc`/`strdup`/`free` / fixed `char[N]` buffers
  there.
- Use `BString::operator<<` and `BString::SetToFormat` instead of
  `sprintf` when working in `BString` code.

### 9.4 Collections

- **Kernel side:** plain C arrays, intrusive linked lists, or
  `DoublyLinkedList<T>` from `<util/DoublyLinkedList.h>` — these
  match what Haiku's other kernel drivers use.
- **Accelerant side:** `BObjectList<T>` over `BList`. The
  type-safety and ownership semantics catch real bugs.
- `std::vector<T>` is acceptable in code that doesn't run in
  kernel mode and where a `BObjectList<T>` would be ill-fitting,
  but prefer Haiku containers unless there's a concrete reason
  to reach for STL.

### 9.5 Casts

- Use C++ casts: `static_cast`, `dynamic_cast`, `const_cast`,
  `reinterpret_cast`.
- C-style casts are only acceptable for primitive numeric
  conversions and must have **no whitespace** after the cast
  operator: `(int)x`, not `(int) x`.
- Down-casts must use `dynamic_cast` when the actual runtime type is
  not statically guaranteed.
- BAR-to-pointer conversions go through `addr_t`:
  `(void*)(addr_t)pciInfo.u.h0.base_registers_pci[0]` — never
  `(void*)pciInfo.u.h0.base_registers_pci[0]` directly. See Haiku
  #20112 for the canonical write-up.

## 10. Pointers and null

- `NULL`, not `0` or `nullptr`. (Haiku tradition.)
- Initialize pointers with traditional assignment, not constructor
  syntax: `AstDevice* device = NULL;`, not `AstDevice* device(NULL);`.
- **Pointer asterisk binds to the type**: `AstDevice* fDevice;`,
  not `AstDevice *fDevice;`. This is consistent with how Haiku
  writes function signatures and matches `clang-format`'s
  `PointerAlignment: Left`.
- Do **not** check for `NULL` before `delete` or `free` — both
  accept `NULL` and the check is noise:
  ```cpp
  delete fEdid;   // not: if (fEdid != NULL) delete fEdid;
  ```

## 11. Boolean conventions

- Use `true` / `false` from C++, never `TRUE` / `FALSE` macros.
- Functions that return success/failure return `status_t` (`B_OK`
  on success), not `bool` — `bool` should mean a genuine yes/no
  flag, not "did it work".

## 12. Returns and parentheses

- Do not parenthesise the return expression: `return result;`, not
  `return (result);`.
- Prefer early returns. Keep happy-path code at one indent level.

## 13. Comments

- Prefer `//` over `/* */`.
- Explain **why**, not what. `i++; // increment i` is noise.
- For genuinely tricky code, describe the constraint or pitfall, not
  your feelings: not `// this is a hack!` but `// AST2500 inverts the
  PLL feedback divider polarity vs AST2400; see Linux
  drivers/gpu/drm/ast/ast_post.c ast_init_dvo() comments for the
  silicon-rev rationale.`
- No author initials in comments. Git already knows.
- No `// TODO: kevin` style markers. Plain `// TODO:` is fine.
- No `#if 0`'d dead code. Delete it; git has the history.
- **Doxygen** (`/*! ... */`) for documenting public/header API
  surface. Used for code comprehension, not end-user documentation —
  that lives in `docs/`.

## 14. Includes

### 14.1 Ordering

Within a source file (`.cpp`), in this order, with **one blank line**
between groups:

1. The corresponding header (`#include "AstDevice.h"` from
   `AstDevice.cpp`).
2. POSIX / standard C headers (`<stdio.h>`, `<stdlib.h>`, ...).
3. C++ standard headers (`<vector>`, `<memory>`, ...) — only when
   needed.
4. Haiku API headers (`<Application.h>`, `<Drivers.h>`, ...).
5. Haiku private headers (`<private/...>`, `<graphics/...>`) — only
   when unavoidable for a kernel/accelerant driver, which is most of
   the time.
6. Local project headers (`"AstDevice.h"`, `"AstRegs.h"`).

Within each group, **alphabetize** include lines.

### 14.2 Style

- `<angle>` for system / framework headers.
- `"quoted"` for local project headers.
- Use **C-style header names**: `<string.h>`, `<stdlib.h>` — not
  `<cstring>`, `<cstdlib>`. (Haiku tradition.)
- Avoid path components when the build system makes them
  unnecessary: `<Application.h>`, not `<be/app/Application.h>`.

## 15. Header files

### 15.1 Layout

```cpp
/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  [...standard GPL v2 boilerplate, see §16.1 for the full block...]
 */
#ifndef AST_DEVICE_H
#define AST_DEVICE_H


#include <KernelExport.h>

#include "AstRegs.h"


class AstDevice {
public:
                            AstDevice(int deviceFd);
    virtual                 ~AstDevice();

            status_t        Init();
            status_t        ReadEdid(uint8 connector, uint8* buffer);

private:
            status_t        _ProgramPll(uint32 targetClock);
            void            _ResetI2cBus();

            int             fDeviceFd;
            volatile uint8* fRegisters;
            uint32          fChipRevision;
};


#endif  // AST_DEVICE_H
```

### 15.2 Header-guard rules

- Form: `#ifndef CLASS_NAME_H` / `#define CLASS_NAME_H` /
  `#endif  // CLASS_NAME_H`.
- The guard immediately follows the copyright block — **no blank
  line between them**.
- **One blank line** after the `#define`.
- **Two blank lines** before the closing `#endif`.
- The closing `#endif` carries a `// CLASS_NAME_H` comment.

### 15.3 Member declaration alignment

- Members and methods inside a class are typically aligned in
  columns (see example above): access-specifier-relative indent,
  return type column, name column. This matches Haiku public-header
  style.
- For private implementation classes that won't be reviewed against
  Haiku conventions, plain left-aligned declarations are fine.

## 16. Copyright headers

AST2400 is **GPL v2 (only)**. The Linux kernel — which is the primary
porting source — is GPL v2-only (no "or later" clause), so derivative
work must also be GPL v2-only. **Every source and header file carries
a GPL v2 header.** Preserve existing Linux upstream copyright lines
when porting a file substantially intact; **add** your line, do not
replace.

### 16.1 New AST2400 source files (no Linux ancestry)

For files written from scratch — driver glue, accelerant entry
points, build infrastructure, anything not lifted from upstream:

```cpp
/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
```

### 16.2 Files ported from Linux `drivers/gpu/drm/ast/`

When you port a file (or a substantial section) from Linux,
**preserve the upstream copyright lines** and add a new line below
them recording the port. Do not remove or alter the original credits:

```cpp
/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *
 * Copyright 2012 Red Hat Inc.
 *   Authors:
 *     Dave Airlie <airlied@redhat.com>
 *   (Original Linux drivers/gpu/drm/ast/ast_mode.c)
 *
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>.
 *   (Haiku port — accelerant API, RAII, Haiku types, TRACE logging.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  [...standard GPL v2 boilerplate...]
 */
```

Check the original Linux file for the actual copyright lines — the
above is illustrative. ASPEED, Red Hat, and various individual
contributors hold copyrights across `drivers/gpu/drm/ast/`; copy
the real lines from the upstream file at port time.

### 16.3 Files ported from `xf86-video-ast`

X11 driver predecessor; also relevant for some register sequences.
Originally MIT-licensed by ASPEED. Mixing MIT-licensed ported code
into the GPL v2 AST2400 codebase is permitted (MIT is GPL-compatible).
Preserve the upstream MIT copyright; the **combined** AST2400 binary
is GPL v2. Mark MIT-derived files with both licence blocks:

```cpp
/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *
 * Copyright (c) 2005 ASPEED Technology Inc.
 * (Original xf86-video-ast — see [filename] for the full upstream MIT block.)
 *
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>.
 *   (Haiku port.)
 *
 * The original xf86-video-ast file is MIT-licensed; the Haiku port
 * and combined AST2400 binary are GPL v2. See the upstream file for
 * the unmodified MIT block.
 */
```

### 16.4 Years

Update the year range when you make a substantive change. `2026` for
a brand-new file; `2026-2027` if you meaningfully edit it next year.
Trivial typo fixes do not bump the year.

### 16.5 Why GPL v2 and not MIT

Haiku itself is MIT, and Haiku's bundled drivers are typically MIT.
AST2400 is **not** an upstream Haiku driver — it's distributed as a
separate `.hpkg` and is not part of the Haiku codebase. Because we
port substantial code from Linux's GPL v2-only `drivers/gpu/drm/ast/`,
the combined derivative work must be GPL v2. New code in this project
is GPL v2 to keep all files under one license.

This also means **AST2400 cannot be upstreamed into Haiku proper** in
its current form — Haiku won't accept GPL drivers into the MIT-licensed
tree. That tradeoff is accepted as the price of being able to lean on
the Linux driver as a reference.

## 17. Resource management

- Stack objects over heap objects whenever possible.
- For locks in the accelerant, use Haiku's `AutoLock` template —
  never `Lock()`/`Unlock()` pairs by hand, and **not** `BAutolock`
  (deprecated in favour of `AutoLock`).
- For locks in the kernel driver, prefer `MutexLocker` /
  `InterruptsSpinLocker` from `<lock.h>` and `<kernel/lock.h>`. Same
  rule applies: RAII scoping, not manual acquire/release pairs.
- For dynamically-allocated kernel resources (areas, semaphores,
  driver cookies): wrap acquire/release in small RAII helpers when
  the lifetime is scoped; explicit cleanup in `uninit_driver` /
  `free_device` otherwise.
- No `goto cleanup:` patterns. RAII or early return. (Linux's
  `drivers/gpu/drm/ast/` uses goto-cleanup; we explicitly rewrite
  those when porting.)

## 18. Dead code, debug code, and printfs

- No `#if 0` blocks. Delete the code; git keeps history.
- No leftover `printf` / `fprintf(stderr, ...)` — promote to
  `TRACE()` (see §19) or remove.
- Long-lived diagnostic code lives behind `#ifdef TRACE_AST2400`
  and **must compile warning-clean** in both debug and release
  builds.
- Prefer `ASSERT(condition)` (kernel) / `debugger("msg")` (user) for
  invariants over ad-hoc `if (!x) abort();`.

## 19. Logging

### 19.1 Kernel driver

```cpp
TRACE("AstDriver: init_driver: chip rev 0x%x, %u MB framebuffer\n",
    chipRevision, framebufferSizeMb);
TRACE_ERROR("AstDriver: PLL lock timeout on connector %u\n", connector);
```

Defined in `src/add-ons/kernel/drivers/graphics/ast/ast_driver.h`.
`TRACE()` is gated by `#define TRACE_AST_DRIVER` at the top of the
file; `TRACE_ERROR()` is always on and lands in `dprintf()` /
syslog. Pattern matches what `radeon_hd` and `intel_extreme` do.

### 19.2 Accelerant

```cpp
TRACE("AstAccelerant: set_display_mode(%u x %u @ %u Hz, %u bpp)\n",
    mode->virtual_width, mode->virtual_height,
    mode->timing.pixel_clock / (mode->timing.h_total * mode->timing.v_total / 1000),
    bpp);
```

Defined in `src/add-ons/accelerants/ast/AstAccelerant.h`. Same
gating as the kernel side. Writes to the Haiku syslog
(`syslog(3)`).

Do not introduce per-file `fprintf(stderr, ...)` loggers in either
half — the kernel doesn't have `stderr`, and the accelerant runs
inside `app_server`'s process where stderr behavior is
unspecified.

## 20. Tooling

- **`haiku-format`** — clang-format-based auto-formatter using
  Haiku's config. Run before pushing. The AST2400 `.clang-format`
  overrides the upstream Haiku config in exactly one place:
  `ColumnLimit: 100`.
- **`checkstyle.py`** — Haiku's Python style checker. We carry a
  small wrapper in `scripts/checkstyle-ast2400.py` that suppresses
  the `LineTooLong` rule below column 100 and otherwise defers to
  upstream.
- **`pre-commit`** hook — runs `haiku-format --dry-run` and the
  checkstyle wrapper; non-zero exit blocks the commit.

The hook is opt-in (`scripts/install-hooks.sh`) so contributors can
disable it for WIP commits, but CI runs the same checks on every
PR and will block merge on failure.

## 21. PR checklist

Before opening a PR, verify:

- [ ] `haiku-format` is clean (or the deviation is justified in the
      PR description).
- [ ] `checkstyle-ast2400.py` is clean.
- [ ] No lines over 100 columns without justification.
- [ ] Public/header API has Doxygen comments.
- [ ] No `printf`/`fprintf` debug leftovers; logging goes through
      `TRACE()`.
- [ ] No `#if 0` blocks.
- [ ] Copyright headers present and correct (GPL v2 for new files;
      upstream lines preserved for ported files).
- [ ] If the change ports code from Linux, the source file and
      kernel version are cited in a comment near the ported code.
- [ ] File ends with a newline.

---

## Appendix A — Quick reference card

```
Indent: TAB (width 4)
Line:   target ≤80, soft warn at 100
Brace:  class same line; function own line; if/for/while same line
Naming: UpperCamel types/funcs, lowerCamel vars, f/k/g/s prefixes, _ private
        (driver/accelerant entry points stay lower_case_with_underscores)
Pointer: AstDevice* device = NULL;
Cast:   static_cast<T>(x);   (T)x for primitives only
        (void*)(addr_t)bar for BAR-to-pointer
Null:   NULL, no nullptr
Bool:   true/false, never TRUE/FALSE
Bitmask: if ((x & MASK) != 0)
Switch: case indented; { } if vars; default: required
Strings: char[N]+snprintf in kernel; BString in accelerant
Errors: status_t, B_OK on success
Logging: TRACE("..."); TRACE_ERROR("...");
Licence: GPL v2 (only) — Linux-derived
```

## Appendix B — Linux radeon/ast anti-patterns we are NOT carrying over

We port from Linux's `drivers/gpu/drm/ast/` but **NOT** the following
Linux/DRM habits:

- `goto err_*` cleanup chains → use RAII or early return.
- `pr_err()` / `dev_dbg()` logging → use `TRACE()` / `TRACE_ERROR()`.
- DRM object-model wrappers (`drm_crtc`, `drm_connector`,
  `drm_encoder`) → use Haiku's accelerant model directly.
- `mutex_init` / `mutex_lock_irqsave` → use Haiku's mutex / spinlock
  helpers (`mutex_init`, `MutexLocker`, `InterruptsSpinLocker`).
- `kzalloc` / `kfree` → `new`/`delete` (or `malloc`/`free` only at
  ABI boundaries that require it).
- `struct foo { ... } __packed` followed by raw casts → use Haiku's
  packed-attribute conventions and named field access; if reading
  from MMIO use explicit `read32` / `write32` helpers, not pointer
  casts.
- Per-file CamelCase macros (`AST_READ32`) → use lowercase
  inline functions, or shared `ast_read32(reg)` helpers, in a
  shared header.
- C89-style "declarations at top of function" → declare close to use.
- Yoda conditions (`NULL == ptr`) → variable on the left.
- Single-letter variables outside trivial loop scopes → name them.

These are not retroactive cleanups — we don't rewrite imported
Linux files all at once. But any **new** code, and any imported file
we **substantively modify**, conforms to this guide.
