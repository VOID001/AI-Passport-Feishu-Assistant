<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- The macOS Work Assistant launcher now starts the Feishu calendar
  authorization flow when the local `lark-cli` user is not authenticated,
  verifies the completed login, and continues startup automatically.

- Standardized the Work Assistant BLE summary at 16 same-day calendar events,
  255-byte UTF-8 event titles, 16 tasks, 127-byte UTF-8 task titles, and a
  12 KiB payload. The local bridge, demo console, and firmware now enforce the
  same release limits.

- Removed the Work Assistant Wi-Fi provisioning, QR-code, and direct-sync path.
  Work Assistant synchronization now uses BLE only, and the unused Wi-Fi scan
  demo and direct-Wi-Fi helper implementations have been removed.

- Added a one-time upgrade migration that erases the legacy `feishu_agent` and
  `feishu_user` NVS namespaces after both cleanups succeed. Failed migrations
  retry on the next startup without erasing unrelated NVS data.

- The isolated QEMU build now seeds the real `wa_bridge.summary` NVS blob with
  a valid protocol-v4 Work Assistant summary, including its RGB565 avatar,
  five calendar events, a task, and 999 unread messages.

- All-day calendar events now display an all-day label instead of `00:00`.

- The local Work Assistant web console now includes a demo mode. It can author
  a bounded profile avatar, same-day events, tasks, and message count locally,
  then send that summary through the existing protocol-v4 BLE link without
  querying Feishu. Five consecutive clicks on the Passport mark enable it
  directly, confirm activation with a toast, and pause automatic
  synchronization.

- Selecting a task on the Passport now opens its details with explicit
  complete/cancel-completion and back actions. Completion is reversible on the
  device and remains local, so a later sync does not delete the task. The BLE
  bridge now verifies its protocol version before transfer, and both the
  device settings screen and local console show it.

- The local Work Assistant web console now identifies the macOS CoreBluetooth
  `Peer removed pairing information` failure and directs users to forget the
  Passport in macOS Bluetooth settings before pairing again.

- Added cursor navigation for the Work Assistant home screen. The selected
  calendar or task row now receives a high-contrast outline and accent; select
  the user avatar and press OK to open settings. The summary card remains the
  calendar/task switch.

- Replaced the Work Assistant's double-click-OK night-mode shortcut with a
  persistent settings screen for automatic screen-off (5 seconds, 15 seconds,
  30 seconds, 1 minute, or never) and light/dark mode. The settings screen also
  displays the device Bluetooth address as its serial number.

- Fixed BLE summary recovery so NVS initializes before the stored `wa_bridge`
  summary is read during a cold start.

- Restore the stored summary timestamp on cold start so the current or next
  event indicator is available without a new BLE transfer.

- Wait briefly for NVS summary recovery before showing the BLE setup screen,
  avoiding a loading-screen flash when prior work data is available.

- Added a macOS distribution packager for the local Work Assistant service.
  It produces a double-clickable app archive that bootstraps dependencies on
  each user's Mac without packaging user credentials, BLE pairing state, logs,
  or virtual environments.

- The Work Assistant now exposes Bluetooth pairing as its only connection
  entry. The latest valid BLE transfer is retained in NVS and opens as
  the default home view after restart or disconnection. A new host pairing
  request switches to the pairing-code view, while a Passport with no stored
  transfer enters Bluetooth initialization directly.

- The Feishu Work Assistant now synchronizes up to 16 incomplete tasks due
  today or within the previous 29 calendar days over the macOS BLE bridge. A
  single OK click toggles between today's calendar and the 30-day task list,
  while UP/DOWN scrolls the active list.

- The display backlight now turns off after 60 seconds without button input.
  Pressing any function button wakes the screen, and that wake-up press is not
  forwarded to the active page.

- Added the Feishu Work Assistant BLE bridge for macOS. It synchronizes only
  owner calendars through the local `lark-cli` user identity, renders a
  scrollable same-day calendar with Simplified Chinese titles, subdued ended
  events, blue event strips, and a current/next-event pointer, and transfers a
  bounded profile avatar plus task and Feishu Dock unread counts. Account OAuth
  and API tokens remain outside the firmware.
- Added a localhost-only Node.js control surface for manual and ten-minute
  automatic synchronization, live progress, status counts, avatar, and complete
  agenda preview. A scrollable terminal-style console now shows sanitized
  service, source-result, summary, payload/CRC, BLE discovery/connection,
  per-frame transfer, commit/acknowledgement, scheduling, success, and failure
  logs in real time. The service reuses the Python BLE bridge and does not
  expose credentials to the browser. A root-level `run_assistant_web.sh`
  launcher now checks the environment and installs missing Python and npm
  dependencies before starting the console. BLE failures now retain the
  device rejection status or underlying exception type and reason in the
  local console instead of collapsing every failure into a generic message.

- Added the supplied 80-byte CW2017 profile for the specified 520 mAh cell, including content/update-flag checks, verified writes, the required restart sequence, and bounded SOC-readiness polling.

- Reorganized the documentation by function area with a dual entry point: the root `AGENTS.md` is now a thin router (hard constraints + task routing only) and the detailed AI workflow lives in `docs/development/ai-guide.md`; `agent-guide.md` was folded in. `docs/development/` gained a second level (`engineering/`, `ci/`, `release/`), and the `plays/` application archive and `experiences/` moved into a `docs/reference/` area with a dedicated README. Removed `docs/software-design/` (empty scaffold); folded the three `assets/{fonts,images,music}/README` leaves into the `assets/` README; flattened the six `project-completion` sub-documents into a single file; and unified each directory to a single README, eliminating every `INDEX` file and a duplicated experience index. All cross-references and bibliographic links were updated; no content was dropped.

- Made mini-program BLE install compatibility a template-level invariant: fixed
  protected `cardid`/Recovery partitions, retained the five-second UP-key
  Recovery boot hook, and added CI validation for merged-image structure,
  partition MD5/ranges, the 3 MB app limit, and protected payload exclusion.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
