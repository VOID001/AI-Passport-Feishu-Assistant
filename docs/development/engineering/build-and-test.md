<p align="right">
  <a href="build-and-test.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Build and Test

Use ESP-IDF 5.5.3. On a clean machine or when the toolchain is missing, follow
the [environment bootstrap](environment-setup.md) first.

> Prefer `make build-device` for firmware builds and flash its
> verified `build/FoloToy-AI-Passport-full.bin` at offset `0x0` only when the
> target is blank or the merged byte range ends before protected `cardid`.
> On a provisioned device, prefer mini-program install or segmented
> `idf.py flash`. Treat
> `idf.py build` and `idf.py flash` as incremental development commands, not the
> default delivery path.

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version             # must report ESP-IDF v5.5.3
make build-device             # preferred: build and verify merged 0x0 image
make build-device-dev         # optional incremental application build
make flash-device PORT=/dev/cu.usbmodem1101
make run-qemu
```

`idf.py fullclean` does not fully synchronize an existing `sdkconfig` with
changed defaults. Preserve intentional local settings, then run
`idf.py set-target esp32c3` when the target or tracked defaults must be
regenerated.

The tracked `dependencies.lock` pins Managed Component resolution. After changing an `idf_component.yml`, regenerate the lock with ESP-IDF 5.5.3, review version changes, and commit it with the manifest. An ordinary build must not leave an unexplained lock-file diff.

Firmware validation uses a fresh temporary build directory and an isolated `sdkconfig` generated from the tracked defaults. It does not consume or overwrite a developer's root `sdkconfig`, and it copies only the verified merged image to `build/FoloToy-AI-Passport-full.bin`. The gate also enforces the [mini-program BLE compatibility contract](ble-recovery-compatibility.md): protected partition addresses, application size, partition-table MD5, absence of protected payload data, and the Recovery bootloader hook.

The baseline also has a hardware-independent logic test:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

Use the unified Makefile validation entry point:

```bash
make test          # repository checks, workflows, links, secrets, host tests
make build-device  # build, merge-bin, offsets, and BLE compatibility
make check         # complete gate; requires an activated ESP-IDF environment
```

`make test` installs missing npm dependencies from the committed public-registry
lockfile, then runs the C/Python host tests plus the Work Assistant Web Node.js
suite. CI calls the same script. Fix the shared script or environment if local
and CI behavior differs; do not duplicate command sequences in workflows.

Before delivery, run `make run-qemu` after the final successful production
build and confirm the application reaches its startup log. CI validates host
tests and the production image but does not launch the interactive graphical
QEMU process, so record this final QEMU result in the release evidence.

## Makefile Targets

The repository-root `Makefile` keeps device and QEMU builds isolated:

```bash
make build-device
make build-qemu
make run-qemu
make flash-device PORT=/dev/cu.usbmodem1101
make dist
```

- `build-device` runs the firmware gate; the sole release artifact remains
  `build/FoloToy-AI-Passport-full.bin`.
- `build-qemu` writes the isolated
  `build-qemu/FoloToy-AI-Passport-full.bin`, for QEMU only. Do not release it
  or flash it to hardware.
- `flash-device` uses segmented `idf.py flash` and does not write `cardid` or
  permanent Recovery.
- `flash-device-full` writes the Full Bin from `0x0`. It requires explicit
  `FULL_FLASH=1` confirmation and is only for a confirmed-safe device:

```bash
make flash-device-full PORT=/dev/cu.usbmodem1101 FULL_FLASH=1
```

- `dist` produces `dist/AI-Passport-Sync-macos.zip` and the verified
  `dist/FoloToy-AI-Passport-full.bin`. The ZIP is the macOS Web launcher; the
  firmware remains the production Full Bin, never a QEMU image.

Hardware-affecting changes must also run the applicable on-device checklist in the hardware guide. Report compilation separately from physical-device validation.

Never upload the app-only `build/FoloToy-AI-Passport.bin` to the community. Only
the validated `build/FoloToy-AI-Passport-full.bin` contains the structure the
mini-program can inspect and transform safely.
