<p align="right">
  <strong>简体中文</strong> · <a href="build-and-test.md">English</a>
</p>

# 构建与验证（Build & Test）

使用 ESP-IDF 5.5.3。全新机器或缺少工具链时，先按
[环境引导](environment-setup.zh_CN.md)完成安装。

> 固件编译优先运行 `make build-device`，烧录优先把验证通过的
> `build/FoloToy-AI-Passport-full.bin` 写入空白设备；对已有身份的设备，只有合并文件
> 在保护区 `cardid` 之前结束时才可从 `0x0` 直刷，其余情况优先用小程序或分段
> `idf.py flash`。`idf.py build` 和
> `idf.py flash` 只作为增量开发命令，不作为默认交付方式。

```bash
source <ESP-IDF-v5.5.3-路径>/export.sh
idf.py --version             # 必须输出 ESP-IDF v5.5.3
make build-device             # 优先：编译并验证 0x0 合并固件
make build-device-dev         # 可选：增量 app 编译
make flash-device PORT=/dev/cu.usbmodem1101
make run-qemu
```

`idf.py fullclean` 不能让已有 `sdkconfig` 完整同步变更后的 defaults。需要重建
target 或已跟踪 defaults 时，先保留有意的本地设置，再运行
`idf.py set-target esp32c3`。

仓库提交 `dependencies.lock` 以固定 ESP-IDF Managed Components 的解析结果。修改 `idf_component.yml` 后必须使用 ESP-IDF 5.5.3 重新生成锁文件、review 版本变化并与 manifest 一起提交；普通构建不应产生未提交的锁文件差异。

固件门禁使用全新的临时构建目录，并从仓库 `sdkconfig.defaults` 生成隔离的 `sdkconfig`。它不会读取或覆盖开发者根目录的 `sdkconfig`，只把验证通过的合并镜像复制到 `build/FoloToy-AI-Passport-full.bin`。门禁同时强制检查[小程序 BLE 兼容契约](ble-recovery-compatibility.zh_CN.md)：保护分区地址、应用大小、分区表 MD5、保护区数据不入包，以及 Recovery bootloader hook。

当前基线含一个可独立运行的纯逻辑测试：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

统一 Makefile 验证入口：

```bash
make test          # 仓库一致性、workflow、文档链接、敏感信息、host tests
make build-device  # ESP-IDF build、merge-bin、偏移与 BLE 兼容校验
make check         # 完整验证
```

`make test` 会从已提交且只引用公共 registry 的 lockfile 安装缺失的 npm
依赖，然后运行 C/Python host tests 和 Work Assistant Web Node.js tests。
完整验证要求预先激活 ESP-IDF 5.5.3。CI 与本地使用同一脚本；若 CI 和本地行为
不同，应先修复共享脚本或环境，而不是维护两份命令。

交付前，最终生产构建成功后运行 `make run-qemu`，并确认应用到达启动日志。
CI 会验证 host tests 和生产镜像，但不会启动交互式图形 QEMU，因此应在发布证据
中记录最后一次本地 QEMU 结果。

## Makefile 入口

仓库根目录的 `Makefile` 将真机与 QEMU 构建隔离：

```bash
make build-device
make build-qemu
make run-qemu
make flash-device PORT=/dev/cu.usbmodem1101
make dist
```

- `build-device` 运行固件门禁，唯一发布产物仍为
  `build/FoloToy-AI-Passport-full.bin`。
- `build-qemu` 输出隔离的 `build-qemu/FoloToy-AI-Passport-full.bin`，仅用于
  QEMU，不得发布或写入真机。
- `flash-device` 使用分段 `idf.py flash`，不会写入 `cardid` 或永久 Recovery。
- `flash-device-full` 会从 `0x0` 写入完整镜像，必须显式传入
  `FULL_FLASH=1`，且只可用于已确认安全的设备：

```bash
make flash-device-full PORT=/dev/cu.usbmodem1101 FULL_FLASH=1
```

- `dist` 会生成 `dist/AI-Passport-Sync-macos.zip` 和经过验证的
  `dist/FoloToy-AI-Passport-full.bin`。ZIP 是 macOS Web 启动端，固件始终为
  生产 Full Bin，绝不使用 QEMU 镜像。

涉及物理外设的改动必须在真机运行硬件指南验收清单，并把“编译通过”与“硬件验证通过”分开记录。

社区只能上传验证通过的 `build/FoloToy-AI-Passport-full.bin`，不得上传应用单镜像
`build/FoloToy-AI-Passport.bin`，后者没有小程序可安全解析与转换的完整结构。
