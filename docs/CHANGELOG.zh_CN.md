<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

## Unreleased

- macOS 工作助手启动器检测到本机 `lark-cli` 用户未认证时，现在会自动启动飞书
  日历授权流程，验证授权结果，并在成功后继续启动。

- 工作助手 BLE 摘要发布限制现统一为最多 16 条当日日程、日程标题 255 个 UTF-8
  字节、16 条待办、待办标题 127 个 UTF-8 字节，以及 12 KiB payload。本地
  bridge、演示控制台与固件现在执行相同限制。

- 移除 Work Assistant 的 Wi-Fi 配网、二维码和直连同步路径。Work Assistant
  现在仅通过 BLE 同步；未使用的 Wi-Fi 扫描 demo 和直连辅助实现一并删除。

- 新增一次性升级迁移：仅在 `feishu_agent` 和 `feishu_user` 两个旧 NVS
  namespace 均清理成功后记录完成状态；失败时下次启动重试，且不擦除其他 NVS
  数据。

- 隔离的 QEMU 构建现在会向真实的 `wa_bridge.summary` NVS blob 写入合法的
  protocol-v4 工作助手摘要，包含 RGB565 头像、5 条日程、1 条待办和 999 条
  未读消息。

- 全天日程现在显示“全天”，不再显示 `00:00`。

- 本地工作助手 Web 控制台新增演示模式：可本地编辑受限长度的头像、当日日程、
  待办和未读消息数，并通过既有 protocol-v4 BLE 链路同步到设备，无需查询飞书。
  连续点击 Passport 图标五次可直接开启，成功后会显示 Toast 提示，且会暂停自动
  同步。

- Passport 现在可展开待办详情，并提供明确的“完成／取消完成”和“返回”操作。
  完成状态可在设备上撤销且仅保留在本地，因此后续同步不会删除该待办。BLE bridge
  会在传输前校验协议版本，设备设置页与本地控制台均会显示版本号。

- 本地工作助手 Web 控制台现在可识别 macOS CoreBluetooth 的
  `Peer removed pairing information` 失败，并提示用户在 macOS 蓝牙设置中忘记
  Passport 后重新配对。

- 工作助手首页新增光标导航。选中的日程或待办行会以高对比描边和加宽色条强调；
  选中用户头像后按 OK 可进入设置，汇总卡片仍用于切换日程和待办。

- 工作助手取消双击 OK 切换夜间模式。设置页会持久化自动熄屏选项（5 秒、15 秒、
  30 秒、1 分钟或永不）与浅色／深色模式，并显示设备蓝牙地址作为序列号。

- 修复 BLE 摘要恢复顺序：冷启动时先初始化 NVS，再读取已保存的 `wa_bridge` 摘要。

- 冷启动时恢复已保存摘要的时间戳，无需新的 BLE 传输即可显示当前或下一个会议指示三角。

- 展示 BLE 配置页前短暂等待 NVS 摘要恢复，已有工作数据时不再闪现加载页。

- 新增本地工作助手服务的 macOS 分发打包脚本，生成可双击启动的应用压缩包。每位用户
  的 Mac 会自行安装依赖；压缩包不包含用户凭证、BLE 配对状态、日志或虚拟环境。

- 工作助手现在只保留蓝牙配对入口。最近一次有效 BLE 传输会保存在 NVS 中，重启或
  断联后默认展示该数据。主机发起新的
  配对请求时，界面会切换到配对码页；没有已存传输数据的 Passport 则直接进入蓝牙
  初始化模式。

- 飞书工作助手现在可通过 macOS BLE bridge 同步最多 16 条截止于今天或此前
  29 个自然日内的未完成任务。单击 OK 可在“今日日程”和“近 30 天待办”之间
  切换，UP/DOWN 用于滚动当前列表。

- 屏幕背光会在 60 秒无按键操作后自动关闭；按任意功能键可重新亮屏，且用于唤醒的
  这次按键不会继续触发当前页面操作。

- 新增 macOS 飞书工作助手 BLE 桥接：通过本机 `lark-cli` 用户身份仅同步
  owner 日历，以简体中文显示可滚动的当日日程、淡化已结束日程、蓝色日程条带和
  当前/下一日程指针，并传输有界头像、任务数与飞书 Dock 未读数。账号 OAuth
  与 API token 始终保留在固件外部。
- 新增仅监听 localhost 的 Node.js 控制台，支持手动同步、每 10 分钟自动同步、
  实时进度、状态数量、头像和完整日程预览。页面底部新增可滚动的 Terminal 风格
  控制台，实时显示经过脱敏的服务、数据源结果、摘要统计、payload/CRC、BLE
  发现与连接、逐帧传输、commit/ack、调度、成功与失败日志。服务复用 Python
  BLE bridge，不向浏览器暴露凭证。仓库根目录新增 `run_assistant_web.sh`
  一键启动脚本，自动检查环境并按需安装 Python 与 npm 依赖。BLE 失败不再
  被统一折叠为通用错误，控制台会显示设备拒绝状态或底层异常类型与原因。

- 加入厂家为优特利 520mAh 电芯生成的 80 字节 CW2017 profile，并实现内容与更新标志检查、写入后校验、规定的重启时序以及有上限的 SOC 就绪等待。

- 按功能域整理文档并采用双入口：根目录 `AGENTS.md` 变为薄路由（只保留硬约束与任务路由），详细的 AI 开发工作流下沉到 `docs/development/ai-guide.md`，`agent-guide.md` 并入其中。为 `docs/development/` 增加二级分区（`engineering/`、`ci/`、`release/`），把 `plays/` 应用档案与 `experiences/` 移入带专属 README 的 `docs/reference/` 参考区；删除 `docs/software-design/`（空脚手架）；把 `assets/{fonts,images,music}/README` 三个叶子 README 并入 `assets/` README；把 `project-completion` 的六个子文档压平为单文件；并把每个目录统一为单一 README，消除所有 `INDEX` 文件与一处重复经验索引。所有交叉引用与文献链接已更新；未丢弃任何内容。

- 将小程序 BLE 安装兼容提升为二创模板强制契约：固定保护 `cardid`/Recovery 分区，
  保留上键持续 5 秒进入 Recovery 的 bootloader hook，并在 CI 强制校验合并镜像结构、
  分区表 MD5/范围、3 MB 应用上限和保护分区数据不入包。
- 规定多应用发布的 Release 标题约定：tag 按 `v<版本>-<应用名>`（如 `v0.1.0-voice-keychain`）命名，让 Release 标题同时带版本与应用名；发布成功后核对标题，保证一眼扫 Release 列表就能区分是哪个应用。
- 新增发布后收尾流程：`issue-suggestions` skill 用于把用户反馈作为 issue 提交到上游项目；`experience-pr` skill 用于把可复用的开发经验作为文档 PR 提交；新增 `docs/experiences/` 目录保存单条经验文件；并配套 `project-completion`、`file-issues` 与经验索引文档。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。
