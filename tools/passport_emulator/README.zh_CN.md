<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# ESP32-C3 QEMU 支持

本目录只保留仓库原生 ESP32-C3 QEMU 目标所需文件：

- `sdkconfig.qemu.defaults`：仅用于 QEMU 的 ESP-IDF 配置。
- `qemu_components/esp_lcd_qemu_rgb/`：MMIO RGB 显示组件。
- `run-qemu.sh`：根 Makefile 的兼容入口。

在仓库根目录运行：

```bash
make run-qemu
```

该命令会在 `build-qemu/` 中构建隔离的 QEMU 固件，生成 8 MiB 虚拟 Flash，
并启动带图形显示的 Espressif QEMU。QEMU 构建只用于启动和 UI 验证，不得发布
或写入物理设备。

QEMU 不能替代实体屏幕、按键、音频、电池、Wi-Fi、Bluetooth 射频和 Recovery
手势测试。
