# 模型修复与智能切片引擎：执行与验证记录

## 当前执行状态

- 10-07 入库与手工交接：用户已授权将本批模型重建、关联 macOS 列表修正、GMP 平台修正及测试/文档作一笔中文提交，不推送。提交前基准仍为 `master / 34477d7411...`，不收教材文档、planning 产物或 result.json。本节随该笔提交归档，确切提交身份以包含本节的 Git 提交为准；以下“未提交/待授权”均是历史。沿用已验证 `89bed37f...` 包，不改业务代码或重复构建；[手动 GUI/CLI 快速清单](2026-10-05-reconstruction-gui.md#10-07-用户手动验证快速清单)新增实际输入、可复制命令及回传目录。自动化/已覆盖 GUI 已通过，Windows/Linux、实机及剩余 GUI 项仍待验，入库不等于发布验收完成。
- 10-07 用户重新打包后复验：常用包与直接应用均为 `89bed37f...`，简中 MO 均 `09567746...`，测试仍 `8dcaaf7e...`，未重复构建。新包 CLI 27 项、真实 worker 1 用例/22 断言通过。CUA 已实操默认预闭合、封口风险提示、采用、菜单撤销/重做、正常退出重开及再次切片；两次实际保存网格七项完整为零，50 层完整主体路径，565 项导出有效配置相同。证据根 `planning/repair/reconstruction-boundary-postbuild-VtFcVD`；本轮只补验证/文档，不改业务源码、不提交。
- 10-07 开放边界优化已实现并通过自动化：基准仍为 `master / 34477d7411...`，保留既有改动，未暂存/提交。重建新增默认可关闭的受限预闭合，封口副本完整检查通过才用于 AlphaWrap；受控拒绝回原汤并提示薄壳风险。旧高级修复、七项公开检查、用户配置不变。新直接应用 `89bed37f...`、测试 `8dcaaf7e...` 已构建；223 核心用例/15326 断言、27 重建 CLI、76 检测 CLI、真实 worker 1 用例/22 断言、21 样本实际导出复检通过；新专项连续 20 轮通过。同一 EP7 工程 567 项 G-code 配置一致，缺面方块由薄壳碎路径恢复为 50 层墙/填充/顶底面。
- 10-07 当前剩余：此前启动及文件选择框交互阻塞已解除；用户关闭系统弹层后，通过最近工程继续完成关闭预闭合 GUI 对照，同配置复现薄壳/碎外墙，风险提示正确。闭合输入不误报封口/开放薄壳，“保留原模型”后网格和摆放不变。新增英文/暗色布局、当前版运算中取消等未重新 GUI 验证；Windows/Linux 和实机仍未运行验证，不能宣称完整发布验收。具体结果与后续操作见下面最新节及 GUI 交接。
- 10-07 工具定位：CodeGraph MCP 未暴露，CLI 增量 sync 成功更新 30 文件，但函数查询覆盖不足且随后源码已变；历史 Understand 图谱仍为 60148c114，目标新文件不在图中，本轮按源码确认共享入口，未生成误导性图谱覆盖。下面旧版阶段状态为历史，不覆盖上述当前结论。
- 10-07 新打包 GUI 复验完成：同一 `5758588c...` 包实操大开口与相交双壳，风险确认、参数关系拦截、保留/采用、Undo/Redo、正常退出重开、显式落床、取消及内存预算失败均有证据；本轮未再出现闪退、未遗留 worker。两个实际保存候选的原始 OBJ 七项完整为零。双壳切片 51 层路径完整；大开口候选为薄壳，50 层只有少量碎外墙并有悬空警告，不能判为可打印。证据在 `planning/repair/reconstruction-gui-20261007-mkXJqu`。只验证/更新记录、不改业务代码、不提交；下一步如继续优化，应先补薄壳候选的试切片验收，其他 GUI 项和 Windows/Linux 运行仍待验。
- 10-06 23:13 打包版复验完成（GUI 仍阻塞）：用户新打包与直接产物均为 `5758588c...`，应用未再改源码/重链；打包路径实跑 26 项重建 CLI、真实 worker 1 用例/22 断言通过，当前测试程序 30 用例/680 断言通过，10 项平台选择测试通过。Windows/Linux 仅配置和源码核查，没有目标系统编译/运行证据；Docker CLI 存在但服务 socket 不存在，未启动环境或安装工具链。CUA 在读取窗口时多次返回超时/`AXError.cannotComplete`，GUI 不能记通过；连接打包路径已启动新 PID 84168（23:16），另有旧 PID 9046（17:08）及 `/Applications` 版并存，未强制终止或修改工程。下一步保存/关闭旧实例后复验新 GUI，另按下面跨平台清单在目标系统验收；本轮只更新验证文档，没有改业务代码或提交。
- 10-06 23:13 用户重新编译后的复验开始：`master / 34477d7411...` 源码未变，打包与直接应用现均为 `5758588c...`，测试仍 `d2ef457d...`，无需重复构建。准备对打包路径复跑 CLI/真实 worker 并审查 Windows/Linux 依赖和进程分支；平台运行与静态检查分开。Computer Use 首次全局读取超时、按打包路径连接返回 `AXError.cannotComplete`，另有构建前启动的旧 GUI 进程仍在运行，GUI 尚未验证；保留用户现场，不终止/覆盖工程。
- 10-06 GMP 定向修正完成：仅 macOS arm64 项目依赖升级到官方 6.3.0；正式库/应用/测试已重建。200 次连续重建、220 项修复核心、26 项重建 CLI、76 项检测、78 项修复切片、真实 worker 集成全部通过；21 样本实际导出 STL/原始 OBJ 七项完整为零，STL 与升级前逐字节一致。原先 GMP 压力崩溃在本轮未再复现，不等于任意负载稳定性保证。额外几何 35 用例中 1 项旧凸凹角度测试失败，升级前诊断二进制同样复现，未修改该独立问题。GUI/跨平台/实机未验证，原打包副本仍旧；下一步用新直接产物按 GUI 清单复验，未暂存/提交/推送。完整证据见紧随的 GMP 节。
- 10-06 GMP 正式构建通过：项目 `dep_GMP` 已从固定官方 6.3.0 源码构建、安装到本仓库 arm64 前缀；应用与测试重新链接成功，SHA-256 为 `5758588c...`／`d2ef457d...`，不是 Homebrew 对照版。库内 x18/w18 指令引用为零，GMP 自检 199 通过/1 跳过/0 失败，修复核心 220 用例/15254 断言通过。平台选择 10 用例通过，Linux/Windows 实机未验证；CLI、连续压力、21 样本复检仍在进行，打包目录仍旧。下一步完成这些回归并更新 GUI 交接。
- 10-06 GMP 正式修正开始：用户已授权“优化下”，仅 macOS arm64 目标升级固定 GMP 至 6.3.0；Linux、Windows MSVC、macOS x86_64 保留既有版本和路径，不改变高级修复策略。当前基准仍为 `master / 34477d7411...`，保留全部既有未提交实现。下一步核对官方源码校验值、补平台选择回归、构建项目 dep_GMP 与应用/测试目标，重跑稳定性及修复回归；正式结果尚待本批验证，未提交/推送。
- 10-06 本轮收尾：build3 补齐“输出检查引擎异常不得当作几何不合格继续重试”，arm64 Release 构建成功；最终直接应用 SHA-256 `6647c0b0...`、测试 `3308c3e0...`，重建及原修复合并 220 用例／15254 断言、CLI 26 场景、真实 worker 1 用例／22 断言通过。GMP 6.3.0 **隔离链接对照**连续 200 次核心测试通过且库内无 x18/w18 汇编引用，当前项目正式依赖仍为 6.2.1，不能把对照结果当正式包修复。升级选择未收到答复，保留明确稳定性阻塞，未提交；下一步是确认依赖处理后重建正式目标并回归，再验新版 GUI。
- 10-06 缺陷优先重建已经实现并完成首轮回归，但**稳定性尚不通过，不可据此入库／发布**。21 个样本（20 个缺陷、1 个健康薄体对照）全部产生候选，实际 STL 和不经 STL 自动清理的 OBJ 复检均为七项完整零缺陷；此前高级修复失败的 8 件全部包含在内。8 件使用固定测试配置再切片：5 件有完整逐层路径，3 件因首层无路径／厚度低于首层失败，其中 2 件成功切片仍有悬空警告，不把网格通过当可打印。旧高级修复未改。本轮连续压力测试两次卡住，LLDB 第 57 次复现 `__gmpn_sub_n` 越界，现有 GMP 6.2.1 的 arm64 汇编使用 macOS 保留的 x18，符合官方已知兼容问题；正在独立核对升级路线，尚未修改／替换项目 GMP 依赖。已向用户询问是否升级固定版本到 6.3.0 并回归。最终应用直接产物 `64630f69...`，打包副本仍为旧 `b3dc2aa1...`；新版 GUI、其他平台、实机未验，详见下节。
- 10-06 用户已授权“优化下”，开始实施上述重新定位：GUI／CLI 共用 `reconstruct_model_candidate → reconstruct_mesh`，本批拟改 triangle-soup 输入、严格输出复检、最多三档重建尝试、偏差告警及进程监测短暂失败处理，并更新中英文和回归。保留旧高级修复、用户预设和所有无关改动，不提交。现有构建配置为本目录 arm64 / Release，无并行构建；源码修改后将最小构建 OrcaSlicer、libslic3r_tests，再实跑此前 8 件失败样本及导出复检／诊断切片。GUI 新交互及跨平台暂为待验证，不能沿用旧二进制结论。
- 10-06 扩展验证后的目标修订（设计，尚未实施）：用户明确重建应接手高级修复失败模型，本轮优先完整网格检测归零，允许形状／结构变化。此前单壳、受限开口及偏差阈值不再适合作为新目标的验收设计；当前源码仍是旧限制，不能将本节建议当成已生效。基准仍为 `master / 34477d7411...`，现有未提交实现及其他用户文件保留；本轮只核对源码、固定版本官方资料并更新本记录。下一步应按下节方案验证原高级修复失败的 8 件样本，再决定产品接入，不以小缺口方块成功代替新增修复能力。
- 10-06 问题模型扩展验证完成：同 SHA 的应用实跑 21 个样本（20 个缺陷输入、1 个健康薄实体对照）。现有高级修复 12 件 repaired、1 件 not_needed，13 件导出后的独立七项检查完整且为零；另 8 件未修复成功。默认重建仅小缺口样例产生候选并完整复检通过，体积相对完整 2 mm 方块增大约 14.08%，其余 20 件拒绝；对高级修复失败的 8 个保留基础副本再次重建，也没有得到候选。另发现两次 `monitor_failed`，同输入各三次重试恢复为几何拒绝，需排查进程监测稳定性；本轮只验证/记录，不改源码或放宽守卫。完整证据与结构对比见下节。历史 `eae30a.../6d00cd...` 是三壳样本，不是 9 月托架，已纠正该记录中的误称。下一步应先处理监测异常，再明确真实失败模型覆盖，不据本批宣称重建商业可用。
- 10-06 17:22 用户重新打包后的专项复验完成：打包副本与直接产物 SHA-256 均为 `b3dc2aa1...`。在新启动的打包程序 PID 12945 上完成原闪退重开序列、非模型行点击、真实改名、打印开关、落床 Undo/Redo、中文参数取消及候选生成/保留原件；未再复现闪退。保存副本的网格面数和工程工艺配置未变，原输入 SHA 未变。本轮只补验证记录，不改源码、不重复构建、不提交；剩余完整重建验收与实际修复收益仍待验证，见下方最新复验节。
- 10-06 闪退修复已完成局部验收：仍在 `master / 34477d7411...` 的既有未提交重建工作区。已分离 macOS 只读可编辑性查询与真实动作、保护非法落床目标、修正参数取消翻译；未改重建算法、CLI 协议或用户预设。最终 arm64 Release build-4 成功，直接应用 SHA-256 `b3dc2aa1...` 与完成 GUI 回归及 26 项 CLI 回归的 build-3 字节相同。方块→自交已采用工程→方块重开、双击名称、打印开关、落床撤销/重做及参数取消通过；尚未覆盖的完整重建 GUI/跨平台/实机门槛不提升。当时打包目录仍旧；17:22 用户已重新打包并复验，以上一条为准。下一步是剩余重建验收，不自动提交/推送。
- 10-06 修复前 GUI 验证：在用户已打包、启动的项目应用上完成候选保护、预览、采用、Undo/Redo、大开口拒绝、显式落床和方块手动切片；随后显示对象列表并再次打开已保存工程时，16:41:29 发生真实闪退。堆栈是 macOS 辅助功能查询 → 对象列表编辑回调 → `ensure_on_bed`，不是 AlphaWrap worker；相关回调在 HEAD 已存在，尚不能认定由新重建代码引入。当时只更新验证记录，未修源码；后续授权修复与实际结果以上一条及下方修复节为准。
- 10-05 当前实施：用户已确认开始编码，无新增待选产品项。基准 `master / 34477d7411f076f4c9fe9200fcfc69c9253b0293`，保留现有高级修复；独立近似重建原型、受限 worker、共享验收、CLI 和 GUI 比较/确认已实现。最终 build-5 的应用/测试构建成功；新增 28 用例/340 断言、实际 worker 集成 1 用例/22 断言、重建 CLI 26 场景通过；旧核心 190 用例/14574 断言、检测 CLI 76 场景、普通修复/试切片 78 场景也在最终二进制重跑通过。2D 凸包缓存与主线程 QHull 问题已修复并有回归。文档全量/差异检查通过。下一步为 GUI 实操、真实失败模型收益与后续试切片链路，未暂存、提交或推送。
- 10-05 交付边界：当前是实验性原型，不是完整商用验收。macOS 内存采用 RSS watchdog（非硬上限）；GUI 独立同参诊断试切片尚未实现，报告固定 `trial_slice=not_run`、`print_ready=false`。历史三壳样本的未修复导出件因约 6.07 mm 开口拒绝、用户标记的 2.4.2 修复件因多壳拒绝，证明守卫生效但不能证明真实修复收益（原记录误称托架，10-06 按文件 SHA 纠正）。GUI、Windows/Linux、真实失败样例效果及实机质量未验证。[GUI 交接](2026-10-05-reconstruction-gui.md)保留具体步骤；不能以人工方块候选替代这些发布门槛。未触碰用户教材文档或 result.json。
- 10-05 设计时历史：用户明确暂缓 AI，沿用现有开源许可并接受商用开源交付；当时仅记录设计、没有业务实现。当前已获编码授权，下方设计节保留当时审核文本，不能将其中“尚未实现/待授权”当作当前状态。当前实现及测试范围以上两条与本轮验证节为准。两条既有提交已按要求改为中文标题，修复提交为 `cb7b88aa729dcbeac5b03f2c9398d841e747b29c`，下方旧哈希属于当时历史。
- 10-05 最新授权与步骤：用户要求提交已验证修改，并分析后续模型修复方案。修复核心/测试/对应文档已提交为 `81a55459da`，当前为向导日志/对应文档独立提交收尾；本记录为两批共用证据。全量及第一笔 staged 文档/差异检查通过。只提交本任务文件，不收其他任务的 `2026-10-04-study-textbook.md`、日志、模型产物或 result.json，不推送。下方“未提交”均为当时的历史状态。提交后仅分析后续方案，下一步重点是失败样例分类、同参验收和按耗时/退出原因决定是否优化，不恢复已撤回实现或直接接入新引擎。
- 当前目标：只优化现有高级修复中必要的诊断与确定无用的距离计算；原智能引擎实施方案已撤回，后续章节只保留历史证据。
- 边界：不覆盖用户预设，不改变七项公开检测含义，不自动提交/推送/发布，不默认启用近似重建或自由生成脚本。
- Orca 基准：`master / 9e9381d595`，开始时工作区干净。
- Blender 服务基准：`feature/f_模型打印对比 / 8372a7a`，开始时工作区干净；不切换或合并其分支。
- 参数匹配服务基准：`Print3dParamAutoMatchService / feature/f_模型打印对比 / da99c43`，开始时干净；已定位现有 TemplateRuleEngine 和模板推荐服务，尚未修改后端两仓。
- 前置撤回（2026-10-04）：用户否定本批方案并要求先撤回代码、重新分析两张截图。已撤回本批 35 个跟踪文件的源码、测试、翻译与模块文档改动，恢复为 `master / 9e9381d595` 后再实施下述必要优化；不回退此前提交，不修改后端仓库。
- 可恢复备份：`planning/repair/withdrawn-smart-engine-20261004-151hK1/withdrawn.patch`；撤回前记录和简中 MO 同目录保存。无关 `result.json`、`one_vertex_touch.svg`、`2026-10-04-study-textbook.md` 保持原内容。
- 证据边界：撤回检查时 35 个目标文件与 HEAD 的差异为空；之后新增的必要优化已单独重建、验证。下文历史区的 194/81/86 等结果、构建哈希及 GUI 操作属于已撤回实现，不能作为当前代码的验证，也不要按旧 GUI 清单验收。
- 当前任务（随后获用户授权）：只实施必要的小范围优化——共享 CGAL 修复阶段计时、局部拓扑分支退出原因、已确定拒绝候选的昂贵距离检查短路。不恢复保守模式，不改变修复算法、容差、公开 JSON、取消架构或 20,000 面限制。
- 10-04 自动化结论：Release arm64 构建完成；核心 190 用例、组合检测 CLI 76 场景、普通修复加试切片 78 场景通过。真实托架导出顶点/面与原标准模式结果一致。GUI 当时待验证，其后进展见下一条；Windows/Linux、实机打印未验证。未暂存或提交。
- GUI 更新（2026-10-05）：用户已重新构建；打包路径与 `src/Release` 的应用哈希相同，日志包含新增计时。托架 GUI 修复、备份落盘、重启恢复及重切片已有日志和实际产物证据；颜色、取消、正式另存重开等未覆盖项仍待验证。详见下面的日志复核，不把局部通过扩展为全部 GUI 通过。
- 当前任务（10-05 随后获授权）：仅收敛配置向导的全量 JSON 与逐耗材常规日志，保留数量/加载结果/异常，删除只为日志进行的重复序列化；不改预设解析、继承、选择、保存和网页传输，不处理其他低优先级报错，不自动提交。
- 当前完成（10-05）：向导日志降噪已完成，Release arm64 应用构建及向导 4 用例/184 断言通过。用户随后完成打包，`src/Release` 和 `build/arm64/OrcaSlicer/` 两份应用哈希均为 `07fe16b6...`，源码哈希不变。Computer Use 已验证加载、EP3/EP7 搜索、取消重开及原选择确认；选中配置摘要哈希前后一致，日志降噪生效。新程序保留原工程停在准备页；本轮无需用户重复上述向导测试。取消机制、空间索引和修复策略重构仍暂缓，未提交。

## 2026-10-07 用户重新打包：预闭合 GUI 与导出复验（最新）

### 身份和范围

仍为 `master / 34477d7411f076f4c9fe9200fcfc69c9253b0293` 加既有未提交改动。常用包 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` 与直接产物 SHA-256 同为 `89bed37fcafd3ed31abf6494629a569e60ea3722ad062b29511abe0044b4387e`；测试为 `8dcaaf7e4554b8c91d0168658747eda65f2934555fcf7562fcd0822f28830771`，简中 MO 为 `095677465c93273dcf933eeb29cbbfd47d7c3b3d6e8cf093f5fe743df6197adb`。源码/二进制没有再次修改，不重复构建。

CUA 按完整打包路径启动 PID 8127；保存新工程后正常退出，PID 消失，再启动 PID 8453 验证真实跨进程重开。未操作独立 `/Applications/OrcaSlicer.app`，未覆盖原件/用户预设、未打印/上传/提交。以下相对产物路径均位于 `planning/repair/reconstruction-boundary-postbuild-VtFcVD`。

### GUI、文件及自动化结果

| 验证 | 结果 |
| --- | --- |
| 输入/有效配置 | 原件为 `planning/repair/reconstruction-gui-20261007-mkXJqu/missing-top-original.3mf`，缺顶面 10 mm 方块、10 面。EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard，层高及首层 0.2 mm、classic 墙、支撑关闭，无后处理。工程配置 SHA-256 `fba8d1d4ab42e17194faf8ea4e04ecac20aa6d0bf72eeaffea4ef9a2cc3814aa`。 |
| 新选项/风险 | “包裹前尝试闭合开放边界”默认勾选，说明可能填上有意开口；风险确认未勾时生成禁用。alpha 0.5 / offset 0.05 / 告警 0.25 mm 生成后明确显示已加入封口面、超采样偏差，要求检查开口及试切片，没有把七项通过说成可打印。 |
| 实际候选 | 3648 面、体积 1029.45015 mm³，各轴约 10.100 mm；相对理想完整 10 mm 方块体积增约 2.95%。默认不落床，底面 0→约 -0.05 mm。原缺面网格的有符号体积不是实体体积，不用于体积保真比较。双向采样偏差约 0.05/5.00025 mm，封顶确实改变原开放结构。 |
| 采用/独立检查 | 保存 `boundary-closed.3mf`，提取真实存储顶点和拓扑到原始 OBJ，不经 STL 自动修复。打包程序独立 `--check-model`：七项均 complete=true/count=0、check_complete=true、mesh_status=passed、退出 0；重开后另存结果同样通过。 |
| 撤销/重做 | 实际使用“编辑→撤销”恢复原 10 面，`boundary-undo-confirmed.3mf` 原始网格 hash 与输入完全相同；菜单重做的 `boundary-redo.3mf` 与采用网格完全相同。早先快捷键尝试未实际撤销，`boundary-undo.3mf` 仍为候选，明确排除于撤销通过证据；不声称快捷键已验。 |
| 退出重开 | 正常退出后重开 `boundary-redo.3mf`，另存 `boundary-reopened.3mf`。面拓扑不变；加载重新居中导致局部点和实例平移互补变化，世界坐标逐点最大差约 5.10×10^-8 mm，无有意义的移动/变形。全部保存副本工程配置 hash 相同。 |
| 真实 GUI 切片 | 采用及重开后各自 Cmd+R 切片并导出 `boundary-closed.gcode` / `boundary-reopened.gcode`；均 50 层，无模型空层、无 NaN/Inf，存在内外墙、稀疏/实心填充、顶底面/内部桥接，不再是原先少量碎外墙。两份导出有效配置 565 项全部相同；模型正向挤出均约 222.716 mm。脚本逐层核查，不冒充逐层人工 GUI 目视或实机打印。 |
| 关闭预闭合对照 | 从最近工程打开 `boundary-undo-confirmed.3mf`，取消选项，其他参数相同。预览正确提示开放输入可能包成薄壳、缺墙体路径；采用后保存 `boundary-surface.3mf`，11302 面、体积 60.70210 mm³，实际原始 OBJ 也七项完整为零。GUI 切片/导出 `boundary-surface.gcode`：同样 50 层但只有零碎外墙，模型正向挤出约 3.895 mm，无内墙、填充和顶底面，导出另有悬空警告。与默认结果的 565 项配置全部相同，证明改善来自几何而非换参，也再次证明“七项为零/层非空”不能代替打印路径验收。 |
| 闭合输入/保留 | 重开已经独立检查通过的 `boundary-reopened.3mf`，默认生成，预览不显示新增封口或开放薄壳警告。新候选会再次扩大到约 10.200 mm，未采用；点击“保留原模型”，另存 `boundary-kept.3mf`，原始网格 hash、实例摆放和配置与重开输入相同。留在此已保存工程，没有为重复重建覆盖原件。 |
| 新打包 CLI/worker | `scripts/test_cli_model_reconstruction.py --binary <上述包> --output-dir <证据根>/cli`：27 通过/0 失败/0 跳过；`ORCA_RECONSTRUCTION_BINARY=<上述包> libslic3r_tests '[ReconstructionIntegration]'`：1 用例/22 断言通过。日志 `cli.log`、`integration.log`，无残留 worker。此前同身份核心全量结果见下节，本轮未重复全量测试。 |

`audit.py`、`audit.json`、`audit.log` 保存原始网格、配置、七项和逐层结果，全部断言通过。路径长度按 G2/G3 端点弦长统计，只作同参对照，不把它当精确弧长；是否存在模型路径的结论不受影响。不能用本样例证明自动识别有意开口或所有模型可打印；内部仍 `trial_slice=not_run`、`print_ready=false`，本次切片是 GUI 明确另行执行。

应用日志为用户数据目录 `OrcaSlicer/log/debug_Wed_Oct_07_11_24_41_8127.log.0`、`debug_Wed_Oct_07_11_32_28_8453.log.0`。未发现本轮重建失败/新增崩溃报告；有未安装 3Dconnexion 驱动、临时预设 parent 查询和嵌套导出跳过告警，实际文件保存、配置和重开切片已核对，不将这些告警说成日志完全无问题。

### 未完成项与续接

尝试重开原件验证取消预闭合时，CUA 曾卡在 macOS“前往文件夹”弹层：输入完整路径可显示，发送确认又回到 `/`；Escape 和其他受支持输入方式也未可靠关闭。用户回复“关掉了”后，回主页最近工程选择 `boundary-undo-confirmed.3mf` 继续，上述对照已完成。保存对照时工具曾报 native pipe closed，应用仍存活、文件已成功落盘，重新获取窗口即恢复；没有新增崩溃报告。不把自动化交互故障认定为算法失败，也不隐去这次人工协助。

当前版运算中取消、英文/暗色/缩窗和其余历史未覆盖 GUI 场景仍待验证；中文当前窗口下的新开关和两类提示已可读。Windows/Linux 未编译运行、实机未打印。本轮没有新增业务代码，未暂存、提交或推送。具体步骤保留在[GUI 交接](2026-10-05-reconstruction-gui.md)。

## 2026-10-07 开放模型预闭合：实现与同配置切片验证（实现时记录）

### 改动与边界

用户“继续优化”后，针对缺顶面方块原先包成约 0.1 mm 薄壳、七项通过但缺少主体路径的问题，在共享核心加入 `close_open_boundaries=true`。GUI 默认勾选“包裹前尝试闭合开放边界”，CLI 可用 `--reconstruction-no-close-open-boundaries` 关闭。封口可能改变有意开口，参数和预览均提示；不自动采用模型，不调整用户预设或增大 offset 来掩盖问题。

仅对有边界的输入尝试：可构成非自交流形、64 环/单环 512 边/总 2048 边内，禁止 cubic 补洞回退；新增面和输出面受限，操作计入同一 worker 总时间/内存预算。所有补面后按实际 float 重建、校验闭合和非零体积、定向并完整七项复检。成功才替代包裹输入；受控几何/预算拒绝退回原汤并记录原因，意外异常/检查引擎错误停止。闭合输入不补面；对比偏差仍基于原始几何。旧高级修复、check-model v4、普通修复 JSON 不变。

报告 `engine.boundary_closure` 记录是否采用、边数、环数、新增面及失败原因；未采用预闭合的开放输入额外 `open_surface_envelope_warning`。这个优化不是通用“所有模型可打印”保证：平面、开放自交等仍可能只包出薄壳，报告仍为 `trial_slice=not_run`、`print_ready=false`。有意开口/内腔无法靠补面自动识别，需用户检查或关闭选项。

### 构建与自动化

- 构建：本仓库 `build/arm64`、Release，执行 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2` 成功；日志 `planning/build/reconstruction-boundary-20261007-build1.log`。既有 duplicate-library/Homebrew zstd 目标版本链接警告保留，非本轮新增依赖。
- 新直接应用：`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA-256 `89bed37fcafd3ed31abf6494629a569e60ea3722ad062b29511abe0044b4387e`。
- 新测试：`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests`，SHA-256 `8dcaaf7e4554b8c91d0168658747eda65f2934555fcf7562fcd0822f28830771`。
- 核心 `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess],[ReconstructionWorkerGuard],[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`：223 用例/15326 断言通过，日志 `planning/repair/reconstruction-boundary-core1.log`。
- 新 `[BoundaryClosure]`：3 用例/72 断言，正反缺面实体、禁用薄壳、闭合方块/贯通孔/相交闭壳不变、平面/开放自交/513 边超限回退；随后连续 20 轮均通过，日志 `reconstruction-boundary-core-focus1.log` / `reconstruction-boundary-repeat20.log`（均在 `planning/repair/`）。不是任意负载稳定性保证。
- 重建 CLI 27 通过/0 失败/0 跳过，证据 `planning/repair/reconstruction-boundary-cli1/summary.json`；新默认和禁用选项实际经 worker 校验。真实 worker 集成 1 用例/22 断言通过，日志 `planning/repair/reconstruction-boundary-integration1.log`。
- 旧检测 CLI 76 场景通过，日志 `planning/repair/reconstruction-boundary-check-cli1.log`；证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-model-check-j7py_9ik`。GMP 平台选择 10 通过，只证明配置选择，不证明 Windows/Linux 运行。
- 21 个问题/对照模型均生成候选；真实 STL 与不经 STL 自动清理的原始 OBJ 独立七项完整且为零，输入 hash 不变。证据 `planning/repair/reconstruction-boundary-models1/{identity,results,suite-summary}.json`。预闭合实际采用在缺顶面、小缺口和 `100173`；其余跳过或拒绝后仍走原包裹策略，不能称 21 件都已改善打印。
- 简中 `msgfmt --check-format` 通过，生成的本地 MO 不提交；新增文案/帮助原文已同步。模块正文与指纹同步，`python3 scripts/check_ai_module_docs.py --all` 复核 35 模块、问题 0；`git diff --check` 通过。未暂存，不声称已执行 staged 提交检查。

### 同有效配置诊断切片

输入取已保存 GUI 工程 `planning/repair/reconstruction-gui-20261007-mkXJqu/missing-top-original.3mf`，保留项目/对象配置与实例变换。工艺 EP7 0.4 nozzle / QIDI PLA Rapido / 0.20mm Standard，层高及首层 0.2 mm、classic 墙、支撑关闭，无后处理。项目配置 SHA-256 `fba8d1d4ab42e17194faf8ea4e04ecac20aa6d0bf72eeaffea4ef9a2cc3814aa`。

在物理坐标重建，结果按原变换写回独立 3MF；精确相同 float 顶点共享、不作容差焊接。两份实际 G-code 的 567 项配置逐项一致。证据及脚本为 `planning/repair/reconstruction-boundary-slice2/summary.json`、`planning/repair/reconstruction-boundary-slice.py`。

| 同一缺顶面方块 | 开启预闭合（默认） | 关闭预闭合（旧策略） |
| --- | --- | --- |
| 重建体积 | 1029.450 mm³ | 60.702 mm³ |
| 路径层数 | 50 层 | 50 层 |
| 实际模型路径 | 外墙、内墙、稀疏填充、顶底面、内部实心/桥接均有 | 仅少量碎外墙 |
| XY 挤出路径总长 | 6820.476 mm | 117.665 mm |
| E 正挤出合计 | 222.716 mm | 3.895 mm |

开启后各层模型路径均超过 20 mm，无 NaN/Inf。关闭结果复现旧 GUI 的薄壳问题；两者都“各层非空”，再次说明非空层不是充分验收条件。此处是外部诊断切片，不把报告中的 `trial_slice` 改成自动通过；未做实机打印。

首轮 `reconstruction-boundary-slice1` 失败证据保留：测试写出的 3MF 按 STL 各三角形独立建点，未共享相同顶点，导致首层路径缺失。定位后仅修正测试工程构造，第二轮同一业务二进制成功；首轮不能算产品重建算法失败，也不删除失败证据。

### GUI 与剩余验收

先在旧验证实例确认工程已保存，再经 CUA 正常退出 PID 1234；未操作独立 `/Applications/OrcaSlicer.app`。新直接应用启动 PID 4085，但 CUA 超时；一秒进程采样停在 `wxApp::DoInitGui → NSApplication initialize → NSBundle localizations → open$NOCANCEL`，尚未进入重建，采样保存在 `planning/repair/reconstruction-boundary-gui-startup.sample`。这只定位当前阻塞位置，不证明具体系统根因。

为排除直接资源符号链接因素，把同一新二进制和实际资源复制到隔离 `planning/repair/reconstruction-boundary-gui-h4KiyH/OrcaSlicer.app`；SHA 与新直接应用相同，但启动 PID 4401 仍返回 `AXError.cannotComplete`。未取得可操作窗口，未强制终止实例或改用户设置，未把它记成算法失败或 GUI 通过。常用 `build/arm64/OrcaSlicer/` 仍是旧 `5758588c...` 包，不能用它验新选项。

待重新打包/恢复 GUI 连接后，按[交接清单](2026-10-05-reconstruction-gui.md)检查新默认/禁用选项、封口/薄壳提示、采用/Undo/Redo/保存重开和实际切片。Windows/Linux 编译运行、剩余 GUI 场景和实机仍待验证；本轮只使用现有 CGAL 接口，没有再改 GMP 平台规则。未暂存、提交、推送或覆盖用户预设。

## 2026-10-07 新包 GUI 与导出复核（优化前历史）

### 身份及证据

仍为 `master / 34477d7411f076f4c9fe9200fcfc69c9253b0293` 加原有未提交修改。使用 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA-256 `5758588c2673568579637ba1dd685ffe9c8d87120848c70108ceb95888c13a2f`，无需重复构建。用户关闭旧项目实例后，CUA 按完整路径启动 PID 712，正常退出后以 PID 1234 完成跨进程重开。未操作独立的 `/Applications` 版、未覆盖用户工程/预设、未打印/上传、未改业务代码。

隔离目录 `planning/repair/reconstruction-gui-20261007-mkXJqu` 保存各阶段 3MF、两份 G-code、原始 OBJ、独立检查 JSON 和 `audit.json`。应用日志为用户数据目录 `OrcaSlicer/log/debug_Wed_Oct_07_10_08_15_712.log.0`、`debug_Wed_Oct_07_10_21_32_1234.log.0`。本轮无新增 OrcaSlicer 崩溃报告；日志中的 3Dconnexion 驱动未安装错误不属于重建失败。

输入 `01-missing-top.stl`、`08-intersecting-shells.stl` 与 HEAD 字节一致，SHA-256 分别为 `69d81fc1d7d559ef201560cdfe0255626f9676cddfcb0a2dd4c05ac165223c74`、`928d578feffff2c93f18a308738ad5684378b83ba82be545dce7c8b1362dd502`。

### GUI 实际结果

| 项目 | 结果与边界 |
| --- | --- |
| 入口/确认/非法参数 | 画布菜单保留旧修复和独立重建。未勾风险确认时生成禁用；alpha 0.5 / offset 0.2 被关系校验拒绝。非正值和对象列表右键入口未单独测试。 |
| 大开口方块 | 默认 alpha 0.5 / offset 0.05 / 告警 0.25 mm，10 面变为 11302 面。尺寸 10→约 10.100 mm，双向采样偏差约 0.05000/0.41136 mm，超阈值提示而不拒绝，明确结构可变化、待试切片。保留原件后再生成未累积改变。 |
| 采用及 Undo/Redo | 默认不落床，实例保持 (90,90,5)。撤销恢复 10 面；按菜单 Cmd+Y 重做恢复 11302 面，网格/配置 hash 与采用副本一致。误用 Cmd+Shift+Z 曾继续撤销导入，生成空的 `missing-top-redo.3mf`；该操作失误文件排除于产品缺陷及通过依据，正式证据为 `missing-top-redo-confirmed.3mf`。 |
| 退出重开 | 重开另存 `missing-top-reopened.3mf`，拓扑完全一致。导入重新居中导致局部坐标与实例平移互补变化，世界坐标逐点最大差约 2×10^-8 mm，没有实际移动/变形。对象列表反复读取未再闪退，不等于所有编辑路径已验。 |
| 内存预算与取消 | 在 11302 面候选上设 alpha 0.01 / offset 0.001：第一次实际触发内存预算失败；第二次运算中点 macOS 进度窗关闭控件，明确返回已取消。均原件保留、worker 回收。未观察到独立文字取消按钮，关闭控件可取消；首次预算失败不计作取消测试。 |
| 相交双壳及落床 | 默认得到 4810 面、约 15.101×10.100×10.100 mm 候选，双向采样约 5.05/0.05048 mm，显示偏差警告。勾 Z 落床后局部最低 Z=-5.05000019 与实例 Z=5.05000019 相抵为 0，XY 仍为 (90,90)。 |
| 独立复检 | 两件实际保存网格提取为原始 OBJ，不经 STL 清理，调用同包 `--check-model --outputdir <隔离目录>`：均 check_complete=true、七项 complete/count=0、mesh_status=passed、退出 0。 |

### 试切片及实际缺口

两件均沿用 EP7 0.4 nozzle、QIDI PLA Rapido @EP7 0.4 nozzle、0.20mm Standard @EP7，层高/首层 0.2 mm、classic 墙生成、支撑关闭、无后处理。各 3MF 的 `project_settings.config` SHA-256 均为 `fba8d1d4ab42e17194faf8ea4e04ecac20aa6d0bf72eeaffea4ef9a2cc3814aa`；完整配置已保存，不与此前 CLI 的 EP3 Plus 冒充同参。

- **双壳**：51 层均有模型正挤出，内外墙、填充、顶底面正常，导出无悬空警告，无 NaN/Inf 或空挤出层。候选体积约 1539.302 mm³，与原方块并集 1500 mm³ 加表面偏移相符；不是尺寸保真/实机质量保证。
- **大开口**：候选围绕开放表面形成薄壳，没有恢复为实心方块；闭合体积约 60.702 mm³。GUI 预览大范围缺路径，导出提示浮空区域。50 层虽都存在正挤出，但模型路径合计仅约 117.665 mm、E 约 3.895 mm，只剩碎外墙。G-code 总耗材 0.13 g 含 Brim/自定义起始线，不能以总耗材或“无空层”掩盖主体丢失。开放原件无有效实体体积，不拿其有向体积计算保真率。

因此七项网格归零已验证，但**开放面包成过薄实体仍是可打印性缺口**。界面保留风险/试切片待执行，没有宣称可打印；后续自动引擎必须检查有效路径和薄体丢失，不能只看七项归零/每层非空，也不能未经确认一律填满孔洞与空腔。本轮未实施后续优化。

中文/亮色/当前 Retina 显示可读；英文、暗色、缩窗/Tab、绘制/附属结构拒绝、缩放镜像、完整对象列表入口未重验。G-code 全部层已审查路径，但未逐层人工点查全部 GUI 截面。Windows/Linux 仍只有下节静态/配置证据，无目标平台运行及实机打印。收尾 `python3 scripts/check_ai_module_docs.py --all`（35 模块、0 问题）和 `git diff --check` 通过。只更新验证文档，未暂存/提交/推送。

## 2026-10-06 23:13 用户重新打包后的复验与跨平台审查

### 当前 macOS 结果

用户构建日志 `planning/build/macos-build-arm64-20261006-231240-155820.log` 到达应用构建、gettext 和包资源复制阶段，无构建失败；打包和直接应用均与上轮已测的 `5758588c2673568579637ba1dd685ffe9c8d87120848c70108ceb95888c13a2f` 一致，测试仍为 `d2ef457d...`。本轮复用该二进制，不重复构建、不重放上轮 200 次压力与 21 模型验收冒充新覆盖。

- `scripts/test_cli_model_reconstruction.py --binary build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --output-dir planning/repair/reconstruction-gmp63-packaged-cli`：26 通过/0 失败，日志 `planning/repair/reconstruction-gmp63-packaged-cli.log`。
- `ORCA_RECONSTRUCTION_BINARY` 指向该打包路径，测试 `[ReconstructionIntegration]`：1 用例/22 断言通过，日志 `planning/repair/reconstruction-gmp63-packaged-integration.log`。
- `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess],[ReconstructionWorkerGuard]`：30 用例/680 断言通过，日志 `planning/repair/reconstruction-gmp63-postbuild-core.log`。
- 平台配方测试 `python3 scripts/tests/test_gmp_platform_selection.py -v`：10 用例通过；文档全量与差异检查通过。

CUA 全局状态请求超时，按包路径连接出现 `AXError.cannotComplete`；后续应用清单可以读出，但再次获取目标窗口为 `timeoutReached`，无法安全选中/操作特定窗口。首次连接包路径启动了新应用 PID 84168（23:16），并未成功读到其界面。旧 PID 9046 的 `sample` 记录启动时间 17:08、主线程在正常事件循环等待；辅助功能错误不能直接认定为应用卡死，旧进程也不能当成新构建身份。采样保存为 `planning/repair/reconstruction-postbuild-old-gui.sample`。已请用户保存并关闭旧实例，没有强制结束、重置配置或丢弃工程；新 GUI 的采用/取消/Undo/保存重开仍未通过本轮验收。

构建还有旧 zstd 动态链接警告：当前 app 的 `otool -L` 仍指向 `/opt/homebrew/opt/zstd/lib/libzstd.1.dylib`，日志提示该库为 macOS 26.0 构建而目标为 11.3。本次是当前本机功能验证，不是可独立分发、支持旧 macOS 的发行包验收；本轮不扩大范围修改其打包方案。

### Windows/Linux 的准确边界

| 范围 | 已核对 | 尚不能下的结论 |
| --- | --- | --- |
| 本次 GMP 版本选择 | 新版本只在 Apple arm64 条件内；Linux 保留旧 6.2.1 URL/hash/补丁，MSVC 保留原 x64/x86 `.lib/.dll`；CMake 条件矩阵通过 | 不是 Windows/Linux 应用已构建/运行通过 |
| macOS 列表修正 | `mac_enable_object_list_editing` 声明/调用有 macOS 宏保护，`.mm` 是 Apple 源；非 Apple 不进入 AppKit 实现 | 不等于新重建 GUI 的全部跨平台行为已验 |
| 共享重建 | CGAL 仍固定 5.6.3，AlphaWrap 与检查源码属于共用核心；同一有效参数和严格输出校验不因平台放宽 | macOS 结果不能保证 MSVC/GCC 编译或所有数值边界一致 |
| Windows worker | UTF-8→UTF-16/参数转义、挂起创建后加入 Job、内存/时间限制、父身份和退出监测已存在；libslic3r 和测试 helper 已链接 Psapi | Job 限制、取消、中文路径、父进程退出需 Windows 实测；部分 POSIX 测试在 Windows 不编译，不能按 Mac 用例数宣称覆盖 |
| Linux worker | posix_spawn 独立进程组、waitpid 回收、`/proc` RSS、worker 内 RLIMIT_AS/PR_SET_PDEATHSIG 已按平台分支接入 | 实际虚拟地址空间预算、发行环境、取消/父退出仍需目标系统验收 |

额外关注 Windows 的既有启动包装层 `src/OrcaSlicer_app_msvc.cpp`：带 GUI 的 exe 在进入 DLL 的 `CLI::run` 及 `--reconstruction-worker` 分流之前仍做 OpenGL 探测/可能加载 Mesa；因此当前不能声称 worker 在 Windows 启动全过程都无图形依赖。`orcaslicer_main` 还沿用既有分配失败处理；受 Job 内存限制时的失败分类和是否弹原生错误框需实测。上述是具体风险/覆盖空白，不是已在 Windows 复现的故障，本轮未擅自改启动器。

本机 `docker` CLI 存在，但 `desktop-linux` 的 Unix socket `/Users/liwei/.docker/run/docker.sock` 不存在；没有可用 Linux 运行环境。未获得 Windows 编译/执行环境，本轮不启动虚拟机、不安装工具链。结论为“GMP 平台选择保持原行为；整套新增重建尚未跨平台验收”，不能承诺无问题。

目标系统最小验收：使用各自 Release 构建运行 `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess],[ReconstructionWorkerGuard]` 和指定实际 exe 的 `[ReconstructionIntegration]`，执行重建 CLI 26 项；GUI 各验证中文/空格路径、正常候选、取消、超时/内存受限失败、采用后的 Undo/Redo/保存重开。分别记录编译器、CGAL/GMP 版本、二进制身份及失败码；Linux 特别确认默认 1024 MiB 预算对真实应用有效，Windows 特别确认 Job/启动包装层未阻塞 worker。任何缺项保留待验，不用静态检查替代。

## 2026-10-06 macOS arm64 GMP 正式修正

### 范围、来源和影响

用户授权后只改 `deps/GMP/GMP.cmake` 的平台定向依赖选择，另加 `scripts/tests/test_gmp_platform_selection.py` 和相关模块/验证记录。基准 `master / 34477d7411...` 未变，所有既有重建实现保留；本批没有再改 `MeshBoolean.cpp`、检测定义、修复/重建算法、用户预设或后端。

macOS arm64 固定为 [GMP 6.3.0 官方源码](https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz)，SHA-256 `a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898`，与已安装 Homebrew 配方记录的同一上游包 hash 相符，CMake 解包时再次校验。沿用用户已有系统代理完成下载，没有修改代理设置或关闭 TLS 校验。源码按对应版本正常提取/编译，没有修改第三方源文件、没有把 Homebrew 库复制进项目。此平台不应用旧 GCC15 补丁，Apple Clang 的配置/构建及自检实际通过。

版本由目标 `CMAKE_OSX_ARCHITECTURES` 决定，未设置才使用 `CMAKE_SYSTEM_PROCESSOR`。Intel 宿主→arm64 目标使用新版本；arm64 宿主→Intel 目标仍用旧版本。Linux 各架构和 macOS x86_64 保留原 6.2.1 URL/hash/补丁，Windows MSVC 保留原预编译 `.lib/.dll` 路径。10 项 CMake 条件测试覆盖这些组合（Windows 测试含 x64/x86），没有在 Linux/Windows/Intel 设备运行应用。

图谱审查：CodeGraph MCP 未暴露，CLI 存在但本地索引早于新重建文件；understand 图谱仍为 `60148c114...`，没有 GMP 配方或新测试节点，未作为当前调用关系证明，也未生成误导性的完整影响覆盖。实际依赖路径按当前 CMake 和 Ninja 链接命令核对为项目 arm64 前缀；GMP 影响 GUI/CLI 共用 CGAL 核心，因而不仅重跑重建，也回归原修复/检测/切片。

### 正式构建身份

- `cmake --build deps/build/arm64 --target dep_GMP --parallel 2`：通过，日志 `planning/build/reconstruction-gmp63-dependency-20261006.log`。
- 在 `deps/build/arm64/dep_GMP-prefix/src/dep_GMP` 执行 `make -j2 check`：总计 200、通过 199、跳过 1、失败/错误 0，日志 `planning/build/reconstruction-gmp63-check-20261006.log`。`t-addaddmul` 因当前架构无 `HAVE_NATIVE_mpn_addaddmul_1msb0` 按上游测试逻辑返回 77，不算通过。
- `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2`：通过，日志 `planning/build/reconstruction-gmp63-app-20261006.log`；重编译受 GMP 头文件影响的 CGAL/几何对象并重新链接，不是只换测试路径。
- 项目 `deps/build/arm64/OrcaSlicer_dep/usr/local/include/gmp.h` 为 6.3.0；同前缀 `lib/libgmp.a` SHA-256 `29ee4f13580626a4818610b1e293532a70d156955c344a443fabcbec50a3e3a5`、`lib/libgmpxx.a` 为 `603de4f17cb4791ff31fdbdd649e40c60e05ec63996bff34ba5c2731728c7d47`。`llvm-objdump` 检查项目 libgmp 的 x18/w18 指令引用为零。
- 新直接应用 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` SHA-256 `5758588c2673568579637ba1dd685ffe9c8d87120848c70108ceb95888c13a2f`；测试 `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` 为 `d2ef457d6a4607ba36d717cf16aa21bca00e3a28e3defef68084b00673cfa60c`。
- `build/arm64/OrcaSlicer/OrcaSlicer.app` 打包副本仍是 `b3dc2aa1...`，本批没有打包/发布。仅 `-s` 构建应用不会升级旧机器上的 GMP：其他 arm64 工作区须先重建 `dep_GMP` 或完整依赖再构建应用；当前工作区已完成，无需再用 Homebrew 替换。universal 脚本会逐架构构建，本轮未验 universal 包。

### 验证结果与残留边界

证据根目录为 `planning/repair/`，下列均使用上述新应用/测试身份；历史失败日志保留，不用单次通过覆盖旧异常。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| `[MeshReconstruction]` 200 次重复、逐次 10 秒 deadline | 200 次退出 0、无超时；总计约 40.7 秒 | `reconstruction-gmp63-project-stress/summary.json` 和逐次日志 |
| 重建、进程、原修复、保色、OBJ/STL 合并标签 | 220 用例/15254 断言通过 | `reconstruction-gmp63-core.log`，标签与上个 build3 相同 |
| 显式实际 worker 集成（`ORCA_RECONSTRUCTION_BINARY` 指向新应用） | 1 用例/22 断言通过 | `reconstruction-gmp63-integration.log` |
| 独立重建 CLI | 26 通过、0 失败/跳过 | `reconstruction-gmp63-cli/summary.json` |
| 原组合检测 CLI | 76 通过 | `reconstruction-gmp63-model-check.log`；临时证据 `orca-model-check-cs7q3hhb` |
| 原修复/试切片 CLI | 78 通过 | `reconstruction-gmp63-repair-slice.log`；临时证据 `orca-repair-acceptance-dggt0hp9` |
| 同一批 21 个模型重新重建，实际 STL 和精确 float OBJ 独立复检 | 全部七项完整通过；输入 hash 未变；候选 STL 与前次 `reconstruction-defect-first-models1` 逐字节一致 | `reconstruction-gmp63-models/{identity,results,suite-summary}.json` |
| 以前高级修复失败的 8 件同参诊断切片 | 5 成功/3 失败，和前次一致；成功件全部有逐层挤出，无异常空层 | `reconstruction-gmp63-slices/summary.json` |
| 额外 `[Geometry],[its],[VoronoiOffset],[VoronoiOffset4],[VoronoiOffset5]` | 35 用例：34 通过、1 失败；939 断言中 7 失败，不能称全量通过 | `reconstruction-gmp63-geometry.log`，旧二进制对照见下段 |

额外失败为 `Scenario: Polygon convex/concave detection`，位于 `test_geometry.cpp:385/389/402/414/426/439/456`。升级前保留的诊断测试程序 `planning/repair/reconstruction-asan-local/libslic3r_tests_asan`（18:50 构建、SHA-256 `15eaa3eea8c258723ef16993cb28cf794895687d2733053aa00c8e8d0865c3c3`）复现完全相同 7 个断言，日志 `reconstruction-gmp63-geometry-baseline.log`。当前 `Polygon.cpp` 的该逻辑为普通浮点角度筛选，不调用 GMP，两源文件均无工作区差异；本批不修这个独立旧问题。额外测试生成的 `one_vertex_touch.svg` 已移入 `planning/repair/reconstruction-gmp63-one-vertex-touch.svg`，可恢复，未留仓库根目录或删除用户文件。

8 件切片使用上节记录的 EP3 Plus 同一隔离配置（无支撑、首层 0.24 mm、无后处理）。09 和 folded 仍无首层路径；孤立三角形重建件仍约 0.1 mm 厚，低于首层。这三件返回 -100，不伪装为可打印；61460/f79 成功切片仍有悬空提示。GMP 修正消除的是已复现的底层兼容风险，不恢复设计结构、不消除切片工艺限制。

`python3 scripts/check_ai_module_docs.py --all` 和 `git diff --check` 通过。GUI 新交互、其他平台运行与实机打印均未验，见[更新交接清单](2026-10-05-reconstruction-gui.md)；本批未暂存、提交、推送或发布。

## 2026-10-06 缺陷优先重建：实现及验收

基准 `master / 34477d7411f076f4c9fe9200fcfc69c9253b0293`，保留此前未提交重建及对象列表修复；未修改旧 `MeshBoolean.cpp`、`ModelRepair.cpp`、七项检测定义、用户预设、后端、无关教材文档或 result.json，未暂存／提交。

### 已实施内容

- 共享 `reconstruct_mesh` 不再先要求输入闭合、单壳、无贯通孔或只有微小开口；精确焊接后移除零面积及重复三角形，丢弃只被移除面引用的顶点。非流形、自交、大开口、嵌套／相交壳体及薄面均可尝试。
- AlphaWrap 按原值、2 倍、4 倍最多三档，全部共用一个 worker 的资源／时间预算。只有受控预算拒绝和候选不合格可继续尝试，崩溃、监测失败、取消不重启。
- 输出及变换后的实际 float 网格必须闭合、流形、方向正确、无自交、正体积且七项检查完整通过；检测未知／未完成不算成功。结构变化不再构成拒绝，采样距离改为告警；CLI 参数名保留但帮助、报告和 GUI 一致标明新含义。
- GUI 展示有效参数、偏差告警与孔／腔／薄壁可能变化的风险；保持预览、明确采用、原件保留和 Undo/Redo。`trial_slice=not_run`、`print_ready=false` 未改，自动诊断切片尚未接入。
- POSIX 父进程 RSS 读取瞬时失败增加最多两次 20 ms 的退出复查；连续第三次仍报监测失败，取消／超时照常检查，不把未知内存视作零。

### 构建和自动化证据

使用 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2`；build1/2 均成功，日志在 `planning/build/reconstruction-defect-first-20261006-build{1,2}.log`。build2 只增加 GUI 正参数防护及两个最终壳体方向测试，共享重建核心与 build1 相同。最终直接应用路径 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` SHA-256 `64630f693d5eec31c21ec1951da7b486f1fafcc0385b8a8733a8d247e11731d9`；测试路径 `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` SHA-256 `73792ce1c498ee4a8fb18ba77645234da5b715cb2955134cf6754ad90f734f4a`。打包目录没有自动同步。

以上为 build2 阶段身份。随后 build3 仅补输出检查返回 `engine_error` 时立即失败、不得作为候选几何拒绝进入下一档。相同构建命令成功，日志 `planning/build/reconstruction-defect-first-20261006-build3.log`。**本轮最终直接应用** SHA-256 `6647c0b0d3cf1c2f1f474826b0a9a36ead28a202eb2103b0f37ff653d193f661`，测试 SHA-256 `3308c3e06c0bffd26d9a4eb20dd925783f122945117584d4309e95b55f310166`；打包副本仍未更新。最终合并标签 `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess],[ReconstructionWorkerGuard],[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]` 为 220 用例／15254 断言通过（标签重叠，不与下表相加），CLI 26 场景通过，worker 集成 1 用例／22 断言通过。证据是 `planning/repair/reconstruction-defect-first-build3-{core,cli,integration}.log` 和 `reconstruction-defect-first-build3-cli/summary.json`。下表保留早期分组和真实模型运行身份，不能将原生压力异常计为通过。

| 验证 | 实际结果与证据 |
| --- | --- |
| 最终重建四组 C++ 标签 | 30 用例／680 断言通过，`planning/repair/reconstruction-defect-first-core-final.log`；不能覆盖下述压力异常 |
| 最终真实 worker 集成 | 指定 `ORCA_RECONSTRUCTION_BINARY` 为上述直接应用，1 用例／22 断言通过，`reconstruction-defect-first-integration-final.log` |
| 最终重建 CLI | 26 通过／0 失败／0 跳过，`reconstruction-defect-first-cli-final/summary.json`；独立 schema、已存在目录、错误索引／NaN、超时及候选实际检查 |
| 原修复核心（build1，同共享核心） | `[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`：205 用例／14711 断言通过，`reconstruction-defect-first-existing-core1.log` |
| 原 check-model／repair 切片（build1） | 76／78 场景通过，`reconstruction-defect-first-check1.log`、`reconstruction-defect-first-repair-slice1.log`；证据目录分别为临时 `orca-model-check-b1500ul5` 与 `orca-repair-acceptance-g4d4ex3j` |
| 21 个重建输入（build1） | `planning/repair/reconstruction-defect-first-models1/{identity,results,suite-summary}.json`，每件含原输入 hash、独立 worker 报告、实际 STL 和原始 float OBJ 的独立检查；21 件均完整七项零缺陷，输入未改 |
| 8 件诊断切片（build1） | `planning/repair/reconstruction-defect-first-slices1/summary.json`；旧高级修复失败样本中 5 成功、3 失败，详见下一节 |
| 翻译／文档 | `msgfmt --check-format` 简中校验通过；模块指纹与全量文档检查在本轮收尾执行。GUI 新版、Windows/Linux、实机仍待验证 |

### 修复收益和切片边界

以前高级修复失败的 `08-intersecting-shells`、`09-nonmanifold-edge`、`10-only-triangle`、`375236`、`37825`、`61460`、`folded-default`、`f79-large`，这次全部在默认 alpha 0.5／offset 0.05／告警 0.25 mm 下产生可复检候选。不是把拒绝条件删掉后直接报成功：实际序列化 STL 和不经导入清理的 float OBJ 均重新检测，完整七项为零。原件及修复前报告保留；健康薄体对照不计为“新增修复成功”。

诊断使用隔离数据目录及 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/` 的 `machine_settings_1.config`、`process_settings_1.config`、`filament_settings_1.config`，EP3 Plus／0.4 mm／101×101 mm 平台／105 mm 高度，层高 0.2、首层 0.24 mm、不启用支撑，无后处理脚本，不覆盖预设或执行打印。5 件成功的有效挤出层数依次为 08：50、375236：32、37825：116、61460：212、f79：475；均无空挤出层。61460 有悬空悬臂警告，f79 有悬空区域警告，仍需摆放／支撑确认。09 和 folded 首层没有可用路径，孤立单三角形候选约 0.1 mm 厚，小于 0.24 mm 首层，失败返回 -100。故七项网格归零目标在本批得到改善，不代表所有候选能按同一工艺打印。

三份配置 SHA-256（machine／process／filament）：`525f96226d82b47a33e5b6ee69bbda6d3291deb8855e967b013764b0d30df9ab`／`9f5cd82ce8a5f33995bb38cc36dab0677a225d43bcddbeeffc2ff1c60fb762ec`／`778e0f655f7c5895db968ed9e733b572b52be1ab4dd2cf75ea288e153951255f`。

### 压力测试异常（阻断稳定性结论）

首轮 core1 在 CGAL 精确谓词运算时卡住，保留 sample 后结束已核实的测试进程；core2 重跑成功不能抹去此失败。30 次重复通过后，第二批在第 126 次超时（此前 125 次通过），`planning/repair/reconstruction-defect-first-stress2/125.sample` 显示 `CGAL::Mpzf::aors`／Catch 信号处理；该批跨过测试二进制重新链接，只作辅助证据，不当作固定身份的整批通过。最终固定测试二进制在 LLDB 重复启动，第 57 次抓到真实 `EXC_BAD_ACCESS`，`planning/repair/reconstruction-lldb-stress.log`：`__gmpn_sub_n +136` 写越界，x3=4，但 x18 循环计数已异常为负，栈遭破坏。

已核对现有 GMP 6.2.1 arm64 静态库确实在加减汇编使用 x18；[GMP 官方已知问题](https://gmplib.org/#STATUS)明确该版与 Apple 保留 x18 的冲突，[官方后续修正](https://gmplib.org/list-archives/gmp-commit/2020-November/003063.html)及 [6.3.0 说明](https://gmplib.org/list-archives/gmp-discuss/2023-October/006934.html)说明已避开该寄存器。这是当前明确的依赖兼容缺陷，不能用重建形状阈值修补。独立 AlphaWrap ASan 1000 个随机种子及局部 ASan 核心 100 次未报错，但不覆盖未插桩 GMP 汇编，也不推翻原生复现。所有诊断文件在 planning，未改第三方快照或安装新依赖；已询问用户是否升级固定 GMP 并回归，确认前保留稳定性阻塞。

进一步做了隔离链接对照：沿用 build2 的原生对象文件，仅将 libgmp.a/libgmpxx.a 指向本机已有 `/opt/homebrew/Cellar/gmp/6.3.0/lib/`，输出到 `planning/repair/reconstruction-gmp63-local/libslic3r_tests_gmp63`（SHA-256 `d69fde63c58963878e6a48fbb4c8fd58c8023b81a696076b886400425603c600`）。`[MeshReconstruction]` 连续 200 次全部退出 0，`planning/repair/reconstruction-defect-first-gmp63-stress/summary.json` 及逐次日志保留。对两个静态库执行 llvm-objdump：项目 6.2.1 的 libgmp 有 155 处 x18/w18 引用，Homebrew 6.3.0 为零。对照支持修正路线，但不是正式依赖重建／发布验证；没有把 Homebrew 库复制到项目，没有升级系统包或改 CMake 配方。官方/GNU 镜像源码下载尝试因连接超时失败，未据此绕过完整性验证或引入不明源码。

收尾检查：`python3 scripts/check_ai_module_docs.py --all`（35 模块、0 问题）、`git diff --check`、简中 `msgfmt --check-format` 均退出 0。未暂存、提交、推送或发布；这些静态检查不解除上述依赖稳定性阻塞。

## 2026-10-06 重建修复重新定位（历史审核方案，后续实施见上节）

### 已确认目标与原因

用户本轮要求先研究优化：重建修复必须对高级修复失败的模型产生实际补救作用，优先消除检测缺陷，暂不以保形、保孔及保结构作为硬门槛。用户所说“死角”暂按自交／非流形／开边等网格缺陷理解；悬垂、支撑死角和薄壁不可打印属于另一个检测维度，不能承诺仅靠网格重建消除。

源码核对表明，当前 `MeshReconstruction.cpp` 在 AlphaWrap 调用之前要求输入无退化、能组成规则 surface graph、能通过小开口闭合、单一 genus-zero 壳及正代数体积；输出还受 `0.25 mm` 默认偏差、体积比和原内部见证点限制。上述条件是旧保守设计，不是 AlphaWrap 的全部输入要求。折叠管默认候选已通过内部几何检查，最终因采样偏差约 `0.4152 mm` 被拒绝；另多件复杂输入根本未进入重建。因此现有证据不能证明算法本身不能解决这些问题，但确实证明当前产品链路没有完成兜底目标。

已核对本地依赖为 CGAL 5.6.3-I-900；[同版本官方说明](https://doc.cgal.org/5.6.3/Alpha_wrap_3/index.html)支持非流形、自交、重叠等 triangle soup，并说明输出闭合流形及双侧薄包覆的局限。本地 `Triangle_soup_oracle.h` 跳过退化三角形，不要求坏输入先成为有效闭合曲面。历史 understand 图谱没有新增重建节点，CodeGraph MCP 未暴露；本轮以当前源码及固定版本资料为准，没有初始化／重建图谱。

### 建议修改边界

1. **宽进、严出，不放宽公开检测。** 输入只拦截非法索引、非有限坐标、空几何及资源超限；记录并清理精确退化／重复面，若清理后无可重建三角形则明确失败。不强求原件闭合、单壳、朝向一致或无自交；二维三角面允许作为加厚重建输入，不能假装恢复了原始厚度。直接将有缺陷的三角面集合交给 AlphaWrap，不依赖先建立可靠原实体。
2. **有效性与保真分离。** 撤除保形用途的单壳／genus-zero、开口大小、偏差、原体积比、原内部采样保留等硬门槛。合法贯通孔及多个互不相交的闭合实体本身不是缺陷；输出仍必须无退化／重复／非流形／自交／开边，具有正确的实体边界朝向及有限正实体体积。尺寸、部件数、表面偏差记录为结果说明；源实体定义不可靠时不计算冒充真实体积的变化百分比。不会修改原高级修复的保形规则。
3. **有限重建尝试。** 按物理尺寸和资源预算设置经过样本校准的参数档，初次失败时最多尝试 3 档并共用总预算；不是把原 `0.25 mm` 随意放大，也不是无限循环。alpha 控制可进入的开口／细节尺度，offset 控制包覆距离；更粗档允许封口、合并交叠壳体及损失小细节。具体默认数值需由失败样本实测决定，不在未验证前宣称统一适用。输入错误、取消和进程监测异常不作为几何失败自动换参数重试。
4. **最终产物独立复检。** 保留浮点输出、单位／变换往返、序列化身份及资源校验；对实际交付网格执行现有七项检查，全部完成且归零才标记网格修复成功。不能依赖 STL 再导入时的隐式自动修复获得假通过；超限／未完成必须保持未知。GUI 与 CLI 接共享核心和相同有效策略，保留原件、比较、显式采用及 Undo，不静默删除绘制或改用户预设。
5. **单独记录切片可用性。** AlphaWrap 可能得到拓扑干净但很薄的双侧包覆；不能靠原坏模型的内部见证点推断正确实体。对交付候选补诊断试切片，至少发现空路径、异常空层和过薄结构的路径丢失。网格通过但试切片失败须分开报告；相同配置及显式摆放条件下才比较结果，不自动认定可打印、无悬垂或无需支撑。形状变化不是本轮拒绝条件，原件和变化说明仍保留。
6. **稳定性与收益一起验收。** 排查两次 `monitor_failed`，区分 worker 已退出、监测不可用和真实超限，不能忽略真实监测故障或把它计作几何拒绝。先用高级修复失败的 8 件固定样本作准入集，再回归其余样本；逐件列原缺陷、高级修复残留、重建后七项／完整性、耗时／资源及结构变化。单三角面单列为加厚场景，不拿它代表复杂实体修复能力。允许失败，但不能把失败候选算成功；没有真实新增收益不接入为可用兜底功能。

本轮推荐先验证现有 CGAL 的直接 triangle-soup 路径，不升级依赖、不先引入 Blender／体素／AI。若上述失败集仍有明确不能覆盖的类别，再用同套验收比较第二引擎。算法文档描述不等于本应用、有限精度和资源限制下对所有模型的成功保证；不采用凸包／包围盒替代主体或删空模型来刷检测通过率。

本节只记录设计依据和拟改边界，业务源码、测试、默认参数、协议及构建均未变；不能用先前测试证明拟议新版通过。

## 2026-10-06 更多缺陷模型：独立复检与结构对照

### 输入、方法与证据边界

- 分支/HEAD 未变，使用已核对的打包应用 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA-256 `b3dc2aa1457f47b84a269c998399b4b2c3d2a0833cdec9bd2cea704eb459fcd3`；没有修改业务源码或重新构建。
- 证据根目录 `planning/repair/reconstruction-more-models-20261006-ghtdBE/`：`identity.json`、`summary.json`（首批 19 件）、`additional-summary.json`（自交管和 f79 大模型）、`advanced-summary.json`、`structure-summary.json`、`fallback-summary.json`、`monitor-retries.json`。每件子目录含命令、stdout/stderr、退出码、独立输出 JSON 和实际模型。19+2 是样本数，不把重复检查/重试累计成更多独立样本。
- 使用 `tests/data/model_repair_acceptance/01` 至 `10`（07 为健康薄实体对照），`tests/data/model_repair_topology/bracket.stl`（9 月原始托架，862 面）、已有七件 `planning/repair/repair-test-models/{100026,100173,101951,375236,37825,61460,68246}.stl`、已有 `folded_tube.stl` 和 Downloads 下 `f79ce30d-6b4f-4e32-bcbf-737f6dbf3e29.stl`（103674 面）。另按现有单元测试 `cube_with_small_opening()` 的几何生成 2 mm 小缺口夹具，17 面、缺口包围盒对角线约 0.141421 mm；不是下载的真实模型。所有原输入末次 SHA 与本轮初始一致。
- 默认重建命令：`<binary> --reconstruct-model --accept-reconstruction-risk --reconstruction-timeout 60 --outputdir <全新目录> <source.stl>`。alpha/offset/偏差均保持 `0.5/0.05/0.25 mm`，不放宽阈值争取成功。普通高级修复对照：`<binary> --check-model --repair-model cgal --export-stl --outputdir <全新目录> <raw-exact.obj>`；额外两件使用原 STL，分别记录，不与原始 OBJ 检查混算。
- `--check-model` 会走原生导入；因此首批原 STL 另生成保留三角坐标/顺序的无色 OBJ 分析副本，避免把 STL 自动修复效果算给高级修复/重建。成功导出均重新做 STL 检查，另由实际 STL 顶点重建 OBJ 再独立检查七项及 `check_complete`；没有调用高级修复来“复检”。生成候选的内部完整浮点几何验收与公开检测分开记录。
- 几何辅助统计按精确同坐标连边计算尺寸、面数、连通部件和面积；原始退化面参与这些自算计数，不代替公开七项定义。表面距离为顶点/边中点/面心采样，不能称最大 Hausdorff 保证。原件有开口、自交或朝向冲突时，代数体积不视为真实实体体积。只在已知闭合参考方块上计算重建体积增幅。
- 本轮是同共享核心的 CLI/文件验证，没有新的 GUI 实操、诊断试切片、打印或模型设计者确认。没有覆盖原工程/预设，也没有提交、推送或改检测协议。

### 检测与两类修复结果

下表“通过”均指本轮模型导出后完整七项归零，**不代表结构不变或可打印**。“未解决/失败”不交付为成功修复件。`07` 是对照，不计为修复了一个坏模型。

| 输入 | 原始主要问题 | 现有高级修复及独立复检 | 默认重建 |
| --- | --- | --- | --- |
| 01 缺顶方块 | 4 开边 | repaired；通过，10→12 面 | 大开口拒绝 |
| 02 微缝 | 4 开边、1 自交 | repaired；通过，12 面 | 开口拒绝 |
| 03 反向面 | 9 局部朝向冲突 | repaired；通过，12 面 | 拓扑拒绝 |
| 04 重复面 | 1 重复组、3 非流形边/点 | repaired；通过，13→12 面 | 拓扑拒绝 |
| 05 退化面 | 2 退化面 | repaired；通过，14→12 面 | 退化输入拒绝 |
| 06 内部孤立面 | 3 开边 | repaired；通过，13→12 面 | 开口拒绝 |
| 07 健康薄实体 | 七项本就为零、两个实体 | not_needed；通过，24 面/两个实体保留 | 多壳拒绝 |
| 08 相交双壳 | 52 自交 | unresolved；仍为 52 自交 | 多壳拒绝 |
| 09 共边非流形 | 1 非流形边、2 非流形点 | unresolved；问题不变 | 拓扑拒绝 |
| 10 单三角面 | 3 开边、无实体 | failed；保留基础输入 | 尺度/非三维拒绝 |
| 小缺口方块 | 3 开边，2 mm 方块上约 0.141 mm 缺口 | repaired；通过，17→18 面，尺寸 2 mm | candidate_ready；通过，17→270 面，尺寸约 2.100 mm |
| 用户托架 | 5 开边、10 非流形边/点、28 自交、3 退化、49 朝向冲突 | repaired；通过，862→856 面 | 退化输入拒绝 |
| 100026 | 27 开边、16 非流形边、17 非流形点、41 自交、11 退化、17 朝向冲突 | repaired；通过，270→262 面 | 退化输入拒绝 |
| 100173 | 4 开边 | repaired；通过，250→252 面 | 大开口拒绝 |
| 101951 | 36 局部朝向冲突 | repaired；通过，168 面/两个部件保留 | 拓扑拒绝 |
| 375236 | 8 自交 | unresolved；仍为 8 自交 | 壳体/handles 拒绝 |
| 37825 | 1 非流形边、2 非流形点 | unresolved；问题不变 | 拓扑拒绝 |
| 61460 | 2 重复组、4 非流形边、6 非流形点 | unresolved；问题不变 | 拓扑拒绝 |
| 68246 | 3 开边、1 退化、7 自交 | repaired；通过，78 面 | 退化输入拒绝 |
| folded_tube | 6 自交 | unresolved；仍为 6 自交 | 生成的内部候选偏差超限，不导出 |
| f79 大模型 | 589 自交、2 非流形点 | unresolved；问题不变 | 拓扑拒绝 |

现有高级修复：12 repaired + 1 not_needed；13 件独立 STL 和原坐标 OBJ 复检均 `mesh_status=passed`、`check_complete=true`、七项 count=0/complete=true。本批没有 `partially_repaired`；另外 8 件为 unresolved/failed，不能表述成已修好但可以忽略剩余问题。

默认重建：1 candidate_ready + 20 拒绝，仅小缺口样例产生可供人工确认的候选。此前 folded_tube 曾使用 `0.25/0.025/1.0 mm` 的专门测试参数成功，本轮默认参数下采样距离约 `0.2960/0.4152 mm`，超过 `0.25 mm` 被拒绝；不是同参数结果互相矛盾，也不能把此前非默认成功推广到默认表现。

对 8 件高级修复失败模型，又使用其工作流保留的 `prepared` 基础副本尝试默认重建；不是选择磁盘上任何失败候选继续用。若基础副本为 OBJ，仅按其三角坐标转成无色 STL，不额外清理或减面。最终均没有候选，其中两个首轮出现下面的监测异常，不能将这些运行记成正常几何拒绝。

### 结构变化的实际证据

- **小缺口重建**：开边 3→0、单一闭合壳，独立七项全零；尺寸由 2×2×2 到 `2.100274×2.100000×2.100000 mm`，边界最大变化约 `0.050274 mm`；相对于应有完整方块的 8 mm³，候选约 `9.126683 mm³`，增大约 **14.08%**。网格变为 270 面，不只是给 17 面原件补一小片。现有高级修复在同一例只增 1 面到 18 面、尺寸仍 2 mm。这直接证明“网格合格”不能替代尺寸/结构保真验收。
- **托架高级修复**：862→856 面，426 个几何顶点、包围盒 `135×102.5×70.000175 mm` 和一个连通部件保持；采样候选→原表面约 0.0000038 mm，原→候选约 2.4786 mm（存在被移除的内部面，不能用这一个方向直接说外轮廓缩小）。沿 X 的射线在孔中心 Y=30/Z=44、45、46 均无交点，邻近板材 Y=25/Z=45 及 Y=30/Z=40、50 保留 X=0、5 的交点。可证明抽查的原贯通孔仍通、邻近板材仍在，不是全部特征尺寸认证。
- **真实 100173**：4 开边→0，250→252 面，补了缺口表面，部件仍 1；包围盒最大变化约 0.0000038 mm，新增表面采样到原表面最远约 0.17517 mm。外部尺寸近似保持，但“开口变封闭”本身是结构修改，是否符合设计须看原件用途。
- **真实 101951**：168 面/2 部件保持；主要修正面朝向，双向采样表面距离小于 0.000001 mm，没有合并两个部件。原代数体积因反向面不可靠，不以体积数值变化断言模型被填实。
- **真实 100026 / 68246**：分别 270→262、78→78 面，均复检通过；最大包围盒变化分别约 0.00000017 / 0.00000147 mm。100026 存在表面删除/整理，原→候选采样距离约 1.25 mm，不能只凭外尺寸相同声称全部内部结构未变；68246 的双向采样距离约 0.0000016 mm 范围，不代表全部设计语义已认证。
- **退化/孤立面夹具**：05 包围盒 X 从 20→10 mm，原因是删除伸到 X=20 的零面积面，原 10 mm 立方体实体面积/体积保持；06 删除内部孤立单面，连通部件数 2→1，立方体主体仍 10 mm。此类变化是已知夹具的预期清理，但不能推广成真实模型中任意小部件均可删。
- **薄实体对照**：24 面/2 个实体保留，未删除 0.001 mm 薄实体；没有试切片，不能据网格健康推断普通喷嘴/层高能打印它。

### 新发现：间歇性 worker 内存监测失败

- `08-intersecting-shells/retained_baseline_reconstruction` 和 `375236/retained_baseline_reconstruction` 首轮分别约 102/93 ms 返回 `monitor_failed`，消息 `Cannot observe reconstruction worker memory usage.`。退出 2、无可交付候选，未替换/覆盖原件；这两次属于软件执行异常，不是模型拓扑结论。
- 同参数、同基础副本各追加 3 次独立重试，6 次均返回相应 `unsupported_shells`；保留首次异常，不能用重试覆盖失败历史或推导发生概率。没有自动放宽参数。
- 源码定位 `ReconstructionProcess.cpp` 的 macOS `proc_pidinfo` RSS 读取失败路径：已有一次重新 poll，但本次仍走监测失败。短生命周期进程结束窗口是需要进一步核对的可能原因，尚无进程级证据证明唯一根因；不直接声称一定是退出竞态。需要后续专项修正和回归；本次测试授权没有扩大为修改进程代码。

### 本轮结论与下一步

原高级修复对多种基本缺陷及部分真实模型确有完整七项归零的结果，同时真实复杂自交/非流形仍有失败。独立重建当前限制很窄：本批没证明它能补救原高级修复的失败模型；成功的小缺口还不如原高级修复保形。不能据候选内部检查通过就说功能目标完成，更不能自动打印。优先排查监测异常，再用用户实际失败类别决定是否扩展重建支持范围；任何范围变化须另行设计/授权，不能靠放宽误差掩盖问题。

本轮记录更新后 `python3 scripts/check_ai_module_docs.py --all` 通过（35 模块、0 问题），`git diff --check` 通过；没有暂存、提交或修改业务源码。

## 2026-10-06 用户打包后的 GUI 验证与后续闪退修复

### 17:22 重新打包后的专项复验（完成）

- 身份：分支/HEAD 未变；新打包应用与直接产物 SHA-256 同为 `b3dc2aa1457f47b84a269c998399b4b2c3d2a0833cdec9bd2cea704eb459fcd3`。只操作用户新启动的打包应用 PID 12945（17:22:24 启动），未操作旧直接产物 PID 9046 或 `/Applications` 实例。日志 `planning/debug/orca-user-gui-run-20261006-172220-530984.log`；主要 GUI 操作 17:23—17:35，复核至 17:36。没有重新编译；同一 SHA 的 26 项 CLI 回归可复用，不计为新跑一轮。
- 输入及配置：复用下方已落盘的方块采用工程与自交落床工程。EP7 0.4、光滑高温打印热床、QIDI PLA Rapido @EP7 0.4、0.20mm Standard @EP7；简中/亮色、层高 0.2、填充 15%、墙 2、支撑关闭。测试未改设备/耗材/工艺预设，未打印、上传或覆盖原工程。
- 对象列表：展开并读取盘/模型/盘外行无副作用，单击盘行的空落床列不崩溃、不增加未保存标记；双击模型名称改为 `packaged-regression-cube` 并回车实际生效，然后撤销。打印开关关/开、主动落床、Undo 恢复沉床提示、Redo 再落床均正常。日志只有 17:25:11 的主动操作产生 `Shift objects to bed`；随后的工程读取/重开没有再次产生该快照。
- 保存及重开：测试另存为 `planning/repair/packaged-gui-regression-20261006-1722.3mf`，SHA-256 `01e052fe0824ee26652c2fa638968c6b8258804b24eb8822e913b83da8b9e16f`。1826 顶点/3648 面、printable=1 不变，最低世界 Z 从 -0.05000019 到 0，X/Y 序列化差小于 0.00000005 mm；`Metadata/project_settings.config` 全键比较无差异。保持对象列表可见，17:29:20 打开自交落床工程、17:29:52 再开原方块工程，均完成且无闪退，方块名称/打印开关/原沉床提示保留。两个原工程 SHA 与下表相同。
- 参数/候选：画布右键进入重建，风险未确认时生成禁用；中文“取消”退出不产生未保存标记。重新进入并确认风险，以默认 `0.5 / 0.05 / 0.25 mm` 生成候选成功；本次输入是已经采用过的约 10.100 mm 方块，不是初始 10 mm STL。候选约 10.200×10.200×10.201 mm、底面 -0.050→-0.100 mm、双向采样距离 0.07099/0.06917 mm、包围盒偏差 0.05030 mm；三截面和未试切片提示可见。点击“保留原模型”后仍为 3648 面/原尺寸，标题无未保存标记；未再次采用或试切片。
- 日志：唯一 error 是可选 3Dconnexion 驱动缺失。另有网页导航/消息、`get_version not supported` 以及工程临时预设找不到父项的 warning；后一项走默认配置基底再应用工程配置，后续 `validate presets success`，保存前后工程参数未变化，不是重建 worker 失败。不能称日志零警告，也未对这些既有外围项作全面修复。至复核时 PID 12945 仍运行，诊断目录只有先前 16:41 的崩溃报告，未见本次新增报告。
- 结论：这次打包已包含闪退修复，专项 GUI 回归通过。只更新验证记录；运算中取消、全部属性/拓扑守卫、完整采用/保存重启链路、英文/暗色、跨平台、真实失败模型收益及实机质量不因此计为通过。应用停在原方块隔离工程且无未保存标记；新增证据留在 planning 忽略目录。
- 记录更新后 `python3 scripts/check_ai_module_docs.py --all` 通过（35 模块，0 问题），`git diff --check` 通过；未暂存或提交。

### 随后授权的修复（已完成局部回归）

- 首批修改：`MacDarkMode.mm` 的 `shouldEditTableColumn` 改为只读查询；实际 `EDITING_STARTED` 回调移到原生 `editColumn:row:withEvent:select:`，先验证原生行列及模型可编辑性。`GUI_ObjectList.cpp` 两处落床入口均检查对象索引范围/空指针，非法目标不取撤销快照；编辑开始时过滤空 item 和受抑制列表事件。重建参数窗口显式使用已有 `_L("Cancel")`/“取消”翻译，不改核心/CLI。
- 鼠标操作补齐：仅在对象列表注册真实单击/双击 target-action，图标与耗材单击、名称双击转入编辑请求，Return/程序性编辑保留。仅移动回调到 `editColumn` 的 build-2 不能覆盖实际鼠标路径，未作为最终版本交付；没有在只读查询中通过当前鼠标事件猜测“是否可以修改”。辅助列表的编辑开始处理本为空，正常原生编辑仍沿用系统路径。
- 构建前核对当前工作目录、arm64、Ninja Multi-Config、Release、资源软链接及没有其他编译进程。命令 `cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 4`：build-1 因私有 `wxDVCNSTableColumn` 不可见失败，改用公开 peer 列查询后 build-2 成功；补齐鼠标动作的 build-3 成功；范围格式化后 build-4 成功。日志为 `planning/build/object-list-edit-20261006-build-{1,2,3,4}.log`。C++ 修改范围运行 clang-format；仓库配置只允许 Cpp，Objective-C++ 格式化工具拒绝该配置，`.mm` 沿用相邻风格人工核对，没有改全局配置或格式化无关文件。既有编译/链接警告仍存在，未宣称零警告。
- 最终直接应用 SHA-256：`b3dc2aa1457f47b84a269c998399b4b2c3d2a0833cdec9bd2cea704eb459fcd3`（build-3/build-4 相同）。Computer Use 测试 PID 9046、macOS arm64/简中/亮色；日志 `/Users/liwei/Library/Application Support/OrcaSlicer/log/debug_Tue_Oct_06_17_08_16_9046.log.0`。没有操作 `/Applications/OrcaSlicer.app` 的另一实例。打包副本 `build/arm64/OrcaSlicer/OrcaSlicer.app` 未更新，不可拿它验证这次修复。
- GUI 回归：17:08 打开已采用方块工程，展开对象列表并多次读取辅助功能树不再触发编辑或落床；双击名称能进入编辑器并取消；打印单击关闭后模型变灰/切片禁用，再单击恢复；显式落床后橙色提示消失，Undo 恢复、Redo 再消失。17:11 打开已采用自交工程，17:12 再开方块工程无闪退、标题无未保存标记、橙色沉床提示仍在。日志唯一 `Shift objects to bed` 位于实际点击的 17:09:50，工程重开/查询阶段不再产生该快照。参数窗口显示“取消”，点击后返回原对象且未新增未保存标记。重命名输入并完成另在 build-2 验证；最终版重点复验了新接入的双击开始/取消。
- 保存证据 `planning/repair/object-list-regression-20261006.3mf`，SHA-256 `0ae4af651ffd32929566566c529e7777d0185feb51f2cd150e649760e0b932c3`。与原采用工程同为 1826 顶点/3648 面、printable=1；世界最低 Z 从 -0.05000019 变为 0，X/Y 边界差小于 0.00000005 mm（序列化精度），符合显式落床。原采用工程 SHA 未变。仅写隔离证据，不覆盖原模型/用户工程/预设。
- 最终同 SHA 应用运行 `python3 scripts/test_cli_model_reconstruction.py --binary build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --output-dir planning/repair/reconstruction-20261006-object-list-regression`，退出 0，26 passed / 0 failed / 0 skipped；摘要与逐例报告在该目录。CLI 无对象列表/取消按钮，对此交互不适用；该回归只确认共享重建入口未被影响，不替代 GUI 验证或实机质量。
- 本次不改旧高级修复/AlphaWrap 算法，不引入 AI，不新增自动采用或放宽候选守卫。图谱对修改文件已过期，使用源码核对；文档检查与图谱独立。已更新 scene-editing/platform 正文及审核指纹；`python3 scripts/check_ai_module_docs.py --all`（35 模块、0 问题）与 `git diff --check` 通过。未暂存、提交或推送。

### 身份与边界

- 仓库仍为 `master / 34477d7411f076f4c9fe9200fcfc69c9253b0293` 加未提交重建实现。打包路径 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` 与下方 build-5 直接产物 SHA-256 均为 `0cd1f21345729f8a00be4f7eab548fff69dcd5690b5693010f0323b6d5d90191`；三项重建核心及对话框源码身份未变，简中 MO 与下表一致。因此复用之前自动化证据，本轮没有重新构建或把旧程序当新程序验证。
- 只操作用户项目应用 PID 2318，初始为空白工程；没有操作 `/Applications/OrcaSlicer.app` 的另一实例及其恢复提示。macOS 26.3.1、arm64、简体中文、亮主题、Retina 3024 × 1754。运行日志 `planning/debug/orca-user-gui-run-20261006-161306-277893.log`；用户给出的 `planning/build/macos-build-arm64-20261006-160747-581370.log` 当前不存在，没有审阅该构建日志。
- 测试在独立临时工程中进行，产物仅写 `planning/repair/`，未覆盖原 STL、用户工程或预设，没有打印/上传。当前配置为 `EP7 0.4 nozzle`、0.4 mm 喷嘴、`High Temp Plate`（光滑高温打印热床）、`QIDI PLA Rapido @EP7 0.4 nozzle`、`0.20mm Standard @EP7`；层高/首层均 0.2 mm，支撑关闭。完整实际工程配置保存在采用后的 3MF，非另改一套测试预设。

### 实际完成项

1. 健康 10 mm 方块从画布右键进入独立重建，旧“修复模型”项仍存在。未勾风险确认时生成按钮禁用；`alpha=0.5 / offset=0.2 / deviation=0.25 mm` 被明确拒绝。默认 `0.5 / 0.05 / 0.25 mm` 能生成三视图中截面对比，显示原件 10 mm、候选约 10.100 mm、最低点 0 → -0.05 mm，双向采样偏差约 0.05000 / 0.05043 mm，并明确不是最大误差保证及尚未诊断试切片。
2. 一次“保留原模型”后重新生成，源尺寸仍为 10 mm。第二次不勾 Z 落床而采用，显示待切片提示；Undo 恢复 12 面/1000 mm³ 原件，Redo 恢复 3648 面/约 1029.45 mm³ 候选。保存的工程实例变换仍为平移 `(90,90,5)`，网格最低局部 Z 为 `-5.05000019`，确认采用本身没有偷偷落床。未对“重复放弃两次”、全部对象切换或撤销栈条数作通过声明。
3. 方块工程第一次重新打开成功，手动切片并导出 `.gcode`：50 个层块、Z=0.2…10 mm，每层均有 XY 正挤出运动（最少 21 条），无空挤出层和 NaN/Inf。只是方块基础路径检查，不是自动诊断试切片、完整设计保真或实机质量验收。
4. `01-missing-top.stl` 默认参数明确拒绝大开口，提示无法区分缺失表面与设计开放壳体，未进入采用。人工单壳自交 `folded_tube.stl` 导入时拒绝自动尺寸换算，使用样例参数 `0.25 / 0.025 / 1.0 mm` 成功生成候选，并加粗提示内部实体范围尚未验证；没有误报恢复原设计。此宽偏差仅用于人工样例 UI 测试，不能推广到产品默认。
5. 自交样例勾选显式 Z 落床后采用、保存，Undo/Redo 的面数和尺寸正常；保存工程局部最低 Z=-0.624504924、实例 Z=0.624504924，最终最低点为 0，X/Y 仍为 90/90、旋转和缩放为恒等。没有对此小型人工样例进行打印质量验收。

### 闪退与待处理项

- 自交样例 Undo/Redo 后打开“对象”列表，模型名称行未正常显示，盘名称呈编辑状态；辅助功能读取/点击时还出现打印启用状态改变。此现象可能受原生自定义控件的辅助功能副作用影响，不能直接断言候选丢失了模型名称。
- 随后放弃本次测试交互造成的未保存变化，再次打开已经成功重开的方块工程。日志 16:41:28.648865 已记 `load project done`，16:41:29.053483 出现 `Shift objects to bed` 撤销快照，随后进程退出。崩溃报告 `/Users/liwei/Library/Logs/DiagnosticReports/OrcaSlicer-2026-10-06-164133.ips` 确认 PID 2318、`EXC_BAD_ACCESS / SIGSEGV`、主线程崩溃时间 16:41:29.0754。
- 堆栈：`NSAccessibilityProxy accessibilityIsAttributeSettable` → `wxCocoaOutlineView ... shouldEditTableColumn:item:` → `ObjectList::OnEditingStarted` → `ModelObject::ensure_on_bed`。当前 `GUI_ObjectList.cpp` 的 `colSinking` 分支没有检查 `get_selected_item_indexes` 返回的索引，后者对非对象/体积行可返回 -1；崩溃寄存器中亦出现 -1。源码与栈支持“无效列表目标被当作模型执行落床”的原因，具体事件 item 身份仍需受控复现。该回调与 HEAD 一致，本轮新代码未改它；不能称为 AlphaWrap 运算崩溃，也不能断言所有普通重开都会崩溃。GUI 验收仍应阻断，修复时需同时防止纯可编辑性查询触发模型写操作，不能只加索引检查后跳过复验。
- 次要问题：中文重建参数窗口的取消按钮显示英文 `Cancel`。其他主体中文和当前高 DPI 下底部按钮可读可达；英文主题、暗色、缩窗/键盘遍历未验。
- GUI 运算中取消、绘制/附属属性拒绝、多实例、孔洞/多壳、缩放/镜像、自动备份恢复等清单仍未实操；本轮闪退不能算崩溃恢复通过。没有再次启动/恢复崩溃工程，也未为完成清单操作用户的另一个应用实例。
- 结论：重建基础 happy path 与保护提示有实际 GUI 证据，但完整 GUI 验收失败。先修对象列表辅助功能/落床回调并复验同序列，再补剩余关键项；真实失败模型收益、跨平台和实机质量依旧未验收。本轮不修源码、不提交。记录更新后 `python3 scripts/check_ai_module_docs.py --all` 通过（35 模块、问题 0），`git diff --check` 通过；这两项仅为文档/差异检查，不改变 GUI 验收结论。

### 保存的证据

以下均为本机忽略目录产物，不入库；UI 图像由 Computer Use 当时返回，未另存截图文件。两个 3MF 中部件变换均为恒等，下面的实例 Z 与局部坐标可直接核对。

| 产物（`planning/repair/` 下） | 内容 / SHA-256 |
| --- | --- |
| `reconstruction-gui-20261006-adopted.3mf` | 方块默认不落床；1826 顶点/3648 面；`f8733364e11b3ec0e9f9942ae0dd9c71e7c8563576c0bd5ee60d7f8acc11d295` |
| `reconstruction-gui-20261006-adopted.gcode` | 50 层基础路径验证，132522 字节；`055bbd2183e4bbb3ade6d3f393c30125d91a96adda0bdafc01c3fdb7ae97d476` |
| `reconstruction-gui-20261006-folded-on-bed.3mf` | 人工自交候选显式落床；235 顶点/466 面；`10af96520871707656affb61a5af38015318b39d7ec361af093025dc0c3e51a0` |

测试后方块 SHA-256 仍为 `8618fc713878420a4e66bf21b8c4ee271ccc91eaf19b0cfbd5b821f8564b1065`，自交样例仍为 `3b903d8e64c30b28bc44a537a5ab00eef66555173e0e426d5786a5647618e5f6`。大开口样例当前 SHA-256 为 `69d81fc1d7d559ef201560cdfe0255626f9676cddfcb0a2dd4c05ac165223c74`；测试未保存/覆盖输入文件。

## 2026-10-05 独立重建首批实现与验证

### 实现与范围收敛

- 新增 `MeshReconstruction`（CGAL 5.6.3 AlphaWrap）、`ModelReconstruction`（模型快照/单位变换/验收协议/CLI）、`ReconstructionProcess`（可等待、取消、超时、内存及父死亡监督）；GUI 两菜单均经共享核心生成候选，另有真实三视图中截面、误差/摆放信息和显式采用。普通高级修复入口、策略、阈值、check-model v4 和旧 JSON 均不改。
- 首批严格限制：单普通体积、单实例、无绘制/附属编辑；只支持可形成单一 genus-zero 参考壳的输入。大开口、贯通孔/handles、独立或嵌套多壳直接拒绝，不为了生成候选自动填腔、合壳或删零件。单壳自交可以产生候选，但明确内部实体未验证，体积仅作启发式比较。此范围比审核设计的最终目标更窄，不能把 AlphaWrap 对任意输入的生成能力当作产品可采用能力。
- 全部昂贵 CGAL/QHull 计算在 worker 内；源和最终物理/局部浮点网格有严格检查，文件大小/计数先于大内存构造，输入/候选/凸包含校验值。父进程只装载凸包，更新体积与实例轮廓缓存。通过最小 `ModelVolume::set_convex_hull` 接口安装匹配凸包，不改变现有 calculate 路径。
- macOS RSS 为采样监控，不是硬内存上限；每个进程只允许一个同步重建，但独立应用/CLI 进程间没有全局资源配额。主线程有界装载、预览截面及导出不计入 worker 硬截止时间。独立进程不是操作系统安全沙箱。
- GUI 是实验性候选采用原型，尚无独立同有效配置试切片。不是“模型已可打印”流程；原件保持，采用可 Undo，默认不自动落床。自动试切片、真实收益、跨平台发行仍是明确未完成项，不将本轮称为全方案交付。
- 本轮发现并修正：初次 STL const/path 和 Vec3i 编译接口；非法索引在 TriangleMesh 构造前检查；ASCII STL 长行/数值限制；逆变换精度与最终浮点网格复检；复制后旧二维轮廓缓存；主线程凸包耗时；小屏正文滚动与按钮可达。新增 175 条可提取文案均有非空简中，恒等 `L(...)` 不改变 CLI 英文协议；只生成当前简中 MO，不批量改其他语言。

### 构建与证据

- 基准 `master / 34477d7411f076f4c9fe9200fcfc69c9253b0293` 加本轮未提交差异；CGAL 5.6.3、本机 arm64 Release、Ninja Multi-Config。未运行其他分支/平台的构建，未提交/推送。
- 最后一轮构建退出 0：`cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 4`，日志 `planning/build/reconstruction-20261005-build-5.log`，包含安装凸包接口、恒等翻译标记及可滚动 GUI。其后没有修改 C++ 源码，最终二进制身份如下。旧编译器未知警告选项、未改头文件 override/unused 等警告、重复链接库和本机 zstd 的部署目标警告仍存在；不能据此宣称旧 macOS 运行已验证。
- 第一轮新增 CLI 补单壳自交后为 26 场景（`planning/repair/reconstruction-20261005-cli-02/summary.json`），全部通过，但这仍是 build-4 身份；最终 CLI 将同时断言 worker 返回凸包有效性及 checksum，不复用旧日志。
- 两份真实旧样例已用最终应用复跑，证据为 `planning/repair/reconstruction-20261005-real-original-final/reconstruction.json` 和 `reconstruction-20261005-real-official-final/reconstruction.json`，控制台为同前缀 `.log`。原件返回 `unsupported_openings`：最大开口包围盒对角线 `6.074116 mm`，默认上限 `0.25 mm`；官方修复件返回 `unsupported_shells`。两者退出 2、没有候选，不调大阈值争取成功；这些拒绝不能计作真实修复收益。原 STL SHA-256 分别为 `eae30a1a2801093ba6f323c3e2e93448ee02a842895ef1d5ee47a02ad79d9b49`、`6d00cd22cde2cdf2cecef7336dfbd9a81fc2aa5b9c3b04ce6f7a5d1021c135aa`，输入未改。
- 最终 GUI 操作及回传按[交接附件](2026-10-05-reconstruction-gui.md)。新应用是 `build/arm64/src/Release/OrcaSlicer.app`，不是未更新的 `build/arm64/OrcaSlicer/OrcaSlicer.app`。GUI 尚未操作，不能声称采用/Undo/保存重开或新增窗口已验收。

| 最终验证（均退出 0） | 结果 | 证据（仓库 `planning/repair/` 下） |
| --- | --- | --- |
| C++ `[MeshReconstruction],[ModelReconstruction],[ReconstructionProcess],[ReconstructionWorkerGuard]` | 28 用例 / 340 断言；含真实超时/取消/父进程退出/内存/参数与缓存失效 | `reconstruction-20261005-core-tests-final.log` |
| C++ `[ReconstructionIntegration]`，`ORCA_RECONSTRUCTION_BINARY` 指向下表应用 | 1 用例 / 22 断言；镜像/非均匀缩放/局部和物理复检/配置及旧轮廓缓存 | `reconstruction-20261005-integration-tests-final.log` |
| `python3 scripts/test_cli_model_reconstruction.py --binary <下表应用绝对路径> --output-dir planning/repair/reconstruction-20261005-cli-final` | 26 通过，0 失败/跳过；人工单壳自交候选仍明确内部未验证，导出一致性/凸包校验通过，无旧 result.json | `reconstruction-20261005-cli-final/summary.json`；逐项命令/原始日志同目录 |
| C++ `[ModelRepair]~[ReconstructionProcess]~[ReconstructionWorkerGuard],[MeshBoolean],[SavedPainting],[OBJ],[stl]` | 190 用例 / 14574 断言通过；旧测试夹具仍输出预期非法输入/只读备份路径日志，不表示本轮 GUI 备份已验证 | `reconstruction-20261005-legacy-tests-final.log` |
| `python3 scripts/test_cli_model_check.py <下表应用绝对路径>` | 76 场景通过，不含打印验收 | `reconstruction-20261005-legacy-cli-check-final.log`；完整证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-model-check-z8y_o8o7` |
| `python3 scripts/test_cli_repair_acceptance.py <下表应用绝对路径> --load-settings <machine;process> --load-filaments <filament;filament>` | 78 场景通过，含旧修复试切片；不是新重建自动试切片 | `reconstruction-20261005-legacy-cli-repair-final.log`；完整证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-repair-acceptance-yfapjv78` |

上表普通修复配置仍为 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/` 中 `machine_settings_1.config`、`process_settings_1.config` 和两份 `filament_settings_1.config`，命令使用全部绝对路径，逐场景完整调用在对应证据目录。新增进程/核心与旧回归分开统计；不把同一测试反复运行累加成更多场景。新 CLI 的小 alpha 用例实际触发资源预算，不能算观察到超时，超时证据来自独立进程专项。

| 最终产物/源码 | SHA-256 |
| --- | --- |
| `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `0cd1f21345729f8a00be4f7eab548fff69dcd5690b5693010f0323b6d5d90191` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `98f90dcb732a0b012dd0fd53a00cd6c02e3068918723bee67c7c83cb7f5dff84` |
| `build/arm64/tests/libslic3r/Release/reconstruction_worker_test_helper` | `54805a0ac5a6f6a5602c94158ae4275c919afaa6b1fb028718913e8e5863f9c3` |
| `src/libslic3r/ModelReconstruction.cpp` | `6806473eff95c8a774129cedd8e401cc4698a1101d85650515aaf27a92503e1c` |
| `src/libslic3r/MeshReconstruction.cpp` | `b79c08654bce683352893e1dbf95c7ea0875b70108f7e318ebaf3e261fa460ed` |
| `src/libslic3r/ReconstructionProcess.cpp` | `65a4c06f0c1b2e50a732c3914512a17f320e77b5293fa06eb5adbc8a333e6aa9` |
| `src/libslic3r/Model.hpp` | `be5481120de95ba28572a1eb3a1271cc983ec4c95abb6bb01e78c772dfbc36d0` |
| `src/slic3r/GUI/ModelReconstructionDialog.cpp` | `0cb13bc70ed0ceee4a4de3094ea4c9f92ea2cc0215a50ee49d36bfb0a36cd2c5` |
| `localization/i18n/zh_CN/OrcaSlicer_zh_CN.po` | `0148fcbb87ed4411cb4073c4200237b9cd0a7ced2fc868a7f93c4cfde24b0c9c` |
| 本地 `resources/i18n/zh_CN/OrcaSlicer.mo`（不提交） | `9b08b2ba54584f36b7a895899088806e22f86e0c84bea0488b3217ad26a202fa` |

最终 `msgfmt --check --check-format` 通过；四个新源文件的 xgettext 提取为 175 条、简中缺失/空翻译 0。`python3 scripts/check_ai_module_docs.py --all`（35 模块、问题 0）和 `git diff --check` 通过；未暂存，故没有以 staged 检查代替当前工作区审核。未修改旧 MeshBoolean/ModelRepair/ModelCheckWorkflow/FixModelByCgal 源码。此次仅达到实验原型自动化交接，不达到跨平台商业发布/实机验收。

## 2026-10-05 独立重建修复设计（已获实施授权，以下保留审核时设计）

### 范围与当前证据

- 现有自动导入修复、高级修复、`--check-model` v4 及普通修复 JSON 均不改变；没有因为检测健康而新增禁止用户主动高级修复的策略。重建不成为这些入口的静默兜底，不涉及 AI 或参数推荐引擎。
- 新功能拟定义为“近似重建候选”，不是无损恢复、尺寸精修或打印成功保证。首版需要用户明确发起并确认采用；后台不能因为候选有效就自动进入正式打印。
- `deps/CGAL/CGAL.cmake` 固定 5.6.3 和下载 SHA-256。本机应用构建缓存指向 `deps/build/arm64/OrcaSlicer_dep/usr/local`，其中已有 `alpha_wrap_3.h`，版本头为 `5.6.3-I-900`。这里只确认依赖可定位，未编译或测试新算法。
- 已有共享事务在 `ModelRepair.cpp`，GUI 暂存桥接在 `FixModelByCgal.cpp`，场景提交/撤销/备份在 `GUI_ObjectList.cpp` 等处。重建可以复用私有快照和原子提交基础设施，但不能直接把 AlphaWrap 塞进现有 repair hook：现有修复还包含拆壳、删除非三维部件、保形及绘制政策，不等同于重建语义。
- CodeGraph MCP 未暴露；本机 v0.9.6 CLI 可用。ModelRepair/FixModelByCgal/OrcaSlicer 索引哈希与源码一致，用于导航；MeshBoolean 索引过期，该部分回到源码，没有同步或初始化图谱。Understand 历史图谱为 2026-08-28/60148c114，不能作为当前实现依据。

### 适用场景与触发规则

| 场景 | 首版建议 |
| --- | --- |
| 高级修复完成后仍有已确认的自交、裂缝、错误连接等几何缺陷，原外形大致完整，用户接受近似重建 | 建议尝试新入口，保留原件，不自动执行 |
| 高级修复失败/无进展，但输入有效、失败属于几何问题 | 检查是否符合新入口范围后给出建议；失败本身不是重建充分条件 |
| 检测未发现问题，但用户主动希望重建 | 不以检测健康为禁用理由；说明通常无必要且可能改变细节，符合首版输入约束才允许预览 |
| 超时、内存不足、检测异常/未完成、非法坐标/索引、输入损坏 | 返回明确原因；不把未知改写成有缺陷，不自动升级重算 |
| 切片缺失、悬空或细节不出路径 | 先检查有效切片参数、线宽、支撑、摆放、修改器和网格缺陷；这些现象不能直接触发重建 |
| 已知空腔、精密孔槽/螺纹/装配配合、关系不明的多实体，或需要猜测的大面积缺失 | 不承诺恢复；首版受支持流程暂不覆盖，提示回原设计/定向修补；不可将其静默实心化 |

首版拟支持：用户选中的单个普通对象、一个 MODEL_PART、一个实例、均匀材料、无面级绘制，目的是得到单一预期实体的近似外形。不能把“连通分量数量”直接等同于预期零件数：裂缝也会把一个零件分成多片；独立壳体关系不能确定时返回不支持。模型是否存在设计空腔/精密配合不能可靠全自动识别，应结合明确用途确认与可检查的几何证据，不声称一个分类器就能排除所有风险。

首版暂不覆盖：多材料体积组合、支撑/接缝/分色/模糊皮肤绘制、负体积/修改器组合、多实例、非空分层配置/可变层高、自定义 Brim 点、SLA 支撑/排液孔、切割连接件、仍保留参数化文字/浮雕等。遇到这些状态明确提示原因，不以先删属性或默认取最大部件来满足限制。均匀材料、名称和普通对象配置保留；外表面分类等派生缓存随新网格重新计算。

### 首版引擎与商业许可

- 技术上先用单个 **CGAL Alpha Wrapping** 原型，不同时引入 Blender、OpenVDB、fTetWild 或多引擎轮询，不因新功能升级现有整个 CGAL 依赖。发布前仍须检查固定版本与实际编译器相关问题；可用头文件不等于通过上线验收。
- [5.6.3 官方手册](https://doc.cgal.org/5.6.3/Alpha_wrap_3/index.html)说明：它生成严格包住输入的近似闭合流形，可改变内部结构和细节；破洞配合小 alpha 可能产生 two-sided wrap，即“合法薄壳”而不是原本实体。`alpha`/`offset` 不是最大几何误差承诺，不可直接包装成精度保证。
- [固定版本头文件](https://github.com/CGAL/cgal/blob/v5.6.3/Alpha_wrap_3/include/CGAL/alpha_wrap_3.h)许可为 GPL-3.0-or-later OR LicenseRef-Commercial；当前仓库 LICENSE.txt 为 AGPLv3。GPL/AGPL 允许商用，但开源交付需履行实际源码、声明等义务；闭源分发需求须另核实并取得相应商业授权，购买 CGAL 授权也不会消除 Orca 衍生代码本身的 AGPL 义务。[CGAL 官方许可说明](https://www.cgal.org/license.html)
- 用户已回复“沿用现有开源许可，可商用即可”。据此选择开源 CGAL 路径，不因商业用途本身要求另购商业许可证；按现有 AGPL 及 CGAL GPLv3+ 的组合许可要求交付。最终固定版本、依赖、链接和交付物须具体合规核对；独立进程用于故障隔离，不作为规避开源义务的方法。
- 若必须采用宽松许可证且不购买 CGAL 商业授权，可另评估 Apache-2.0 的 OpenVDB v13.0.0；但其标准 SDF 转换对闭合输入有要求，不能把它当作任意破洞修复器的等价替代。本轮不扩展该自研工程。

### 最小可交付链路

1. **明确发起**：GUI 独立重建菜单，CLI 独立动作（参数名拟为 `--reconstruct-model`，尚未实现）。不改旧入口默认行为。GUI/CLI 使用相同快照、策略、验收和失败定义。
2. **快照及预检查**：冻结目标几何、属性、单位、变换和有效配置身份，保留原件。仅检查和执行不改变表面的必要数据准备，不沿用删除小部件或合并所有实体的启发式。输入规模超限直接返回；STL 按明确的导入单位语义处理，不凭尺寸猜单位。
3. **隔离计算**：固定版本、固定白名单参数的独立 worker。输入/输出文件、面数、内存、耗时、线程和并发均限额；文件大小与计数在大内存分配前检查。取消/超时结束 worker 及其子进程，清理本任务临时目录；不强杀共享线程，不遗留后台任务。不自动降精度/调大 offset 争取成功。
4. **两级验收**：先证明最终落盘再读取的网格有限、索引正确、非空、闭合、流形、可定向且无自交；检测超限/异常即未验证，不放行。再检查实际尺寸、表面偏差、部件、截面、薄壁/孔槽等变化；原模型不具备可靠实体含义时不使用原始有符号体积比作强证据。抽样距离标为估计，不冒充严格误差上界。新增补面与原有表面位移要区别记录；不能为了允许补洞就豁免所有远离源表面的变化。
5. **同参诊断试切片与预览**：使用快照对应的变换及有效配置；检查非空路径、异常空层、可观测的区域/部件缺失。显示原件叠加/剖面及变化区域。原模型切片可能本来就错，不能把旧 G-code 当真值，也不能因为新 G-code 非空就宣称原设计恢复。没有可用配置时试切片状态是未验证，不是通过。
6. **明确确认与事务采用**：只允许验收满足要求的候选进入确认；严重偏差、结构含义不明、未完整验收不得伪装成低风险结果。默认保留原文件，GUI 经现有快照/原子模型更新/备份机制一次采用并可撤销；CLI 只输出新候选与独立报告，不覆盖源文件、不直接连到打印。重建后的模型须重新分析特征，旧分析和切片缓存失效。

几何误差、公差及资源预算必须根据真实样例和目标机器标定。物理参数在毫米空间定义，非均匀缩放/镜像转换及逆变换后都要检查；首版限制单实例，避免一次替换意外改变其他缩放实例。不要直接使用包围盒比例示例作为产品默认，也不在未测试时宣称“固定 0.05 mm 就能保孔”。不进行无限自动重试。

摆放的一个必要特例：外包裹会在底面向外扩张，原本贴床的模型可能有少量新表面低于平台。默认保持原变换并报告变化，不偷偷调用 `ensure_on_bed()` 移动模型或裁掉下沿；需要重新落床时，明确展示 Z 位移并由用户确认，随后按最终变换重做边界检查和试切片。原本有意沉床也不能被自动纠正。超出平台且未解决时，不给出已可打印结论。

### 防止线上明显异常的必做项

- **故障不污染原件**：worker 崩溃、超时、取消、输出截断或复检失败时，原模型及工程状态不变；进程隔离之外仍要限制内存和并发，不能承诺仅靠子进程就不会拖垮主机。
- **防过期结果覆盖**：输入 hash、对象/体积/实例 ID、相关配置/属性与任务 generation 校验；期间删除、撤销、变换、修改配置或开始新任务后，旧候选不能应用；配置改变至少使试切片证据失效。
- **不误报成功**：区分候选生成、几何验证、形状确认、试切片、最终采用。失败、取消、超限、不支持、过期、候选待确认分别可诊断，不复用旧 `success=true` 表示全部完成。
- **不遗留附属状态**：现有 `commit_move_object_volume_structures` 只提交体积和实例变换，不替换对象级 layer ranges/profile、brim_points、SLA/切割状态；新入口必须前置阻断或另行明确处理。首版阻断这些能力，不仅在 staging 清空后误认为原工程也已清空。
- **不误用已有进程工具**：`FixModelByCgal` 当前使用线程，`Utils/Process` 的 GUI 启动路径存在 detached spawn，均不具备本方案要求的完整 worker 生命周期。新 worker 必须可等待、可取消、可取真实退出码；重建与昂贵复检均受同一隔离/预算约束，不在主进程重新执行无界 CGAL 检查。
- **不丢属性或改单位**：无可靠映射的绘制不支持；不自动删小零件、合壳、实心化内腔、改比例、旋转摆放或增厚来凑出封闭结果。
- **可关闭、可复核**：新入口默认不自动调用，保留功能开关。记录任务/模型身份、引擎版本、有效参数、分阶段耗时、拒绝原因、验收和产物校验值；不记录模型正文/敏感路径/用户凭据。worker 默认无网络，使用隔离目录和受控本地输入。

### 验证与发布门槛

- 正向：真实高级修复失败样例至少证明新路径有实际收益，同时保住用户认可的外形与可切片性；不能只用人工立方体通过作结论。
- 反向：健康件、薄壁、小孔/槽、空腔、相邻实体、破洞生成双面壳、不同缩放/镜像、绘制、附属配置均有保留/明确拒绝用例。统计“生成候选”与“可采用”数量，不能混算成功率。
- 工程：取消/超时/worker 崩溃/内存限制/磁盘不足/异常输出/并发编辑/过期结果/中文路径；GUI 确认与取消、撤销重做、保存重开、备份恢复，CLI 同有效配置对照。故障用例保证原件不变及无遗留进程。
- 平台：按实际计划发布的 Windows/macOS/Linux 分别验证 worker 打包、启动与资源约束，未验证的平台不宣称支持；AGPL/商业授权及第三方清单核对完成后才发布。
- 当前结论：本节为设计而非实现，无新自动化、CLI、GUI、跨平台或实机打印通过记录。开源商用路径已由用户确认；下一步审核实现范围，获准后先做隔离原型和实际问题模型对照，效果不成立则停止接入，不将整套重建默认投产。
- 本轮文档检查：`python3 scripts/check_ai_module_docs.py --all` 通过（35 模块，问题 0），`git diff --check` 通过。只修改本记录，未暂存、提交或推送；不将文档检查解释为算法/商业发行/平台验收通过。

## 2026-10-05 配置向导日志降噪（实现、自动化及最小 GUI 验证完成）

- 仅修改 `WebGuideDialog.cpp`：删除两次仅为日志生成的完整 `m_ProfileJson.dump`；读取耗材不输出文件正文，网页消息不输出完整收发内容，仅保留大小/命令。完整网页消息仍按原机制序列化并发送，不改变业务载荷。
- 高频的逐耗材路径、列表加入和正常父级查找信息降为 debug；info 保留各品牌数量、加载完成汇总、选择/保存/取消和原有失败线索；解析及坏机型 error 原样保留。没有调全局日志等级，所以不会隐藏修复计时。
- GUI 入口是“添加/选择打印机”网页向导；CLI 不提供该窗口和消息处理，且共享配置解析/继承代码未改，本次不新增 CLI 参数或声称改变 CLI 行为。
- 验证：静态核对去掉日志语句和两处日志专用变量后，修改前后业务 token 一致；所有 error 日志保留。实际向导网页交互仍需新程序手测。
- 构建前核对：仍为当前仓库 `master / 9e9381d595`，Ninja Multi-Config / arm64 / Release，无其他构建进程。`cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2` 退出 0，日志 `planning/build/guide-log-noise-20261005.log`。重新编译 WebGuideDialog 并链接应用；已有测试目标未过期。原工具链未知警告选项、未改代码的 override/unused capture、重复库及 zstd 部署目标警告仍存在，不扩展为旧 macOS 或其他平台已验证。
- 自动化：`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests '[Preset][Guide]'` 退出 0，4 用例/184 断言通过；日志 `planning/quality/guide-log-noise-20261005-tests.log`。覆盖向导机器元数据继承、喷嘴与源预设保留；不把这些核心测试称为网页日志运行验证。修复源码及测试未再修改，本轮不重复全套修复测试。
- 资源身份：本次无资源改动；新应用 `Contents/Resources` 指向当前仓库 `resources/`。差异审查图谱停留在 2026-08-28/`60148c114...`，不作为本次源码依据，已回到当前差异及源码核对。

| 本轮产物/源码 | SHA-256 |
| --- | --- |
| `src/slic3r/GUI/WebGuideDialog.cpp` | `d136767cf0f84efbc02764a3b2104ddfc4accdef025cb4518e3257c06952d2c8` |
| 新 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `07fe16b6c44f79884e39ffff9ed22789e0e5d7c1768c89fdf4a920bd49b3decb` |
| 旧 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `89a56d28707bd0504ceb7c6bdeb2ce717074908c561bc896b10a1e965e1d0c05` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `8ff1cf92dc002ca1507a0d4888d05eb559d332d829f6a26140013829a8e9e94f` |

### 本次 GUI 最小交接

先正常关闭现有实例，使用以下新程序启动；不要继续使用未更新的打包副本。`--debug 3` 保留 info，未改变全局默认等级：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/debug/guide-log-noise-20261005-gui.log
```

- 输入/配置：沿用现有用户配置和简体中文界面，不需要导入模型、切片或修改机型/喷嘴/耗材/工艺参数；本轮对象变换、对象覆盖、盘数不参与验证。保留原选中设备，打开向导时记录勾选状态，不清空或重置用户预设。
- 准备页 → 打印机选择列表的添加/选择打印机入口 → “选择打印机”窗口：检查机型图片与列表，搜索 EP3、EP7，再清空搜索；预期列表和搜索与修改前一致，没有缺失、空白或加载错误。
- 点击“取消”，重新打开同一窗口：预期原勾选不变。保持原选择点击“确定”：预期窗口关闭且现有机型/预设不变，覆盖取消及 SaveProfile 路径；不为本测试新增或删除设备。
- 日志预期：info 有各品牌汇总和 `LoadProfileData, finished, models=...`，不再输出 `GetFilamentInfo: Json Contents`、全量汇总 JSON 或逐耗材正常加载信息；解析失败等错误仍可见。不主动破坏资源来制造错误。
- 回传 `planning/debug/guide-log-noise-20261005-gui.log`，注明打开、取消、再次确认的时间；若界面有异常附截图。不需要导出 3MF 或 G-code。

当前结论：构建及自动化通过，GUI 待用户操作；此窗口无对应 CLI 操作，共享配置业务未改。Windows/Linux 未验证，实机打印不适用。已避免原来的大段日志及两次无用序列化，但没有新 GUI 对照计时，不声称已测得具体提速比例。未暂存、提交或推送。

本轮差异检查：`git diff --check` 退出 0；`python3 scripts/check_ai_module_docs.py --all` 检查 35 模块，退出 1，唯一问题仍为用户无关的 `one_vertex_touch.svg` 未归属，受影响模块指纹无报错。未修改或忽略该文件、`result.json` 及其他任务记录。

### 10-05 Computer Use 实测（完成）

- 用户授权 AI 操作 GUI，打包程序和源码均与上表新身份相符。按应用名称匹配时先启动了 `/Applications/OrcaSlicer.app`，停留恢复提示，未用于本次验收；随后通过完整路径打开仓库打包程序，进程 29946，09:22:08 启动。
- 新程序出现恢复提示，选择恢复以保留已有工程；恢复后为 EP7 0.4 nozzle、QIDI PLA Rapido @EP7 0.4 nozzle、0.20mm Standard @EP7、光滑高温打印热床，托架在准备页显示。未改变模型、摆放或切片配置。
- 使用程序自动写入日志 `/Users/liwei/Library/Application Support/OrcaSlicer/log/debug_Mon_Oct_05_09_22_10_29946.log.0`，不另启动带 tee 的第二个测试实例。已完成向导搜索、取消、原选择确认及日志摘要核对。

| 场景 | 实际结果 |
| --- | --- |
| 首次打开向导 | 09:24:06–07 加载成功，图片及完整列表显示；汇总为 models=336、machines=815、filaments=3868、processes=2236 |
| EP3 搜索 | 仅显示 EP3/EP3 Pro/EP3 Plus；前两款未勾选，Plus 0.4 已勾选；未触碰复选框 |
| EP7 搜索 | 仅显示 EP7；0.4 已勾选，0.2/0.6/0.8 未勾选；清除搜索后完整列表恢复 |
| 取消并重开 | 09:26:33 `GuideFrame cancelled`，返回准备页；09:26:42–43 重新打开，数量汇总相同，EP3/EP7 原选择一致 |
| 原选择确认 | 09:28:00 `SaveProfile` → `GuideFrame returned ok` → `GuideFrame applied`；日志选中 Generic Marlin Printer 0.4、EP3Plus 0.4、EP7 0.4，返回准备页无错误弹窗 |
| 配置与工程保持 | 确认后屏幕仍为原 EP7/耗材/工艺/平台和托架。取消前、取消后、确认后，对 `OrcaSlicer.conf` 的 `{models,filaments,presets,orca_presets}` 作 `jq -cS` 规范化再 SHA-256，均为 `ca200235b4cd7104ee83dd79123060aa703032418fa5de358108797bd376f1e9`；未新增/删除设备或覆盖预设 |

- 日志降噪：每次 `GuideFrame before ShowModal` 到 `LoadProfileData, finished` 共 294 行、36,844 字节；原完整 JSON 和高频 `tFilaList Add` / `Load Filament:` 匹配数为 0，取消、保存和加载汇总仍存在。两个区间分别约 1.041 s / 0.992 s，含该日志区间内异步数据加载，不是完整窗口渲染计时；没有旧版本同条件基准，不报告提速倍数。
- 稳定日志快照：`planning/debug/guide-log-noise-20261005-cua-29946.log`，1,950 行、323,479 字节，SHA-256 `d8eb0a40c65b4175807f1bc16cee143694f4330d36282020afb867cb7e29ec27`。原运行日志仍会追加心跳；不要将快照字节数与此前 38 MB 不同时长会话简单换算成性能提升。
- 快照仅一条 error：启动时缺少 3Dconnexion 驱动，早于向导；恢复工程时仍有之前已知的父配置 warning。本轮向导操作期间没有新增 error，不等于整个程序无 warning。没有人工破坏预设制造错误，错误路径保留依赖前轮静态核对。
- 新程序保留恢复的工程，停在准备页，未切片、保存项目或改模型；本次不替代高级修复的未覆盖场景。按名称误启动的 `/Applications` 旧版本仍停留恢复提示，正常退出快捷键未生效；未替用户选择丢弃恢复数据，也未强制结束它。它不参与本次验收。
- 结论：本次日志改动的 macOS 最小 GUI 验证通过；无需用户重复清单。自动化结果保持前轮 4 用例/184 断言，CLI 无对应窗口；Windows/Linux、首次安装/其他配置组合未验证。本轮只更新验证记录，不改源码、不重编译、不提交。

### 10-05 入库检查收尾与 result.json 来源

- 用户明确要求删除 `one_vertex_touch.svg`：确认它是仓库根目录未跟踪文件后，移至 `/Users/liwei/.Trash/one_vertex_touch-20261005-orca.svg`，可从废纸篓恢复。未删除或回退 `result.json`。
- 删除后 `python3 scripts/check_ai_module_docs.py --all` 退出 0（35 模块，问题 0），`git diff --check` 退出 0；原未归属阻塞已解除，未提交。
- `result.json` 是 CLI 的结果报告，不是配置输入。当前源码 `record_exit_reson` 默认使用相对路径 `result.json`，传 `--outputdir` 时改写到该目录；CLI 正常结束及多种失败出口都会写报告，`--help` 分支打印帮助后也会继续进入统一成功收尾。GUI 正常启动分支直接返回 `GUI_Run`，不会经过这个 CLI 成功收尾，因此并非每次 GUI 启动或编译都会生成根目录文件。
- 仓库根目录的 `result.json` 已在历史提交中被 Git 跟踪；当前改动仅新增空 `obj_exports` / `output_files`，文件时间仍为 10-04 14:14，未被本轮 10-05 GUI 验证改写。仅凭空成功报告无法反推具体是哪次命令，但生成/覆盖路径已由源码确认。本轮仅解释原因，不改结果协议、输出默认路径、Git 跟踪或忽略配置。

### 10-05 模型修复入库复审

- 用户要求仅检查是否应入库、有无必须追加的修复改动。本轮仍为 `master / 9e9381d595`；未修改生产代码、测试或修复策略，未提交。检查开始时 `result.json` 已不在 Git 差异中，本轮未操作该文件。
- 逐项复审当前 `MeshBoolean.cpp` 和测试差异：阶段计时的日志异常不会从计时器析构传播；退出原因仅细化原有 false 出口和资源保护；两处距离短路保持原有连通部件/包围盒/面积/体积/距离容差，只对已经被原条件拒绝的候选省去距离计算。没有新增网格验收步骤或放宽通过条件；接受路径、部分成果接口、失败回滚及公开协议未变。
- 共享入口复核：`ModelRepair`、GUI 工作线程/切割修复路径、普通 CLI 以及检测组合入口继续调用同一个 CGAL 核心；本次无需分别追加 GUI/CLI 策略开关。未恢复被用户否定的保守模式、智能参数引擎等改动。
- `MeshBoolean.cpp`、`test_meshboolean.cpp` 和测试程序 SHA-256 仍与前轮记录完全相同；直接复核现存原始日志末尾，核心 190 用例/14,574 断言、检测 CLI 76 场景、普通修复及切片 78 场景均通过。本轮复用证据，不重复构建或把旧日志冒充新执行。新打包应用仍为 `07fe16b6...`，此前托架 GUI 修复/备份/重启重切片的证据范围不扩展。
- 审核结论：没有发现本批改动的入库阻塞，建议先入库这些必要优化，不追加算法重构。日志增强提供后续优化依据，确定无用的距离计算短路有实际价值，但不承诺成功模型提速或提高修复成功率。
- 延后项：CGAL 内部即时取消、大模型局部边界空间索引、备用修复引擎与全模型策略升级仍按实际样例和耗时证据单独立项，不是本批前置条件。彩色/复杂场景完整 GUI 回归、Windows/Linux 和实机打印仍未覆盖，不能把本批可入库解释为整个高级修复已经全面验收。
- `git diff --check` 和文档全量检查均通过（35 模块、问题 0）。正式提交仍需用户授权、选择性暂存及 staged 检查；模型修复和向导日志可分开提交，其他任务文档不混入。

## 2026-10-04 必要优化（实现及自动化完成，待 GUI）

- GUI：GUI_ObjectList → FixModelByCgal → ModelRepair；普通 CLI 与检测组合入口也经 ModelRepair 调用 MeshBoolean。计时与提前拒绝放在共享 MeshBoolean，不新增两端策略差异。
- 范围：分阶段单调时钟计时、局部拓扑修复区分预算/不适用/证明失败；保持原候选选择与拒绝阈值，只有原本必定失败的廉价条件才能跳过表面距离。
- 验证计划：日志专项（成功/拒绝/超限）、既有模型修复全专项、普通修复及组合检测 CLI；不运行旧二进制。本轮未提交，用户无关文件继续保留。
- 首批修改：仅 `MeshBoolean.cpp` 及 `test_meshboolean.cpp`；计时使用单调时钟、单次调用 ID 和异常安全析构日志；诊断码不改公开 JSON。两处保形判据先计算原有部件数/包围盒/面积/体积条件，仅在全部满足时计算距离，阈值和最终布尔条件不变。
- 构建前核对：工作目录与 `CMAKE_HOME_DIRECTORY` 一致，Ninja Multi-Config / arm64 / Release；无其他构建进程。运行 `cmake --build build/arm64 --config Release --target libslic3r_tests OrcaSlicer --parallel 2`，首轮日志 `planning/build/repair-diagnostics-20261004.log`。
- 首轮构建退出 0。新日志专项首次执行 2 用例 / 103 断言，1 条断言失败：测试将 Boost 日志的布尔 `true` 错写为数字 `1`，已有日志机制确实输出 `true/false`。只修正精确日志字符串断言，不修改业务行为；失败日志 `planning/quality/repair-diagnostics/focused-r1.log` 保留。随后按差异行格式化并重建，重新执行专项，不用首轮二进制证明后续代码通过。

### 本轮最终构建与验证

- 第二轮构建退出 0，日志 `planning/build/repair-diagnostics-20261004-r2.log`；此后没有修改 C++。原工具链的未知警告选项、重复库和 zstd 部署目标警告仍存在，不证明旧 macOS 已通过。
- 测试程序：`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests`。日志专项过滤 `[ModelRepair][Diagnostics]`：2 用例 / 103 断言通过，日志 `planning/quality/repair-diagnostics/focused-r2.log`。完整过滤 `[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`：190 用例 / 14574 断言通过，日志 `core-r2.log`。专项包含在完整结果内，不重复计数。
- 检测组合入口：`python3 scripts/test_cli_model_check.py build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，76 场景通过，退出 0；日志 `planning/quality/repair-diagnostics/cli-check-r2.log`，证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-model-check-w86ie3aa`。该脚本不证明切片/打印通过。
- 普通修复与试切片：`scripts/test_cli_repair_acceptance.py` 使用同一应用，`--load-settings` 为 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/machine_settings_1.config;planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/process_settings_1.config`（实际传绝对路径）；`--load-filaments` 为该目录 `filament_settings_1.config` 两份、分号分隔。78 通过 / 0 失败，退出 0；日志 `planning/quality/repair-diagnostics/cli-repair-slice-r2.log`，证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-repair-acceptance-7wfvz_bz`。每场景命令和产物保存在该证据目录。

| 当前产物/源码 | SHA-256 |
| --- | --- |
| `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `c30c4facc29fed08c731e0691a5825b651eae686e7ee7ae3b4533933596a7cfd` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `8ff1cf92dc002ca1507a0d4888d05eb559d332d829f6a26140013829a8e9e94f` |
| `src/libslic3r/MeshBoolean.cpp` | `b3245cf9a821414f9cdc8036ccd7cc0ceb50ac9bc0214a846670934e36428560` |
| `tests/libslic3r/test_meshboolean.cpp` | `290c469262d8560ac64709be2a98f57441e38a668704ea279c83b3325dc932fb` |

### 真实托架与耗时解释

- 输入：`/Users/liwei/Library/Containers/com.tencent.xinWeChat/Data/Documents/xwechat_files/wxid_naol68t5y06922_46e9/msg/file/2026-09/(圆角)永久方案托架20260918.stl`，SHA-256 `5df90e7a3122aae5dbb09d5897b074ebc4db2f3197aa0c839741e4e66eebbca8`。单对象/单体积、原始坐标，无人为缩放或旋转。
- 新应用使用 `--debug 3 --repair-model cgal --export-obj`，隔离 datadir 和 outputdir 在 `planning/quality/repair-diagnostics/bracket/`；日志 `run.log`，结果和 OBJ 在 `output/`。退出 0，862 面/114 开边 → 856 面/0 开边，`success=true / committed=true / mesh=passed`，无部件删除。
- 对比当前 OBJ 与 `planning/quality/smart-engine/bracket-standard/(圆角)永久方案托架20260918.obj` 的全部 `v` / `f` 行，`diff` 退出 0，顶点和面一致。这里旧产物仅作几何对照，不用旧二进制冒充新代码测试。
- 单次 CGAL 核心总耗时 12.5662 ms：局部拓扑准备 10.5046 ms、拓扑验收 1.32604 ms、表面距离 0.211125 ms。该样例的距离检查不是主要耗时，不能据此删掉保形验收；也不能外推大型模型的耗时。
- 计时只覆盖 CGAL 候选处理，不含外层检测、绘制恢复、导出或 GUI；没有同机重复基准与 2.4.2 整链路对照，不报告提速倍数。确定失败的候选可少算一次距离，但不保证成功模型加速。

### 本轮 GUI 最小交接（待用户操作）

先关闭其他 Orca/WiseBeginner 实例；从当前仓库启动新构建，`--debug 3` 用于记录 info 级耗时：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --debug 3 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/repair-diagnostics/gui-run.log
```

本轮未新增策略选择框。沿用已有“高级修复”及属性保留规则，不按后文已撤回的“保守/标准”清单操作。

- 托架验证只做几何修复：新建单盘工程、导入上面的原始 STL、一个实例、保持 100% 缩放和原始朝向，不自动摆正、不做切割。机型/喷嘴/耗材/工艺不参与此次 CGAL 判定；记录当前实际机型即可，不为该修复测试改动用户预设。原托架的尺寸须保持，孔道不应被封闭。
- 彩色验证及切片使用已有未修复工程 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/smart-engine/gui-input/repair-strategy-input-3slots.3mf`（SHA-256 `f636015d602d46d42d06723d31b4f111e529d9ee5315b983c88cb980269c9b13`）。它是测试输入，不含已撤回策略配置：单盘/单对象 10×10×10 mm 缺顶面立方体、10 面、红绿面着色、100% 缩放/无旋转；EP3 Plus 0.4 nozzle，0.20mm Standard @EP3 Plus，层高 0.2/首层 0.24 mm，Cool Plate，支撑关闭，3 槽 EP3-PLA悬垂优化 @EP3 Plus（白/红/绿），无对象覆盖。若加载时替换了预设，请回传提示和最终参数，不视为同参通过。
- CLI 对应配置来自上面三份展开 config；机器/工艺/耗材 SHA-256 分别为 `525f96226d82b47a33e5b6ee69bbda6d3291deb8855e967b013764b0d30df9ab`、`9f5cd82ce8a5f33995bb38cc36dab0677a225d43bcddbeeffc2ff1c60fb762ec`、`778e0f655f7c5895db968ed9e733b572b52be1ab4dd2cf75ea288e153951255f`。CLI 彩色 OBJ 使用两份耗材配置，GUI 工程显式包含默认白色及红绿两色，故不声明 GUI/CLI G-code 逐字节相同。

| 场景 | 操作 | 预期及回传 | 状态 |
| --- | --- | --- | --- |
| 原托架 | 准备页选对象 → 右键高级修复；完成后另存 `bracket-repaired.3mf` | 对象仍完整、原孔道保留；对应 CLI 为 856 面/0 开边；日志有阶段、拓扑分支结果和计时汇总 | 待 GUI |
| 彩色及撤销重做 | 打开上述原始彩色工程 → 高级修复（保留现有颜色）→ Cmd+Z → Shift+Cmd+Z | 补齐顶面且红绿面保留；撤销恢复缺顶面，重做恢复补面；对象/盘/位置不变 | 待 GUI；核心及 CLI 属性/事务测试通过 |
| 保存重开及切片 | 彩色修复后另存 `colored-repaired.3mf` → 重开 → 切片单盘 → 导出 `colored-repaired.gcode` | 补面和颜色仍保留；10 mm 高度有连续挤出路径，不是空切片；新增顶面允许默认颜色，所有警告原文回传 | 待 GUI；CLI 修复后切片通过 |

回传 `planning/quality/repair-diagnostics/gui-run.log`、上述两个 3MF 和 G-code，注明操作时间段及异常截图。无新增界面/翻译，未改变取消机制，本轮不重复要求慢 CGAL 硬取消验收。GUI、Windows/Linux、实机打印均未执行；自动化通过不替代这些结论。

### 本轮差异与文档检查

- 生产源码仅 `MeshBoolean.cpp`；测试仅 `test_meshboolean.cpp`，模块文档及本记录同步。没有恢复保守模式、没有更改补洞/删部件/保色策略或公共参数/JSON。
- 无关 `result.json`、`one_vertex_touch.svg` 和其他任务记录未修改；其他任务可能并行更新其文档，不回写或覆盖。
- 最终 `git diff --check` 退出 0；`python3 scripts/check_ai_module_docs.py --all` 复核 35 模块、12853 归属文件，退出 1，唯一问题为无关的 `one_vertex_touch.svg` 未登记归属。受影响模块指纹无报错；未删除或忽略用户文件规避检查。GUI 输入和三份配置哈希均复核一致。未暂存、提交或推送。

## 2026-10-05 用户 GUI 日志及重启恢复复核

本次只分析，没有改业务代码、启动 GUI 或重新构建。

### 身份与证据

- 分支仍为 `master / 9e9381d595`；`MeshBoolean.cpp` 哈希仍为上表 `b3245c...`。用户 08:38:14 新构建的 `build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` 与 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` SHA-256 同为 `89a56d28707bd0504ceb7c6bdeb2ce717074908c561bc896b10a1e965e1d0c05`。因此这次打包副本不是上一轮提醒避免使用的旧程序。它不是昨天自动化所用二进制，但本次修复源码未变且 GUI 输出新诊断事件。
- 首次日志：`planning/debug/orca-user-gui-run-20261005-083841-368302.log`，38,126,356 字节、1,063,318 行，SHA-256 `8ac036b6c1dbb7938ee0e87e69e760e09481180017db5af93699d6999b3d9dc2`。
- 重启日志：`planning/debug/orca-user-gui-run-20261005-084143-526136.log`，复核时仍持续追加备份心跳；核心恢复及切片完成于 08:41:49–08:41:52。日志含 NUL 字节，检索使用 `rg -a`；不能因普通 rg 报 binary matches 就认定无修复事件。

### 修复与备份结果

| 项目 | 证据及结论 |
| --- | --- |
| 第一次修复 08:39:52 | `status=repaired`，862→856 面、118→0 开边，failed/unresolved 均为 0；scene_committed 和 backup_queued 成功。CGAL 候选耗时 14.7103 ms，其中局部拓扑准备 12.0957 ms、距离 0.239083 ms |
| 第二次修复 08:40:40–41 | 同为 862→856 面、118→0 开边；CGAL 候选耗时 15.4462 ms，其中拓扑准备 12.7197 ms、距离 0.2605 ms。未移动顶点、未补洞，删去 6 个经过内部性证明的面；不是删除整个部件 |
| Undo/Redo | 08:40:26 和 08:40:36 有 snapshot reloaded；随后再次修复的输入为 862 面。可证明快照恢复入口执行，不能只凭该日志证明完整撤销/重做操作顺序和界面效果全部通过 |
| 备份写盘 | 第二次提交后 08:40:44.030 写入对象备份，08:40:44.043 元数据备份导出完成；不只停留在排队事件 |
| 重启恢复 | 08:41:49 明确走 `IMPORT_STAGE_RESTORE / m_load_restore=1`，读入 1 个对象，随后重新切片，完成事件 status=0；这是自动备份恢复，不是正式另存 3MF 后重开的验收 |

两次修复均为保留标注策略，不允许主动清空属性；当前 STL 样例不能证明彩色模型保色通过。计时只覆盖 CGAL 核心，不是整个对话框耗时；这一托架的距离验收不足 0.3 ms，无证据表明应该为提速删掉保护。

### 恢复模型与 G-code 实物核对

证据目录：`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orcaslicer_model/Mon_Oct_05/08_38_52#71715#56/`。文件属运行时备份，未修改、搬移或删除。

- `3D/Objects/(圆角)永久方案托架20260918.stl_1.model` 是 ZIP 包而不是裸 XML；打开其内部模型检查，426 个顶点、856 个三角面、按坐标焊接统计开边 0、超过两个邻面的边 0。局部包围盒约 135×102.5×70.0001754 mm。此只读结构检查不替代全套几何自交验收。
- 重启前 `Metadata/.71715.1.gcode` 与重启后 `Metadata/.79905.0.gcode` 均为 3,309,900 字节、126,376 行，去注释后的 115,905 条非空指令全部相同；逐行对比仅第 2 行生成时间不同，内嵌配置无差异。
- 两者均为 350 层、45.76 g、预计 1h25m43s；预览路径包围盒均为 `{10.838, 11.119}–{150.983, 124.33}` 且在平台内，不是空 G-code。此为软件一致性，不证明实机打印效果。
- 两份 G-code SHA-256 依次为 `e070be1ff224b74b55fe8fd70e069e1a130818462e923325e56d4cf460611a99`、`9e8c0f57d54c4dc4686d1814f0482564149b5679eb82bbcc518fb1a6904d6653`。

### 其他日志问题的分级

1. **建议后续优化：配置向导日志过量。** 首份日志在 08:39:29–30 加载各品牌/耗材时输出大量配置内容，未带时间前缀的 JSON 行约 100 万。源码 `WebGuideDialog.cpp` 的 `GetFilamentInfo` 在 info 级记录完整文件内容，`LoadProfileData` 还记录汇总 JSON。与新增 CGAL 计时无关；可增加终端/磁盘输出负担，但本次未测量开关日志的性能差值，不能断言造成了几秒卡顿。建议降到 debug/trace 并只保留摘要，不为降噪改动预设加载业务。
2. **此次无切片差异：重启后三条父预设警告。** 备份的机器/耗材/工艺嵌入配置 `inherits` 均为空；`PresetCollection::load_project_embedded_presets` 无父项时使用默认配置后覆盖工程内配置，仍无条件记录缺父项 warning。日志随后 `validate presets success`，本次前后 G-code 除生成时间外一致，因此没有证据表明参数丢失。未概括为所有真正缺父项的情况都无害。
3. **低优先级：`calc_exclude_triangles` 三条 error。** 发生在 EP3 Plus 平台初始化/新建工程时，来源为平台禁入区域绘制，不是托架三角网格修复。空/退化禁入区域亦可能走到该错误日志；本次未独立复现具体运行态区域，不直接断言其几何内容。之后切换 EP7，切片完成且路径在平台内，没有本样例打印失败证据。
4. **环境提示：3Dconnexion 驱动缺失。** 两次启动各一条，影响专用 3D 鼠标；不使用该硬件时不需要处理，不是 CGAL 或切片错误。网页导航被记为 warning、网络插件未启用等信息不代表本次模型处理失败。

未发现修复失败、切片失败或崩溃堆栈。第一份结尾记录“取消关闭”后只剩备份心跳，没有正常退出完成记录，不能从此断言用户是正常退出还是强制结束。当前可确认的是修复成果已落盘且第二次启动成功恢复、重切片一致；颜色、工作中取消、正式另存重开及实机打印仍未覆盖。

## 2026-10-04 撤回与重新审查

- 用户明确：先剔除刚才的未提交实现，关注现有算法是否过度复杂、能否直接对齐 2.4.2；本轮不授权新的修复算法改造。
- 撤回范围：`repair_conservative` CLI/GUI 策略、外层保形验收、组合检测分流，以及对应测试、翻译、模块正文和指纹；保留以前已经提交的模型修复和备份修正。
- 当前参考：本地 `upstream-v2.4.2` 源码与当前 HEAD；图谱早于相关代码，不作为现状证据。性能差异须同模型/同机器实测，不能从检查数量推定耗时倍数。

### 两张截图的重新分析（撤回后的 HEAD）

- 本地 `upstream-v2.4.2` 为 `8500fcdccaa10b5099ac20d252af3a7c560046f1`，并核对官方 v2.4.2 的 MeshBoolean.cpp、FixModelByCgal.cpp。官方 GUI 拆壳/清理后以开放边非零作为调用 CGAL 修复的条件；底层执行 soup 清理、退化/非流形整理、与自身布尔并集尝试、补洞及朝向整理，末尾主要以闭合作为显式失败条件，没有本分支同等的最终自交/表面距离验收。其布尔与补洞同样可能昂贵，未做对照计时不能断言官方一定快。
- 对齐基础修复步骤可以讨论，但直接搬回官方的跳过条件和成功标准不适合无人值守的检测→修复→切片放行。保留有效性检查、结果提交/回滚和有标注时的属性保护有实际理由；重复网格转换、自交/距离验证及每个中间候选的验证成本需要计时后分层，不能以“更严格”证明所有开销合理。本轮不增加新模式、不替换算法。
- 真实反例：`planning/repair/bracket-242-comparison-20260929.md` 中用户标为 2.4.2 产物的 fix.stl 与原 STL，本轮 SHA-256 均与旧报告相符，完整哈希及输入路径保留在该报告。旧只读比较记录了开放边归零但残留非流形/共面重叠，并确认一处原孔道被新增面阻挡。本轮仅验证文件身份并复核报告，没有重新运行修复或用旧应用做回归；不能据一个样本推断总体成功率。
- 图 2：备份刷新已在 `87dd192c17` 提交，不需重复开发；`doc/model-check-workflow.md` 的 2026-09-29 16:09–16:24 记录还包含实际落盘与单对象 Undo/Redo，但未覆盖真实崩溃恢复。阶段失败日志已部分存在，局部拓扑分支超面数/预算耗尽/证明失败和阶段耗时仍未细化，建议优先补齐。当前 CGAL 调用结束前无法真正停止，先改善取消反馈和自有循环检查，独立进程硬取消另行评估，不为该体验问题立即大改架构。边界查询仍为遍历且有 20,000 面保护，建议有实际超限/性能证据时优化空间查询，不能直接移除保护；其他位置已有 AABB 树不代表此处已优化。
- 撤回检查：35 个目标跟踪文件与 HEAD 完全一致，`git diff --check` 通过；简中 MO 按恢复后的 PO 重新生成。文档全量检查唯一问题为用户无关的 `one_vertex_touch.svg` 未登记归属，没有修改或忽略该文件。本轮不提交，不重建应用，不将撤回前测试证据当作当前测试结果。

以下章节保留为**已撤回实现的历史记录**，不代表当前生效功能或继续执行指令。

## 关键决策

| 决定 | 理由与适用范围 | 状态 |
| --- | --- | --- |
| 保形优先、必要时确认 | 通用模型兼顾功能件，不从拓扑合法推导设计正确 | 用户已确认 |
| 保留 v4 公共检测摘要 | 后端任务身份不回填到已经精简的协议 | 用户已确认 |
| 部分修复不自动进入正式打印 | 保留现有安全进展，但不能冒充完整验证 | 用户已确认 |
| GUI/CLI 共用业务策略 | 同一有效选项应有相同结果；保留已明确的导入规则差异 | 用户已确认 |
| AI 完成必要最小构建和自动化，用户 GUI 手测 | 按 2026-10-04 AGENTS/validation/gui-validation；无实机打印授权 | 用户已确认 |

## 阶段与步骤记录

| 阶段 | 范围与验收 | 状态 |
| --- | --- | --- |
| 1. 修复可靠性 | 检测分流、策略解耦、保形/风险判断、核心及 GUI/CLI 回归 | 进行中 |
| 2. 参数闭环 | 同一产物身份、特征事实、受限参数覆盖、最多两次调整、切片验收 | 未开始 |
| 3. 备选方案 | 固定局部/重建候选、独立验收、失败与需确认结论 | 未开始；不上线自由生成脚本 |

## 构建与自动化证据

- 构建配置：现有 `build/arm64`，Ninja Multi-Config / Release / arm64；配置的源码根与本工作目录一致，无其他构建进程。
- 计划命令：`cmake --build build/arm64 --config Release --target libslic3r_tests OrcaSlicer --parallel 2`。
- 状态：首轮构建退出 0，日志 `planning/build/smart-engine-20261004.log`；沿用的第三方编译选项与 zstd 部署目标警告仍存在，不代表旧 macOS 已通过。
- 首轮定向回归：`[ModelRepair][Conservative],[ModelRepair][Routing]`，5 用例 / 76 断言通过，退出 0；日志 `planning/quality/smart-engine/focused.log`。此结果为专项子集，不与全专项叠加计数。
- 首轮完整核心专项：`[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]`，193 用例 / 14547 断言通过，退出 0，日志 `planning/quality/smart-engine/core.log`。
- 首轮普通 CLI 修复：59 通过 / 0 失败，退出 0，日志 `planning/quality/smart-engine/cli-repair.log`；未提供切片配置，该脚本本轮不证明试切片通过。
- 首轮组合检测 CLI：退出 1，`cap-repair-obj` 被新增的 `!check_complete` 总开关拦截。其七项检查完成且缺陷已确认，只是缺陷导致包含关系不确定，不能等同于资源超限。修正为：unknown、基础检查未完成、部件上限或面积统计未完成才拦截；确认缺陷仍允许修复，并保留导出复检的完整验收门槛。失败日志 `planning/quality/smart-engine/cli-check.log`，不得覆盖作通过证据。
- 第二轮构建退出 0：`planning/build/smart-engine-20261004-r2.log`。核心 194 用例 / 14556 断言、检测 CLI 81 场景、旧修复加试切片 78 场景全部通过（各退出 0），日志分别为 `core-r2.log`、`cli-check-r2.log`、`cli-repair-slice-r2.log`。
- 第三轮补普通 CLI 保守策略端到端覆盖：84 通过 / 2 失败（退出 1，`cli-repair-slice-r3.log`）。新增测试错误地要求“未修好的开放模型禁止诊断性试切片”，而原入口 best_effort 明确允许试切片，且报告正确保留 `unresolved / mesh=not_passed / success=false / committed=false`。按已批准的“保留诊断性试切片、普通 CLI 默认不变”修订测试：要求诊断完成但绝不能宣称网格通过，不改业务代码掩盖结果；第四轮复测结果见下一条。切片退出 0 不能被后端解释为正式打印许可。
- 第四轮普通 CLI：86 通过 / 0 失败，退出 0，`planning/quality/smart-engine/cli-repair-slice-r4.log`。覆盖保留/忽略材质下的保守健康导出/切片、开放模型拒绝修复导出、开放模型诊断性试切片不冒充网格通过。证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-repair-acceptance-si0qbzoo`；每场景有命令、结果和 G-code（如适用）。86 为当前完整结果，先前 59/78/84 不叠加。
- `msgfmt --check-format` 退出 0；已只生成当前简体中文 MO，没有批量改其他语言。`python3 scripts/check_ai_module_docs.py --all`：35 模块、12853 归属文件、0 问题，退出 0；`git diff --check` 通过。未暂存/提交。

### 当前构建身份

第二轮之后未再修改 C++；后续只扩展 Python CLI 回归与文档。程序均来自本目录的 Release arm64 构建，不使用另一份旧打包目录。

| 产物 | SHA-256 |
| --- | --- |
| `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `5ed539b89ed473a676e27431286dd88910c7b5f5069b1c3c1b58a2b78332da3a` |
| `build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests` | `5e9bbc413e9ebf05fa4241d9c370a56d05df51fa8b3d7926ca534b9c18e9ea5d` |
| `resources/i18n/zh_CN/OrcaSlicer.mo` | `7f797bd25bca182299c6881aa0175bef29d5d6eb019fdffff027c2779efbac8b` |

应用 `Contents/Resources` 指向本仓库 resources；代码/测试实际内容由本次 Git diff 与对应模块 review 指纹记录。日志、输入产物和 MO 构建产物不提交。

### 第一批实现

- 未确认缺陷或检查超限时保留 prepared 模型、unknown/needs_repair 及 not_started；确认缺陷导致的包含关系不确定，不阻断原有修复。
- 新增显式 `--repair-conservative 0|1`，不改变旧命令默认。保守模式不删除小部件、不填洞，worker 返回的候选也接受双向表面距离/面积/部件数检查。
- GUI 高级修复增加保守/标准/取消选择，保色与几何策略独立；临时切割修复保持旧行为。
- 标准模式的历史材料相关删除默认暂保兼容；新保守模式不会因忽略材质放宽几何处理。保守模式并非功能孔/薄壁与实机质量的完整证明。

## GUI 验证交接

以下 GUI 项目全部**待用户验证**。先关闭其他 Orca/WiseBeginner 实例，不要打开旧 `build/arm64/OrcaSlicer/` 打包副本。使用本次已构建应用：

```sh
/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer 2>&1 | tee /Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/smart-engine/gui-run.log
```

### 输入与有效配置

- 已准备可直接打开的工程：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/planning/quality/smart-engine/gui-input/repair-strategy-input-3slots.3mf`。哈希 `f636015d602d46d42d06723d31b4f111e529d9ee5315b983c88cb980269c9b13`。
- 单盘、一个 10×10×10 mm 缺顶面立方体，10 面，红绿面着色，位置已摆盘、无缩放/旋转。工程存有面颜色，未进行修复或切片。来源为 `tests/data/model_repair_acceptance/11-colored-missing-top.obj` 和同目录 `colors.mtl`；可从源重建，不需改原文件。
- 工程有效参数：EP3 Plus 0.4 nozzle；0.20mm Standard @EP3 Plus；层高 0.2、首层 0.24 mm；3 槽均为 EP3-PLA悬垂优化 @EP3 Plus，槽色白/红/绿（模型使用后两色）；Cool Plate；支撑关闭；无用户对象参数覆盖。界面语言先简中，再按下表核对英文。加载时如提示缺失预设/自动更改参数，请先记录提示和实际配置，不直接视作同参测试。
- CLI 切片回归使用本地已展开配置 `planning/repair/gui-acceptance-20260908-K6TfAE/local-presets/{machine,process,filament}_settings_1.config`；机器/工艺/耗材哈希分别为 `525f96226d82b47a33e5b6ee69bbda6d3291deb8855e967b013764b0d30df9ab`、`9f5cd82ce8a5f33995bb38cc36dab0677a225d43bcddbeeffc2ff1c60fb762ec`、`778e0f655f7c5895db968ed9e733b572b52be1ab4dd2cf75ea288e153951255f`。CLI 脚本对彩色 OBJ 使用两份耗材设置并按原入口映射，手测工程显式包含白/红/绿三槽；该槽位表达差异已记录，不能据此宣称逐字节路径相同。

### 操作与预期

每个独立场景先重新打开上述**原始工程**，不沿用上个场景修复后的模型。

| 场景 | 操作 | 预期与回传 | 状态 |
| --- | --- | --- | --- |
| 策略取消 | 准备页选择对象 → 右键高级修复 → 在新策略窗口点取消 | 不出现修复提交，缺顶面与红绿面不变；保存 `cancel.3mf` | 待 GUI |
| 保守修复 | 同一入口选“保守修复” | 显示无安全进展/仍有问题，说明不补洞；不封顶、不丢色、不删除对象；保存 `conservative.3mf`，截取提示 | 待 GUI；对应 CLI conservative-open-* |
| 标准修复 | 重新打开原始工程，选“标准修复” | 顶面补齐、旧红绿面保持；新增顶面可使用默认部件颜色，不要求推断新花纹 | 待 GUI；对应 CLI 彩色缺顶面修复/导出回读 |
| 撤销/重做 | 在标准修复完成后 Cmd+Z，再 Shift+Cmd+Z | 撤销恢复缺顶面与原着色；重做恢复补面；对象/盘归属不变 | 待 GUI；核心事务测试不替代交互 |
| 保存重开及切片 | 标准修复完成后另存 `standard.3mf`，关闭后重开；切片并导出 `standard.gcode` | 补面与颜色保留；10 mm 模型有连续挤出路径，不为空；有效配置变化和警告均回传。不要真实发送打印 | 待 GUI；CLI 有修复后切片，尚无此工程 GUI/CLI 同参路径比对 |
| 健康模型 | 新建工程导入 `tests/data/model_repair_acceptance/00-healthy-cube.obj`，选择同机型/工艺，运行保守修复 | 提示无需修复，几何与位置不变 | 待 GUI；对应 CLI conservative-healthy-* |
| 工作中取消 | 在需要耗时修复的实际模型上启动修复，进度窗口点取消；若瞬间完成，记为“未覆盖”而不是通过 | 取消后模型、颜色、选择和盘不变，不留下半个事务；保存取消后工程与完整日志 | 待 GUI；核心已有 worker/分壳取消覆盖 |
| 英文 | 切换英文、按应用提示重启，在原始工程再次打开策略窗口并选保守 | Conservative repair / Standard repair / Cancel 与拒绝原因英文完整；不执行自动重建 | 待 GUI；中英文源串与 MO 已检查 |

回传到 `planning/quality/smart-engine/gui-results/` 或用户指定目录：`gui-run.log`、`cancel.3mf`、`conservative.3mf`、`standard.3mf`、`standard.gcode`、策略及结果提示截图，注明操作时段。文件保存和重开是否成功须实际操作确认。

## 真实模型与仍不能安全自动处理的情况

- 用户原托架：`/Users/liwei/Library/Containers/com.tencent.xinWeChat/Data/Documents/xwechat_files/wxid_naol68t5y06922_46e9/msg/file/2026-09/(圆角)永久方案托架20260918.stl`，SHA-256 `5df90e7a3122aae5dbb09d5897b074ebc4db2f3197aa0c839741e4e66eebbca8`。
- 当前普通 CLI 标准模式：退出 0，862 面/114 开边 → 856 面/0 开边，`repaired / mesh=passed`；导出在 `planning/quality/smart-engine/bracket-standard/`。保守模式：进程 231 / JSON -25，无法证明表面保真，保留 862 面/114 开边，`unresolved / committed=false`，不输出冒充修复成功的 OBJ；证据 `bracket-conservative/result.json`。
- 上述真实模型首个试跑因隔离 datadir 的父目录未建而在应用初始化退出 134（未开始修复），日志 `bracket-{standard,conservative}.log`；创建父目录后重新运行的真实业务结果在 `bracket-{standard,conservative}-r2.log`。不将初始化错误解释为修复算法错误。
- 保守模式不处理需新增表面的开放孔、无法建立可靠源表面的非流形/退化组合，不能仅因部件小就自动删；标准模式仍可由用户确认采用。体素或 AI 重建尚未接入，不能宣称这些失败已解决。

## 后续实施边界

第一阶段尚需完成：特征前单位/变换/壳体方向准备契约，风险和差异的细化报告，以及 GUI 验收。本批不改变公开七项检查、正常壳体空腔方向和导入默认行为；也不声称局部坐标距离阈值能证明任意缩放下的世界尺寸保持。

第二阶段尚未实施：后端任务/产物身份、Blender v2 特征、有效配置解析/锁定和设备限制、可解释覆盖、两轮试切片闭环。已有后端 TemplateRuleEngine 不能当作该新闭环已完成。

第三阶段尚未实施：同一问题模型库的 Blender 局部/体素候选对照及固定策略准入；本轮只建立现有 CGAL 两策略的真实模型基线。

## 未验证项

- 自动化/CLI 以上述明确范围通过；GUI 全部待验证。尚未形成后端闭环、备选修复验收或实际质量提升的打印证据。
- Windows/Linux 与实际打印尚未验证；不得从 macOS 自动化推断通过。
