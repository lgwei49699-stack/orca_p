# 2026-10-29 原尺寸模型盘外排查

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

- 目标：分析原日志模型及用户追加STL的摆盘/支撑情况，按需交付模型修正版；不修改业务源码或部署。
- 基准：`codex/model-feature-analysis-20261018 @ aa39c82eb278014fa9046b44daf2fd932905db8f`；开始时已有多份模块/验证文档、参数审核脚本及result.json改动，全部保留。
- 输入：`/Users/liwei/Downloads/Explore-logs-2026-10-29 17_58_14.txt`；最新请求缩放1、旋转0、位置(60,50,-53.2)，arrange=true、auto-plate=0、spacing=0、ensure-on-bed、ignore-wipe-tower。日志无返回变换JSON，进程退出0。
- 已完成：同模型/同配置完成5组CLI对照。原尺寸原配置摆盘失败；关闭支撑或显式间距2mm均原尺寸转90度后居中成功；93%缩放成功。原因是默认树状支撑预留及单盘失败输出规则。未修改业务源码。
- 输入与配置保存在`planning/arrange-log-20261029/`（本地产物，不提交）；STL SHA256 `e3c4dabea96b074f517cf0e18aa63a2834326e5ca2afe30238dea40a2e9684a1`。
- CodeGraph MCP未暴露，CLI v0.9.6及现有索引可读；OrcaSlicer.cpp、Arrange.cpp/.hpp、ModelArrange.cpp、PartPlate.cpp的索引SHA256与当前源码逐一匹配，使用query/callers辅助定位。未sync或重建，不声称覆盖所有动态调用。
- 当前状态：动态支撑误判已确认。原配置开启支撑、100%转90度固定居中实际切片成功，悬垂层数0、支撑耗材0、无支撑路径；根因是预检查把被底板承托/包埋的上层图案底面当成悬垂。原因分析及模型副本修正完成，业务源码未改。并集STL保持原尺寸，通过完整模型检查、原配置100%自动摆盘和切片；已交付下载目录，移动端重新上传待用户验证，线上Linux身份未验证。

- 追加模型状态：GUI失败已复现并定位。当前1026_AI工艺启用Cura筏层，粗支撑预检查使每侧预留15.5mm且不能压缩，导致原尺寸盘外。同GUI程序/完整工程仅改传统模式且筏层层数0即可100%自动居中并切片；保留Cura、固定转90度居中也切片通过，含线宽的实际模型/筏层占地105×52.086mm。此前“无需处理”仅适用于旧日志工艺；引擎仍未修改，GUI绕过操作待用户验证，步骤见文末。
- 当前追加任务：修复规模评估完成，建议按中等改动安排：共享排布预估局部调整，疑似误拒对象按需复核，补两端/铺满及性能回归。不是降低12mm常量即可完成；第二份模型有1层悬垂但最终无树路径，仅增加内部面排除也不足以证明解决。方案尚未实现或压测，业务源码未改。
- 最新决定与分析：用户暂缓通用修复、要求仅分析替代方案。本轮新增临时CLI对象参数对照，确认“只关闭本对象支撑、保留Cura筏层”可以100%自动转90度居中；与开启支撑但手动固定90度的基线相比，51930条非注释指令完全一致。推荐该现有配置方式处理已确认的本模型，GUI设置/原工程/模型/源码均未更改；通用自动识别仍未解决。

## 配置与原因

九次请求的machine/process/filament分别逐JSON比较一致。最新请求为17:57:12.820（日志27行），日志9行记录命令；未传model-scale表示保持1。工艺为enable_support=1、support_type=tree(auto)、support_style=default、no_brim、raft_layers=0、skirt_loops=0、by layer。ignore-wipe-tower且实际日志need_wipe_tower=false。模型厚度约3.6mm，限高110mm，不是超高。

1. 本体原始范围X=±19.91379356、Y=±52.5、Z=±1.79999995，尺寸39.827587×105×3.6mm。原朝向Y超出100mm床面；平面转90度后本体尺寸105×39.827587mm，能放入120×100mm床面。
2. `ArrangeParams`默认每侧缩床1mm，日志确认可排布床为(1,1)..(119,99)，即118×98mm。
3. `ModelArrange.cpp:178`的`estimate_arrange_support_margin`检测到开启自动树状支撑且存在床面以上向下表面，选用12mm单侧预留（文件22–30行常量）。本体11220面低于20000面扫描上限；按该判据检查存在2621个床上向下面，例如804号面Z=-0.25，在原底面-1.8以上。这是保守占地估算，不是切片后真实支撑轮廓，也不代表每处都需要12mm支撑。
4. `Arrange.cpp:144`在min_obj_distance=0时取该默认预留；0不是“没有安全量”。以转90度的长边计算，105+12×2=129mm，超过缩床后的118mm。实际排布日志输出bed=-1、unarranged=1。
5. 失败后`PartPlate.cpp:4781`把未摆入对象移到盘外区域。本例X=39.827587/2=19.913793，Y=100−105/2−100×1.2=-72.5。`OrcaSlicer.cpp:6480`在单盘模式仍应用该偏移，再把盘号归0；6855行导出保留真实XYZ。因此plate_index=0及exitCode=0均不保证几何在床内。日志185行（17:53:36）下一次请求的(19.913793,-72.5)与本地复现完全一致，支持此归因。
6. `--ensure-on-bed`只保证底面贴床，不能把XY越界对象自动塞进床面。输入负Z在`--model-position`阶段就被固定底面Z=0处理（OrcaSlicer.cpp:2780），不是此次XY失败原因。`orient=false`关闭三维定向，排布Z旋转仍启用；日志allow_rotations=1。
7. `calc_exclude_triangles:Unable to create exclude triangles`来自禁放区显示几何初始化；本例bed_exclude_area只有退化点0x0。关闭支撑后成功排入的对照同样有该日志，因此它不是本次盘外的原因。

## CLI复现

复用当前arm64 Release应用`build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`；SHA256为`148a4adba161abe68aa4f9f0117c1fbe8aa359786c20a5ee018f6d8db31342e4`，与2026-10-29提交审核及2026-10-28构建验证清单一致。相关排布/CLI源码自f9bcf0afe0后无变化，源码/资源/测试无未提交差异；未重复构建。最新分支HEAD及本轮源码/配置/模型哈希见本地`identity.json`。

请求参数保持日志含义，仅替换本地路径、debug从1提高到4，并显式写入scale=1；各对照只改变表内变量。所有命令、完整日志、result.json及summary.json保存在`planning/arrange-log-20261029/`，cwd分别为输出子目录，未覆盖根result.json。

| 对照 | 实际导出结果 | 判定 |
| --- | --- | --- |
| 原尺寸、原配置、间距0 | position=(19.913793,-72.5,0)，rotationZ=0，scale=1 | bed=-1，盘外 |
| 原尺寸、输入旋转90度、其余原值 | position=(19.913793,-72.5,0)，最终rotationZ=0 | 仍未排入；失败会改变试探姿态 |
| 原尺寸，仅enable_support=0 | position=(60,50,0)，rotationZ=90，scale=1 | bed=0，盘内 |
| 仅缩放0.93 | position=(60,50,0)，rotationZ=0，scale=.93 | bed=0，盘内 |
| 原尺寸，仅arrange-spacing=2 | position=(60,50,0)，rotationZ=90，scale=1 | bed=0，盘内 |

第一阶段全部导出进程退出0，不能将5组计为5次摆盘成功。当时未执行切片或打印，关闭支撑和间距2仅用于原因对照；随后实际切片见下方动态支撑复核。

93%时Y=97.65mm已小于缩床98mm，触发`Arrange.cpp:146–150`按当前多边形剩余空间缩小inflation（约0.175mm）；原尺寸Y=105mm未触发该分支，因此会出现缩小一点后突然能排入的行为。该估算与旋转候选之间的适配值得进一步修正，不能据此断言实际树状支撑已安全。

## GUI/CLI及后续方向

GUI `ArrangeJob::prepare_arrange_polygon`与CLI共用`get_instance_arrange_poly`，均调用同一支撑预估与inflation更新；这属于共享核心的排布约束。CLI单盘强制盘号0及成功导出语义另属CLI层。本轮只做源码核对和CLI运行，GUI/移动端未操作。

若后续修复，应分别处理支撑占地预估过于保守/与候选旋转不一致，以及摆盘失败仍导出成功的表达；不能只把盘外JSON位置强改中心来冒充几何适配，也不能未验证实际支撑就全局取消边距。本次无业务改动，无提交或部署。

文档收尾：`python3 scripts/check_ai_module_docs.py --all`通过（35模块、0问题）；本轮模块文档差异`git diff --check`通过。仅新增本分析记录并追加arrangement模块入口，保留原有未提交改动，未暂存或提交。

## 用户截图补充核对

用户补充两张移动端照片后重新核对：第一张为红色长条薄板、100%缩放，界面尺寸40×105×4mm，与本轮STL精确尺寸39.827587×105×3.5999999mm的整数显示一致。第二张请求/响应中的STL地址为同一个`8650ee36-773b-4875-82db-47e718f9e466.stl`，响应positionX=19.913793、positionY=-72.5、positionZ=0、modelScale=1，与本轮original复现逐值对应。因此此次模型身份已得到截图佐证；截图不是最新17:57请求响应的时间证据。只记录业务字段，不转录截图中的认证信息。

树状支撑12mm指后台工艺启用支撑后算法采用的保守预留，不是从照片认定薄板实际需要12mm支撑。该阶段实际支撑仍未切片验证；随后新增运行结果见下节。

## 动态支撑判断复核（完成，确认误判）

用户指出该薄板可能不需要支撑，要求核对已有动态计算。当前`has_arrange_overhang`只扫描床面以上的向下三角面，未核查该面下方的模型承托；命中后树支撑预留采用固定12mm。需用实际切片确认，不能将向下面计数等同真实支撑需求。计划运行原配置及仅关闭支撑两组，均100%原尺寸、Z旋转90度、位置(60,50)、关闭再次排布。证据子目录为`slice-support-on`/`slice-support-off`。

### 实际切片与修正结论

两组均复用上述已核对的当前二进制/输入/机型/耗材，关闭arrange、auto-plate=0、orient=0、100%缩放、Z旋转90度、底面中心(60,50,0)、ensure-on-bed。仅enable_support开关不同。命令、日志、G-code和result.json均已保存在计划子目录，`support-comparison.json`汇总结果。

- `slice-support-on`退出0，原工艺enable_support=1、tree(auto)/default；运行日志182行明确`Tree support overhang detection done. 0 layers with overhangs. nEnforced=0`。支撑耗材0.0g，G-code无Support或Support interface路径，最终warning_message为空。
- `slice-support-off`退出0，支撑耗材同为0.0g，G-code无支撑路径，无悬空警告。两组模型总耗材都为9.44g。
- 两组非注释指令并非逐字节一致（87493/87480条，预估3210/3209秒），不宣称完整G-code等价；本任务确认的是开启自动支撑后真实生成器仍不需要生成支撑。

网格按相同坐标共享顶点分组得到4个拓扑分量。底板804面，Z=-1.8..-0.20；其余三个分量底面Z=-0.25、顶面Z=1.8，底面嵌入底板约0.05mm。预检查命中的第804号三角面就是上层分量底面，并非独立悬空面。该分组不是布尔并集后的物理零件计数，不能用它声称模型有4个分离零件。

已有动态计算并未缺失：`estimate_arrange_support_margin`会在`has_arrange_overhang=false`时保留默认1mm；但`has_arrange_overhang`当前只判断面是否高于全模型最低Z+首层高度，以及法向是否朝下（ModelArrange.cpp:84–98），没有检查下方的模型承托、重叠壳内部面或逐层真实悬垂。此模型的图案底面被该粗判命中，于是直接选固定12mm树支撑预留（190–198行）。实际切片采用逐层几何检测，得到0悬垂，两个阶段判断不一致。

因此应将原因精确表述为“已有动态支撑需求预检查误判”，而非“只要启用支撑就必须12mm”，也不是该模型真正需要12mm支撑。前两轮已证明12mm导致盘外，但没有证明真实支撑需求，本次补充切片纠正该解释缺口。

后续若修复，落点应是共享`has_arrange_overhang`的承托/内部面判断，保留已覆盖的真正悬垂、斜放立方体尖尾、手工强制支撑等场景；不能简单删除12mm保护或只改CLI返回坐标。GUI/CLI共用该预检查，需共同验证。本轮用户仍在询问原因，未修改业务实现。

动态复核收尾：模块文档全量检查通过（35模块、0问题），模块文档diff检查通过；两次实际切片退出0。业务源码、测试、资源未修改。

## 模型副本修正（完成，本地CLI验证通过）

用户询问能否修改模型解决。制作新STL副本，对底板与三个上层重叠壳做实体布尔并集，不缩放、不改引擎、不覆盖原STL。采用独立本地几何工具处理，并用当前Orca完整模型检查、原配置摆盘及切片验证；比较尺寸和生成的支撑情况。产物和工具环境放入本地planning目录，不提交。

### 处理与交付

使用仅安装在本地planning独立虚拟环境的manifold3d 3.5.4（Mesh64双精度布尔并集）及numpy 2.0.2，对四个闭合壳依次求并集。没有体素重建或缩放，未安装到系统Python/项目依赖。输出为一个闭合实体；面数11220→11172；原包围盒每个坐标逐值相同，尺寸39.8275871277×105×3.5999999046mm；原2621个首层以上向下面已全部消失。重叠体积只计一次，体积计数由原壳相加7988.308343变为并集7955.573357mm³，不代表删除外部设计体积。

输出副本：`/Users/liwei/Downloads/长条薄板_实体合并_原尺寸.stl`，SHA256 `6799fed62de23f672ec7644e1c0c221c8b7576b9bf29ea0bc73a182336cb2d49`。原STL SHA256重新核对不变。几何报告`union-geometry.json`、交付报告`union-delivery.json`位于既有planning目录。

- `union-check`：当前Orca完整检测退出0；check_complete=true、mesh_status=passed，开边、退化、重复、非流形边/顶点、方向、自交七类检查均完整且计数0。未请求修复；普通导入也显示not_needed，无删面/反面等修复动作。
- `union-arrange-slice`：保持原machine/process/filament（enable_support=1），arrange=1、auto-plate=0、orient=0、arrange-spacing=0、原输入旋转0、缩放1。退出0，输出盘0、位置(60,50,0)、旋转(0,0,90)、缩放(1,1,1)。切片成功、支撑耗材0g、总耗材9.44g、无支撑角色路径、warning_message为空。未将原模型的失败盘外坐标强改成成功；实际由引擎自动摆入。
- 外形通过布尔并集操作及相同精确包围盒核对；没有声称完整G-code逐指令一致或完成实机打印。原始固定摆位切片与并集后自动摆位切片预估时长略有差异（3210/3207秒）。

### 移动端交接（待用户验证）

1. 重新上传下载目录的`长条薄板_实体合并_原尺寸.stl`作为新模型，避免继续引用旧STL地址或旧变换。
2. 选择EPONEPro和相同工艺，缩放保持100%，执行一键适配；预期自动转90度并回到盘内中心。显示尺寸可能按旋转后的轴显示105×40×4mm。
3. 若仍显示红色盘外，提供新请求的模型URL及变换响应，核对确实使用新文件；本地预期position=(60,50,0)、rotateZ=90、modelScale=1。

本次交付是该模型的修正版，不是引擎通用误判修复；其他含重叠内部面的模型仍可能触发原问题。业务源码、测试、资源未修改，也未上传服务、提交或部署。

## 第二份模型检查（旧日志工艺下通过，不能外推GUI当前工艺）

用户新增`/Users/liwei/Downloads/4b1b2aff-4d4b-4479-b862-190c22df74b8.stl`。SHA256 `3b2095d113d7dd66600ce1bc2e054ea37bbe2b932d66e74c3b2cac95dc7c1013`，15970面，尺寸40.086208×93×3.6mm；按精确坐标焊接为1个拓扑连通壳，初步有563个首层以上朝下面，不能直接套用上一模型的四壳并集结论。准备使用同身份程序、原EPONEPro配置进行完整网格检查及100%自动摆盘切片。原件只读，证据放在planning/arrange-log-20261029/second-model/。

### 第二份模型验证结果

复用SHA256已再次核对的当前arm64 Release应用（148a4ad...42e4）及首份日志原配置，没有改模型、缩放、间距或支撑开关。

- `original-check`退出0：check_complete=true、mesh_status=passed；开边/退化/重复/非流形边/非流形顶点/方向/自交七类检查均完整且0缺陷，未请求修复。
- `original-arrange-slice`退出0：arrange=1、auto-plate=0、orient=0、spacing=0、enable_support=1；原尺寸自动居中position=(60,50,0)、rotation=(0,0,0)、scale=(1,1,1)，无需转90度。切片成功，支撑耗材0g、总耗材8.53g，无Support/Support interface路径，warning_message为空。
- 本例563个首层以上向下面均位于原始Z=-1.0，拓扑上只有一个闭壳且完整自交检查通过，不沿用上一份“四个重叠壳”的解释；不能为了消除朝下面而删除合法几何。粗支撑预检查仍会命中，但本体Y=93mm已小于缩床后98mm，现有inflation按剩余空间收敛至约2.5mm，足以排入。
- 与上一份105mm长板相比，本例长度93mm、短12mm；第一阶段正常通过，不需要生成所谓修复副本。原文件哈希复核保持不变。证据汇总`second-model/summary.json`。

移动端对应操作：重新上传本次`4b1b2aff-4d4b-4479-b862-190c22df74b8.stl`，选择相同EPONEPro配置、100%缩放后点一键适配；预期无需旋转即可居中盘内。手机端未操作；若与本地不同，应核对实际上传URL、参数和返回变换，不能把该已测原件预先归为相同故障。引擎代码、原STL均未改动。

第二份模型补充口径：日志实际为`1 layers with overhangs`，最终支撑耗材/支撑路径均为0；与首份0悬垂不同，不将这份的粗判直接称为内部面误判。未做实机打印验证。第二份结果写入后模块文档检查通过（35模块、0问题）。

## 桌面GUI失败复核（已定位，未修复引擎）

用户确认失败在一键适配后的盘外，并澄清为桌面GUI；截图模型15970面、40.0862×93×3.6mm。已通过CUA只读读取实际窗口及本地日志，不再要求手机请求。此前把“本地旧工艺CLI通过”推及用户GUI是验证范围错误。

- 运行GUI PID1466，路径`build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer`，SHA256 `b0f013aba4136052df22f5a5103d33214c43dda38c163310dc5dff66ada2f02e`，不同于此前CLI构建树148a4ad...42e4；未切换或替换应用。
- GUI日志`~/Library/Application Support/OrcaSlicer/log/debug_Thu_Oct_29_17_47_38_1466.log.0`在18:37:46/job17及18:42:23/job18明确movable=1、unarranged=1、has_unpackable_items=true；床1..119×1..99、间距0、旋转开启、逐层，确认真实失败而非颜色展示误差。
- 当前工程来自Downloads/f1ff8580-46e6-407b-8d2f-5f805341b09a.3mf；GUI工艺为EPONE Pro 树状支撑 常规生成-15%填充 1026_AI，耗材EPONE Pro-PLA 1026 AI。当前自动备份确认raft_mode=cura_v1、base_layers=1、interface_layers=2、surface_layers=2、base_margin=6、interface_margin=2.5、surface_margin=2；旧日志配置未启用此Cura筏层。raft_layers=0是传统筏层字段，不代表Cura筏层关闭。无对象级覆盖。

### 同程序、同完整工程复现

从活动备份`orcaslicer_model/Thu_Oct_29/17_52_09#1466#130`复制`.3mf`元数据，并解开其独立压缩的`3D/Objects/4b1b2aff-4d4b-4479-b862-190c22df74b8.stl_5.model`成员，组装为本地`second-model/gui-config/gui-complete-v2.3mf`。只补齐备份外置网格，没有修改工程有效配置、对象几何或原备份；完整project_settings包含耗材密度1.24和直径1.75。GUI实际程序b0f013...2e与CLI构建树148a4ad...42e4交叉对照：两个程序用旧工艺均排入、用GUI工艺均盘外、仅改raft_mode=legacy均排入，因此本次差异由有效配置解释。

| 完整工程对照（均使用GUI实际程序） | 结果 | 证据目录（second-model下） |
| --- | --- | --- |
| 原工程、自动排布、间距0、100% | 导出(20.043104,-66.5,0)，旋转0，盘外；进程0不等于摆盘成功 | gui-full-project-arrange-v2 |
| 仅raft_mode改legacy，原raft_layers仍为0，自动排布并切片 | (60,50,0)，旋转0，缩放1；切片退出0，支撑0g，总耗材9.58g，warning为空 | gui-full-project-no-cura-arrange-slice |
| 保留完整Cura工艺，关闭自动排布，固定Z旋转90度、底面中心(60,50,0)、100% | 切片退出0，总耗材14.76g，筏层计入support_filament_g=5.16g，warning为空 | gui-full-project-fixed90-slice-v2 |

固定90度G-code头与配置确认filament_density=1.24、filament_diameter=1.75、raft_mode=cura_v1。解析所有非Custom挤出路径端点，并按各段线宽向外加半线宽；无挤出圆弧，包围盒为X=7.5..112.5、Y=23.957..76.043，即105×52.086mm，完整位于120×100床面内。模型本体外墙X=13.5..106.5、Y=29.957..70.043。Support角色仅Z=0.32、Support interface仅Z=0.62/0.92/1.12/1.32，对应1+2+2层筏层；模型挤出从Z=1.78开始，无模型高度内的额外树支撑路径。实际悬垂检测仍报告1层，不能改写为0悬垂。范围核对不包含机型自定义开始/结束G-code中的清料、擦嘴动作，不代替实机打印验证。报告为对应目录`extrusion-bounds.json`。

### 排布拒绝的具体计算

当前共享`estimate_arrange_support_margin`仍会命中此模型的床面以上向下面，树状支撑粗估为12mm；default树样式走Organic/SupportCommon路径。Cura基础层外扩6mm、表面层外扩2mm及细扩0.5mm，使额外支撑扩张为max(0,6−2−0.5,2.5−2)=3.5mm；最终每侧预留max(6,12+3.5)=15.5mm。

`get_instance_arrange_poly`把这个含保守树支撑估算的整体值写入minimum_inflation，`Arrange.cpp`不能像旧工艺那样按床内剩余空间缩小到约2.5mm。即使转90度，长边93+2×15.5=124mm，大于缩床后118mm，排布失败后按既有规则移到盘外。实际切片仅需105mm长边，因此本例是排布占地高估，尺寸、模型完整性并非失败原因。动态判断存在，但仍是向下面粗检+固定支撑预留，未使用该模型实际切片支撑占地；Cura最低预留使误差直接阻断摆盘。不能把筏层本身的物理占地保护全局去掉。

后续通用修复仍应落在GUI/CLI共享的支撑需求与占地估算，区分实际筏层边界和未证实的树支撑外扩，并保留真正悬垂、手工支撑、斜放尖尾及筏层越界回归。本轮仅定位和对照，没有修改引擎、模型、用户工艺或活动工程。

### 复测中的无效尝试（保留，不计通过）

- 初次抽取preset配置为from=user且inherits为空，CLI以239拒绝不兼容；更正副本元数据from=system后才得到交叉排布结果，未改用户预设。
- 抽取的单独filament_settings并非完整继承结果，初次固定90度切片出现耗材0；其结果不作为同GUI有效配置证据，已由完整工程复测替代。
- 首次组装备份误把外置.model压缩包原样嵌入，解析返回250；解压该单成员后完整工程读取成功。首次完整工程固定切片的--model标记仍用了STL名而非输入3MF名，返回254；改为匹配输入名后重新执行成功。各次command.json/run.log/result.json保留在不同目录。

### GUI操作交接（复现已确认，绕过操作待验证）

无需新构建；继续使用上述正在运行的GUI。中文界面，EPONE Pro 0.4 nozzle、120×100床、当前1026_AI工艺/耗材，选中15970面的第二份STL，100%缩放。两种绕过方法择一；不把手动摆位成功当成自动排布已修复。

| 场景 | 具体操作顺序 | 预期 | CLI对应 | GUI状态 |
| --- | --- | --- | --- | --- |
| 原状复现 | 当前Cura工艺，一键适配/自动排布 | 模型仍被移至盘外 | gui-full-project-arrange-v2 | 用户截图及18:37/18:42日志已确认 |
| 保留Cura筏层 | 选中此STL，旋转工具把Z设为90°；移动工具把X=60、Y=50，执行放置到打印板/贴床；不再点击自动排布，点击切片单盘，预览首层和筏层 | 缩放100%，模型在床中；含筏层占地约105×52.09mm，切片成功；重复自动排布仍可能再次移到盘外 | gui-full-project-fixed90-slice-v2 | 待用户验证 |
| 不需要筏层时 | 全局工艺→筏层→筏层模式改“传统模式”，传统筏层层数保持0，再点自动排布并重新切片 | 不旋转也能原尺寸居中，自动支撑仍开启，实际支撑0g | gui-full-project-no-cura-arrange-slice | 待用户验证 |

已读取现有GUI日志，无需再次索取相同证据。若绕过操作表现不同，记录操作时间，并另存当时工程3MF以核对有效配置/对象变换；不要求提供认证头。GUI本轮仅查看界面并切换到筏层页签，未更改参数、保存或替换活动工程。实机打印未验证。

本轮收尾：完整工程三组结果与路径包围盒汇总在本地`second-model/gui-full-project-summary.json`；原STL与GUI程序SHA256复核不变。`python3 scripts/check_ai_module_docs.py --all`通过（35模块、0问题），arrangement模块差异`git diff --check`通过；src/tests/resources无未提交差异，无需构建。未暂存、提交或部署。

## 修复规模评估（源码分析，未实施）

用户询问改动大小。本次重新核对同分支/HEAD及src/tests/resources无差异；CodeGraph MCP仍未暴露，CLI与已有索引可读。ModelArrange.cpp/.hpp、Arrange.cpp、ArrangeJob.cpp、TreeSupport.cpp、TreeSupport3D.cpp、PrintObject.cpp、TriangleMeshSlicer.hpp、test_model_arrange.cpp九项内容哈希与当前源码一致。图谱未识别get_instance_arrange_poly和generate_support_preview符号、estimate调用者解析部分失真，已按源码补全；这是符号解析覆盖限制，不是索引缺失或过期，未重建索引。

### 入口与难点

- 核心为ModelArrange.cpp:43的has_arrange_overhang、178的estimate_arrange_support_margin、697的get_instance_arrange_poly。GUI ArrangeJob.cpp:106、CLI OrcaSlicer.cpp:5718/5964/6077及FillBedJob.cpp:59/172复用同一转换，因此业务判断可以共享；自动排布、组装盘、当前盘和铺满都需核查，不能仅改一个界面入口。
- ArrangeJob.cpp:482通过call_on_main_thread执行prepare，后者读取模型并调用预估。当前最多检查约20000个三角面；直接塞入截面/支撑生成会增加UI阻塞。若需要较重复核，应使用不可变输入快照在工作线程执行，支持取消并缓存同轮重复实例；对象配置、涂绘、变换或网格变化后必须失效，不能用旧切片结果放宽新对象。
- TreeSupport::detect_overhangs依赖PrintObject逐层轮廓、喷嘴/线宽、支撑阈值、小悬垂过滤、强制/禁止区域、桥接和尖尾规则；会清理支撑层/预览缓存，不是可直接安全调用的无状态布尔查询。PrintObject::is_support_necessary是告警分类，亦不能作为“实际支撑占地为零”的等价证明。
- 本例实际detect_overhangs结果为1层，Organic后续仍只输出筏层。仅修重叠内部面、恢复角度阈值或只检查悬垂列表为空，不能据此保证解决第二例。要以最终无树路径作为豁免依据，需要进一步复用支撑生成/过滤或证明一个更窄的安全条件。
- 项目已有PrintObject.cpp:3322的generate_support_preview（slice后generate_support_material），GLGizmoFdmSupports.cpp:791–821已异步使用，提供复用起点；但其轻量准备与正式流程不必等价，remove_bridges_from_contacts读取周长和填充面，未生成时不得擅自视为相同结果。精确复核也要区分筏层与支撑路径，不能用support_filament_g>0判断有树支撑。
- 当前测试有真实支撑越界历史：test_model_arrange.cpp:1021记录普通支撑低估导致Y=101.199超过101mm床；1052的约44.6°斜方块需要尖尾树基座，原角度预检查曾漏掉。降低12mm或移除Cura minimum_inflation可能让这些场景回归。

### 方案边界与推荐

| 方案 | 规模判断 | 限制 |
| --- | --- | --- |
| 单纯降低边距、删minimum_inflation或薄板一律免支撑 | 改动行数少 | 不能证明真实支撑/筏层仍在床内，不建议 |
| 增强轻量几何分类并保守回退 | 局部小到中等 | 可能消除部分内部面误判，但第二例必须另证；不承诺一次覆盖所有支撑规则 |
| 快速估算保留，只有疑似支撑预留导致拒绝的对象做受限复核，再重试排布 | 中等，建议优先验证 | 复核用共享核心，GUI工作线程/CLI同步接入；支持取消、输入身份和缓存；证据不足继续保守。真实筏层/固定障碍/床面约束必须保留 |
| 所有对象每次摆盘都先完成真实支撑计算、全面采用精确轮廓 | 中到大 | 需处理重复实例、床面与候选位置依赖、缓存失效、取消和性能，首轮范围不宜扩大到此 |

推荐路线的业务修改预计集中在共享预估/复核辅助逻辑及GUI/CLI接入，可能需Arrange的失败重试入口；现有支撑生成算法可作为复用对象，不必重写生成树、填充或G-code。实际文件数量取决于复核原型能否复用当前预览准备，尚不报未经验证的行数或完成工期。首先验证两个实物输入上“只预览支撑”的结果是否与正式切片一致，再决定准备阶段范围。

现有test_model_arrange.cpp有33个TEST_CASE声明（静态清点，不表示本轮运行），已有筏层物理边距、普通/树/Organic样式、对象覆盖、固定对象、禁放区、单/多盘、取消及大网格检查基础。新增应包含两个输入或等价确定性夹具、内部承托与真实悬垂成对用例、手工/强制支撑、Cura与传统模式、旋转/缩放/镜像及对象覆盖、复核失败/取消回退；核对GUI/CLI/铺满共享规则，记录大模型、多实例和重复排布耗时。较多工作在准确性与交互性能验证，不能以这两个模型能排入代替回归。

本轮只做源码及既有证据分析，不运行旧测试证明新方案、不构建应用、不改变模型/配置/业务源码；后续实现需要最小构建及自动化、CLI、GUI验证。

## 暂缓引擎修复后的替代方案（仅分析，新增CLI证据）

用户明确改动大则暂不处理，随后要求分析合适处理方式。本轮无业务修改、无GUI参数操作、无原模型/原工程修改，仅运行隔离输出目录的命令行参数对照并更新分析记录。分支、HEAD、GUI打包程序b0f013...2e及业务工作区状态与上轮一致。

### 首选：仅本对象关闭支撑，保留Cura筏层

之前只对照“关闭筏层”与“手动摆正”，遗漏了更直接的对象级支撑开关。当前引擎支持该方式：Print.hpp:434–442分别判断has_support与has_raft；ModelArrange.cpp:191–193在enable_support=false时仍返回真实筏层预留；GUI TabPrintModel已提供对象enable_support配置，CLI的--model-support 0也只写对象配置。因此不需要新参数、新界面或修改支撑算法。

复用完整GUI工程gui-complete-v2.3mf（SHA256 08ca64fc8f21e112df421b96f277b6c1a0331f3b46979d9896a3ad6289b4871e）、GUI实际程序；仅命令行追加`--model gui-complete-v2.3mf --model-support 0`，不另存修改后的工程。其余仍arrange=1、auto-plate=0、orient=0、spacing=0、原尺寸、Cura base/interface/surface外扩6/2.5/2mm。本例无强制层、无手工支撑标记。输出在`second-model/analysis-object-support-off-cura/`，含command.json、identity.json、run.log、result.json、plate_1.gcode及comparison.json。

- 退出0，自动导出position=(60,50,0)、rotation=(0,0,90)、scale=(1,1,1)，模型15970面，warning_message为空。
- 工艺仍为cura_v1；筏层保留，总耗材14.76g、筏层计入support_filament_g=5.16g、预估5357秒。
- 与此前保留支撑/原Cura、固定90度居中基线`gui-full-project-fixed90-slice-v2`逐行去除注释及空白后比较：两者均51930条指令，完全一致，SHA256均为e96733eb17a0aa2e3683785aa0862d300f4da559477ad22383896d4ac89e15f9。这里可确认实际指令相同，而不是只比较耗材或时间。文件注释/配置记录不必相同，不声称整个G-code字节一致。
- 输入工程读取前后哈希相同。仅对象支撑关闭后，预留从15.5mm回到筏层6mm；旋转后的长边93+2×6=105mm，自动摆盘可容纳。该结论限定当前模型、姿态和有效工艺；模型或姿态变更应重评支撑需求。

### 方案取舍

| 方式 | 是否改引擎 | 适用与结论 |
| --- | --- | --- |
| 当前对象关闭支撑、保留原筏层 | 否 | 本例首选；自动排布和最终指令均已CLI验证，其他对象可继续使用支撑 |
| 原工艺不变，手动转90度居中后直接切片 | 否 | 已CLI验证；绕过自动排布，每次再点自动排布可能复发 |
| 已确认模型在业务侧保存对象支撑参数 | 否，已有CLI字段可用 | 属参数管理方案；确认无需支撑后传model-support=0或同等对象覆盖，保持排布/切片一致；若model-process同时覆盖enable_support，最终生效以其后应用的值为准 |
| 所有未知模型自动判准，再自动缩减支撑预留 | 是 | 仍是上节中等规模修复，本轮继续暂缓 |

不推荐通过减小排布间距处理当前Cura案例：minimum_inflation仍限制为15.5mm；不推荐删合法模型几何，或把“薄板”“尺寸能放下”直接等同无需支撑。降低筏层外扩会改变实际筏层，优先级低于本例已验证指令不变的对象级方案。也不应仅在排布时临时关闭支撑、切片又恢复而不做一致性核对；对于已确认模型，持久的对象配置更清楚。

### GUI建议操作（仅交接，未替用户执行）

1. 在现有工程选中`4b1b2aff-4d4b-4479-b862-190c22df74b8.stl`，从工艺“全局”切到“对象”，确认选中的是该对象。
2. 在对象支撑设置取消“开启支撑”；保留当前Cura筏层模式与6/2.5/2mm外扩，缩放仍100%。这是对象覆盖，不修改所有模型的全局支撑策略。
3. 执行自动排布，再切片单盘；预期自动Z转90度、XY中心60/50，筏层仍存在，首层及筏层在盘内。对应上述CLI证据；GUI本轮未操作，待用户验证。
4. 若希望后续沿用，可由用户另存项目保留对象覆盖；换模型、改变X/Y倾角或工艺后重新判断支撑，不将该配置当全模型通用规则。

本轮分析结论：对当前已确认不生成树支撑的模型，有无需引擎修改且保持实际打印指令的处理方式；通用的无人干预识别仍是另一项任务。当前GUI设置保持原状，未建立新预设、未上传、未提交。
