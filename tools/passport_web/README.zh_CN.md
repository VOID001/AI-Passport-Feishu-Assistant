<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport 本地 Web 服务

## 启动

在仓库根目录执行：

```bash
./run_assistant_web.sh
```

脚本会检查 macOS、Node.js 20+、Python 和 `lark-cli` 登录状态。用户未授权时，
脚本会自动启动飞书日历授权流程，并在授权成功后继续启动。脚本按需创建
`/tmp/ai-passport-ble-venv`、安装 Python / npm 依赖，然后启动本地同步台。
使用 `./run_assistant_web.sh --check` 可只准备和检查环境，使用
`./run_assistant_web.sh --no-open` 可禁止自动打开浏览器。默认端口被占用时，
脚本会自动选择 `4178` 至 `4197` 之间的空闲端口。

如需手动启动：

```bash
cd tools/passport_web
npm install
npm start
```

打开 `http://127.0.0.1:4177`。服务启动后立即同步；启用自动同步时，之后每
10 分钟同步一次。

页面底部 Terminal 保留最近 200 条脱敏日志，详细展示数据源计数、摘要与
payload 元数据、BLE 发现与连接、每个数据分片、commit 校验和设备最终 ACK。

服务复用 Python bridge。如果依赖不在 `/tmp/ai-passport-ble-venv`，请设置
`PASSPORT_PYTHON`；如需固定 CoreBluetooth 设备标识，请设置
`PASSPORT_DEVICE`。首次连接某张 Passport 时，卡片屏幕会显示六位配对码；
请在 macOS 蓝牙弹窗中输入该码。认证绑定会被保留，除非删除蓝牙配对，后续连接
无需再次输入。

```bash
PASSPORT_PYTHON=/path/to/venv/bin/python \
PASSPORT_DEVICE=<CoreBluetooth-ID> \
npm start
```

服务仅监听 localhost，不会把飞书凭证写入浏览器响应、命令行参数或服务配置。

## 打包 macOS 脚本包

在仓库根目录执行：

```bash
./tools/package_assistant_web.sh
```

脚本会生成 `dist/AI-Passport-Sync-macos.zip`。压缩包是普通脚本包，不包含未签名的
`.app` 或 `.command` 启动器。用户解压后打开 Terminal，进入 `AI Passport Sync`
目录并执行：

```bash
bash start.sh
```

压缩包包含应用源码和准备脚本，但不包含 Node modules、Python 虚拟环境、日志、BLE
配对记录或飞书凭证。每位用户仍需在自己的 Mac 上安装 Node.js 20+、Python 3 和
`lark-cli`，并授予蓝牙权限；启动脚本会引导首次使用者完成 `lark-cli` 授权。

## 测试

```bash
npm test
```
