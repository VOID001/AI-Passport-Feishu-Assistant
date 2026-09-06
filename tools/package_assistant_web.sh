#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${repo_root}/dist"
package_name="AI-Passport-Sync-macos"
stage_dir="$(mktemp -d /tmp/ai-passport-package.XXXXXX)"
release_root="${stage_dir}/AI Passport Sync"
runtime_root="${release_root}"

cleanup() {
    case "${stage_dir}" in
        /tmp/ai-passport-package.*) rm -rf -- "${stage_dir}" ;;
    esac
}
trap cleanup EXIT

[[ "$(uname -s)" == "Darwin" ]] || {
    echo "ERROR: This package can only be built on macOS." >&2
    exit 1
}
command -v ditto >/dev/null 2>&1 || {
    echo "ERROR: macOS ditto is required to create the archive." >&2
    exit 1
}

mkdir -p "${runtime_root}/tools"
cp "${repo_root}/run_assistant_web.sh" "${runtime_root}/"
cp -R "${repo_root}/tools/passport_web" "${runtime_root}/tools/"
cp -R "${repo_root}/tools/passport_bridge" "${runtime_root}/tools/"

# Dependencies and runtime output are user-specific and must not ship.
rm -rf \
    "${runtime_root}/tools/passport_web/node_modules" \
    "${runtime_root}/tools/passport_web/test" \
    "${runtime_root}/tools/passport_web/test-output" \
    "${runtime_root}/tools/passport_bridge/__pycache__"
rm -f \
    "${runtime_root}/tools/passport_web/.gitignore" \
    "${runtime_root}/tools/passport_web/README.md" \
    "${runtime_root}/tools/passport_web/README.zh_CN.md"
find "${runtime_root}/tools" -type d -name "__pycache__" -prune -exec rm -rf {} +
find "${runtime_root}/tools" -type f -name "*.pyc" -delete
chmod +x "${runtime_root}/run_assistant_web.sh"

cat > "${release_root}/start.sh" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail

script_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${script_root}"
exec ./run_assistant_web.sh "$@"
LAUNCHER

cat > "${release_root}/README.txt" <<'README'
AI Passport Sync for macOS

1. Open Terminal.
2. Run `cd `, then drag this "AI Passport Sync" folder into Terminal and press
   Return. Run `bash start.sh`.
3. The first launch installs the bundled Node.js/Python
   dependencies when needed.
4. If needed, complete the lark-cli authorization opened by the launcher.
   Startup continues automatically after authorization succeeds.
5. Allow Bluetooth access when macOS asks, then keep the AI Passport in its
   Bluetooth pairing screen.

Requirements: macOS, Node.js 20 or later, Python 3, and lark-cli.
The script runs only on this Mac because it reads this Mac's lark-cli identity and
uses this Mac's Bluetooth hardware. No credentials are packaged in this archive.
README

mkdir -p "${output_dir}"
archive_path="${output_dir}/${package_name}.zip"
rm -f -- "${archive_path}"
(
    cd "${stage_dir}"
    ditto -c -k --sequesterRsrc --keepParent "AI Passport Sync" "${archive_path}"
)

echo "Created ${archive_path}"
