# tsc-sync

*[Português](README.md)*

Measures, and on request fixes, the TSC (time-stamp counter) of CPUs that the
firmware brings up out of sync. **Status: experimental, validated on a single
laptop.** Installing it enables nothing.

## The problem

On some AMD Ryzen laptops, the firmware brings up **CPU 0 with its TSC a few
seconds behind** the other CPUs. On the laptop studied (a Ryzen 7 5000-series
"Cezanne", already on the manufacturer's latest BIOS), only CPU 0 lagged, and the
other 15 agreed with each other. The gap changes on every boot (2.0 to 20.7 s
over 89 boots) and follows the time the firmware spends in POST. The kernel
detects it at boot:

```
Measured 10821117556 cycles TSC warp between CPUs, turning off TSC clock.
tsc: Marking TSC unstable due to check_tsc_sync_source failed
clocksource: Switched to clocksource hpet
```

These Ryzen CPUs have no `TSC_ADJUST`, the feature Linux uses to resynchronize, so the
kernel can only switch its clock to HPET. The system clock stays correct, but
**programs that read the TSC directly (the `RDTSC` instruction) still see CPU 0
behind**. Dota 2 times short waits (`ThreadSpin`) that way: when a thread moves
to CPU 0 in the middle of a wait, the clock "goes back" a few seconds and the
game freezes for the length of the gap (ValveSoftware/Dota-2#3558). Booting with
`amd_prefcore=disable` cut the frequency (~85%) but doesn't remove it, because
threads still land on CPU 0 now and then.

## What tsc-sync does

```
tsc-sync                      # measure (default, no root)
sudo tsc-sync sync --dry-run  # show what it would write
sudo tsc-sync sync            # write
```

**Measuring.** To compare CPU *k* with the reference *R*, it reads *R*'s TSC,
moves to *k*, reads *k*'s TSC, moves back to *R* and reads it again. Each read
sits between two reads of `CLOCK_MONOTONIC_RAW` (HPET here, one clock shared by
all CPUs), and the narrowest of 64 attempts is kept. Interpolating *R*'s two
reads at the instant of *k*'s read gives the gap **directly in cycles,
independent of the TSC frequency**. This is repeated 15 times and the median is
used. The reference is a CPU from the majority group; only the CPUs that
disagree with it are corrected.

Example, from a boot where the kernel measured 10,821,117,556 cycles:

```
TSC at 3194.000 MHz; tolerance 50 us; measurement uncertainty ~1.4 us
reference: CPU 1 (majority group)
  CPU  0   -3.387952 s    BEHIND
  CPU  1        +0.0 us   ok
  CPU  2        -0.1 us   ok
  ...
CPU 0: would write TSC + 10821117635 cycles (+3.387953 s)
```

The measurement matches the kernel's to within a few dozen cycles (tens of
nanoseconds), and the 15 CPUs in the group agree with each other to under 1 µs.

**Fixing.** Pinned to the lagging CPU, it reads its own TSC and writes to MSR
`0x10` (`IA32_TSC`, through `/dev/cpu/N/msr`) that value plus the gap plus the
estimated cost of the write itself. It then measures again and refines until it
is within tolerance (50 µs by default, at most 6 passes). A write lasts until
the next boot.

| Exit code | Meaning |
|---|---|
| 0 | in sync (or synced) |
| 1 | error: not root, `/dev/cpu/N/msr` missing, write refused |
| 2 | invalid usage |
| 3 | out of sync (from `measure` and `--dry-run`) |
| 4 | wrote, but some CPU did not converge |

## Results on the tested laptop

With the service enabled, on two boots (kernel 7.0):

| Boot | CPU 0 behind by | After `sync` |
|---|---|---|
| 1 | 3.158645 s (the kernel measured 3.158 s) | +6.0 µs, in one pass |
| 2 | 3.971967 s | +4.5 µs, in one pass |

Five hours after the first boot, `tsc-sync` still reported "in sync". In a
71-minute Dota 2 session in that state (with `amd_prefcore=disable`), the GPU
never stopped for 0.5 s or more. Before, there were ~58 stops of 1 s or more per
hour, and ~8 per hour with `amd_prefcore=disable` alone. Resume from suspend has
not been tested yet.

## Install

```bash
sudo ./install.sh              # build and install; does NOT enable
sudo ./install.sh --enable     # install and enable at boot and on resume
sudo ./install.sh --disable    # disable, but keep it installed (to measure)
sudo ./install.sh --uninstall  # disable and remove
make check                     # check the catalogs, measure and show the --dry-run, no root
```

It installs `/usr/local/sbin/tsc-sync` and two units:

- `tsc-sync.service`: early at boot (before `sysinit.target`), so that no user
  program sees the jump;
- `tsc-sync-resume.service`: on resume from suspend or hibernation. If
  everything is in sync, it only measures and exits.

## What to test before enabling

1. `tsc-sync`: confirm that only CPU 0 shows as behind.
2. `sudo tsc-sync sync --dry-run`: the gap should match the kernel's
   (`journalctl -k -b | grep "TSC warp"`, in cycles).
3. `sudo tsc-sync sync`: once, with the system idle. Then `tsc-sync` should
   report "in sync".
4. Check `journalctl -k -b | tail` for warnings from the `msr` driver or the
   scheduler.
5. Play a match and compare the stops with
   [monitor](https://github.com/rattones/monitor): the spin on core 0 should no
   longer freeze the game.
6. Suspend and resume, then run `tsc-sync`: shows whether the firmware
   desynchronizes again on resume.
7. Only then `sudo ./install.sh --enable`.

## Risks and limits

- **It writes a CPU register**, something Linux itself avoids on AMD: in a
  discussion on the kernel mailing list (LKML), the timekeeping maintainer
  explains that attempts to fix the TSC failed because the firmware's SMM
  delayed the write. That is why tsc-sync measures again after writing and only
  reports success within tolerance.
- **CPU 0 "jumps" forward once** (3.39 s in the example). The system clock is
  unaffected (it is HPET). The scheduler's internal clock on CPU 0
  (`sched_clock`) sees the jump; with the TSC marked unstable the kernel filters
  per-CPU jumps, but CPU time accounting may look odd at that instant. That is
  why the service runs before any user program.
- **The kernel stays on HPET** after the fix: it marks the TSC unstable at boot
  and never goes back. The system clock gets no faster; only programs that use
  `RDTSC` get consistency.
- **The `msr` driver logs a warning** on every boot where it writes, and marks
  the kernel as tainted (`CPU_OUT_OF_SPEC`): `msr: Write to unrecognized MSR 0x10
  by tsc-sync ... tainting CPU_OUT_OF_SPEC`. With Secure Boot and kernel lockdown
  active the write is refused (lockdown is `none` here).
- **To undo**: `sudo ./install.sh --disable` (or `--uninstall`) and reboot.
  The fix applied in the current boot lasts until you reboot; after that the
  firmware leaves the TSC as it was.
- It only makes sense on physical hardware (`ConditionVirtualization=no`).

## Language

Messages from the program and from `install.sh` follow the system language:
English or Portuguese, and any other language falls back to English.

```bash
LANG=pt_BR.UTF-8 tsc-sync     # Portuguese
TSC_SYNC_LANG=en tsc-sync     # force English, ignoring the locale
```

Detection follows POSIX precedence (`LC_ALL` > `LC_MESSAGES` > `LANG`), with
`TSC_SYNC_LANG` above all; `C` and `POSIX` mean English. The systemd service
uses the system language (`/etc/default/locale`) when writing to the journal.

The pattern is the same as [monitor](https://github.com/rattones/monitor)'s:
English (`i18n/en.h`) is the source language and the base layer, and each
translation (`i18n/pt.h`) overrides it key by key. A key without a translation
comes out in English, never empty. The catalogs are compiled into the binary: a
program that runs as root does not read text from files at runtime. Since each
message becomes a `printf` format, `make check` runs `i18n_check`, which
verifies that every translation uses exactly the same placeholders (`%d`, `%s`,
`%+.3f`...) as the English text, in the same order. Numbers always use a dot as
the decimal separator, in any language. To add a language: an
`i18n/<lang>.h`, its table in `i18n.c` and an entry in `i18n_check.c`.

Code comments are in Portuguese.

## Files

| File | Role |
|---|---|
| `tsc-sync.c` | the program (C, libc only) |
| `i18n.h`, `i18n.c` | language detection and message lookup |
| `i18n/en.h` | English catalog (source and base) |
| `i18n/pt.h` | Portuguese catalog |
| `i18n_check.c` | checks each translation's placeholders (`make check`) |
| `systemd/tsc-sync.service` | runs at boot |
| `systemd/tsc-sync-resume.service` | runs on resume from suspend |
| `install.sh` | installs, enables (`--enable`) or removes (`--uninstall`) |
| `Makefile` | `make` builds; `make check` checks the catalogs and measures without root |
