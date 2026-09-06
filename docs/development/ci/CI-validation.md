<p align="right">
  <a href="CI-validation.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pull Request Validation

`.github/workflows/ci.yml` runs for pull requests, pushes to `main`, and manual dispatch. Local development and CI use the same Makefile targets.

## Jobs

- **Repository and host checks:** validates English-default bilingual Markdown,
  relative links, full-SHA Actions, issue forms, dependency locks, release
  artifacts, conflict markers, and likely sensitive data; then runs
  `actionlint`, C/Python host tests, and the Work Assistant Web Node.js suite. Missing npm
  dependencies are installed from the committed public-registry lockfiles. The
  workflow creates an isolated Python environment and installs
  `tools/passport_bridge/requirements.txt` before running this gate.
- **ESP-IDF 5.5.3 firmware:** runs `make build-device` for ESP32-C3 in a fresh isolated build/configuration directory, verifies the build and merged `0x0` image contents/offsets, and retains the artifact for seven days.

The workflow has only `contents: read` and uses no repository secrets, so it can validate fork pull requests. Every GitHub Action is pinned to a full commit SHA.

## Reproduce locally

```bash
make test
make build-device
```

Follow the [environment bootstrap](../engineering/environment-setup.md) if ESP-IDF 5.5.3 is
not installed. Reproduce a CI failure with the same mode locally. Do not
maintain duplicate validation commands inside the workflow.
