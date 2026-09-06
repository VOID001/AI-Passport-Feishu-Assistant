<p align="right">
  <a href="README.zh_CN.md">Simplified Chinese</a> · <strong>English</strong>
</p>

# AI Passport Local Web Service

## Start

Run this from the repository root:

```bash
./run_assistant_web.sh
```

The launcher checks macOS, Node.js 20+, Python, and the local `lark-cli` login.
If user authorization is missing, it starts the Feishu calendar authorization
flow and continues automatically after authorization succeeds. It creates
`/tmp/ai-passport-ble-venv` when needed, installs missing Python and npm
dependencies, and starts the local console. Use
`./run_assistant_web.sh --check` to prepare and verify the environment without
starting the server, or `./run_assistant_web.sh --no-open` to keep the browser
closed. If the default port is busy, the launcher automatically selects a free
port from 4178 through 4197.

To start the service manually:

```bash
cd tools/passport_web
npm install
npm start
```

Open `http://127.0.0.1:4177`. The service synchronizes immediately, then every
10 minutes while automatic synchronization is enabled.

The terminal at the bottom retains the latest 200 sanitized entries. It reports
source counts, summary and payload metadata, BLE discovery and connection,
individual data frames, commit validation, and the final device acknowledgement.

The service reuses the Python bridge. Set `PASSPORT_PYTHON` when its dependencies
are installed outside `/tmp/ai-passport-ble-venv`, and set `PASSPORT_DEVICE` to
pin a CoreBluetooth device identifier. The first connection to a Passport shows
a six-digit pairing code on its screen; enter that code in the macOS Bluetooth
prompt. The resulting authenticated bond is retained, so later connections do
not require another code unless the Bluetooth pairing is removed.

```bash
PASSPORT_PYTHON=/path/to/venv/bin/python \
PASSPORT_DEVICE=<CoreBluetooth-ID> \
npm start
```

The server binds localhost only. It never places Feishu credentials in browser
responses, command-line arguments, or service configuration.

## Package for macOS

Create a distributable archive from the repository root:

```bash
./tools/package_assistant_web.sh
```

The result is `dist/AI-Passport-Sync-macos.zip`. It contains a plain script
bundle, not an unsigned `.app` or `.command` launcher. Users unzip it, open
Terminal, change into the extracted `AI Passport Sync` directory, and run:

```bash
bash start.sh
```

The archive includes the application source and setup scripts, but excludes
Node modules, Python virtual environments, logs, BLE pairing records, and
Feishu credentials. Each user still needs macOS, Node.js 20+, Python 3,
`lark-cli`, and Bluetooth permission. The launcher guides first-time users
through `lark-cli` authorization.

## Test

```bash
npm test
```
