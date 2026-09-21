# 仓库协作指南

## 项目结构与模块组织
OrcaSlicer 的 C++17 源码位于 `src/`，按功能模块和平台适配层拆分。用户资源、图标、打印机预设位于 `resources/`；翻译文件位于 `localization/`。测试位于 `tests/`，按领域分组，例如 `libslic3r/`、`sla_print/` 等，测试夹具放在 `tests/data/`。CMake 辅助文件位于 `cmake/`，较长的参考文档位于 `doc/` 和 `SoftFever_doc/`。自动化脚本应放在 `scripts/` 和 `tools/`。`deps/` 与 `deps_src/` 中的内容视为第三方快照，除非同步对应上游 tag，否则不要直接修改。

## 构建、测试与开发命令

### AI 编译与验证约束

- 默认不主动执行编译、构建或打包，包括全量编译、局部编译、测试目标编译，以及会触发编译的脚本。用户自行编译并提供日志，AI 可按下述授权运行测试。
- 只有用户明确要求编译、构建或打包时，才执行对应命令；修改代码、添加日志或分析问题不视为编译授权。
- 2026-09-19 用户长期授权：用户完成编译后，AI 可以直接运行与当前改动相关的已有测试程序、CTest 或不触发编译的 CLI 验证脚本，无需再次询问测试授权。此授权适用于后续修正后的复测，但不包含 AI 自行编译、构建或打包。
- 运行测试前核对构建日志、配置及测试程序，确认包含待验证的源码和测试改动。编译失败、测试程序缺失或仍是旧版本时，不运行旧程序证明新改动通过；说明需要用户重新编译的目标。源码再次修改后，待用户完成相应编译即可直接复测，记录命令、日志、统计及退出码。
- 尚无包含当前改动的构建时，通过源码审阅、`git diff --check` 等静态检查验证，并明确说明尚未完成运行验证。
- 本约束优先于本文其他构建验证建议；下述编译命令仅供参考，不构成编译授权，测试执行遵循上述长期授权。

使用 out-of-source 构建：

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`：配置依赖并生成构建文件。
- `cmake --build build --target OrcaSlicer --config Release`：编译应用；可加 `--parallel` 加速。
- `cmake --build build --target tests` 后执行 `ctest --test-dir build --output-on-failure`：运行自动化测试。

平台辅助脚本如 `build_linux.sh`、`build_release_macos.sh`、`build_release_vs2022.bat` 会封装同一套流程并附带工具链参数。复现 macOS 构建问题时优先使用 `build_release_macos.sh -sx`，需要可复现的容器构建时使用 `scripts/DockerBuild.sh`。

## 界面语言与翻译范围

- 产品主要面向简体中文和英文用户。新增或修改界面文案时，默认完整覆盖英文原文与简体中文翻译。
- 沿用项目现有国际化机制：代码中使用英文原文及翻译标记，并同步维护 `localization/i18n/zh_CN/OrcaSlicer_zh_CN.po`；不要直接硬编码中文界面文案。
- 其他语言缺少对应翻译时，允许回退显示英文，不作为默认入库阻塞项。除非用户明确要求多语言适配，否则不主动批量补译其他语言的 `.po` 文件。
- 保留现有其他语言翻译，不删除或覆盖无关条目；中英文验证应关注菜单、提示、错误信息及占位符的一致性。

## 代码风格与命名约定
`.clang-format` 约定 4 空格缩进、140 列限制、对齐初始化器，并对类和函数使用花括号换行。提交前对改动文件执行 `clang-format -i <file>`；当 LLVM 工具位于 PATH 中时，也可以使用 CMake 的 `clang-format` target。类名优先使用 `CamelCase`，函数和局部变量使用 `snake_case`，常量使用 `SCREAMING_CASE`，并与 `src/` 中既有风格保持一致。头文件应自包含，include 顺序需与 IWYU pragma 保持一致。

## 测试指南
单元测试基于 Catch2（`tests/catch2/`）。测试文件按被测组件命名，例如 `tests/libslic3r/TestPlanarHole.cpp`；长耗时用例应添加标签，保证 `ctest -L fast` 仍然可用。新算法应配套确定性的 fixture 或样例 G-code，放在 `tests/data/`。当自动化覆盖不足时，在 PR 中说明手动打印机验证或切片回归检查过程。

### 筏层专项验证

- 2026-09-21 16:39 人工复核收尾：用户针对上一条单对象筏层参数保存重开后的回显确认“我这边验证是正常的”，按用户人工验收关闭该待验证项，不改写此前自动化操作不确定的原始记录。新日志 `planning/debug/orca-user-gui-run-20260921-163947-176753.log` 空选择错误为 0，14 条 error 仍为既有预设、驱动、旧临时文件及禁打区预览类别，没有新的筏层异常证据；该日志无切片过程，不算新一轮切片验收。源码／应用／测试哈希仍匹配 13:52 完整 Raft 70 用例／5,501 断言退出 0 的版本，不重复测试。结合既有静态审查和 GUI 验证，当前试用范围未发现入库阻塞项；物理质量、完整 CTest 和跨平台范围不扩大。记录 `planning/raft/user-gui-confirmation-20260921-1639/validation.json`；本轮仅更新文档，未修改生产／测试源码、未编译或提交。

- 2026-09-21 16:07 新进程复核：16:06 两次用户构建仅执行增量检查和打包，相关源码、应用及 `fff_print_tests` 哈希与 13:52 通过记录完全一致，未重复运行同版测试；Geometry 23／1,326、完整 Raft 70／5,501 的退出 0 结果仍有效。独占新 GUI 进程 70107，验证全局支撑／筏层页切换、分块参数提交、一个五部件对象拆成五对象、全局参数保存重开；本次日志空选择错误为 0。单对象平滑 3 mm 已写入测试 3MF（全局 2 mm，其他对象无覆盖），但重开后的自动化对象选择出现回显 2 mm、盘名编辑和可打印状态意外改变，无法可靠完成该项 GUI 验收，不能将其宣称通过或直接归因于筏层补丁。已恢复空工程并丢弃临时预设更改，用户原模型未改；证据 `planning/raft/gui-validation-20260921-1607/validation.json`。本轮未改生产／测试源码、未编译／提交、未 GUI 切片或打印；历史预设／驱动／禁打区预览日志仍存在。剩余为单对象参数重开回显的定向人工复核，不需要再次编译同版源码。

- 2026-09-21 13:49 新构建复测：分支 `codex/raft-geometry-review`，基线 `41f2845d5299ed11056789ff43e55e1c45c38cd0` 加当前补丁，macOS arm64 / Release。用户构建 `planning/build/macos-build-arm64-20260921-134906-468093.log` 已重编 `Tab.cpp` 和 `test_support_material.cpp` 并链接应用及 `fff_print_tests`；相关源码与 13:04 入库审查哈希一致。AI 运行新测试程序：Geometry **23 用例／1,326 断言**、完整 Raft **70 用例／5,501 断言**全部通过，退出码均为 0，前者包含于后者。覆盖下条失败测试助手修正后的重放／恢复断言，不能改写原失败日志。证据 `planning/raft/merge-validation-20260921-1352/validation.json`。GUI 新日志 `135049-106570` 的启动快照暂无空选择错误或切片过程；旧进程 38574 与新进程 54355 同路径并存，界面工具定位旧窗口，故新 GUI 修正仍待独占新实例操作验证。差异／中文翻译格式检查通过；AI 未编译、未完整 CTest／其他平台／实机验证、未提交。

- 2026-09-21 10:32 新构建及实际下发复核：`planning/build/macos-build-arm64-20260921-103041-490838.log` 编译 RaftGeometry／RaftMerge／test_support_material 并链接应用及 `fff_print_tests`；随后两次 10:31 日志仅为无源码编译的增量打包。源码与孤立基底快照匹配。Geometry 退出 8；完整 Raft **70 用例：69 通过、1 失败；5,385 断言：5,377 通过、8 失败**，均为新增多对象用例误调用要求每个对象有挤出的 `require_raft_inside_bed()`。已只将该调用改为已有 `require_shared_rafts_are_disjoint()`，保留单次输出、非空、边界和冲突断言；生产源码未变，测试修正须用户重建 `fff_print_tests` 后复测，不能宣称全部通过。GUI `103207-090673` 在平滑 0 下记录清理 1 个基底组件，切片／导出成功。10:43／10:48 两次向同设备下发同哈希 G-code，上传 HTTP 200、业务 code=0/data=true；实际下发红框区域已无挤出，基底 12→11 个名义料线组件，无新增小于 1 mm² 基底小岛。下发平台为 101×101，料线最大 Y=100.998，不是严格 100 mm 实机范围验收。后续平滑 5 重切成功，截至 10:57 日志未见新下发。证据 `planning/raft/orphan-base-validation-20260921-1032/`；AI 未编译、未操作打印机、未验证实际剥离或机端执行完成。

- 2026-09-21 孤立基底清理（源码完成，待用户构建）：不照搬 Xmaker 参数／外观。新增 `find_unneeded_raft_base_regions()`，只针对开启分块的兼容 CuraV1 组；联合所有基底实体／料线投影，保守保留与上方筏层原始／规划轮廓、实际料线或模型首层有关的组件。只有确认从较大原轮廓分离、无承托且原轮廓仍有有用分块的区域才移除；原小岛、长窄承托、相接及跨基底连接保留。受影响基底层重生成墙／填充并同步输出岛，平台／混摆门禁、流量、平滑、墙数和摆盘不变。新增 3 个 `[SupportMaterial][Raft][Geometry][Bearing]` 定义（Geometry 共 23 个），包括同源龙上层轮廓、多基底层、零中间层、多对象单次输出、模型保护及重切恢复。独立 Shapely 核算仅验证历史几何能识别一条无用途长带，不是新 C++ 运行；证据 `planning/raft/orphan-base-fix-20260921/`。本轮未编译、未运行新／旧 C++、未 GUI／实机；用户重建应用及 `fff_print_tests` 后用原 Geometry／完整 Raft 入口复测，不能复用下面 20／67 用例通过记录。

- 2026-09-21 红框孤立条新问题（待修正）：用户后续截图中的长带在 09:41 实际 G-code 基底 Z=0.30 mm，范围 X22.420…49.159/Y50.902…52.687，约 26.739×1.785 mm；名义料线覆盖约 46.014 mm²，与同层其他块隔 0.804 mm、四个上方筏层不接触、前三个模型层不重叠。它来自原基底外扩区被切缝隔离，08:18 旧产物已有；小面积／短路径筛选和无挤出元数据清理不能处理。下一条 67 用例通过不等于该承托关系缺陷已解决。Xmaker 参考样本五个基底块均与中间层接触，但参数不同，不能推断其内部有特定删除算法。方案及证据 `doc/print_settings/support/raft_isolated_strip_review_20260921.md`、`planning/raft/strip-review-20260921/`。此轮仅分析，未改源码／参数、未编译／跑新 C++、未提交；后续若修正仍须新构建和新增回归。
- 2026-09-21 09:41 碎片优化新构建验收：用户 `planning/build/macos-build-arm64-20260921-093948-978982.log` 含 RaftGeometry、RaftMerge、SupportCommon 及测试编译／链接，源码与 `fragment-fix-review-20260921` 快照一致，打包／链接应用哈希相同。macOS arm64 / Release，master `41f2845d5299ed11056789ff43e55e1c45c38cd0` 加当前补丁。Geometry **20 用例／443 断言**、完整 Raft **67／4,618** 均退出 0，前者包含于后者。用户 `094059-896214` GUI 日志显示五层分块采用、切片／导出成功、预览床内；实际 G-code 基底 <1 mm 路径 179→2、<0.5 mm 路径 176→0、名义包络 <1 mm² 独立小岛 4→0，序列化配置相同。当前配置为 101×101 mm，基底 Y 包络到 100.998 mm，不能作为严格 100 mm 实机范围验收；上层稀疏线分离不全算碎片。证据 `planning/raft/fragment-validation-20260921-0941/`。此条覆盖下面待构建条目的对应范围；AI 未编译／改源码，未另做 Computer Use、完整 CTest、CLI 端到端、旧 macOS／跨平台或实机打印，未提交。后续源码变化仍须新构建复测。
- 2026-09-21 碎片优化复审（待用户构建）：发现面积单条件会误删长窄区域，且无删除的分块不应再次内缩／扩回填充。现对新切割中心线分量同时检查面积小于 `(线宽/2)²` 与包围盒对角线小于一条线宽；原小岛豁免，只回构实际部分删除的分块，其他填充轮廓原样保留。新增旋转长窄区、轮廓精确保留及细颈残片／孔保护回归，Geometry 共 20 个源码定义。此条取代下一条面积单条件和所有分块回构的实现描述。证据 `planning/raft/fragment-fix-review-20260921/`；Shapely 仅独立核算合成反例及历史捕获，未执行新 C++。未编译、未 GUI／实机；用户构建应用和 `fff_print_tests` 后按原 Geometry／完整 Raft 过滤器复测，不复用旧通过数字。
- 2026-09-21 碎片优化（待用户构建）：`RaftGeometry` 记录各阶段切缝前轮廓，新路径先按实际半线宽 +0.002 mm 生成安全墙中心线，绕过支撑助手的重复内缩；新切割区域中退化的中心线分量按 `(线宽/2)²` 筛除，原独立小岛豁免，逐块回构填充并与原允许区求交。最终裁剪的可选碎段清理默认关闭，仅试用新路径启用，保留原有短线和旧调用行为；不改平台／混摆门禁、工艺、摆盘或 GUI 参数。新增真实龙 217 点名义轮廓及三项 Geometry 回归，定义总数 19；墙连续性用例在不启用短线过滤的严格裁剪下比较路径，另覆盖小岛、窄区、原短线、属性和取消。静态审查及哈希 `planning/raft/fragment-fix-20260921/`，进度 `doc/print_settings/support/raft_geometry_trial_progress.md`。本轮未编译、未运行新／旧 C++、未 GUI 或实机验证；下面 16／325、63／4,500 只对应修订前构建，不可复用为本补丁通过。需用户重建应用和 `fff_print_tests` 后按原 Geometry／完整 Raft 过滤器复测。
- 2026-09-21 08:17 GUI 碎片核查：用户成功构建 `planning/build/macos-build-arm64-20260921-081651-981022.log` 包含 RaftGeometry／RaftMerge／测试编译和应用／`fff_print_tests` 链接，生产与测试哈希符合上一轮圆形覆盖修正。macOS arm64 / Release，`master` / `41f2845d5299ed11056789ff43e55e1c45c38cd0` 加当前未提交补丁。AI 直接运行已有程序：Geometry 16 用例／325 断言、完整 Raft 63 用例／4,500 断言，均退出 0，前者包含于后者。用户 GUI 日志 `081740-112282` 为 `status=applied`、五层分块采用、导出及床内检查成功；但真实 G-code 基底仍有 179 条总长 <1 mm 的连续挤出路径，最短约 0.015 mm，另有四个名义线宽包络 <1 mm² 的小岛，不能宣称碎片质量验收通过。实际平台配置 101×101 mm，包络最大 Y=100.999 mm，不能用作 100 mm 实机验收。`omitted_fragments=4` 只表示省略无挤出的候选轮廓。证据 `planning/raft/fragment-review-20260921-0817/`；分析 `doc/print_settings/support/raft_fragment_review_20260921.md`。本轮未修改生产／测试源码、未编译、未 Computer Use 新验证或实机打印；两次 LLDB 启动超时没有捕获，不计为 CLI 通过。后续参数来源对齐及碎片处理尚未实现，源码更改后须用户新构建再复测。
- 2026-09-21 07:23 反馈后修正（待用户新构建）：`planning/build/macos-build-arm64-20260921-072125-148399.log` 编译／链接应用和 `fff_print_tests` 成功，源码与上一轮快照一致。同版 Geometry、完整 Raft 均退出 3，失败为孔洞精确空交集、仅平滑及移开小筏层候选未采用；不能算通过。GUI 龙模型 5 层已规划切缝，却因 `extrusions leave the assigned region` 沿用原筏层。只读捕获确认通用 `jtSquare` 料线覆盖在转角外伸，而相同路径的圆形扫掠在原 0.002 mm 范围内；孔洞失败是约 `6.34e-9 mm²` 数值碎屑。现仅修正试用候选的圆形覆盖检查，并给圆角明确 0.001 mm 精度以避免原 28 秒过密采样；平台、混摆门禁及分块规则不变。增加转角／圆形端部回归，Geometry 共 16 个定义；尚未编译或运行新源码。用户需重建应用和测试后复跑专项及同一龙模型。证据 `planning/raft/geometry-validation-20260921-0723/`，进度 `doc/print_settings/support/raft_geometry_trial_progress.md`。中止的宽断点调试不作为验收；未实机、未提交。
- 2026-09-20 23:30 对齐 X-PRINT（历史状态，后续见上条）：实际失败日志 `planning/build/macos-build-arm64-20260920-233012-814074.log` 在新增测试因 `print.objects()` 返回 const 指针停止；已改用 `print.get_object(0)`，不移除 const。用户要求对齐参考算法，现替换此前跨阶段共缝、小脚避让、保孔／碎片拒绝规则：名义轮廓并集 → 各阶段独立闭／开运算 → 阶段半宽内缩 → 按岛条带／放射分块 → 墙和填充；无路径碎片不取消其他分块。保留平台线宽裁剪、混摆校验、原适用范围和关闭时基线。15 个 Geometry 定义已更新，未编译或运行新 C++／CLI／GUI，旧 13／60 项通过不能代表本修订；用户新构建应用及 `fff_print_tests` 后需复跑聚焦、完整 Raft 和真实龙模型。参考与静态证据 `planning/raft/xmaker-alignment-20260920/`，详情 `doc/print_settings/support/raft_geometry_trial_progress.md`。下面记录保留旧方案历史。
- 2026-09-20 23:06 分块反馈：用户 `230345-809142.log` 对应源码哈希已核对，修改前 Geometry 13 用例／277 断言及完整 Raft 60 用例／4,452 断言退出 0（不重复相加）。真实龙模型仍因 `unprintable raft fragment` 整组回退：调试器只读捕获确认基底三个约 0.6～0.7 mm² 楔形岛，半线宽预判与两次半线宽墙线内缩不一致。仅临时调整调试进程的规划宽度后 2 条切缝生效、5 层路径通过原校验；这属于定位实验，不是新源码验收。当时源码按阶段墙线所需空间做碎片预判，保留有效切缝及最终门禁，新增两项回归（共 15 个定义）；此设计已由上条对齐修订替换。证据 `planning/raft/geometry-validation-20260920-2306/`，详见筏层试印进度文档；未实机、未代编译、未提交。
- 2026-09-20 22:47 分块反馈后修正（待再构建）：用户 `224030-176115.log` 已重编测试并完成资源／打包；同版 Geometry 和完整 Raft 各有 1 条失败断言（四腿新夹具的固定切缝坐标），不能算通过。GUI 龙模型已开启 40／0.8 分块，同版 CLI 复现 `no safe geometry change`：44 个首层接触岛中 42 个受保护，4 条候选均触及保护区。现改为保护区局部保留连接、其他安全缝段仍参与分块，继续碎片／覆盖检查；新 Geometry 夹具明确居中并补第 13 个用例，增加规划计数日志、中英文说明。生产和测试均需用户再构建后复测；证据 `planning/raft/geometry-validation-20260920-2247/`，进度文档同下。未实机打印，不把 Shapely 轮廓诊断当作新 C++ 验收。
- 2026-09-20 22:40 首次试印版构建失败后修正：本机实际最新日志 `planning/build/macos-build-arm64-20260920-223427-324924.log` 已编译筏层生产源码并链接 OrcaSlicer，但 `fff_print_tests` 在 `test_support_material.cpp:3949` 因两参数 `set_deserialize` 调用失败。仅将该测试改为已有 `set_deserialize_strict`，保留严格往返断言；未更改生产代码、未代编译、未运行旧测试。需用户重跑构建后再执行新专项；最新状态见 `doc/print_settings/support/raft_geometry_trial_progress.md`，下条“未编译”保留首次提交前状态。
- 2026-09-20 分块／平滑试印版（待用户新构建）：新增默认关闭的 `raft_segmented` 与默认 0 的 `raft_smoothing`，40 mm 参考尺寸／0.8 mm 缝宽；仅有效 CuraV1、逐层、单耗材、关闭支撑的兼容单实例组。纯几何助手与现有合并基线接入，补 12 个 `[SupportMaterial][Raft][Geometry]` 用例，包含于原完整 `[SupportMaterial][Raft]`。本轮未编译、未运行新增 C++、GUI／CLI 或实机验证，不能复用下方旧通过数字。源码、范围及试印步骤见 `doc/print_settings/support/raft_geometry_trial_progress.md`；用户编译应用和 `fff_print_tests` 后按长期授权核对并运行聚焦及完整筏层专项。
- 2026-09-20 开启／关闭筏层混摆校验已通过本机自动化验证：用户实际构建日志 `planning/build/macos-build-arm64-20260920-142033-391636.log` 包含校验源码、OrcaSlicer 及 `fff_print_tests` 的编译／链接；macOS arm64 / Release，`master` 的 `12a8ca0199` 加本次未提交补丁，源码哈希与构建前静态检查记录一致。`[SupportMaterial][Raft][Clearance]` 9 用例／145 断言、完整 `[SupportMaterial][Raft]` 47 用例／4,171 断言均退出码 0，专项已包含于完整统计，不重复相加。证据、命令、二进制／源码哈希在 `planning/raft/clearance-validation-20260920-143113/`。校验覆盖逐层打印时实际筏层路径与未开启筏层对象的模型、支撑、Brim 和逐对象裙边，使用物理 Z 区间及线宽，在切片完成及导出前拦截冲突；不修改参数或摆盘。静态差异和中英文翻译格式检查通过；本轮未做 GUI、CLI 命令端到端、实机打印、完整 CTest 或其他平台验证，不能复用旧 GUI 记录宣称本补丁 GUI 已通过。后续修改源码仍须用户重新编译后复测。
- 2026-09-20 入库审查补充：生产源码、测试与上述构建哈希保持一致；增加同版 CLI 四个合成混摆场景对照：筏层与未开筏层对象支撑冲突、仅其逐对象裙边冲突均返回 `result.json.return_code=-100`（进程码 156），日志包含双方对象名且没有生成 G-code；分离支撑及移开裙边两例均返回 0、盘警告为空并生成非空 G-code。证据在 `planning/raft/clearance-review-20260920-merge-ready/review.json`，覆盖上一条当时尚未验证的 CLI 范围，不扩大为 GUI、实机或所有裙边组合验收。未发现阻塞入库项，未修改生产／测试源码，未提交。
- 当前 macOS arm64 / Release 测试程序为 `build/arm64/tests/fff_print/Release/fff_print_tests.app/Contents/MacOS/fff_print_tests`。用户完成相应编译后，可直接运行 `'[SupportMaterial][Raft]' --reporter compact`；合并聚焦过滤器为 `'[SupportMaterial][Raft][Merge]'`，包含于完整筏层专项。
- 2026-09-19 最新用户构建日志 `planning/build/macos-build-arm64-20260919-160153-075775.log` 对应：合并专项 9 用例／1,814 断言、完整筏层专项 38 用例／4,026 断言，均退出码 0；证据在 `planning/raft/merge-validation-20260919-160440/`。不能重复相加这两组统计，也不能用旧失败构建的日志替代。
- 同版 Computer Use 验证通过五个独立对象的筏层合并、移出一条腿后重切分离、共享筏层贴平台边界裁剪；三个项目和 GUI 导出 G-code、截图、日志及线宽边界核查在 `planning/raft/gui-merge-validation-20260919-1607/`。这是合成夹具 GUI 验证，未实机打印、未覆盖完整 CTest 或其他平台。后续修改源码后须等待用户新构建再复测。详情见 `doc/print_settings/support/support_settings_raft.md`。

### 模型修复专项测试：固定命令入口

- 2026-09-20 CGAL 前 STL 自动修复开关已完成入库前验证：普通 `--repair-model cgal` 复用 `--auto-repair-model 0|1`，显式 1 为 ADMesh 导入修复 → CGAL → 可选切片／导出；显式 0 或未传仍跳过 ADMesh，普通非 CGAL 默认开启不变。OBJ 不执行 ADMesh；独立 `--check-model` 仍固定原生导入，拒绝该开关及切片。用户构建 `planning/build/macos-build-arm64-20260920-174141-288544.log` 包含本次 CLI／PrintConfig 编译及应用链接，源码哈希与构建前记录一致。macOS arm64 / Release 同版 CLI：修复／导出／切片 78 项、检测入口 76 项、材质 30 项通过，退出码均为 0。裂缝 STL 进入 CGAL 前开放边：默认／关闭为 4、开启为 0；OBJ 三种模式均为 4。切片使用维护台 EP3 Plus 工艺和两槽通用 PLA，检查盘警告及非空 G-code；原有保色、部分修复和检查绕过限制保留。证据在 `planning/repair/cgal-auto-repair-switch-20260920/review/review.json`。AI 未编译，未做本轮原生 GUI 修复、实机打印、完整 CTest 或其他平台验证；本次提交排除无关 `result.json` 及被忽略的维护台文件。说明见 `doc/model-check-workflow.md`。

- 2026-09-17 入库前证据核对：v4 已有用户构建及实际 CLI 运行记录，不再仅是待构建状态。`planning/build/macos-build-arm64-20260916-165319-477418.log` 包含 macOS arm64 / Release 的 ModelCheckWorkflow、CLI 和检测测试编译，以及应用／libslic3r_tests 链接完成。`planning/debug/runs/cli-20260917-164729-9c5bb039/result.json` 与 `cli-20260917-164901-a3ca78ea/result.json` 均为顶层三字段、摘要六字段的 v4；真实模型检测／组合修复分别为 not_requested／partially_repaired，开放边 3→0、退化面 1→0、自交计数 60→56，仍未完全修复。未找到此次 v4 完整专项及整套 CLI 回归的新通过记录；9 月 15 日 178 用例／73 项记录仅覆盖当时版本，不能代替 v4 回归。此次提交前仅核对既有日志、更新文档并做静态检查，未重新编译／运行测试。下面“待验证”条目保留历史状态，最新证据以本条和 `doc/model-check-workflow.md` 第 7 节为准；Blender／压薄工具翻译不纳入本次 Orca 提交。

- 2026-09-16 检测业务摘要 v4（待新构建验证）：`--check-model`（含组合 CGAL）顶层仅 return_code／error_string／model_checks；摘要仅 schema_version／mesh_status／check_complete／input_materials_ignored／repair_status／issues 六字段。按用户确认删除 output_files 文件路径，也不输出输入路径、working_model、next_action、repair_message、supplementary 或修复前摘要。仅精简输出，内部文件导出、两项结构检查、整体完整性、修复／回退／保色门禁与普通切片／单独修复协议不变；历史 JSON 不改写。更新 `[ModelRepair][Inspection][JSON]` 和既有 CLI 检测脚本（修复前基线独立获取，产物只在测试独占目录内校验，不把路径混回响应），维护台本地兼容 v4。本轮仅静态检查，未编译／未运行测试；下面 178 用例／73 项 CLI 不能当作 v4 已通过的证明。字段及迁移见 `doc/model-check-workflow.md` 第 2、7 节。

- 2026-09-15 23:50 最新验收：测试坐标基准修正后，用户授权重编译 `build/arm64` / Release 的 `libslic3r_tests` 成功；聚焦 1 用例／786 断言、固定完整修复专项 **178 用例／13,611 断言**全部通过，退出码 0。修正仅在测试：局部涂色区域排除未引用顶点，整模型导出按应用变换后的坐标精确比较，保留面积／位置及原网格／矩阵不变断言；生产修复与导出不变。此前 207 是 JUnit 展开记录数，不是独立用例总数，统计以 Catch2 的 178 为准。23:17 同版应用的 CLI 检测 73 项、材质 30 项、导出 50 项及工具 7 项已通过，真实模型开放边 3→0、退化面 1→0、自交计数 60→56（再次 45），仍为部分修复。本条覆盖下面同日“待验证”的对应 C++／CLI 范围，旧数字及历史 GUI 记录不作为新 GUI 验收。新日志 `planning/repair/cap-coordinate-validation-20260915-2QL12a/`；关联真实模型／CLI 证据 `planning/repair/cap-validation-20260915-Du2nKj/`，详见 `doc/model-check-workflow.md` 第 7 节。未重打包 GUI、未切片／打印、未验证完整 CTest／旧 macOS、未提交。

- 2026-09-15 共线退化连接准备基线（待新构建验证）：显式 GUI／CLI 部分修复共用 `repair_degenerate_edge_caps()`，只翻转闭合流形图中可证明保持原表面的孤立共线 cap；复用原 float 顶点和绕序，复杂簇／已有对角线等不支持条件不放行，相交查询截断不批准。独立基线可在后续普通几何拒绝时保留，内部 changed 与净删面数分开；异常、取消、非法输出、严格保色、核心严格默认及通用部分门禁不变。新增回归仍在 `[ModelRepair][Partial][Preparation]` 等固定过滤器及 `scripts/test_cli_model_check.py`，不新增命令参数或业务 JSON 字段。本轮未编译／未运行测试，详情见 `doc/model-check-workflow.md` 第 5.1.1、7 节。

- 2026-09-15 取消整体壳体朝向公开检测项（待新构建验证）：业务 supplementary 仅保留 components／internal_components，demo 和维护台删除方向条目；其未知／奇偶壳层约定不再影响 check_complete、mesh_status、next_action。局部朝向冲突仍属七项，CGAL 修复内部安全校验与旧底层诊断数据保留；包含关系等其他未完成判断仍不得伪报通过。未入库 v3 以当前字段文档为准。更新 `[ModelRepair][Inspection]`、`[ModelRepair][Inspection][JSON]` 和 CLI／demo 用例；本轮未编译／未运行测试。

- 2026-09-15 业务 JSON 精简（待新构建验证）：`--check-model`（含组合 CGAL）对外 `model_checks[].schema_version=3`，仅输出任务结果／当前文件组、七项 count+complete、三项补充摘要、修复状态与可选修复前数量。检查未取得时为 null，不完整的 0 不作无问题；不混出历史副本或未发布候选。检测／修复算法、内部详细报告和普通切片／修复 JSON 不变，不新增参数。字段与迁移见 `doc/model-check-workflow.md` 第 2 节；新增 `[ModelRepair][Inspection][JSON]` 与更新 CLI 检测脚本仍用固定入口。本轮未编译／未运行测试，下面旧版本通过数字不能证明本次协议修改通过。

- 2026-09-15 当前验收状态：macOS arm64 / Release 构建通过；固定完整修复专项 169 用例／12,521 断言通过（含 GUI 导出坐标专项 2 用例／4,932 断言），CLI 检测 60 项、材质 30 项、修复导出 50 项及验收工具 7 项通过。原生 GUI 同部件部分修复、单对象／旋转缩放／多对象合并的右键 STL 与 OBJ 导出对照通过。下面 9 月 14 日记录中的“待验证／未编译”描述保留当时状态，已由此次对应范围的验证覆盖，不代表当前仍未验收；完整证据见 `doc/model-check-workflow.md` 第 7 节。阶段性入库不等同完整 CTest、真实大模型全集、旧版 macOS 兼容性或切片／打印验收；后续源码修改仍须重新验证，不能复用本次结果。

- 2026-09-14 22:10 构建验证后修正（待重新构建）：`is_discardable_repair_part()` 保留 2.4.2 数值阈值，但体积／体积比删除只用于共享 CGAL 只读检查确认的有界实体；开口、朝向抵消、自交等不按零有符号体积删除。原尺寸阈值、删空／保色／异常保护不变。回退夹具编辑索引后重建 TriangleMesh 并检查分支执行次数；CLI 检测按原生导入后的基线区分有补洞进展与 unresolved，OBJ 部分修复及颜色断言不放宽。此前完整专项 163 通过／2 失败、CLI 几何 50 通过不能当作本补丁通过证明；详情见 `doc/model-check-workflow.md` 第 7 节。本次未编译、未运行测试，仍用既有固定入口。

- 2026-09-14 同部件部分成果保留（本轮待新构建验证）：GUI/CLI 显式部分模式共用 `repair_with_partial()` / `is_safe_partial_repair()`，严格 `repair()` 和核心默认不变，不新增 CLI 参数。可保留缝合／补洞／闭合自交处理的已验证检查点；要求有效、无退化／非流形的前后曲面，残留相交对与边界段按坐标／绕序为原集合的子集且至少一项减少，并保持既有几何限制；20,000 对截断不批准。复杂拆点 soup 不开放实验性路径。普通候选外层几何复检失败仅拒绝该候选；非法数据、删空、异常、取消、严格保色及 GUI 场景变化仍整笔不提交。新回归在 `test_meshboolean.cpp`、`test_model_repair.cpp` 的既有专项标签内；本轮仅静态检查，不能复用旧日志宣称通过。细节和边界见 `doc/model-check-workflow.md` 第 5 节。

- 2026-09-14 更新独立 `--check-model` 工作流（待新构建验证）：每次输入固定按 Orca 原生导入准备（STL 开启 ADMesh；OBJ 保持原生导入／材质规则），不再独立预删面，然后保存副本 → 七项检测及三项补充检查 → 可选 CGAL 部分修复 → 导出只读校验。同一次任务内部校验不再修复；working_model 作为新任务输入仍执行原生导入，不提供跳过参数，不靠文件名识别阶段。model_checks[].schema_version=2，basic_repair 改为导入统计，inspection 协议仍为 1。正式说明见 `doc/model-check-workflow.md`；新入口不替换普通导入/切片流程。回归入口仍是 `python3 scripts/test_cli_model_check.py <新构建的 OrcaSlicer 路径>`，可选 `--demo-inspector <已有原生 demo 程序>` 对同一工作 STL 做比较；不编译、不切片。新增测试仍使用 `[ModelRepair][Inspection]` / `[ModelRepair][Preparation]` / `[ModelRepair][Partial]`，已在完整专项中。GUI/CLI 共享高级修复将增加未配对边的预删面推迟到 CGAL 原拓扑修复，失败不单独保留这批删面；其他有效修改继续保留。代码和脚本存在不代表本轮验证通过。

- 基础修复验收夹具在 `tests/data/model_repair_acceptance/`，由 `scripts/generate_repair_fixtures.py` 确定性生成；`manifest.json` 记录预期结果，不能当成测试已经通过。只生成夹具不会构建或运行应用。
- 2026-09-14 用户确认薄小部件清理对齐 2.4.2：启用 `remove_non_3d_parts` 时，局部包围盒最小尺寸或绝对体积 ≤ EPSILON（1e-4）、最小／最大尺寸比或体积／包围盒体积比 ≤ 1e-6 的部件进入删除流程；其中体积类条件须先证明有界实体，未证明的异常表面留给修复，不按零体积删除。不使用实例／volume 缩放。只调整部件清理策略，不替换结构性退化判定或 CGAL 候选门禁，不放宽原有严格保色禁删分支。删空对象、静默丢色仍中止事务，其他有效部分成果仍可保留。低于阈值的旧缩放保留用例改为验证清理关闭／删空保护；常规 0.001 mm 薄片夹具仍保留。新增阈值／保色／部分清理用例在 `[ModelRepair][Scale]` 等既有标签内，须新构建验证。
- 2026-09-14 STL 导出：部分结果允许保留零面积面，ASCII／二进制写入器不得以此判为写入失败或偷偷删面；零面积写零法向，其他有限 float 顶点用 double 中间量生成法向。空网格、非有限坐标、非法索引、二进制面数超限仍拒绝，原子替换／符号链接保护不变。可导出不等于已完全修复；回归在 `test_stl.cpp` 的 `[stl][CLI][validation]` 和 `test_model_repair.cpp` 的 `[ModelRepair][Partial][Preparation]`，已纳入完整专项，须新构建验证。
- 新增聚焦标签为 `'[ModelRepair][Acceptance]'`，仍包含于下面的完整专项；涵盖修复后 50 层截面与健康模型对比、危险模型失败不改原件、薄实体保留、孤立涂色面的严格保色失败保护。截面测试不等同完整 G-code 切片验证。
- CLI 几何/导出验收固定入口：`python3 scripts/test_cli_repair_acceptance.py <新构建的 OrcaSlicer 路径>`。脚本创建独立临时目录，保留原始输入哈希、命令、日志、结果 JSON，检查完全修复的输出重导入无需再次修复；不编译。部分修复输出另行断言仍有未解决项，不能套用完全修复的闭合/重导入通过标准。
- 验收脚本还直接检查 OBJ 文件索引边的闭合和朝向，不通过坐标焊接掩盖色区接缝；这不替代 CGAL 自交/非流形顶点检查。单平面负例必须在修复阶段失败且不提交，不能以导入或导出失败冒充修复保护通过。
- 验收工具自身的无编译测试：`python3 scripts/test_repair_acceptance_helpers.py -v`，覆盖夹具确定性、短 STL 探测长度、色区断边、独立壳体和错误索引。它不代表 C++ 改动已通过。
- 彩色 OBJ 共享索引和涂色 T 形接缝的聚焦用例为 `'[OBJ][Topology]'`；模型清理不得删空一个对象，但同一对象仍有实体时可删除平面杂片。两者已纳入固定完整专项，无需修改构建页面测试菜单。
- 实际切片验收在同一脚本后加 `--load-settings '<打印机JSON绝对路径>;<工艺JSON绝对路径>' --load-filaments '<耗材1JSON绝对路径>;<耗材2JSON绝对路径>'`，必须使用服务器实际解析完成的参数，双色 OBJ 须配置两个耗材槽。不提供参数时脚本明确标记切片未验证；0.001 mm 薄实体只验证几何保留，不能承诺当前层高可打印。
- `--repair-model cgal --slice ...` 默认也严格保留支持的 OBJ 面颜色；不得搭配 `--no-check`、`--allow-newer-file` 或 `--load-slicedata` 跳过检查/复用旧切片缓存。未请求修复的旧流程不受本限制影响。
- 新增 `result.json.repair_validation` 区分网格检查与切片状态。`mesh=passed` 不代表已切片；`slicing=completed` 也不等于无警告或所有打印条件都满足，须同时检查任务返回码、盘数、`sliced_plates[].warning_message`、非空 G-code 和逐层预览。没有自动发往打印机的步骤。
- 2026-09-10 部分修复策略：GUI 显式“修复模型”和 CLI `--repair-model cgal` 启用 `allow_partial`；核心 API 默认及切割工具内部仍严格。可保留独立基础清理（精确零面积面、同向重复面）及其他部件的有效修改，不采用 CGAL 检查失败的候选。非法数据、内存/工作线程异常、严格保色失败及取消仍中止当前事务。
- 部分修复必须报告 `partially_repaired`、`unresolved` 计数、`mesh_validated=false` 和 `repair_validation.mesh=not_passed`；不能把 `success=true`、`failed=0` 或 CLI 退出码 0 当作网格完全合格。仅有未解决项、没有可保留修改时为 `unresolved`：仅导出任务非零退出；请求切片时允许尝试，但切片警告/冲突/导出门禁不放宽。
- 新增聚焦标签 `'[ModelRepair][Partial]'` 已包含于完整专项。CLI 验收追加同一网格“重复面+非流形边”、部分输出再次检查及两种输入顺序的批处理用例；这些仍是合成夹具，不能冒充真实 Thingi10K 验证。真实样本与本轮进展见 `planning/repair/repair-test-models/mesh-inspector/REPAIR_PROGRESS.md`，未重新编译运行前不能复用旧日志作为新策略通过证明。
- 2026-09-13 检测验收后修正：部分模式可将同一网格里的接触/嵌套 patch 保持同组，与空间分离的其他组分别修复；组内不拆共享顶点、不逐壳转正、不重新居中。只有实际修复/清理进展才保留分组。新增 `[ModelRepair][Partial][Split]` 回归；`scripts/test_cli_model_check.py` 覆盖两种输入顺序的 STL/OBJ 部分结果及再次修复的 `unresolved`，不放宽取消、异常和严格保色保护。需新构建验证，不复用 17:29 失败前的通过数字。
- 2026-09-13 17:55 验收后补充：共享 STL 读取按长度/面数优先识别二进制，覆盖零法向量及短文件，不修改第三方 ADMesh；空间分组按源面编号精确复制四类标注，避免重复几何重映射，CGAL 修复后的保色门禁不变。新增 `[stl][Import][Binary]`，分组专项补细分标注/面积/位置断言，CLI 新工作流补两种顺序的双色 OBJ/MTL。均包含于原固定入口；必须重新构建后运行，不能把 17:55 的 80 项 CLI 通过当作补丁已验收。记录见 `doc/model-check-workflow.md`。
- 2026-09-13 22:44 验收后补充：共享标注恢复只向有面积重叠且通过平面检查的目标面映射，不把共边/共点染色，也不沿画笔邻面传播绕过逐面门禁。新增 `[SavedPainting][Boundary]` 覆盖边界不扩色、反向面、坐标偏移、重复映射和重新三角化，已包含于原完整专项。保留部分修复的面积/位置断言；用户反馈的 1 个用例/12 个断言失败不是新修正已通过的证明，须重新构建并复跑 GUI/CLI 共用恢复路径及切割专项。
- 修复后切片在 G-code 导出前后都检查警告；CRITICAL 警告或冲突令任务失败，并尝试移除本次刚生成的拒绝 G-code。删除失败会记录日志，调用方仍必须按非零返回码拒绝使用所有产物。普通不修复切片的原有警告策略不变。
- 由用户执行编译并提供日志；编译完成后的测试执行遵循上方“AI 编译与验证约束”中的长期授权，无需再次确认。AI 修改代码、测试或本文档，不代表获得编译授权。
- 不需要为每次修复修改构建页面的测试菜单。页面的 CTest 标签下拉框与 Catch2 标签不是一回事；不得直接把 `[ModelRepair]` 等当成 `ctest -L` 的标签。
- 固定编译目标为 `libslic3r_tests`。只编译 OrcaSlicer GUI 不会更新测试程序；编译失败时必须停止，不得继续运行旧二进制。
- 完整修复专项过滤器为 `'[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]'`，逗号表示匹配任一标签。新增修复/涂色用例应沿用这些标签，避免每次修改入口命令。
- 本轮缺陷聚焦过滤器可用 `'[ModelRepair][Scale],[SavedPainting][Provenance]'`，相邻方括号表示同时满足两个标签；它不替代完整专项回归。
- 顶点拆分候选修复的聚焦过滤器为 `'[ModelRepair][SoupOrientation]'`，已包含于完整专项；拆分后仍相交的候选直接报告不支持安全自动修复，不进入新增实验性自交修复，检查失败后网格/涂色/所有权不变及正常朝向修复。它不替代真实 OBJ/MTL 的 GUI/CLI 修复验收，也不能使用 `[OBJ][GUIColor]` 通过结果作为其验证证明。
- 主要用例文件：`tests/libslic3r/test_model_repair.cpp`、`test_meshboolean.cpp`、`test_saved_painting.cpp`、`test_obj.cpp`、`test_stl.cpp`。专项通过不等于 GUI、Undo/Redo、跨平台或完整 CTest 已通过。

#### macOS arm64 / Release（当前构建页面对应目录）

在仓库根目录执行下面整段命令。显式使用 Bash，使 macOS 默认 zsh 下也能正确读取 `PIPESTATUS`，并用子进程退出避免关闭用户终端。构建日志写入 `planning/build/`，测试日志写入 `planning/repair/`。

```bash
bash <<'BASH'
set -e -o pipefail
mkdir -p planning/build planning/repair
repair_stamp=$(date +%Y%m%d-%H%M%S)
repair_build_log="planning/build/model-repair-build-${repair_stamp}.log"
repair_test_log="planning/repair/model-repair-test-${repair_stamp}.log"
printf 'Build log: %s\nTest log: %s\n' "$repair_build_log" "$repair_test_log"

cmake --build build/arm64 --config Release --target libslic3r_tests --parallel 2 2>&1 \
  | tee "$repair_build_log"

set +e
./build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests \
  '[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]' \
  --reporter compact 2>&1 | tee "$repair_test_log"
repair_codes=("${PIPESTATUS[@]}")
printf 'Test exit code: %s; log writer exit code: %s\n' "${repair_codes[0]}" "${repair_codes[1]}" \
  | tee -a "$repair_test_log"
if (( repair_codes[0] != 0 )); then exit "${repair_codes[0]}"; fi
exit "${repair_codes[1]}"
BASH
```

前提是该目录已配置 `BUILD_TESTS=ON` 并生成 `libslic3r_tests` 目标。缺少目标时先检查既有 CMake 配置和平台依赖，不要擅自切换生成器或另建不同依赖配置。此处提供的整段命令包含编译，不构成 AI 自动执行编译的授权；用户编译完成后，AI 可按上述长期授权单独执行测试部分。

#### 不同构建目录不能混用

| 已配置构建目录 | 编译命令 | 测试程序路径（相对仓库根目录） |
|---|---|---|
| `build/arm64`，Ninja Multi-Config | `cmake --build build/arm64 --config Release --target libslic3r_tests --parallel 2` | `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` |
| 历史 `build`，Unix Makefiles | `cmake --build build --target libslic3r_tests --parallel 2` | `build/tests/libslic3r/libslic3r_tests` |

以上路径来自本机现有生成配置；其他平台或生成器应先核对实际输出位置。Windows/Linux 沿用相同目标和 Catch2 过滤器，不照搬 macOS 的 `.app` 路径。不要运行旧 `repair_review_probe` 或复用历史 81/798 等通过数字来证明新补丁已验证。

#### 反馈与结果判定

- 基础补洞颜色规则：可映射部分保留原色，无映射部分使用所属部件默认色（OBJ exporter 的 state 0 → base_filament）；不创建新色，不推断多色洞口图案。`[ModelRepair][CGAL][Painting][HoleColor]` 用例已纳入既有专项过滤器。用户已确认不需要 UV 贴图/完整渲染材质往返，不将其列为本次基础功能门禁。

- 常用 OBJ 修复命令为 `--repair-model cgal --export-obj`：严格保留支持的 MTL 面颜色，有颜色自动输出 OBJ+MTL，无颜色输出普通 OBJ。映射失败报错；原贴图/顶点色往返不在此能力内。仅需几何时加 `--repair-ignore-materials`。显式多色参数保留兼容；无修复参数的 `--export-obj` 保持旧纯几何语义。CLI 冒烟脚本已追加自动选择、带色补洞及旧命令兼容用例，修改后须重新构建验证。

- CLI OBJ/MTL 专项新增用例使用 `[OBJ][CLI][Import]`，已包含于完整修复过滤器；无需改构建页面菜单。
- 2026-09-13 缺少 MTL：GUI/CLI 共享 OBJ 导入层在引用的库缺失、或 `usemtl` 没有库声明时自动忽略源颜色/UV并继续几何导入；多个库部分缺失按整个模型降级。CLI 的 `input_materials_ignored` 包括该自动降级；显式耗材映射仍保留用户指定的配色。存在但损坏的 MTL、严格保色时未定义的材质和非法几何仍拒绝。新增 `[OBJ][MissingMTL]` 和 CLI 材质冒烟用例，需要新构建验证，不复用旧日志。
- GUI 导入清理只移除不含三角面的空对象，不按带符号体积删除非空网格；开口、朝向抵消、平面和极薄模型保留到显式修复阶段判定，不代表这些模型可打印。零/负/非有限体积不参与体积单位猜测及其选择性缩放；用户明确指定的整模型单位转换仍生效。新增 `[ModelRepair][Import]`，并在缺少 MTL 的 GUI/CLI 对照用例中执行 GUI 清理步骤；均包含于固定完整专项，需要新构建及 GUI 复测。
- CLI 材质冒烟脚本显式使用 `--debug 3` 验证缺少 MTL 的警告。导入负例检查 JSON 的解析失败码 `-6` 和日志中的具体原因，参数冲突仍检查 JSON 错误信息；不得仅凭任意非零退出码判定保护通过。脚本修正可单独验证，但不能替代本轮 C++ 改动的新构建验收。
- GUI OBJ 原色/耗材复用策略用例使用 `[OBJ][GUIColor]`（位于 `test_obj.cpp`），已包含于完整 `[OBJ]` 过滤器；聚焦验证可用 `'[OBJ][GUIColor]'`，无需改构建页面菜单。此类用例不替代实际 GUI 导入/配色/导出的验证；修改后必须重新构建，不能复用旧 GUI 的产物检查作为通过证明。
- 2026-09-12 GUI 保色回归：显式“修复模型”使用 `gui_model_repair_options()`，不再读取实验性 `keep_painting` 偏好；始终尝试保留受支持的涂色，禁止静默清空，无法映射时仍中止当前事务。切割工具、核心 API 默认及 CLI 的既有策略不变。新增 `[OBJ][GUIColor][ModelRepair]` 夹具检查同一 GUI 策略下的 staging/提交/OBJ 导出：绿色原面面积 300 mm² 保留，新增顶面使用红色默认耗材；`[ModelRepair][GUIColor][Painting][Partial]` 检查部分模式也不得清空无法映射的涂色。两者已包含于固定完整专项，未重新编译运行及复测 GUI 前不得宣称丢色问题已验收通过。
- 用户重新构建 OrcaSlicer 应用后，可执行 `python3 scripts/test_cli_repair_materials.py ./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`。服务器替换为当前平台新构建的可执行文件路径。脚本不编译，每次创建独立临时证据目录并保留 JSON/日志，验证 `--repair-ignore-materials` 策略及参数冲突；它不替代 Catch2 完整专项，AI 可按上述长期授权在相关验证任务中直接运行。
- `--repair-ignore-materials` 默认关闭，仅配合 `--repair-model cgal` 显式用于不需要材质/颜色/UV 的任务。纯几何输出不是保色输出；以 JSON 的 `input_materials_ignored` 和输出语义判断，不只看 `annotations_cleared`。

- 编译失败：提供本次构建日志，不继续测试旧程序。
- 测试完成：提供本次测试日志，检查最终用例/断言统计及退出码，不只看中间是否出现 `[error]`；部分负向用例预期输出错误日志。
- `No test cases matched`、程序不存在、日志写入失败或非零退出码均不能算验证通过。
- 记录实际 checkout、构建目录、配置、平台和新日志；未执行的 GUI、完整回归或其他平台验证必须明确标注。

## AI 工具使用
本仓库已初始化 CodeGraph，项目索引位于 `.codegraph/`。在 Codex 中分析代码时，优先使用 CodeGraph 的 MCP 能力进行结构化查询，再结合 `rg`、源码阅读和测试验证。

CodeGraph 适合回答“函数/类在哪里”“谁调用了它”“它调用了谁”“修改这个 symbol 会影响哪些文件”“模块依赖关系是什么”等问题。常用命令：
- `codegraph status`：查看索引状态与统计信息。
- `codegraph sync`：增量同步当前改动。
- `codegraph index`：全量重建索引。

注意：在 zsh 中如果要写注释，请单独成行；不要执行 `codegraph sync # 增量更新`，否则某些命令解析场景会把注释内容当作额外参数。推荐直接执行 `codegraph sync`。

Understand-Anything 已安装到用户级目录，并通过 skills 暴露给 Codex。它适合做人类可读的项目理解、知识图谱、可视化 dashboard、代码问答、diff 影响分析、onboarding 文档和业务域梳理。常见触发方式：
- “分析这个代码库并构建知识图谱”
- “用中文解释这个项目架构”
- “生成 onboarding guide”
- “分析当前 git 改动的影响”
- “打开/生成 Understand-Anything dashboard”

使用建议：日常定位和改代码优先使用 CodeGraph；需要全局架构梳理、知识图谱、交接文档或面向人的解释时使用 Understand-Anything。两者输出都不能替代编译、测试和源码确认；涉及行为变更时必须回到源码与测试验证。

## 提交与 Pull Request 指南
历史提交偏好简洁的句子式标题，可带 issue 编号，例如 `Fix grid lines origin for multiple plates (#10724)`。打开 PR 前先在本地 squash fixup。填写 `.github/pull_request_template.md`，UI 改动需包含复现步骤或截图，并说明影响到的预设或翻译。关联 issue 时使用 `Closes #NNNN`，依赖升级或 profile 迁移需要在 PR 中明确提醒维护者。

## 安全与配置提示
漏洞报告遵循 `SECURITY.md`。不要把 API token 或打印机凭据写入已跟踪配置；实验性设置使用 `sandboxes/`。修改 `deps_src/` 中的第三方代码时，在 PR 描述里记录对应上游 commit 或 release，并运行相关平台构建脚本确认集成无误。
