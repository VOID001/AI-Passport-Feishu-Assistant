#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
web_root="${repo_root}/tools/passport_web"
requirements="${repo_root}/tools/passport_bridge/requirements.txt"
default_venv="${PASSPORT_VENV:-/tmp/ai-passport-ble-venv}"
check_only=false

usage() {
    cat <<'EOF'
Usage: ./run_assistant_web.sh [--check] [--no-open]

Options:
  --check     Prepare dependencies and verify the environment without starting.
  --no-open   Start the service without opening the browser automatically.
  -h, --help  Show this help.

Optional environment variables:
  PASSPORT_PYTHON   Python executable with the BLE bridge dependencies.
  PASSPORT_VENV     Managed virtualenv path (default: /tmp/ai-passport-ble-venv).
  PASSPORT_DEVICE   CoreBluetooth identifier for a specific Passport.
  PASSPORT_WEB_PORT Local web port (default: 4177).
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 is required but was not found."
}

resolve_python() {
    if [[ -n "${PASSPORT_PYTHON:-}" ]]; then
        if [[ "${PASSPORT_PYTHON}" == */* ]]; then
            [[ -x "${PASSPORT_PYTHON}" ]] ||
                fail "PASSPORT_PYTHON is not executable: ${PASSPORT_PYTHON}"
            printf '%s\n' "${PASSPORT_PYTHON}"
        else
            command -v "${PASSPORT_PYTHON}" ||
                fail "PASSPORT_PYTHON was not found: ${PASSPORT_PYTHON}"
        fi
        return
    fi

    if [[ ! -x "${default_venv}/bin/python" ]]; then
        require_command python3
        printf 'Creating Python environment at %s...\n' "${default_venv}" >&2
        python3 -m venv "${default_venv}"
    fi
    printf '%s\n' "${default_venv}/bin/python"
}

port_available() {
    "${python_bin}" - "$1" <<'PY'
import socket
import sys

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    try:
        listener.bind(("127.0.0.1", int(sys.argv[1])))
    except OSError:
        raise SystemExit(1)
PY
}

verify_lark_user() {
    local auth_status

    if ! auth_status="$(lark-cli auth status --json --verify 2>&1)"; then
        printf 'lark-cli authentication check failed:\n%s\n' "${auth_status}" >&2
        return 1
    fi

    if ! printf '%s\n' "${auth_status}" |
        "${python_bin}" -c '
import json
import sys

document = json.load(sys.stdin)
user = document.get("identities", {}).get("user", {})
if user.get("status") != "ready" or not user.get("verified"):
    raise SystemExit(1)
'; then
        printf 'lark-cli did not report a verified user login.\n' >&2
        return 1
    fi
}

ensure_lark_user() {
    if verify_lark_user; then
        return
    fi

    [[ -t 0 ]] ||
        fail "lark-cli authorization is required; rerun this launcher in an interactive Terminal."

    printf '\nFeishu calendar authorization is required.\n'
    printf 'Complete the authorization in the browser; startup will continue automatically.\n\n'
    lark-cli auth login --domain calendar ||
        fail "lark-cli authorization was not completed."
    verify_lark_user ||
        fail "lark-cli authorization completed, but the user login could not be verified."
}

while (($# > 0)); do
    case "$1" in
        --check)
            check_only=true
            ;;
        --no-open)
            export PASSPORT_NO_OPEN=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            fail "unknown option: $1"
            ;;
    esac
    shift
done

[[ "$(uname -s)" == "Darwin" ]] ||
    fail "the Work Assistant BLE bridge currently requires macOS."
require_command node
require_command npm
require_command lark-cli

node_major="$(node -p 'Number(process.versions.node.split(".")[0])')"
[[ "${node_major}" =~ ^[0-9]+$ ]] ||
    fail "could not determine the installed Node.js version."
((node_major >= 20)) ||
    fail "Node.js 20 or newer is required; found $(node --version)."

python_bin="$(resolve_python)"
ensure_lark_user

if ! "${python_bin}" -c 'import bleak; from PIL import Image' >/dev/null 2>&1; then
    printf 'Installing Python BLE bridge dependencies...\n'
    "${python_bin}" -m pip install --disable-pip-version-check -r "${requirements}"
fi

if ! (cd "${web_root}" && npm ls --depth=0 >/dev/null 2>&1); then
    printf 'Installing local Web dependencies...\n'
    (cd "${web_root}" && npm ci --no-audit --no-fund)
fi

printf 'Environment ready: Node.js %s, Python %s, lark-cli user authenticated.\n' \
    "$(node --version)" \
    "$("${python_bin}" -c 'import platform; print(platform.python_version())')"

if [[ "${check_only}" == true ]]; then
    exit 0
fi

web_port="${PASSPORT_WEB_PORT:-4177}"
[[ "${web_port}" =~ ^[0-9]+$ ]] &&
    ((web_port >= 1 && web_port <= 65535)) ||
    fail "PASSPORT_WEB_PORT must be an integer between 1 and 65535."
if ! port_available "${web_port}"; then
    if [[ -n "${PASSPORT_WEB_PORT:-}" ]]; then
        fail "port ${web_port} is already in use."
    fi
    for ((candidate = 4178; candidate <= 4197; candidate++)); do
        if port_available "${candidate}"; then
            printf 'Port 4177 is in use; using %s instead.\n' "${candidate}"
            web_port="${candidate}"
            break
        fi
    done
    [[ "${web_port}" != 4177 ]] ||
        fail "no free local port was found between 4177 and 4197."
fi

export PASSPORT_PYTHON="${python_bin}"
export PASSPORT_WEB_PORT="${web_port}"
printf 'Starting Work Assistant at http://127.0.0.1:%s\n' "${web_port}"
cd "${web_root}"
exec npm start
