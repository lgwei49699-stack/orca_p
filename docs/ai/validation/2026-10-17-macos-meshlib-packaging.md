# macOS 14 / Apple Silicon MeshLib 打包

## 构建问题独立提交检查

- 本次基准：`master` / `4e50224aca881146c0bab13aed9a18495cb4f838`。仅提交 macOS 构建目标选择、SDK 运行库准备/打包、CMake 链接目录及对应测试/文档；模型阶段报告、后端业务、GUI 排布和下载工具改动保持未提交。
- 检查结果：Shell 语法检查通过；47 项 macOS 构建/运行库/zstd/平台配置测试通过（`planning/build/macos-build-commit-review-20261017-tests.log`）；全工作区与暂存快照的 35 个模块文档检查均为 0 问题，工作区及暂存差异检查通过。代码及范围复核未发现入库阻塞项；本次仅暂存 13 个构建、打包、测试及对应文档文件，其他改动保留在工作区。
- 当前发布 App 与 profile validator 各 37 个 Mach-O 切片的包内依赖审计及严格签名校验通过；MeshLib CLI 42/42、无失败或跳过，运行前后程序和夹具哈希不变。证据为 `build/diagnostics/macos-build-commit-review-rs87iV/meshlib-cli/summary.json`，实际 App SHA256 `ae8f22546e6105d3f29b48ade35865c9c40ae895f3a7a758597d3280ecc970c5`。这次复测不沿用下方历史 App 身份，也不把其他业务改动纳入本次提交。
- 构建脚本、运行库工具、CMake 和三个相关测试文件的 SHA256 与 14:48 用户构建验证记录一致；没有重复运行整套应用构建。Windows/Linux 仅配置验证，原生 GUI、macOS14 实机、商业签名/公证仍不计为通过。

## 当前执行状态

- 目标：用户通过本地开发维护台构建和生成 DMG；保留 MeshLib，支持 macOS 14.0+ / arm64。GUI、CLI 业务规则及此前 CLI 越界模型 JSON 修改不变。
- 基准：`master` / `302c2a75f575985b366ff602756f90330c38fb6b`；工作区已有模型修复、CLI 分盘、翻译和模块文档改动，保留这些改动及根目录 result.json。
- 当前步骤：用户 14:48:48 再编译产物复测完成（14:49:19 构建成功）；新 App 的全部相关自动化、CLI、签名、依赖及新 DMG 隔离 SDK 运行均通过。本轮没有重新构建应用或修改业务代码；原生 GUI / macOS14 真机仍待验。
- 构建身份：本仓库 `build/arm64`，Ninja Multi-Config / Release，MeshLib ON，现有部署目标 14.0；无应用构建进程。维护台为 `planning/cache/cache_gui.py --debug`，端口 57122。
- 自动化：23 项 macOS 原生脚本测试通过，另新增 1 项构建缓存门槛测试通过；23 项 MeshLib 平台配置测试通过；维护台 69 项测试通过。完整原生测试含 7 项新运行库测试，不重复计数。证据在 `build/diagnostics/macos-meshlib-packaging-20261017/*tests.log`。
- 下一步：可使用本次用户构建的 App / 下方最新测试 DMG；按 GUI 清单验证原生界面及 macOS14 机器。以后继续沿用维护台 arm64 / Release 构建及 DMG 按钮，不需手工补最低版本参数。未提交或推送。

## 关键决策

- 当前脚本默认 11.3 与保留的 MeshLib ON 缓存冲突；普通 OFF 构建仍保留原默认，ON 的 arm64 构建自动选择至少 14.0。页面命令同时显示目标版本。
- 当前应用仅包含 zstd，MRMesh/MRVoxels 依靠开发机 SDK RPATH。递归闭包为 35 个 dylib，含与应用同名的 zstd，使用独立 `Frameworks/MeshLib` 目录。
- 原始 SDK 不修改。先在构建目录副本中重定位和签名，再由 CMake 记录最终运行库 SHA256；保留适配层运行时身份检查。打包不能再次修改这些库，否则会破坏哈希校验。
- 本任务是本地 App / DMG 打包验证，不包含外部发布、Developer ID 公证、Intel 或旧系统验收。
- CodeGraph MCP 未暴露；存在本地 CLI / 索引，旧索引不能代表当前未提交改动。本轮按已读取模块及实际脚本定位，不依赖索引的陈旧结论。

## 验证与 GUI 交接

本机 macOS 26.3.1 (a) / arm64；当前产物的 Mach-O 与 Info.plist 最低版本均为 14.0。构建、签名、依赖和 CLI 实测不代替在 macOS14 真机的首次启动验收。

首轮小型签名测试发现 Frameworks 目录下的 JSON 被 codesign 当成未签名代码；清单已移入 Resources，库目录只保留 dylib，复测通过。已准备真实 SDK 的 35 个副本；最终脚本再次准备后逐库 SHA256 与编译时一致，原 SDK 35 个文件内容哈希仍与准备前一致。真实构建首轮暴露 PRIVATE 静态库依赖在旧 CMP0099 行为下不传播链接选项，导致测试发现阶段 dyld 找不到库；已用仅限准备版 macOS 的链接接口条目补齐父目录 RPATH，第二轮全部重新链接且未再出现缺库。维护台确认没有活动构建/GUI任务后在原 57122 端口重启，加载更新后的命令生成器。


## 实际构建与运行

- 维护台 API 的 `build_incremental` / arm64 / Release 生成 `./build_release_macos.sh -sxa arm64 -c Release -t 14.0`，最终运行退出码 0；日志 `planning/build/macos-build-arm64-20261017-143925-641057.log`。页面实读已显示该命令与成功状态。
- 主应用和 profile validator 均包含 35 个 SDK 库，加应用 zstd 共检查 37 个 Mach-O；最终 Info.plist 依据主程序写入 `LSMinimumSystemVersion=14.0`，两 App 完整签名校验通过。
- `scripts/test_cli_meshlib_reconstruction.py` 对包内程序：42 passed、0 failed、0 skipped；报告路径、实际加载 SHA256、同进程及失败边界均检查，证据 `meshlib-cli/summary.json`。
- `scripts/test_cli_oversized_arrange.py`：18 次 CLI 对照通过，保持 JSON 投影和原 3MF 行为；`[Arrange]`：39 cases / 3227 assertions 通过。构建树 `src/Release/.../OrcaSlicer --help` 成功，证明新增 RPATH 适用于开发入口。
- 实际 GUI 中选择“生成 macOS DMG”，通过命令编辑框增加独立 `--output-dir ./build/diagnostics/macos-meshlib-packaging-20261017/dmg`，保留维护台要求的日志名称，点击运行及确认。DMG 任务成功退出 0；没有覆盖用户 dist 目录旧产物。日志 `planning/build/macos-build-arm64-20261017-144203-323002.log`。


## 最终产物与隔离验证

- 本地测试镜像：`build/diagnostics/macos-meshlib-packaging-20261017/dmg/OrcaSlicer-mac-arm64-2.3.2.2-Release.dmg`。
- DMG SHA256：`3149c4a4718135e9b50c82973d905658a2eb2964d5a1496d95657ff8b96405be`。
- 镜像内可执行文件 SHA256：`aedcff4940ae96cb7ae15f0bccd20220cf87169cf1498f5d4921624b931c6bdd`；arm64，最低 macOS14.0。具体源码/维护台/二进制身份在 `final-identity.json`。
- 镜像只读挂载后再检查全部依赖与严格签名，随后用 sandbox-exec 禁止读取原 SDK、准备库目录、deps、zstd 构建目录、原打包 App 和 `/opt/homebrew`。先用 cat 确认 SDK 读取确实被拒绝，再从镜像内 App 重建 cube，退出0 / candidate_ready；报告 `runtime_identity_verified=true`，MRMesh/MRVoxels 实际路径均为镜像内 Frameworks/MeshLib。没有移动、删除或改写用户 SDK，完成后已卸载镜像。
- 运行证据：`dmg-verification.json`、`dmg-sandbox-command.json`、`dmg-sandbox.log`、`dmg-sandbox-candidate/reconstruction.json`。
- `python3 scripts/check_ai_module_docs.py --all`：35 模块、0 问题；`git diff --check` 通过。没有暂存/提交，故不把 --staged 当本次验证。

## GUI 验证交接

| 场景 | 操作与预期 | 当前结果 |
| --- | --- | --- |
| 维护台构建 | 刷新 `http://127.0.0.1:57122/`，选择“重新配置并编译应用（推荐）”、arm64 / Release；命令含 `-t 14.0`，运行完成退出0 | 维护台 API实际构建成功，浏览器实读命令和成功状态 |
| 维护台打包 | 选择“生成 macOS DMG”，运行并确认；应检查包内库后成功生成镜像，页面显示成功 | 已用页面按钮实跑，独立验证目录中的镜像通过挂载测试 |
| 本机原生 GUI | 保存当前旧应用中未保存的项目并退出；从上述新 DMG 复制 App 到独立测试位置后打开，导入 `build/diagnostics/macos-meshlib-packaging-20261017/meshlib-cli/fixtures/cube.stl`，确认主窗口和模型显示，无缺失 dylib/SDK身份错误 | 待用户操作；未关闭用户已打开的旧应用，未安装覆盖 `/Applications` |
| macOS14 Apple Silicon | 将同一 DMG 放到 macOS14 的 M系列 Mac，确认可打开主窗口及导入 cube；回传启动报错/截图和实际系统版本 | 待真机；本机26.3.1不能替代此验收 |

包采用本地 ad-hoc 签名，未做 Developer ID / notarization；真实打印、Intel、Windows/Linux 和旧系统均非本次打包验收范围。MeshLib 在原生 GUI 中仍无新入口，GUI/CLI 核心业务规则没有因打包而改变。

维护台截图证据：`build/diagnostics/macos-meshlib-packaging-20261017/maintenance-success.png`，同一画面可见 arm64 / Release、自动 `-t 14.0` 和 DMG 成功/退出0。服务已在原端口加载最终代码；原浏览器页面刷新即可连接新会话，临时验证页不保留。


## 用户重编译后复测（14:48:48 构建）

基准：同仓库 / master / `302c2a75f575985b366ff602756f90330c38fb6b`；用户任务日志 `planning/cache/task-logs/20261017-144848-8a-ELVqufQTZwgwuboeEDQ.log` 最终 succeeded / 0。证据目录 `build/diagnostics/macos-meshlib-packaging-20261017/user-rebuild-144848`；`identity-before.json` 记录当前源码、配置、程序、运行库及根 result.json 身份。计划复用新构建，检查 App 依赖/严格签名/开发入口启动，执行 MeshLib 42项、分盘18次CLI及39项核心用例，随后检查隔离SDK运行及新产物DMG。以上状态目前为运行前记录，完成后回填。

复测第一批完成：新 App SHA256 `f5ce7da238f3b7e51f0abc2610442497cef191099290f6e7bccdff3ccf5ed137`；MeshLib CLI 42/42、分盘 CLI 18次、Arrange 39项/3227断言全部通过；主 App 与 validator 均通过37个Mach-O包内依赖检查及严格签名验证，开发入口 `--help` 退出0。新 App 重新生成验证 DMG，记录 `user-rebuild-144848/dmg-build.log`；下一步挂载、隔离 SDK 运行并核对测试前后身份。


复测最终结果：

- 新验证镜像 `build/diagnostics/macos-meshlib-packaging-20261017/user-rebuild-144848/dmg/OrcaSlicer-mac-arm64-2.3.2.2-Release.dmg`；SHA256 `439449f23653c1ff20e708d1a11aaafd73895d61f655f5ef219bc7959ffb11a4`。生成镜像退出0；原 dist 和上一轮镜像均未覆盖。
- 镜像内主程序与用户编译 App 的 SHA256 均为 `f5ce7da238f3b7e51f0abc2610442497cef191099290f6e7bccdff3ccf5ed137`，arm64 / macOS14.0；依赖检查及严格签名通过。
- 只读挂载镜像，sandbox-exec 禁止读取原 SDK、deps、准备库、zstd 构建目录、源 App 和 Homebrew，先验证 cat 读取确实被拒绝，再执行 cube MeshLib 重建：退出0、candidate_ready、runtime_identity_verified=true，两库实际从镜像内部加载。镜像已卸载。
- 验证前后全部记录的源码、脚本、应用/测试程序和根 result.json 哈希未变；当前测试及报告对应本次新产物，不沿用上一轮结论。详细结果为 `user-rebuild-144848/verification.json`，命令和报告在同目录。
- GUI 检查使用上面的最新 DMG，步骤沿用本记录 GUI 清单；本轮自动化没有代替原生 GUI 和 macOS14 实机检查，没有关闭用户已有 GUI 或修改用户配置。
