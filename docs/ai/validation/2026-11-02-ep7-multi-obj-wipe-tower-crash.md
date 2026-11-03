# EP7 多 OBJ 切片 139 崩溃分析

## Linux 2.3.2 打包分支合并验收

目标为 `linux/release2_3_2`。以 `f7df7e11c8` 合入 `master@d726803049`，含前置 OBJ 第三纹理坐标兼容修复。产品源码自动合并，文档保留双方模块内容。合并后的 macOS arm64 Release 应用及两套测试程序均编译通过，25 个 CLI 专项、43 个 OBJ 用例（5987 项断言）、3 个换料和冲刷用例（134 项断言）全部通过。原三模型按 6 份和 7 份默认配置重切，以及 App 显式绑定的重放均通过，非注释指令数分别为 96465、96465、96459。原三分量纹理 OBJ 和两分量对照各产生 12516 条非注释指令。5 组分别与 master 相应结果逐条一致。实际 Linux 打包、GUI 手工和实机换色仍待验。本地构建、输入、命令及核对结果存于 `/tmp/orca-release2_3_2-20261103-obj`。

## master 验证状态与基准

**用户新增约束（2026-11-02）**：保持原有指定耗材及 App 多色绑定的处理逻辑；不得将 MTL 自动颜色合并扩展到显式耗材绑定路径。用户随后授权继续优化，最新实现及验证见文末“修正普通多配置的耗材重绑定”；现已明确授权通过检查后提交并推送。

**最新结果（2026-11-02）**：已按用户要求修正普通不同耗材被重绑定的风险，仅收紧CLI颜色合并条件。新arm64 Release应用编译通过，25项专项通过，新增4组的对象/面绑定、温度流量及打印指令与保留旧编号的控制路径一致；20个前轮成功场景指令不变。原三模型6/7份默认配置重放及显式绑定重放均成功，指令与前轮一致。详见文末“修正普通多配置的耗材重绑定”。本次按用户授权提交到master并推送codeup/master，未部署；GUI手工、生产Linux、实机未验。

此前验证保留：21个多OBJ专项、3个核心用例/134断言、7个实际/历史输入重放及14/30项外围CLI回归通过；15个已覆盖正常场景非注释G-code逐条一致，原任务显式绑定96459条指令不变。GUI手工、生产Linux及实机未验。

当前已完成多 OBJ 统一槽位、旧冲刷矩阵自动补齐、MTL 黑色误读修复，以及审核后的耗材参数/排序边界兼容完善。最终构建及结果以文末“修正普通多配置的耗材重绑定”为准；此前记录和产物保留供对照。本次提交包含上述修复、测试及对应模块文档，未部署；GUI 手工及实机待验。

2026-11-02：按用户“最小最优范围、不要影响其他”完成本地修复、arm64 Release 应用/测试目标构建及专项回归。原任务保持三个模型、位置和擦料塔开关重放，退出0；修前相同输入可复现SIGSEGV。范围为普通多OBJ旧自动配色、共享擦料塔边界及矩阵兼容、共享MTL读色，未改服务、生产模板或数据库，未提交、发布。Linux线上重放、GUI操作及实机打印未验证；下一步按下方清单验收。

- 源码目录 orca-slicer-master，master HEAD `0af3a6692da440078f4006d200828745e34872ff`。
- 保留既有用户改动：support.md、2026-10-19-cli-fixed-position-slicing.md 和未跟踪的 2026-10-25-cura-raft-first-layer-gap.md。
- 复现程序：`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，文件日期 2026-10-27，版本 `2.3.2.3-Release`，SHA256 `aaf7539e8a55e62c3a9b2d0448abb5db261f4fe89151177bd7e9450ac72b18b8`。这是修前对照程序；修后身份及验证另列，不代表与生产 Linux 二进制一致。
- CodeGraph MCP 未暴露，本工作目录无 .codegraph；已安装 CLI 在 ~/.local/bin/codegraph。按模块文档和源码定位，未初始化索引。
- 原文件、模板、还原配置、命令、运行日志在 `/tmp/ep7-1566775918693703680/`。

## 当前追加：MTL 黑色解析修复

用户要求修正旧 MTL 黑色误读。根因已确认：`Ka=1,1,1` 与明确的 `Kd=0,0,0` 被相加得到白色。共享 `load_obj` 改为优先明确 Kd，缺 Kd 才回退 Ka；新增材质字段存在标记，颜色缺省初始化为零。保留透明度、UV/纹理、显式槽位绑定、几何与变换流程。共用读色、GUI 回调回归、单/多 OBJ 黑白逐面对照及三个原文件重放均已通过；本轮重新构建程序，具体证据见文末。

## 生产证据

任务 id=331539，SN=1566775918693703680，模型 SN=1405907855808376832；EP7，standard，模板 `1527683455341273088`「无支撑无brim」。时间 2026-11-02 11:28:09.017388～11:28:13.726752，记录耗时 4698 ms，退出 139，进度 0.66。容器 print3d-slice-7cd4cc664b-njzdp，服务 IP 10.128.33.102，Orca 2.3.2.3-Release。

三个模型 scale 均为 1、rotateX=90°；modelSource=import，useModelPosition=true，未带 models[].mulColors。模型文件位于 `https://cdn-h.gongfudou.com/print3d/permanently/2026-11-2/`，文件名分别为：

1. 5a7029e4-8978-4a51-8799-1352075733a8.obj
2. 7585da61-1c4a-432d-a9cf-7e28989bd528.obj
3. c4249978-6cd8-45cd-8448-69719a2c54ee.obj

每个 OBJ 均有同名 MTL。第一份含云朵白、夜空黑、苹果红、柠檬黄材质；后两份均为柠檬黄。旧自动映射对第一份报告 4 槽／3 色，后两份各报告 2 槽／1 色；槽数不等于原文件材质数。

数据库 main/standard profile 只读核对，模板 URL：`https://cdn-h.gongfudou.com/print3d/prod/permanently/2026-8-5/b9cbdba5-61bc-4f16-baaa-56ba2c9be1d3.3mf`。Grafana 实际 process 日志确认 `enable_prime_tower=1`、`min_length_factor=0.5`。

线上完整 stdout 确认：

```text
restore OBJ multicolor filament colors after loading filament configs, color count 2
flush_volumes_matrix after computed: 0,245,355,0
```

最后管道进度为 Detect overhangs for auto-lift、total_percent=66；这个进度文本不代表精确崩溃位置。线上未取得 core；以下调用栈来自本地复现。

## 原因

切片服务 hasModelMulColors 只检查请求的 mulColors。本请求未提供，未使用 --model-filaments 显式绑定，进入旧 OBJ 自动配色路径。

OrcaSlicer.cpp 在逐文件导入时直接把 obj_multicolor_filament_colors 赋成当前文件的颜色表。后面的单色 OBJ 覆盖了前一个多色 OBJ 的全局颜色表；随后只按两个槽生成 2×2 冲刷矩阵，前一个模型仍然引用更高槽位。

Print::_make_wipe_tower 调用 WipeTower2::prime，后者直接访问 `wipe_volumes[tools[idx_tool-1]][tool]`，没有验证矩阵维度和工具索引，发生非法内存访问。

```text
EXC_BAD_ACCESS (address=0x4)
Slic3r::WipeTower2::prime(...) + 1132
Slic3r::Print::_make_wipe_tower() + 1412
Slic3r::Print::process(...) + 3312
Slic3r::CLI::run(...)
```

LLDB 故障指令 `ldr s0, [x8, x27, lsl #2]`，x8=0、x27=1，与上述冲刷量读取对应。Java exec 返回子进程 exitValue；本地 SIGSEGV 与生产 139、同阶段和同矩阵异常吻合。

## 对照验证

使用原 OBJ/MTL 和线上模板还原关键配置。仅按服务 staging 行为将 OBJ 的 mtllib URL 改成本地文件名，原始内容保存为 .obj.original；几何未改。配置来自模板 machine_settings_3、process_settings_4、filament_settings_2，补 CLI 包装字段和数据库机器名称。未取得生产进程的完整磁盘配置快照。

| 对照 | 结果 | 解释 |
| --- | --- | --- |
| 原顺序三个对象，擦料塔开启 | SIGSEGV，returncode=-11 | repro.log、lldb-stack.log |
| 只保留首个多色对象 | 退出 0 | 该对象单独切片通过 |
| 把多色对象移至最后，位置跟随对象保持 | 退出 0，4×4 矩阵 | 支持颜色表被后文件覆盖的判断；不能证明颜色正确 |
| 原顺序，关闭擦料塔 | 退出 0 | 绕开崩溃路径，未解决错误颜色映射 |
| 原顺序，统一显式材质映射，擦料塔开启 | 退出 0，4×4 矩阵 | 首对象四材质绑定槽 1/2/3/4，两个黄色对象均绑定槽 4 |

显式对照使用相同耗材物性，颜色 #FFFFFF/#000000/#F15060/#FFD84D。日志确认六条材质绑定；生成切片包 ZIP 检查通过，内含非空 G-code，并出现 T0/T1/T2/T3。对照产物不是已验证的生产实机打印包。

## 原分析建议（本次实际范围见下文）

1. 引擎统一多个 OBJ 的全局材质槽，并同步重映射面材质／体积挤出机、颜色表及冲刷矩阵，不能保留“最后一个文件覆盖全局”的逻辑，也不能只补数组长度。
2. 擦料塔入口校验 N×N 矩阵与所有工具索引；错误时返回可读配置错误，避免 139。prime 的前工具访问还需覆盖单工具边界。
3. 服务对带 MTL 的多 OBJ 请求补齐显式材质绑定，或使用修复后的自动映射；不能仅依据 mulColors 是否存在就认为已覆盖所有多色输入。
4. 回归需覆盖对象顺序、重复颜色、单色与多色混合、槽位边界和 GUI/CLI 一致性；下一步实施修复后再做生产同版本 Linux 对照。

不需要为本例调整首层高度、短墙系数或缩放。调换对象顺序／关闭擦料塔仅是诊断对照，不能作为完整修复。

## 本次最小修复

- CLI仅对多个普通OBJ、没有显式耗材绑定且不是CGAL修复的批次合并自动颜色表。槽1保留默认用途，同色绘制槽复用、新颜色追加；同步后续对象/体积extruder和涂色面索引，不改变网格、位置、缩放或旋转。
- 沿用首文件旧导入配色；含默认槽最多16槽，超过明确拒绝。单输入、显式绑定、混合格式及修复策略保持。
- GUI/CLI共用的Print在创建擦料塔前校验N×N矩阵和全部实际工具范围；WipeTower2::prime再验证矩阵行/耗材参数，空工具返回、单工具不访问前一工具。有效矩阵、冲刷量算法和换料顺序保持。
- 本次未实施上文服务补齐mulColors的建议，修引擎旧路径即可处理本例。服务协议、模板、墙/支撑/筏层及GUI材质映射对话框未改。
- GUI导入已有材质映射回调，不执行该CLI自动批处理分支；两端擦料塔仍走同一共享核心。

### 首轮留下的读色限制（黑色已在本轮修正）

第一份MTL的Ka/Kd组合在旧自动解析中将黑色读成白色，黄色沿用原浮点转8位结果。首轮未改OBJ/MTL读色规则，当时修后自动颜色表为`#FFFFFF/#FFFFFF/#F15060/#FFD84C`，第一槽为默认。显式材质绑定仍能保留黑色。不能把此次“不崩溃”说成原四种材质均已准确还原；该阶段只修后文件覆盖颜色表和矩阵越界；本轮已修正黑色，见文末。黄色浮点转8位的一档舍入偏差不在本轮修改范围。

## 修后构建及验证

构建命令：`cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 2`，成功。日志`/tmp/ep7-1566775918693703680/fix-build.log`，仍有既有编译警告；源码/程序哈希见同目录`fixed-identity.json`。修后应用SHA256为`624585cfaf9a8e602108bd65fa04963c2d9d789930ac6d83f33b8af09dba3ef1`，版本字符串仍为2.3.2.3-Release，不能只凭版本字符串区分修前后程序。

| 验证 | 结果 |
| --- | --- |
| 原失败任务，三个OBJ原顺序/位置，擦料塔开 | 退出0，全局4槽、4×4矩阵；fixed-command/repro.gcode.3mf ZIP通过，G-code为2,327,454字节 |
| 只保留首模型，修前/后对照 | 均退出0；去除注释后38,809行全部指令完全一致 |
| 显式四材质映射，修前/后对照 | 均退出0；去除注释后96,459行全部指令完全一致 |
| `[WipeTowerSlots]` | 6种参数模式，1个测试/9断言通过：正常、越界、缺行、空/单工具、缺耗材参数 |
| `scripts/test_cli_multi_obj_palette.py` | 12例通过：六种顺序、两个单模型、两个显式映射、16槽接受/17槽拒绝；成功例校验逐面RGB及包内非空G-code |
| 原`test_cli_model_color.py` | 14例通过，STL/OBJ混合分组及非法参数策略保持 |
| 原`test_cli_repair_materials.py` | 30例通过，修复策略、缺MTL及多调色板拒绝保持 |
| 英文/简中 | msgfmt格式检查及两条译文读取通过；生成本地简中MO，不提交生成物 |

对照结果在`/tmp/ep7-1566775918693703680/regression-comparison.json`。脚本证据目录如下，含命令、日志和产物；这些是本机临时路径，其他机器需用脚本和对应配置重跑。

- 多OBJ：`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-multi-obj-palette-aizg_aet`
- 颜色分组：`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-cli-model-color-vznimbyj`
- 修复材质：`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-cli-repair-materials-rozyye_1`

## 首轮 GUI 验证交接（历史产物，未执行；本轮用文末新工程）

准确启动应用：`/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-master/build/arm64/src/Release/OrcaSlicer.app`，不要打开另一工作目录或Applications中的旧程序。中文MO已在该构建Resources所指向的本仓库resources中生成。

复核工程：`/tmp/ep7-1566775918693703680/fixed-project/ep7-fixed-project.3mf`。包含三个原模型、位置/旋转、EP7 0.4 nozzle及原模板耗材/工艺，擦料塔开、短墙系数0.5，原几何未改，包内同时有CLI切片结果。仅供软件复核，未验证实机。

| 场景 | 操作 | 预期 | 状态 |
| --- | --- | --- | --- |
| 同配置重切 | 用上述应用打开该工程及项目配置，核对3个对象、4槽、擦料塔开启，重新切片当前盘 | 完成且不崩溃；后两个对象均为黄色，首对象保持工程颜色，注意上文既有黑色读色限制 | 待GUI手测 |
| 保存/重开 | 另存为新3MF，关闭后重开、再次切片，按耗材颜色检查预览 | 对象位置、颜色槽和换料路径保持 | 待GUI手测 |
| 单模型/显式映射 | 单独导入首OBJ切片；另一次同时导入三个OBJ，在GUI材质映射指定白/黑/红/黄，使用相同有效工艺切片 | 正常生成路径；显式绑定保留黑色，后两个黄色对象不串色 | 待GUI手测 |

异常矩阵的核心边界已由单元测试验证；GUI通常会归一化矩阵，不能以删除耗材按钮的表象声称构造了相同越界状态。错误弹窗中英文展示仍未实测。

回传新保存工程和切片结果；GUI日志默认在`~/Library/Application Support/OrcaSlicer/log/`，按本次操作时间段核对。当前无GUI成功、设备下发或Linux部署结论。


## 追加兼容验证

用户要求“能做兼容处理吗，不要报错”。原多OBJ覆盖已由统一颜色槽解决，此轮进一步让可恢复的旧矩阵优先自动兼容，而不是仅报错拦截。

实现仅新增于 `PrintApply.cpp`：在配置差异比较前、调用方配置副本上处理至少两个已配置耗材的矩阵。完整且足够大的矩阵原样保留；过小的完整方阵保留已有配对值（含手动0），缺项按既有GUI `PresetBundle::update_multi_material_filament_presets` 的换出量+换入量规则补齐。缺字段/空/非方阵按已知耗材重建；换色量缺项先回退第一槽同项，再回退140，不用零凑数。非方阵无法确定行步长，不把残缺数据错当旧配对。颜色、温度、倍率和工具映射保持；只记录info，不新增失败或弹窗。无法恢复的实际耗材引用仍保留之前的最终边界检查。

放在apply入口是为了使预览、擦料塔、G-code完整配置及缓存差异均使用同一有效矩阵；不直接改生产模板或用户原文件，不在每次prime临时填数。旧工程文件仍可携带原配置，重新加载时同样兼容。

- 应用与测试目标重新构建通过；日志 `compat-build.log`，测试日志 `compat-tests.log`，均在原任务证据目录。
- `[FlushMatrixCompatibility],[WipeTowerSlots]`：2个测试、13组参数模式、93断言通过。兼容七模式验证了真实0/3双工具、擦料塔生成、非空G-code、导出矩阵/倍率以及同配置再次apply不失效。
- 新构建重新运行12例多OBJ配色脚本全部通过，证据目录 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-multi-obj-palette-7hrjsjto`。
- 原任务、单模型、显式映射三组重新切片退出0；G-code非注释指令均与前一轮相同，产物在 `compat-command`、`compat-first_only`、`compat-explicit_mapping`。
- 原工程只将矩阵改成2×2后重切，日志明确显示 `Auto-completed legacy flushing matrix: 4 -> 16 values for 4 known filaments`，退出0。
- 原工程的空、非方阵、缺字段三个变体均退出0，包内G-code矩阵均为16项；缺字段3MF也可能先由读取器补默认值，直接缺字段进入apply另由集成用例覆盖。证据 `compat-old-project`、`compat-old-empty`、`compat-old-malformed`、`compat-old-missing`。

GUI补充手测：使用本仓库新构建打开 `/tmp/ep7-1566775918693703680/compat-old-project/stale-matrix.3mf`，保持项目参数，重新切片并导出G-code；预期成功，检查原来123/456两个手动配对值保留，导出矩阵为4×4，倍率保持。GUI可能在导入时已经补齐矩阵，应核对实际配置与日志，不把CLI日志当GUI操作证据。GUI与实机仍未执行，未提交或上线。

本轮最终应用SHA256：`8e7b107737ffd8d2316182cb61ac47b290315d7f7c99591a688d541e00fcff80`；源码与测试身份见 `/tmp/ep7-1566775918693703680/compat-identity.json`。最终构建后93断言复验通过，模块文档及差异格式检查通过。


## 追加 MTL 黑色解析验证

原材质 `夜空黑` 明确写 `Ka 1 1 1`、`Kd 0 0 0`。旧加载器在三通道之和均不大于1时叠加 Ka/Kd，将32个黑面读成白面，自动颜色去重继而丢失黑槽。这是读色处理错误，模型无需改材质或重新建模。按照 [Alias/Wavefront MTL 文档](https://github.com/Alhadis/language-wavefront/blob/master/docs/mtl-spec.rst)，Ka 是环境光反射、Kd 是漫反射 RGB；打印配色不应把两者相加。

最小修正位于 `Format/objparser.hpp`、`objparser.cpp` 和 `OBJ.cpp`：每个材质记录 Kd 是否实际出现；明确 Kd 优先，包括全零黑色；没有 Kd 的旧文件回退 Ka，缺省 Ka/Kd 为零。GUI 与 CLI 都经共享 `load_obj` 生效，不添加提示或失败。保留原 Tr/透明度、map_Kd/UV、顶点颜色优先级、显式材质索引、纹理和几何导入路径。已有工程若在旧导入时已经把黑面合并成白面，重切不能凭空恢复原材质，需要重新导入原 OBJ+MTL 或使用下方新工程。

构建 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests fff_print_tests --parallel 2` 成功；`mtl-build.log` 含既有编译警告。源码、应用和两个测试程序身份保存于 `/tmp/ep7-1566775918693703680/mtl-identity.json`，应用 SHA256：`562724902b381f1130ec02daa87459c9a10da60d2072bd5e632a3f245ec676aa`。版本字符串仍为 2.3.2.3-Release，不作为唯一身份。

| 验证 | 本轮结果 |
| --- | --- |
| `[OBJ]` | 43 用例、5987 断言通过；新增两例覆盖黑色、Ka/Kd 顺序、暗灰、红色、Ka-only、Kd-only、缺省、范围约束、每材质标记独立、透明度及 UV/纹理保留，GUI 导入回调收到黑/白两色 |
| `[FlushMatrixCompatibility],[WipeTowerSlots]` | 2 用例、93 断言通过，上一轮矩阵兼容仍有效 |
| 多 OBJ CLI 脚本 | 15 例通过；原12例及新增白色Ka下黑白材质的单对象、双对象两个顺序，逐面颜色与切片包通过 |
| 原三个 OBJ，原位置/缩放/旋转、擦料塔开启 | 退出0；默认槽+白黑红黄共5槽，冲刷矩阵25项；G-code 2,514,847字节 |
| 首个 OBJ 单独自动配色 | 退出0；黑色保留，5槽/25项矩阵 |
| 原三个 OBJ 显式材质绑定 | 退出0；4槽/16项矩阵；96,459条非注释G-code指令与上一轮完全一致 |
| 原文件与导出模型对照 | 原材质面数与导出颜色面数一一吻合；导出的532个三角面坐标与修色前逐面比较完全一致 |

| 颜色 | 原 MTL 对应面数 | 修前导出 | 本轮导出 |
| --- | ---: | ---: | ---: |
| 白 | 168 | 200 | 168 |
| 黑 | 32 | 0 | 32 |
| 红 | 20 | 20 | 20 |
| 黄 | 312 | 312 | 312 |

旧浮点转 RGB8 仍将黄色记录成 `#FFD84C`，本轮没有改为四舍五入（显式配置为 `#FFD84D`），不能宣称所有通道精确复原。黑色恢复后，自动路径的换料次数和冲刷计算会随实际四种颜色变化；显式绑定路径没有变化。

证据：`mtl-obj-tests.log`、`mtl-wipe-tests.log`、`mtl-replay-results.json`、`mtl-color-comparison.json`、`mtl-fixed-project/`、`mtl-fixed-first_only/`、`mtl-fixed-explicit_mapping/`，均在原任务证据目录。15例脚本证据在 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-multi-obj-palette-a75vi55n`。

### 本轮 GUI 交接（待手测）

自动化测试直接调用 GUI 使用的导入回调，尚未操作实际 GUI 对话框/预览。使用本仓库 `build/arm64/src/Release/OrcaSlicer.app`，不要使用其他目录旧程序。

1. 打开 `/tmp/ep7-1566775918693703680/mtl-fixed-project/ep7-mtl-fixed-project.3mf` 并采用项目配置。工程含原三个模型和参数；应有默认白槽及白/黑/红/黄四个材质槽，共5槽。检查首模型黑色细节、后两个黄色对象，保持位置和擦料塔，重新切片成功。
2. 另存新的3MF，关闭后重开并再切片；核对黑面不再变白，切片预览中黑色耗材确实参与。
3. 新工程导入首个原 OBJ（同名 MTL 在同目录），检查材质映射对话框同时显示黑/白/红/黄，手动指定四个耗材后切片；这是原始 GUI 导入路径的手工确认，不能由打开已映射3MF代替。

回传另存工程、G-code 和对应时间段 GUI 日志（`~/Library/Application Support/OrcaSlicer/log/`）。Linux 线上重放、GUI、设备下发与实机打印均未验证；没有修改生产数据库、模板、源 OBJ/MTL，也没有提交或上线。


## 当前改动审核（2026-11-02，未修改业务源码）

范围为 master `0af3a6692da440078f4006d200828745e34872ff` 上当前7个生产C++文件（CLI、MTL三文件、Print、PrintApply、WipeTower2）及配套回归。核对本轮源码/脚本、应用和两个C++测试程序与 `mtl-identity.json` 一致，没有用旧构建证明新源码。CodeGraph MCP仍未暴露，本工作目录没有索引，沿模块文档与源码查调用。原用户筏层/支撑文档不属本轮业务修改范围。

### P1：自动颜色槽扩展没有同步完整耗材参数

位置：`src/OrcaSlicer.cpp:2307` 附近向全局颜色表追加槽、重映射模型，但后续耗材配置仍由传入的 `--load-filaments` 决定。`Print::apply` 通过 `filament_diameter.size()` 确定可用耗材数量，`generate_print_object_regions` 仅在该数量大于1时启用多色区域。因此颜色表和导出OBJ正确，并不能保证实际G-code保留颜色。

复用此前15例的 `order-0` 原样几何/工艺，只改变加载同一耗材配置的份数：

| 配置数 | 项目颜色槽 | 实际G-code工具 | 退出码 |
| ---: | ---: | --- | ---: |
| 8（原回归） | 5 | T1/T2/T3/T4 | 0 |
| 1 | 5 | 仅T0 | 0 |
| 4 | 5 | T0/T1/T2/T3，缺T4 | 0 |
| 5 | 5 | T1/T2/T3/T4 | 0 |

黑白红黄的单模型与多模型用一份耗材配置也均退出0但只输出T0。该问题包含旧单模型入口已有的参数长度缺口；新的多OBJ合并会新增全局槽，不能继续让新增槽缺乏完整参数。此前测试统一给8份参数、只验证导出OBJ逐面颜色与包内G-code非空，未覆盖真实换料工具和耗材数量一致性。

最小修正方向：在自动配色与耗材配置合并完成后统一对齐完整槽位参数，已存在槽位参数原样保留，只有可恢复缺项按确认的默认耗材规则补齐；显式材质绑定保持现有策略。回归要直接检查G-code实际工具与各颜色路径，补单份参数/少于合并槽数/恰好相等的场景，不能只看彩色模型导出。

### P1：旧矩阵保护位置过晚，颜色表缺项仍可崩溃

位置：`src/libslic3r/Print.cpp:2874` 先构造 `ToolOrdering`，到2879才检查矩阵/实际工具。`complete_legacy_flush_matrix`（PrintApply.cpp:1088～1097）仅按颜色数量判断是否补齐：颜色数不足2直接跳过、矩阵阶数不少于颜色数直接保留。颜色表缺项但实际槽位仍存在时可绕过补齐。

隔离复制之前可切片的旧工程，保留原模型及6项耗材参数，将颜色表设为1项或2项、矩阵保留2×2。两种变体当前CLI均 `returncode=-11`（SIGSEGV），没有返回新增的可读配置错误。源码可见 `ToolOrdering` 构造阶段会调用 `reorder_extruders_for_minimum_flush_volume`，其 `solve_extruder_order` 直接以实际工具索引读取矩阵，早于新加保护。此处调用顺序定位来自源码；本轮LLDB在process launch阶段90秒超时，未取得新的调用栈，不冒充栈验证。

最小修正方向：在任何ToolOrdering/换料排序消费矩阵前，按已知耗材参数和实际引用统一兼容/检查。能确认槽位的旧矩阵补齐；无法确认的引用受控处理，不能把防护留到排序完成以后。增补贯穿真实Print/CLI的坏颜色表用例，而非只直接调用WipeTower2::prime。

### 其他范围及验证边界

- MTL明确Kd优先、缺Kd回退Ka在当前共享加载路径未发现新增阻断；不仅黑色，旧Ka+Kd叠加导致变亮的暗色也会恢复为Kd，这是预期读色变化。已有3MF不重新读取原MTL，需重导原OBJ恢复已合并颜色。
- 本轮重新运行 `test_cli_model_color.py` 14例通过：STL分组、混合格式、颜色开关与非法参数；该脚本仅导出/排布，不做打印切片。
- 本轮重新运行 `test_cli_repair_materials.py` 30例通过：缺MTL、显式绑定、ignore材料、修复和多调色板拒绝等；该脚本不代表实机打印。
- 当前构建身份未变，前一轮43项OBJ/5987断言、矩阵2项/93断言、15例多OBJ及原EP7对照仍是对应构建的已通过证据；本轮新增反例优先于此前“常规样例通过”的泛化结论。
- 没有修改墙、支撑、筏层、模型变换、后端、数据库或生产模板。GUI手工、Linux线上和实机仍未验；默认280等兼容冲刷量继承现有GUI规则，不能据软件切片成功承诺实际清色质量。

新证据目录 `/tmp/ep7-1566775918693703680/review-20261102/`：含两份旧工程变体、1/4/5份耗材的CLI命令/日志/产物、summary和review-result。额外通过脚本目录分别为 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-cli-model-color-48d1o4qx`、`/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-cli-repair-materials-_y09soec`。没有提交或发布。


## App 参数链路复核（2026-11-02，只读分析）

本地后端源码基准：Print3DSliceService `feature/f_模型修复独立接入_20261029` / `ea34e78c9dffe4901ba53a8adca1ff7c524a12ef`；主服务同名分支 / `86e740ff731fcbe199b29426198e8e8229e75889`。不将其等同于已部署生产镜像。切片服务CodeGraph虽然status显示最新，函数行号与正文不一致；增量sync修正23个文件后，关键函数位置与源码一致。主服务无索引，按源码核对；没有自动重建。

- App向主服务提交模型/位姿/材质选择，主服务构造SliceReq；Orca CLI由切片服务生成，App不直接传命令行。
- `models[].mulColors` 非空时，`getMultiColorFilamentSettings` 按各模型列表顺序生成耗材JSON；相同ftSn复用文件，`--model-filaments`逐模型绑定。Orca再按配置文件路径去重生成全局槽。slotNo是设备槽位，用于后续consumable映射，不直接作为Orca的T编号；材质列表按OBJ实际usemtl首次出现顺序。
- 当前代码以“任一模型有mulColors”启用整个批次显式绑定，然后遍历每个模型的mulColors；因此同盘所有模型都要带完整列表，包括单色模型。不应仅给首模型，或只把多模型绑定放在根mulColors。主服务只在单模型且模型级绑定缺失时回填根级旧字段。
- 不带models[].mulColors时，逐OBJ通过mtlUrl下载MTL，按实际usemtl所对应Kd颜色去重计数，再把各模型颜色数相加。`buildFilamentSettingsValue`传入1+colorCount份默认耗材参数。本例4+1+1色按当前源码应为7份；此前重放command.json中的6份是本地人工还原配置，不是已确认的服务器原始argv。
- 因此，“多槽只传一份配置”的审核反例是引擎API边界，不能宣称是App有MTL的常规路径必现；MTL缺少/颜色计数不足等情况另需结合实际请求确认。原生产请求确实没有models[].mulColors，走旧自动映射的判据仍成立，不能用缺少显式字段就认定App请求非法。
- 当前EP7显式多色分支还会把process.enable_prime_tower强制设0；旧自动路径沿用模板。该行为是已有服务策略，本轮未修改。
- 手机/iPad Flutter合盘模型toQueueModel会透传上游transformed.mulColors，但不生成绑定；当前抽查H5导出位姿路径没有直接生成该列表，最终原生页面可能再补字段，不能从Flutter透传或后端支持推断所有已发布App请求均已带齐。

建议接口例：三个对象材质分别为白黑红黄/黄/黄时，models[0].mulColors提供4项，models[1]与models[2]各提供同一黄色耗材1项，三个字段slotNo/ftSn/color使用设备选择结果；保留现有URL、MTL及位姿，useModelPosition=true。对应CLI为首模型绑定filament_1/2/3/4，后两模型各绑定同一个filament_4，得到4个显式全局槽；不人为给App增加第五个默认物理料槽，不让客户端拼冲刷矩阵。

本轮未改任何App、后端或引擎业务代码，未执行线上请求或打印。

## 既有耗材绑定逻辑复核（2026-11-02，仅核对）

用户要求：按原有指定耗材或多色绑定处理，不改变既有逻辑。这里按前文 App `mulColors` 路径核对用户所称的“mcolor”；所查 Orca CLI 和后端源码未找到字面字段 `mcolor`，不能把它另行认定为某个 CLI 参数。

- 对照本仓库 HEAD `0af3a6692da440078f4006d200828745e34872ff`：`OrcaSlicer.cpp` 原有 `--model-filaments` 分支（HEAD 3937 行起）按指定耗材配置确定实际颜色，OBJ 材质只负责模型面到槽位的关联。后端 `models[].mulColors` 生成该参数，现有耗材优先级与绑定方式不应修改。
- MTL 自动配色并非此次新引入：同一 HEAD 2278 行起已记录 OBJ 自动颜色，3995 行起在没有显式模型耗材绑定时恢复自动颜色表。此前表述应明确区分已有自动路径与显式绑定，不得把“重复同一份配置文件”说成“只加载一个槽位”。
- 当前新增 `merge_obj_palettes` 条件明确排除 `has_model_filament_mapping`；本轮没有改动显式耗材绑定或加载配置代码。指定耗材的不同物性、颜色、顺序与用户选择不能被自动颜色去重或默认值覆盖；后续兼容缺项也不能擅自改变既有绑定策略。
- 原任务重放通过只证明已测自动路径；仍保留前述两项审核缺口及未验证范围，不据此宣称所有指定耗材入口均已验证。
- 本轮只读取当前差异、修前 HEAD 和后端绑定逻辑，并记录约束；未修改业务源码、后端或生产数据，未重新构建、提交或部署。

## MTL 自动配色的使用入口与 Git 来源（2026-11-02）

用户追问 App 是否使用以及何时修改。核对当前源码及 Git 提交内容，以下日期均为北京时间提交日期，不代表生产上线时间。

| 日期 | 仓库／提交 | 已核对的改动 |
| --- | --- | --- |
| 2026-03-09 09:45:31 | Orca `8aa76d5c30c8`，obj多色切片打印 | `Model::read_from_file` 在没有 GUI 材质映射回调时，按 OBJ 的 MTL 面颜色自动分配槽（默认槽保留，颜色从槽2起），并更新颜色及冲刷配置。是当前自动配色基础分支的来源。 |
| 2026-06-25 19:46:38 | Orca `d12680f1cc6d`，feat:修改擦料塔颜色处理异常问题 | CLI 增加 `obj_multicolor_auto_mapping`，保存每次导入 OBJ 的颜色表；加载耗材配置后恢复 OBJ 颜色并计算冲刷量。后文件直接覆盖缓存颜色表的赋值在本次提交加入。 |
| 2026-06-29 14:51:57 | 切片服务 `02f511631d56`，修复obj多色切片 | 单模型路径按实际引用的 MTL 颜色数量重复加载默认耗材配置；多模型路径完善实际引用颜色计数及公共参数构造。多模型重复配置在此提交前已存在，不能把此日期说成所有后端自动路径的首次加入。 |
| 2026-08-08 18:10:36 | Orca `3b5317e251`，feat:添加多 obj 多色处理 | Git 字符串历史定位到显式逐模型材质槽数据 `model_material_slots` 的加入；本轮未展开该提交全部业务变更。 |
| 2026-08-11 10:14:22 | 切片服务 `491a9ac8aa77`，feat:适配多 obj 模型 | 从根级 `mulColors` 转到 `models[].mulColors`，按实际耗材参数生成逐模型 `--model-filaments`；没有模型级绑定时继续生成旧 `--load-filaments` 路径。 |
| 2026-08-12 16:39:52 | Orca `900d9be43bd6`，feat:修改多 obj 耗材颜色解析 | 显式 `--model-filaments` 从指定耗材配置取得实际颜色；两处恢复 MTL 自动颜色的判断新增 `!has_model_filament_mapping`，避免覆盖显式绑定颜色。 |

当前 App 后端链路可进入自动分支：OBJ/MTL 有可解析面颜色，且最终请求未生成逐模型显式耗材绑定。当前多模型服务以 `hasModelMulColors` 决定显式／旧路径，不以“是否来自 App”决定。已有模型级 `mulColors` 的请求走显式绑定；仅因未提供该字段不能断定请求非法。

线上实际触发证据为用户直接提供的任务331539日志：2026-11-02 11:28:09，`OBJ config update: extruders nums =4, color nums =3`，紧接着 `OBJ multicolor auto mapping detected, color count 4`；对应三个模型请求没有 `models[].mulColors`。这能确认该线上任务进入自动路径，但不能据此声称所有 App 入口或所有已发布 App 版本均不传绑定，亦未据此识别具体客户端版本。

本次11月2日未提交改动是多OBJ颜色表合并、矩阵兼容及已记录的MTL黑色解析修正，不是首次引入自动配色。此次历史查询没有修改业务源码或生产配置。

## 保留既有绑定的兼容完善（2026-11-02）

用户授权“优化处理，不要影响之前线上的处理逻辑”。在本地master同一HEAD上完善既有未提交修改，没有改App、后端、生产数据库或模板，没有提交或部署。

### 实现边界

- 既有 `mulColors` / `--model-filaments` 的参数生成、配置合成、真实耗材颜色优先级均未改。CLI自动合并额外排除 `--load-filament-ids`、克隆、混合格式及修复；同色但不同显式配置仍为不同槽，温度/流量保持各自配置。
- 普通OBJ自动颜色需要更多参数时，在读取耗材配置前补全有效加载列表。只有同一路径的默认配置复用，或已有 `use_first_fila_as_default` 明确规则时才追加；已传条目及顺序保持。不同配置缺项且无明确默认时不猜测材料，使用既有配置错误出口。单文件不执行多模型颜色合并；单文件自动配色的缺参数同样按明确默认兼容，正常足量参数不变。
- 共享 `Print::apply` 的矩阵兼容同时考虑 `filament_diameter` 实际参数数量和颜色表，保留旧方阵已有配对（含手工0）/倍率。不重建缺失颜色，不修改指定耗材。
- `ToolOrdering` 求解器在第一次索引矩阵前验证参与及起始工具，空集合直接返回。保留既有排序算法，后续Print/WipeTower2保护继续保留；不能再等排序完成才检查。
- 已授权的MTL明确Kd/缺Kd回退Ka修复保持，本轮没有继续修改共享MTL读取；没有新增自动选择真实耗材或改变原有优先级。

### 构建与回归

- 构建：`cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 2` 退出0。日志 `refinement-build.log`，仍有既有编译警告。
- 当前应用：`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA256 `f9d47fb203b84633b4c28bb82f3dff1300eefa6dac5e29bb916a079bf95481cc`。源码/测试/脚本及应用身份见 `/tmp/ep7-1566775918693703680/refinement-identity.json`。
- 核心：`[WipeTowerSlots],[FlushMatrixCompatibility]` 共3个用例、134断言通过；包括10种完整Print生命周期矩阵场景、缺色表、保持手工配对及重复apply、排序前置边界。
- 多OBJ：21例通过，核对逐面导出颜色、实际G-code工具、1/4/5份默认配置、单模型单份默认、16槽单份默认、显式同色不同温度/流量。修前16个基线场景包含1个正常拒绝；其余15个正常场景的全部非注释指令与修后逐条一致。脚本结果目录 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-multi-obj-palette-7pxu0539`，基线 `orca-multi-obj-palette-rt_8z9fj`。
- 原任务/历史输入7场景通过：6份默认的原重放、按当前后端逻辑7份默认、原显式绑定、审核反例1/4份默认、颜色表仅1/2项的两份旧工程。结果 `/tmp/ep7-1566775918693703680/refinement-replays/summary.json`。原显式映射96459条指令与前次结果完全一致；默认自动路径恢复全部T1/T2/T3/T4。
- 两份旧坏工程退出0，运行矩阵从4项补成36项，保留原T1/T2/T3。它们来自黑色修复前的旧工程，内嵌原G-code本来只有这三个工具，不能要求兼容矩阵时凭空恢复黑色。初次重放脚本误按四个工具断言而失败；已检查输入内嵌G-code确认，并只重跑这两个场景。CLI生成的项目配置元数据保留原输入2x2矩阵，实际G-code/Print使用补齐矩阵；再次切片会重新兼容，不宣称已经改写原工程配置。
- 旧脚本16槽用例此前只提供8份参数并检查导出OBJ，新增实际工具断言发现丢色。正常基线改为16份（不放宽断言），另增加16槽单份默认的兼容用例。
- 其他CLI回归：`test_cli_model_color.py` 14例、`test_cli_repair_materials.py` 30例通过。日志均为证据目录下 `refinement-*.log`，未重复运行与本轮无关的全库测试。

### 验证范围与交接

这里的正常指令对照基线是本轮完善前的本地构建（含此前MTL修正），原显式路径还保留此前与修复前版本一致的证据；不是已验证的生产Linux镜像。线上完整原始argv与磁盘配置仍未取得，6/7份重放分别标记，不能互称线上原文。

GUI共享Print生命周期已自动化验证，实际对话框/预览操作、Linux线上重放、设备下发和实机仍待验证。GUI仍按前面的3步手测清单使用当前新链接的应用；本轮未操作打印机或线上服务。

## 复审：普通耗材列表的重绑定风险（2026-11-02，仅审核）

用户要求审核改动是否有问题、是否影响旧处理。本轮逐项核对 `refinement-identity.json` 的源码/脚本/应用哈希，全部一致；复用该构建，未改业务源码、未重建或部署。

### P1：普通 `--load-filaments` 多配置仍可能被颜色合并重排

位置：`src/OrcaSlicer.cpp:2147` 的 `legacy_obj_batch` 仅排除逐模型绑定、逐文件ID、克隆和修复；没有验证传入耗材列表是否同一默认配置。随后2303行仅按颜色确定全局槽，2318/2327行改变对象/面的槽位引用。3021行的同默认配置检查只保护“配置数量不足时补项”，不能保护配置足量时的重绑定。

构造两个单色OBJ（红、蓝），普通 `--load-filaments` 加载三份配置，温度依次190/205/225℃、流量0.9/1.0/1.1。使用当前同一程序对照：

| 场景 | 红色对象 | 蓝色对象 | G-code工具及温度 | 退出 |
| --- | --- | --- | --- | --- |
| 新增自动合并启用 | extruder=2 | extruder=3 | T1/205℃ 与 T2/225℃，出现换料 | 0 |
| 已有逐文件零编号路径 `--load-filament-ids 0,0`，不覆盖对象原编号且排除新增合并 | extruder=2 | extruder=2 | 仅T1/205℃ | 0 |

该对照用于隔离“新增合并是否改变实际参数”的效果，不是声称重新运行了生产旧二进制。已核对源码中0编号不改变对象extruder，其他影响仅为已有参数校验及跳过新增兼容分支；HEAD原始代码逐文件本地颜色编号保持不变，与对照一致。日志、命令、项目及脚本位于 `/tmp/ep7-1566775918693703680/review-binding-boundary/` 和同级 `review-binding-boundary.py`。

影响仅针对没有逐模型/逐文件明确绑定、通过普通耗材列表传入不同配置的调用方；原任务的同一默认配置复用以及 `mulColors` / `--model-filaments` 显式路径并不由这个反例推翻。先前21例只有“显式绑定同色不同参数”，普通自动路径使用同一默认文件，未覆盖此组合。

建议收紧新合并条件：只有已确认复用同一默认耗材，或已经具备可靠材质到耗材关联时才重映射；不能只凭“没有--model-filaments”认定可按色值重排实际耗材。为普通不同配置的旧指令增加逐对象槽位/温度/流量对照后再提交。

### 其余影响与范围

- 指定 `mulColors` / `--model-filaments` 的原有配置与颜色优先级未修改，本轮未发现该分支新增问题；已有真实96459条指令和同色不同参数对照证据仍成立。
- MTL修正位于共享加载器，因此GUI/CLI、单/多OBJ导入都以明确Kd读色，不仅黑色，旧Ka+Kd相加导致变亮的颜色也会变化。这是已授权的读色修正，不应宣传所有旧OBJ外观完全不变。
- 矩阵补齐仅修改不完整配置的有效矩阵、保持已知配对，正常足量矩阵不变；其默认补项不是设备清色质量的实机验证。旧项目原配置元数据未重写，重开切片会再次兼容。
- 此次审核没有继续修改产品代码。GUI手工、Linux和设备打印仍未验。


## 修正普通多配置的耗材重绑定（2026-11-02）

用户确认修改。产品代码仅在 `src/OrcaSlicer.cpp` 收紧 `merge_obj_palettes`：除原有入口限制外，还要求传入列表非空、首项非空、所有路径与首项一致。不同文件即使内容相同也保守保留旧编号；不根据RGB推断实际耗材，也不因 `--load-defaultfila` 放开合并。原先默认参数补项、显式 `--model-filaments`、共享矩阵兼容和MTL读色不变。

新增4组普通不同耗材回归：两单色模型、反向导入、默认耗材标志、包含多色面的模型；分别与已有逐文件零编号保留路径对照对象/volume槽位、三角面标记及全部非注释G-code，并核对温度/流量配置。该控制路径用于隔离新合并，不冒充运行旧生产二进制。

修正前构建运行 `--bindings-only`，首例如预期失败：期望仅T1，实际T1/T2；日志 `/tmp/ep7-1566775918693703680/binding-guard-before.log`，输入/产物 `orca-multi-obj-palette-00syyg44`。该测试确实捕获原风险。

已完成：`cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 2`，日志 `/tmp/ep7-1566775918693703680/binding-guard-build.log`。已核对master/HEAD、Ninja Multi-Config/arm64/Release及未有并发构建；新构建后的完整专项、原任务和显式绑定重放结果如下。

本轮新增条件仅属于CLI多文件耗材列表管理。原生GUI经交互选择耗材，不解析 `--load-filaments`；无对应新增GUI开关或管理流程。本轮不改GUI或共享库源码，既有共享MTL/矩阵修复的GUI手测限制继续保留。


### 本轮结果与边界

- 应用构建退出0（既有编译告警保留），版本仍2.3.2.3-Release；实际二进制 `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` SHA256 `148469b729e62dc40f4dda1b43d8d754ba0c0c36e89493955e0e63ca17c3f147`。源码、脚本和程序身份见 `/tmp/ep7-1566775918693703680/binding-guard-identity.json`。
- `scripts/test_cli_multi_obj_palette.py` 全部25项通过（含1项预期拒绝）。新增4组与旧编号控制路径的对象/体积、三角面属性和非注释指令完全一致；正向、反向及默认标志用例均只使用T1，实际温度指令为 `M104 S205`，各1995条指令；含多色面用例保留T1/T2/T3、16569条指令。未以成功退出替代材料对应关系核验。
- 与上一轮完整结果 `orca-multi-obj-palette-7pxu0539` 的20个成功场景逐条G-code一致。本轮证据 `/var/folders/yf/phh21zl17kq613y4x_6dc0n40000gn/T/orca-multi-obj-palette-caz6jmmg`，总日志 `/tmp/ep7-1566775918693703680/binding-guard-cli-tests.log`。
- 原三个模型以本地还原6份默认配置、按当前后端规则7份默认配置重放，均退出0、保留T1/T2/T3/T4、96465条指令；显式绑定重放退出0、T0/T1/T2/T3、96459条指令。三种输入的非注释G-code与前轮各自成功产物完全一致。6份是本地还原指令，7份是当前后端生成规则，仍不声称已取到线上原始argv。命令、输入与结果见 `/tmp/ep7-1566775918693703680/binding-guard-replays/`，日志同级 `binding-guard-replays.log`。
- 本轮产品代码仅修改CLI合并门槛，没有重新改变共享MTL读色、矩阵兼容或显式绑定；共享核心源码/测试不变，不重复累计上一轮核心测试结果。旧MTL配色在不同普通耗材场景下仍沿用原来的槽位/颜色行为，本轮优先确保不重绑定实际耗材，不按RGB自动修复这类历史对应关系。
- GUI无新增入口，本轮未手工操作；整组修复的既有GUI清单继续待验。Linux线上二进制和真实设备打印均未验证。未提交、部署或修改服务/生产配置。

文档收尾：已复核并更新CLI、多材料、测试工具3个模块正文及内容指纹；`python3 scripts/check_ai_module_docs.py --all` 检查35模块、0问题，`git diff --check` 通过。对上一轮源码/测试/构建身份逐项核对，仅CLI入口、专项脚本和应用二进制变化，共享核心及其测试源码保持原身份。


## 提交交付（2026-11-02）

用户明确授权：保持既有流程、修复验证正常后提交并推送。提交前复核13项源码/测试/程序哈希与已验身份一致；25项专项、3个核心用例/134断言及原输入重放证据有效，20个已有成功场景指令一致。完整工作区文档检查35模块、0问题，暂存快照另外执行检查。不重复构建未变化的源码，不以本机结果冒充生产Linux或实机验收。

提交目标为本仓库master跟踪的codeup/master，推送前fetch确认与基准同步。仅纳入多OBJ默认配色、原耗材绑定保护、矩阵兼容/边界检查、MTL读色及其测试/翻译/模块文档；不纳入既有support.md、2026-10-19固定位置记录和2026-10-25首层间隔记录。没有修改服务、生产配置、模板、模型变换或部署程序。

GUI手工、生产Linux重放、实机换色仍待验，具体GUI操作沿上文交接清单；本次提交不能作为这些项目已通过的证明。
