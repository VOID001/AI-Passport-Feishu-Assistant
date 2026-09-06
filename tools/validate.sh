#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_node_tests() {
    local project

    command -v npm >/dev/null 2>&1 || {
        echo "ERROR: npm is required for host tests." >&2
        return 1
    }
    for project in tools/passport_web; do
        if ! npm --prefix "${project}" ls --depth=0 >/dev/null 2>&1; then
            npm --prefix "${project}" ci --no-audit --no-fund
        fi
        npm --prefix "${project}" test
    done
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    bash -n run_assistant_web.sh
    ./run_assistant_web.sh --help >/dev/null
    bash -n tools/package_assistant_web.sh
    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_work_assistant_model.c main/work_assistant_model.c \
        -o "${test_dir}/test_work_assistant_model"
    "${test_dir}/test_work_assistant_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_work_assistant_flow.c main/work_assistant_flow.c \
        -o "${test_dir}/test_work_assistant_flow"
    "${test_dir}/test_work_assistant_flow"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_legacy_nvs_migration.c main/legacy_nvs_migration.c \
        -o "${test_dir}/test_legacy_nvs_migration"
    "${test_dir}/test_legacy_nvs_migration"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_work_assistant_settings.c main/work_assistant_settings.c \
        -o "${test_dir}/test_work_assistant_settings"
    "${test_dir}/test_work_assistant_settings"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_ble_protocol.c main/passport_ble_protocol.c \
        -o "${test_dir}/test_passport_ble_protocol"
    "${test_dir}/test_passport_ble_protocol"
    python3 -m unittest discover -s tests -p 'test_passport_bridge.py'
    python3 tests/test_verify_firmware.py
    run_node_tests
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
