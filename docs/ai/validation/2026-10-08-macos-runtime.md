# macOS 构建依赖兼容性验证

## 当前执行状态

实施基准 master / 2bcc7f9e61。上一批构建优化已提交；本轮仅按用户要求补充 DMG 首次启动说明中的
“未知来源/任何来源”手动开启及系统设置确认步骤。不自动修改系统安全设置，
不重新编译应用；用户已授权本批本地提交，不推送，不动用户 result.json 与学习文档。
用户最新要求：任何来源放第一步、删去英文对照，并删除指定的版本括注和恢复保护段落；已调整模板和对应断言。
本轮只验证两种应用名的小型打包用例，不重新生成完整应用 DMG；现有 guide-simple 包仍是上轮说明，下次打包才带新说明。
上一份 guide-update 包因我显式传入
`--include-validator` 带入开发校验工具，不是默认打包行为；改用普通打包选项生成不含该工具的新包。
已补默认包只有主应用、Applications 链接和说明的断言（即使旁边存在校验工具也不应打入）。
此前纯中文说明两项测试通过（18.429 秒，`guide-chinese-tests.log`）；本轮删除指定文字后两项复测通过（19.022 秒），
日志 `planning/build/macos-runtime-20261008/guide-chinese-trim-tests.log`。
入库复核另运行完整打包回归 11 项通过（20.415 秒），日志为同目录 `guide-precommit-review-tests.log`；
全量文档及差异检查通过。提交范围仅说明模板、测试及三份配套文档，目标机器上的系统设置操作仍待用户手测。

## 简版用户包（2026-10-08，上轮产物，尚未含最新纯中文说明）

- 说明由 77 行压缩至 23 行，保留原两步操作、可选任何来源开启/确认/恢复保护及简短英文对照。
- 使用普通命令 `bash scripts/package_macos_dmg.sh -a arm64 --output-dir planning/build/macos-runtime-20261008/guide-simple`，
  不传 `--include-validator`；不删除源码或开发构建目录中的预设校验工具。
- 最终回归 11 项通过（19.887 秒），日志 `planning/build/macos-runtime-20261008/guide-simple-tests-final.log`；
  打包退出 0，日志为同目录 `guide-simple-dmg-build.log`。
- 新包 `planning/build/macos-runtime-20261008/guide-simple/OrcaSlicer-mac-arm64-2.3.2.1-Release.dmg`，
  SHA256 `97e66173f7fe8bb8a7c5ddb4e19d0845a24ee51c8dc1c9c0cf6d021e97c52ffb`。
  校验值通过；只读挂载后顶层可见内容仅 Applications、OrcaSlicer.app、安装与首次启动说明.txt，
  模板替换逐字一致、主应用严格签名完整性通过，正常卸载。主程序 hash 仍为下述既有 6decd3aafa…，未重编译。
- 原 guide-update 及 dist 包未覆盖，后续人工验证使用上述 guide-simple 包；不改变此前异机/公证未验收的结论。

## 首次启动说明补充（2026-10-08 首轮，产物已由简版替代）

用户确认之前目标电脑未允许未知来源，要求打包附带开启方法。修改说明模板及其测试，
没有修改打包脚本或应用源码，不创建自动提权/关闭门禁的脚本，不改动现有 dist 安装包。
GUI/CLI 业务均未变化；这里的“系统设置”属于用户电脑的手动安全设置，不是切片器的新功能。

依据：Apple [安全打开 App 说明](https://support.apple.com/zh-cn/102445)，以及本机 macOS
26.3.1 的 `/usr/share/man/man8/spctl.8`。前者说明单应用例外及受管理设备限制，后者明确
`--global-disable` 用于显示“任何来源”选项；不能写成一条命令必然已经关闭保护。
说明限定新版受支持系统（以 26 为例），命令不可用时用单应用方式或联系管理员，不宣称覆盖所有旧版本。
补充全局影响、设置确认、恢复原保护选项及中英文；保留有条件的单应用隔离标记处理，不把损坏/恶意软件提示归为来源限制。

自动化与产物：

- 先运行 Orca DMG 说明用例，旧说明因缺少新命令而失败，`guide-red.log`，1 项失败符合预期。
- 修改后 `python3 scripts/tests/test_bundle_macos_zstd.py -v`：11 项通过，19.879 秒，
  `guide-tests-final.log`。两种应用名的真实小型 DMG 均挂载检查；安全命令替身未收到调用。
- 使用既有 arm64 Release 应用执行 `bash scripts/package_macos_dmg.sh -a arm64 --include-validator
  --output-dir planning/build/macos-runtime-20261008/guide-update`，退出 0，日志 `guide-dmg-build.log`。
  上述日志均位于 `planning/build/macos-runtime-20261008/`。
- 新包：`planning/build/macos-runtime-20261008/guide-update/OrcaSlicer-mac-arm64-2.3.2.1-Release.dmg`，
  SHA256 `e23f2d2986a5af085fd6a6097b6b6ec6ffd192458c4941021547d6e4f42e6c63`。
  `.sha256` 校验通过；只读挂载后的说明与替换模板逐字一致，两份 app 严格签名完整性检查通过，正常卸载。
- 镜像内主程序 SHA256 仍为 `6decd3aafa65d3583c8801605a73f742099118b717b7721f8ceee16a00a6728f`，
  没有重编译或运行切片。本次不执行任何更改 Gatekeeper/隔离标记的命令。

目标电脑手动验证（待执行，非本地打包测试结论）：

1. 下一次重新打包后打开“安装与首次启动说明.txt”，确认仅中文三步，第一步为任何来源；现有 guide-simple 仍为上一轮说明。
2. 仅在信任包来源时拖入“应用程序”，从 `/Applications/OrcaSlicer.app` 尝试启动；若有旧应用，
   先保留旧版本，避免无意覆盖。优先按第 2 步对本应用“仍要打开”，成功则无需任何来源。
3. 确实需要全局设置时，记录原选项，再按第 1 步执行并到系统设置选择“任何来源”确认；
   预期是用户手动确认后才能改变选项，不应由安装包自动修改。命令或选项受限时停止并联系管理员。
4. 首次打开后恢复原来的受保护选项；回传 macOS 版本、具体阻止提示、终端输出及操作前后设置截图。
   不回传密码。损坏或恶意软件提示不继续绕过；应用仍打不开时单独排查，而非不断降低保护。

当前仅说明/打包内容与完整性验证通过；异机首次启动、旧系统、Intel 真机和签名公证仍未验收。

## 已完成的构建优化（实施与提交记录）

实施基准 master / 8320d77950。用户授权优化 Mac 依赖打包及构建警告；筏层 GUI 和实机验证交模型师，
不再改切片业务。原有 result.json 与学习文档保持不动。2026-10-08 用户另行授权本批本地提交，不推送。

当前定位：Boost iostreams 的传递依赖加载 Homebrew zstd；应用目标为 macOS 11.3，库要求 26.0。
DMG 入口已有固定版源码构建与包内重定位，但 build_release_macos.sh 只复制应用，缺少这一步。
现有未知枚举告警选项按编译器版本猜测；重复库来自传递静态链接，不以改动链接顺序消警。

实现：复用固定 zstd 1.5.7 源码和校验值，在构建入口按架构/部署版本准备并显式选择兼容库，
整理 .app 时携带运行库/许可并校验签名；能力探测后再添加告警选项。
复用现有原生脚本测试，补构建入口、缓存及工具链选项回归；核对无并行构建后完成必要应用构建。
知识图谱较旧且缺新脚本，按当前源码核查。当前步骤：真实 arm64 构建、包内运行库、CLI
切片和实际 DMG 验证均已完成，已提交为 2bcc7f9e61。GUI/实机打印继续交模型师；旧系统/Intel 真机及
公开发行签名公证尚未验收。

入库复核发现告警测试只模拟“能力支持”，却仍依赖本机编译器版本，可能在旧工具链上
误报失败。已补正测试：实际工具链配置/编译与模拟版本门槛分开验证；生产构建策略不变。
脚本回归 5+11 项通过，版本/能力矩阵包含 8 个子场景；无需重建应用。
本次提交仅包含 4 份生产构建文件、1 份测试和 4 份配套文档；提交前执行全量与暂存文档检查，
核对差异及无关文件未入暂存区。下一步为旧系统/Intel 真机与公开发行验收，不能将本次入库视为其通过。

## 实现及验证进度

构建入口在配置前准备 zstd 1.5.7，SHA256 沿用既有固定值；显式选择其 CMake 配置，避免
Boost 再发现 Homebrew 库。普通/双架构共用同一前缀，双架构运行库在合并前即包含两种架构。
整理应用和预设校验工具时复制运行库、许可、实体资源目录，重定位并校验临时签名。
`-b` 检查旧缓存是否仍指向外部库，不匹配即拒绝并要求去掉 `-b` 重新配置。

枚举告警选项改为编译器能力检测，只施加于 C++；Apple 链接器通过选项探测后仅关闭重复库
提示，保留传递链接顺序，其他链接告警仍启用。CMake 3.18 以下跳过链接选项探测。

- 原生打包回归 11 项通过，日志 `planning/build/macos-runtime-20261008/bundle-tests.log`。
- 构建入口/工具链专项 4 项通过，日志 `entry-tests-r2.log`。使用小型真实 Mach-O、实际
  资源拷贝、lipo 和签名；仅替换耗时的应用编译与上游源码构建。包含双架构、旧缓存拒绝、
  原依赖目录移走后仍运行、资源不回写仓库；不等于完整双架构应用已验收。
- 首轮双架构测试失败是夹具把源码从 stdin 传给多架构 clang，第二架构读到空输入；改为
  临时 C 文件后通过。未放宽业务断言，未将首轮失败计为成功。
- 真实 zstd arm64 已构建，`otool` 的 minos 为 11.3，安装标识是本地固定前缀绝对路径，
  由整理阶段重定位到包内。日志 `zstd-build.log`。
- 真实库 1,114,112 字节压缩/解压往返一致；版本号 10507。初次增量检查发现 CMake 安装时
  即使内容未变仍改写 Mach-O 标识、刷新库时间戳；增加 BUILD_WITH_INSTALL_NAME_DIR 后再次
  构建/安装不再改写库，连续两次 mtime 相同，也没有源文件重编译（`zstd-incremental-final.log`）。
- 最终脚本回归复跑 4+11 项通过，见 `entry-tests-final.log`、`bundle-tests-final.log`；
  包含支持/不支持告警选项的配置分支，模拟支持分支不冒充当前编译器实际支持。
- 已确认无并行用户构建，在现有 arm64 / Ninja Multi-Config / Release 目录重新配置，
  指定新 zstd_DIR；构建 `OrcaSlicer OrcaSlicer_profile_validator --parallel 3`。
  日志 `configure.log`、`application-build.log`，均退出 0，应用构建完成 569 步。
- 使用 `build_release_macos.sh -sxa arm64 -c Release -b` 整理实际主应用和校验工具成功；
  该入口的 `-x` 仍选择现有 `all` 目标，首次整理补建了测试/工具，未改其目标选择行为。
  最终增量整理日志 `stage-application-final.log`，没有重新编译业务源码。
- 真实构建/整理日志不再出现未知 enum-constexpr-conversion 选项、重复链接库、zstd 最低
  系统高于应用三类告警；其余既有编译告警和 CMake dev 警告不在本次消除范围。
- 主应用与校验工具均通过 `codesign --verify --deep --strict`，包内 zstd 为 arm64 / minos
  11.3。依赖引用为 `@executable_path/../Frameworks/libzstd.1.dylib`；主应用 `--help` 退出 0，
  `DYLD_PRINT_LIBRARIES` 确认实际加载包内副本，见 `help-runtime.log`。两者均含 zstd 许可。
- 目录权限检查发现新增资源暂存目录继承 mktemp 的 0700；先补断言复现失败
  （`resource-mode-red.log`），再将暂存目录设为 0755 后重跑 4 项通过并重新整理真实应用。
  仅调整生成副本的目录权限，不修改仓库资源；主应用资源目录已为 0755。
- 最终主程序 SHA256：`6decd3aafa65d3583c8801605a73f742099118b717b7721f8ceee16a00a6728f`。
  新包使用同一 Benchy 有效配置（平滑 0）切片退出 0，约 2.88 秒；与此前同参基线的
  107,122 条非注释机器指令完全一致。证据在
  `planning/raft/ep3plus-path-review-20261008/macos-portable-0/`，对照 `effective-final-0/`；
  指令 SHA256：`5818d7c71221678b2c8269827aa4887751afa7e1324e9b53dd7329ca7dd7da55`。
  这是构建/依赖更换后的切片回归，不代替模型师的 GUI 和实机验证。
- 实际 DMG 生成成功（含主应用/校验工具），只读挂载后再次验证两包签名、内部库引用、
  资源权限/许可；从镜像启动主程序 `--help` 退出 0，加载镜像内 zstd，随后正常卸载。
  日志 `dmg-build.log`、`dmg-verify-final.log`；产物
  `planning/build/macos-runtime-20261008/dmg/OrcaSlicer-mac-arm64-2.3.2.1-Release.dmg`，
  仅本地测试包，未发布。首轮将 CLI 工作目录也设在只读镜像内，旧 CLI 写 result.json
  失败退出 230（`dmg-verify.log`）；改用独立可写临时工作目录后通过，未改 CLI 行为。
- Shell 语法检查、`git diff --check`、文档全量检查通过；模块 35、问题 0。用户原有
  result.json 和学习文档内容 hash 与任务开始一致，未暂存或提交。

## 验证边界

### 入库复核：告警测试的版本门槛

旧测试强制能力缓存为 TRUE 后无条件要求 C++ 带枚举选项，但生产条件同时要求 Clang /
AppleClang 版本大于 15，旧工具链会被测试误报。补正仅修改测试及相关文档，不改生产条件。
本机真实配置/编译继续通过；另用独立临时构建目录模拟 AppleClang 14.0.3、15.0.0、15.0.1、
16.0.0，Clang 16 及 GNU，组合能力支持/不支持共 8 种情况，只配置、不编译模拟选项。
逐组核实 C/C++ 命令均存在，C 不接收专属选项，C++ 是否启用符合版本及能力条件。

- 构建入口专项 5 项通过（9.517 秒），日志 `entry-tests-version-gate.log`。
- 原生打包回归 11 项通过（22.270 秒），日志 `bundle-tests-version-gate.log`。
- 两份日志位于 `planning/build/macos-runtime-20261008/`。旧版本分支模拟不等于旧编译器实机验证。
- 此轮前后四份生产构建文件 SHA256 一致，用户 result.json 和学习文档亦未变化；
  GUI/CLI 业务及应用二进制不变，因此未重建应用、不要求新增 GUI 操作。

### 尚未覆盖的验收

自动化需分别记录脚本测试、构建、包内动态库路径/架构/最低系统、签名及本机 CLI。
旧 macOS 真机、Intel 真机、Developer ID 签名与公证未执行，不能用本机启动替代。
业务 GUI 沿用筏层交接，由模型师执行；此次仅构建和打包，不新增 GUI 或 CLI 业务参数。
