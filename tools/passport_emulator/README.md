<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# ESP32-C3 QEMU Support

This directory contains only the files required by the repository's native
ESP32-C3 QEMU target:

- `sdkconfig.qemu.defaults`: QEMU-only ESP-IDF configuration.
- `qemu_components/esp_lcd_qemu_rgb/`: MMIO-backed RGB display component.
- `run-qemu.sh`: compatibility entry point for the root Makefile.

Run from the repository root:

```bash
make run-qemu
```

The command builds the isolated QEMU firmware in `build-qemu/`, creates an
8 MiB virtual Flash image, and starts Espressif QEMU with the graphical display.
The QEMU build is for boot and UI validation only. Do not publish or flash its
binary to physical hardware.

QEMU does not replace device testing for the physical display, buttons, audio,
battery, Wi-Fi, Bluetooth radio, or Recovery gesture.
