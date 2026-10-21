# 模型打印优化：方案与首批指标实现

## 当前基准与下一步

### 2026-10-22 调整范围：非必要不改 Orca（合并本地提交，未推送/部署）

用户要求非必要不改Orca，并明确授权把本次调整并入上一笔提交，不新增独立撤回提交。原本地 `1f6c8c3cb7` 的流量观察浮点容差及配套新增测试已取消，源码/测试恢复到 `2dd0b6b21f`；Orca最终提交只含模块说明和部署交接，不改历史切片/报告接口、自适应层高、修复、分盘或其他WIP。PAM选参读取Blender特征和有效模板，不依赖切后峰值/超cap统计，因此保留已完成检测和参数优化；Web已按指标隔离不完整报告，不能补零、据此抬高流量限制或掩盖未知。

已验证：`git diff --exit-code HEAD^ -- src tests` 为0，整个src/tests与 `2dd0b6b21f` 无净差异。当前Mac arm64 Release最小重建应用及fff_print_tests成功；`[SliceAnalysis],[GCode]` 85项/26987断言通过，原报告CLI脚本10个场景通过（默认关闭/显式关闭/开启的执行指令、有效配置、原结果一致，并核对非法组合/输出保护）。Web新增3项旧报告兼容回归后全量257项通过：只将不完整流量观察保留未知，覆盖/冷却/成本不变、16类参数/实际改动及请求匹配不丢失；未修改Web生产逻辑或新增解析/存储。模块检查35/0问题、两仓diff --check通过。未重跑无代码变化的PAM/Compare/Blender测试，也未冒用旧40次切片为本轮结果。

日志 `/tmp/orca-stable-engine-{build,native,cli,web}-20261022.log`，CLI证据 `/tmp/orca-stable-engine-20261022.8lUfzK/cli/`。当前应用SHA256 `7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`，测试SHA256 `389d7bc510860649f7565bd9502c5791e66dca0ce596435262be0ab9d2b4efac`。build/arm64/src/Release下应用已同步；旧打包副本未更新，不用于本轮证明。Linux实际镜像/GUI/线上联调及实印仍待验；本轮没有远程部署、业务库或打印操作。

发布清单已同步：本批仅Blender/PAM/Compare/Web，Orca/PDS不因本批重打；前提是现有引擎已具备此前的报告/自适应CLI接口。旧引擎浮点问题保留为报告已知限制，不宣称修复；下方Orca必发与86项测试为撤回前历史记录，不能作为本轮当前身份。

本次提交整理：通过ls-remote确认Orca远端仍是2dd0b6b21f、Web远端仍是c8ad968，两个待改写提交未推送。Web改动和3项兼容回归已并入上一笔，最终提交 `5eb39b7c328229d130f9209fc21b12943e4e81e2`（替代c023310）；提交前重跑257项全部通过，日志 `/tmp/model-amend-web-20261022.log`。Orca同样amend上一笔为纯文档提交，最终身份以Git为准，分支不保留单独的流量补丁/撤回提交。Blender/PAM/Compare原提交不变，不推送、不合并其他分支；原无关文档/result.json/下载工具WIP继续保留。test-tools仅选择本任务段落，以暂存内容计算指纹，不纳入下载工具内容。

后续实现边界：模型测量放Blender、模板及参数选择放PAM/Compare、有效值/收益解释放Web；优先使用已支持的Orca参数和CLI。只有确认现有引擎无法完成必要功能或影响正常切片正确性，且服务层不能合理处理时，才单独说明必要性与影响后修改Orca。不得为了报告全绿增加重复扫描或伪造缺失测量。GUI/QA交接：沿用Web清单的新实验与原流程回归；可在本地新应用打开本轮CLI默认/开启G-code，对照路径、估时和耗材，实际操作尚未执行。

### 2026-10-22 审核提交完成（未推送/部署）

用户授权审核并提交当前模型检测/参数优化改动，交付部署和测试方案；未授权推送/部署。按understand-diff及当前源码核对测量、参数优先级/单位/原生默认值、B覆盖及最终请求、报告归因和未改参数显示，未发现本批QA入库阻断项；旧图谱不作为依赖证明，本次不新增业务规则。PDS c606337工作区干净。Orca只提交流量观察修正、对应测试及模块/本记录；test-tools按暂存内容计算指纹并部分暂存，未纳入独立下载工具及其文档段落，工作树保留。其他无关文档/result.json/下载工具/Blender缓存均未提交。

| 仓库 | 分支 | 本批本地提交 |
| --- | --- | --- |
| Blender | codex/model-feature-analysis-20261018 | 12bd816d7534cfd5a5ac25e21282f76880209600 |
| PAM | codex/model-feature-analysis-20261018 | 727e4e65f053386b19fbed43d092002ee4538f0e |
| Compare | codex/model-feature-analysis-20261018 | 78502feeaaf45622bb83b31bd20720a61480b40e |
| Web | develop | c023310c271814678659cb7f85605d55acb75deb |
| Orca | codex/model-feature-analysis-20261018 | 流量观察补丁及本记录随本次提交，确切身份以Git为准 |

当前业务源码PAM155、Compare228、Web254项通过（上一轮重新执行，日志 `/tmp/deploy-readiness-{pam,compare,web}-20261022.log`）；Compare另96既有禁用集成不计通过。本次追加当前Blender几何20/墙几何14/模板35/独立墙4及Orca原生86项、27380断言通过，Web生产构建通过（既有大chunk提示），日志 `/tmp/commit-model-20261022-*.log`。上述程序退出0，Orca测试SHA256 `282d5f353d9b5e2d95134c6a903d24412e2b0bf227984ec8248c9d5316ea9779`、应用 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`；构建依赖dry-run无C++重编需要。已有40次实际切片为同业务代码前轮证据，见下节，不冒充本轮重跑。未访问线上服务/业务库/设备。

各仓选择性暂存文件清单、暂存差异/空白及新增敏感字段模式检查通过；Orca工作树35模块/0问题、暂存受影响2模块/0问题通过。提交前后的未纳入工作树保留，不安装hooks、不改第三方依赖。当前只本地提交，需要后续授权push或用户自行push后CI才能取得这些提交。

部署：Orca Linux引擎（或PDS内置引擎镜像）+Blender脚本+PAM，随后Compare、Web；PDS Java无新增、无新SQL。先等旧测试任务结束，新建实验确认检测/选定模型/参数/请求/G-code同源，核对普通无调整、薄壁/小孔/悬垂/薄片/坡面/高瘦/小层，再抽验修复/固定位置/分盘/筏层流程，最后小批A/B实印记录质量和时间耗材。GUI/目标Linux/真实服务联调与实印仍待验，不把16类关联记录当16类独立精确检测或成品质量已验收。完整步骤、通过条件与回滚/反馈资料已汇总到Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部。

### 2026-10-22 部署准备复核（仅审查，未提交/部署）

用户询问继续优化还是部署测试。本次核对六仓当前差异、检测→PAM覆盖→Compare冻结请求→Orca消费者及Web逐参数展示，未发现本批进入QA前必须继续编码的阻断项；建议固定当前版本配套部署QA，按新实验联调和实印证据再调规则。不是生产放行或16类独立检测全部验收。源码/分支基准与下节一致，PDS Java干净，其他原WIP保留；本轮未改业务代码，仅补此验证记录。understand-diff按当前源码审查，旧图谱不作为覆盖证明。

本次重新执行PAM155项、Compare228项、Web254项通过，Compare另96项既有集成跳过，三个进程退出0；日志 `/tmp/deploy-readiness-{pam,compare,web}-20261022.log`。六仓diff --check通过。Orca应用hash仍为 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`；下节40次切片与Web构建属于同业务源码的前轮证据，本次不冒充重跑。未连接业务库/线上服务、不下发打印，不运行禁用集成。

发布前先审核提交本批相关文件并推送，勿混入无关文档/result.json/下载工具/缓存；当前检查授权不包括提交推送。累计配套Orca引擎、Blender脚本、PAM、Compare、Web，PAM先于新Compare；PDS Java无需因本批单独修改，内置引擎的镜像需更新，无新增SQL。部署后新建同模型姿态实验，验参数记录与实际G-code一致、普通无调整及修复/分盘回归，再测细壁/悬垂/小层/高瘦的成品与成本。16类包含实测选参、原生路径执行和沿用机料配置，不能称每类都独立推算新参数；尺寸补偿/回抽/强度等缺实机标定或明确目标时不造值。GUI/Linux/真实服务联调与实印仍未验，完整步骤见Web交接文档。

### 2026-10-22 继续：小层冷却独立生效（本地完成，未提交）

沿用下节六仓基准与全部WIP，不提交/推送/部署。差异审查发现PAM把小层冷却降速绑定到普通风扇范围完整，合法的slow_down_min_speed=0也被拒；当前Orca CoolingBuffer独立依据层时间、速度下限和开关降速，风扇另行调度。已修正PAM消费条件：仅使用B已有合法时间/最低速度，显式关闭时开启；缺省开关沿用Orca默认开启、已有开启值不重复覆盖。最低速度0表示不额外限制降速，不是停止运动；不猜风扇/时间/速度新数值，不新增模型扫描、请求字段或数据库存储。非法/缺失时间或速度、多耗材配置不造值。Web同步逐参数原因，0时间明确不触发，默认开启和风扇独立保留不再误标无效；所有16类和未改参数仍可见。

验证完成：PAM155、Web254项通过，Web生产构建通过（既有大chunk提示）；先红PAM4/Web2，再修正至全绿。日志 `/tmp/layer-cooling-{pam-final,web-final,web-build-final}.log`。当前Blender真实模型测量→Compare/PAM实际类（仅网络依赖替身）→PDS命令追加→同一Orca六组前后12次切片通过，证据 `/tmp/layer-cooling-e2e-KOOdMC/run1/`，日志 `/tmp/layer-cooling-cli.log`。8×8×5 mm模型、原外墙60、耗材时间8秒/最低10、缺风扇字段时，冷却开关0→1，Z>0.8 mm外墙速度中位数60→10.83 mm/s；显式最低0/风扇0组为60→7.9。长层保持60，已有开启/缺省继承/零时间对照执行指令完全相同。六组几何/挤出指令及风扇配置值前后不变；实际风扇指令仍可能因原生层时间调度变化，不声称它们一律相同。缺风扇组预计总时长414.4→565.7秒，是换取冷却时间，不是提速收益；实物改善尚未验证。

前10组20次原流程与自动层高8次回归也通过，合计40次真实切片；覆盖普通指令不变、细颈/细壁、贴床薄片、小轮廓、自动支撑、自适应及legacy/Cura筏。证据 `/tmp/effective-feature-e2e-yi5fDv/cooling-regression/`、`/tmp/auto-layer-e2e-xBNNnz/cooling-regression/`；日志 `/tmp/layer-cooling-{existing-cli,auto-cli}.log`。上述测试、构建和CLI进程退出0；不连接业务库/线上服务，不下发打印。Compare228项/96既有集成跳过为上轮未变业务验证，本轮不冒充重新执行。

本轮业务增量仅PAM/Web；Orca共享GUI/CLI冷却核心、PDS、Blender与Compare业务无新修改。复用当前Mac arm64应用SHA256 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`，不重复构建未改C++。CodeGraph MCP未暴露、CLI存在但旧图谱不覆盖服务增量，按源码审查，不重建索引。Orca模块检查35/0问题，相关仓差异检查通过，未暂存/提交；无关文档/result.json/下载脚本及缓存WIP保留。

下一步：累计待发布仍为Orca引擎+Blender+PAM+Compare+Web，无新增SQL，PDS Java无新改动。Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部补新实验和原生GUI速度预览清单。GUI/Linux/目标机型/实印待验证，不用软件降速证明成品质量，也不把16类记录等同16类算法全部验收。

### 2026-10-22 继续：解析自动层高边界（本地完成，未提交）

沿用下节六仓HEAD与全部WIP，当前Orca仍为2dd0b6b，不提交/推送/部署。差异审查发现Compare将min/max_layer_height的0作为缺失值，阻断已有几何命中后的参数调整；已在Compare有效配置解析处对齐Orca共享Slicing.cpp：min=0为0.07 mm、正min至少0.01 mm，max=0为喷嘴直径的75%且不得低于有效min。不新增扫描/存储，不改A或模板，不改原生切片算法。保留缺失/无效/混合向量的未知语义，材料只覆盖第一槽的规则不变。最大值也要求确认最小值，避免原生钳制后反而高于服务判断。

验证完成：旧实现5个红例复现，新增6个回归后Compare全量228项通过、96既有集成跳过；PAM151通过。日志 `/tmp/auto-layer-{compare-red,compare-green,pam}.log`，绿测进程退出0。当前Blender实际测量→Compare/PAM真实类（仅网络边界替身）→PDS实际命令追加→当前Orca共8次切片通过，`/tmp/auto-layer-e2e-xBNNnz/run1/`与`/tmp/auto-layer-cli.log`，退出0。0.24 mm贴床薄片从1层0.2变为2层0.12；0.2 mm带坡细壁自适应质量0.5保持，输入过滤75%→31.154%，Z1.5–4.8 mm中段长挤出0→11，最小/首层挤出85%/100%不变。普通模型及自动0/等效显式0.07与0.3对照的执行指令相同，16项完整、A/源不变。old-boundary是当前PAM重放旧Compare传null的行为，不是旧服务镜像；无线上服务/业务库/打印操作。

本轮业务仅Compare工具方法；PAM/Web仅同步交接文档，其他服务和Orca业务源码未新增。复用当前应用SHA256 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`，GUI/CLI共享原层高核心，没有新原生入口。Orca模块检查35/0问题，未暂存，不声称staged已验；无关WIP保留。

下一步：配套累计改动发布测试环境，用Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部新清单核对新实验16项和实际请求；原生GUI对照run1下薄片2层及细壁路径。GUI/目标Linux/实印待验证，不能用路径输出代替打印收益或宣称16类全量算法已验收。旧图谱未覆盖服务增量，按源码审查；不自动提交/推送/部署。

### 2026-10-22 续查：薄壁测量到Arachne过滤阈值（本地完成，未提交）

沿用下节六仓基准与全部WIP，不提交/部署。差异审查技能使用源码降级，旧图谱不覆盖当前服务增量。补上“已选Arachne但原min_feature_size仍过滤掉细壁”的实际缺口。本轮业务增量Blender/PAM/Compare/Web，Orca/PDS核心未改。

- Blender复用原宽度栅格中已识别持续窄笔画的样本，只新增 `narrowStrokeWidthMedianMm / narrowStrokeResolutionMm` 两标量；不加截面/预算/坐标。是已识别笔画的代表宽度，不是全模型最薄处；短分支阳性、失败或未测不能伪造宽度。多模型保留最小可用代表值和配套采样间距，原覆盖语义不变。
- Compare按相同有效配置优先级传 `nozzleDiameterMm / maximumLayerHeightMm` 标量，不复制整份机器快照。PAM按当前宽度身份、喷嘴和层高检查 `min_feature_size`（喷嘴百分比），仅降低过大的过滤值；扣除两像素量化余量及Orca原生轮廓收缩 `height*(1-PI/4)`，精确外墙Inner/Outer取双倍。固定层使用原普通/首层较大值，自适应须有确认的最大层高；缺失/auto未解析时不猜。正宽度不足时不强制钳零。保留最小及首层挤出线宽，窄壁可能被加宽，不宣称尺寸准确或可承载。
- Web特征/逐参数/CSV补代表宽度与采样间距，保留未改参数并显示服务端实际理由；有阈值降低才显示原值→最终值，不把调整记录等同实际路径验证。
- 两次反例推动修正：初版P10被旋转后端部骨架拉低，改用已通过持续性条件的中位数，0.2/0.36 mm、0/17/45°六个夹具在两像素内；这不是通用误差保证。第一次CLI只扣采样余量后虽写入47.25%，细壁仍无路径；源码定位原生轮廓收缩后修正到36.519%（0.4喷嘴、0.2层高），精确外墙为25.789%。不把失败样例隐藏或作为通过证据。
- 当前Blender真实夹具→PAM/PDS类→Orca六组12次切片通过：普通/45°/Classic切换/精确外墙四组中，细壁中段长挤出段0→16；已有25%阈值与0.5 mm宽墙对照前后执行指令相同。证据 `/tmp/thin-feature-e2e-pkWg2M/final/`，日志 `/tmp/thin-feature-cli-six.log`。原悬垂五组10次及原流程十组20次回归通过，日志 `/tmp/thin-feature-overhang-cli.log`、`/tmp/thin-feature-existing-cli-final.log`。支持原自适应/支撑/legacy及Cura筏场景；不是线上服务/打印验证。
- 本地全量PAM151、Compare222通过（96既有集成跳过）、Web252及生产构建通过（既有大chunk提示）；Blender几何20、墙几何14、模板35、独立墙入口4通过。日志 `/tmp/thin-feature-{pam-final,compare-final,web,web-build,geometry-median,wall,template,wall-entry}.log`。用户原STL复测除两个新null字段外与上一版全部逐值相同：这份模型当前检测命中短分支/局部细颈，没有可靠持续笔画宽度，不能据此虚构新的过滤阈值。

GUI待验：导入final/filtered的before/after G-code在Z1.6–4.6 mm看薄肋路径有无；precise对照同验，permissive前后无新变化。Web部署后新建实验核对16类完整、薄壁代表宽度→阈值/原因→冻结请求/G-code，并实印核验加宽后的细壁和相邻间隙。具体交接见Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部。复用同一Orca应用SHA256 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`；无原生GUI业务新分叉。累计部署仍是前批Orca引擎+Blender+PAM+Compare+Web（先PAM再Compare），PDS Java无新增、无SQL。Linux/目标机型/实印仍未验证，不能称全部16类算法或成品质量已验收。

收尾核对：Blender改为同一次分位数计算获取中位数，最新fixtures-final/features.json与原夹具报告逐字一致；纯几何20/墙几何14/模板35/独立墙4重新通过（`/tmp/thin-feature-{geometry-final,wall-final,template-final,wall-entry-final}.log`）。PAM追加直接API保护不误报已改、原生最大层高不得低于有效最小层高的边界测试，151项通过。最新12+20+10次CLI使用新输出目录final、thin-final2、thin-final，三进程退出码均0；真实Compare/PAM本地串接已核对36.519%进入B、16项完整，证据 `/tmp/thin-feature-e2e-pkWg2M/compare-*.json`。五服务差异空白检查及Orca模块检查35/0问题通过，PDS工作区干净；未暂存/提交，未运行staged检查。无关Orca文档/result.json/下载脚本及原Blender缓存WIP保留。

### 2026-10-22 续查完成：悬垂分档是否真正形成慢速（未提交）

基准/六仓HEAD与下一节一致，全部既有WIP保留；本轮不提交、推送或部署。按understand-diff审查影响，图谱仍为旧HEAD，依当前源码继续。发现PAM以任意正数判定悬垂档有效，但Orca忽略小于0.5 mm/s的档位，100%或不低于参考速度的档位也不能形成降速；Web同样可能把仅开关和正数标为有效。已用回归复现并修正：仅在整套明确无有效慢速时复用B已有慢速；分别检查内/外墙，保留已有有效分档/未知输入，兼容原生插值取整和变化不超过1 mm/s忽略语义，不改变原生相对速度解释或高瘦规则。本轮只改PAM/Web业务及文档，不新增几何、请求或存储，不改A/源模板及原修复/分盘路由。

最终本地：PAM146、Web251项全量通过，Web生产构建成功（既有大chunk提示）；日志 `/tmp/effective-overhang-{pam-final,web-final,web-build}.log`。新闭合阶梯模型经当前Blender真实测量→PAM/PDS实际类→同一Orca二进制五组十次切片通过，证据 `/tmp/effective-overhang-e2e-rD5lvc/run2/`。原100%、80绝对值、0.49档没有有效减速；补B已有25后悬垂外墙60→25、承托外墙60不变，内墙局部80→68为原生插值。已有50%有效档及无悬垂方块两组指令严格相同，未制造差异。前10组20次真实切片回归通过，证据 `/tmp/effective-feature-e2e-yi5fDv/overhang-regression/`，覆盖薄壁/细颈、小柱、贴床薄片、自适应、自动支撑及legacy/Cura筏；日志 `/tmp/effective-overhang-{cli-final,existing-cli}.log`。

GUI/CLI共用引擎本轮未改，复用当前应用SHA256 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`。原生GUI待验：载入run2/percent/after/output/plate_1.gcode按速度着色核对局部25/外墙60，对照before为60；已有50%保持。Web部署态新建实验核对16类完整、最终请求与G-code；步骤已写Web交接文档顶部。累计发布仍需前轮Orca+Blender+PAM+Compare+Web，无新SQL；本轮无需再改Orca/PDS Java。Linux/目标机型/实印仍未验，不以减速证明成品改善。

失败记录：先红PAM3/Web2，改后全绿；第一次真实切片验证在第三组比较`.49`与原生序列化`0.49`时失败（前两组路径已有效），修正临时验证器为数值+百分比单位比较，使用全新run2完成全部10次，不改生产配置绕过。既有20次回归也使用新输出目录，未覆盖原证据。

收尾：上述Maven/Web构建/两批CLI进程退出码均为0；三仓差异空白检查通过，Orca模块检查35/0问题。未暂存/提交，不声称staged检查已执行；无关文档、result.json与下载脚本WIP保留。

### 2026-10-21 再续：小孔轮廓有效检测与改参（本地完成，未提交）

继续核心目标：补有效检测到实际参数执行。本轮仅增量修改Blender/PAM/Web及本记录，Orca保留上轮流量补丁，Compare/PDS业务源码无本轮新改动。基准仍为Orca `2dd0b6b`、PAM `c3e9239`、Blender `e197e83`、Compare `4b24977`、PDS `c606337`、Web `c8ad968`，保留全部既有WIP。无提交、推送、部署、业务库或打印操作。

已完成：小孔内轮廓原未参与小轮廓决策，现复用已有截面环新增 `minimumHolePerimeterMm` 一个标量；不增加截面数、坐标或模型扫描。PAM共用小轮廓规则，孔边按外墙半线宽预留名义中心线偏移，仍用原2倍线宽阈值及B慢速；这是选参范围，不是精确工具路径或实物孔径。另已确认小轮廓速度0：GCode导出器语义为50%外墙速度，但CLI配置下限1会拒绝；几何命中时转成有效绝对速度，不原样传0。不为等于/快于原外墙的档位新启用阈值。高瘦使用同一解释并防二次减速，保留与小孔规则叠加；悬垂缺省策略不扩展。Web测量表/16类逐参数关系/CSV展示新依据，保留未改值，不把孔周长当孔径或尺寸补偿。知识图谱为8月旧HEAD；CodeGraph MCP未暴露、CLI可用但本仓有2文件过期且不覆盖服务增量，按源码继续，不写伪图谱覆盖结果。

本地最终：纯几何18、Blender模板35/独立墙4、PAM142、Web249及生产构建通过，0失败；Web仍有既有大chunk提示。新孔模型4组前后8次切片通过：Classic/Arachne下0.8 mm方孔外墙60→30 mm/s，外围大轮廓60不变；4 mm孔保持60；原0自动档改成30后CLI通过。0档对照为其原生50%含义，不能说原0已成功切片。原10组20次真实CLI回归通过，含普通无调整指令相同、固定细层/贴床薄片、legacy/Cura筏、悬空支撑、自适应及小轮廓。日志 `/tmp/small-contour-{geometry-green,template,wall-regression,pam-final,web-final,web-final-build,cli-final,existing-cli}.log`，新证据 `/tmp/small-contour-e2e-WDf9pz/final2/`；回归证据 `/tmp/effective-feature-e2e-yi5fDv/small-contour-regression/`。复用当前未变Mac arm64引擎SHA256 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`，不重复构建未改引擎。

真实用户STL重新检测：17个原选定截面，除新增最短孔内轮廓0.061754 mm外所有原指标逐字段相同；这是极小网格轮廓，不作为实物孔径或缺陷。原最短外轮廓已经触发该策略，新增孔证据未再制造改参差异；当前PAM新旧证据的process/filament覆盖及自适应选项一致，日志 `/tmp/small-contour-user-pam.log`。没有以丰富报告为由重复修改模板。

GUI/部署交接见Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部。累计仍需Orca引擎+Blender+PAM+Compare+Web，无SQL；当前新增不要求再改Orca/PDS Java。GUI/浏览器部署态、目标Linux和实印未验，不宣称孔径精度或打印收益已验证。原生GUI可载入上述classic-hole/after/output/plate_1.gcode按速度着色核对小孔30/外圈60，before同为60；大孔对照维持60。新实验的模型识别、参数冻结和实际请求必须同一模型/姿态，不回写旧实验。

失败保留：新增红例确认几何2项缺字段、PAM5项失败、Web2项失败，修正后全部通过；最初系统Python版本不匹配隔离numpy，换当前Blender Python3.13后才形成有效红绿结果；临时布尔并集孔夹具非流形失败，改独立闭合立方体减孔并验证流形后执行。验证原0的临时脚本因/private/tmp路径替换未命中而触发已有报告保护，未覆盖原产物；修正路径以新输出目录复测，不把该失败算作0参数验证。

最终收尾：当前CLI原0确实被参数范围校验拒绝（退出238），有效转换后的30已在上述新切片成功；日志 `/tmp/small-contour-zero-rejection2.log`。Orca模块检查35/0问题，Orca及Blender/PAM/Web差异空白检查通过；未暂存，未运行或声称staged检查。现有无关文档/result.json/下载脚本及Blender旧__pycache__保留。

### 2026-10-21 继续有效检测与参数生效（本轮两项完成，未提交）

当前结论：已修复真实模型流量报告失效，并补高瘦百分比慢速到实际G-code的消费，Web完整列出关联参数/来源；未把其他未知项改成通过、未加扫描或重复存储。累计整体目标仍不能宣称16类都是独立精确检测或已证明实印更好：温度/孔补偿/回抽等没有新增测量或标定依据时仍保留具体模板值及用途。用户授权可以修改这些字段，不等于可以推算未测物理量。本轮实际新增改动仅Orca/PAM/Web；前轮Blender/Compare增量全部保留，PDS Java不变。

最终验证：Orca Mac arm64 Release应用/fff测试重建成功；`[SliceAnalysis],[GCode]` 86用例/27380断言通过，真实原G-code的默认/采集对照1项通过（10338045逐字段断言，末次无资源错误）；Python验证器83项、CLI开关/保护10场景通过。PAM136、Web246及生产构建、Compare220通过/96既有集成跳过；Compare→当前PAM本地真实类桥接确认有/无人工记录均可优化，未访问服务/业务库。日志 `/tmp/effective-flow-release-{build,native,file}.log`、`/tmp/effective-flow-validator.log`、`/tmp/effective-flow-cli-regression.log`、`/tmp/effective-tall-{final,web,web-build,compare,compare-pam2}.log`。测试先红后绿，未将0项发现或跳过计为通过。

当前应用SHA256为 `5d7a652b269e668a0cb06a26661d10170e32ead057cd6a24876402e033a13dce`；fff测试为 `282d5f353d9b5e2d95134c6a903d24412e2b0bf227984ec8248c9d5316ea9779`。未打Linux/Windows新包，未提交/推送/部署/打印。

真实用户模型原工艺/优化工艺和细颈3次新切片报告均通过完整校验，峰值/cap恢复有效；配置与旧产物一致。用户模型优化工艺峰值约10.026 mm³/s，上限12，超限路径0；原工艺约12.0034，超限约0.65/129441 mm，不能把极小舍入超限直接当物理缺陷。两次当前同一二进制重切也存在支撑起点/顺序差异，所以没有把独立重切声称指令/估时严格相等；采用同一已导出G-code双解析逐项等价证明观察器不改变原统计。普通CLI夹具开/关指令和细颈旧/新指令严格一致。证据 `/tmp/effective-feature-e2e-yi5fDv/flow-fixed3/`、`flow-cli-regression/`。

高瘦真实链路：Blender生成4×4×40 mm带0.8 mm顶柱的闭合夹具，测得slendernessScore=1及3.2 mm小轮廓；当前PAM/PDS类生成覆盖，Orca两次前后切片验证主体外墙60→30 mm/s，顶柱30→30不二次减半，实际Klipper加速度指令2000→1000 mm/s²。证据 `tall-fixture/`、`tall-run1/`，日志 `/tmp/effective-tall-{blender,cli,acceleration}.log`。这是参数执行证据，不是实印减晃/强度认证。

部署/GUI交接：累计需Orca引擎（PDS实际调用位置或包含它的镜像）、Blender脚本、PAM、Compare、Web，无SQL；PDS Java无新增改动。新建实验核对报告及实际B请求，不回写旧历史。Web具体步骤见相邻仓 `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部。原生GUI无新报告/选参入口，人工待验：打开本仓 `build/arm64/src/Release/OrcaSlicer.app`，导入 `tall-run1/after/output/plate_1.gcode` 查看速度/加速度预览，确认主体30、顶柱30和外墙1000；再导入用户模型 `flow-fixed3/user-after/output/plate_1.gcode` 核对层/时间/耗材预览及文件可正常打开。回传截图/日志，不把自动化代替GUI、目标Linux/EP3Plus或实印。普通修复/分盘路由源码未改，不据此声称全部场景新跑过。

中途失败与处理：临时CLI验证器先因 `/tmp` 与真实 `/private/tmp` 路径身份失败，改resolve后通过；独立重切指令不一致按上文分离验证口径，没有放宽同一路径断言。Compare桥接探针曾与Maven重编译同时读target导致Class缺失，等待构建成功后重跑通过；没有修改生产代码规避。首次Python选择不存在的文件模式发现0项，随后定位正确验证器执行83项。最初加速度探针仅识别M204 P，EP7实际为Klipper SET_VELOCITY_LIMIT，修正验证器后核对真实指令。全部原失败日志保留，不纳入通过计数。

#### 过程记录（保留当时状态，当前结论以上文为准）

启动目标：补实际检测缺口和有依据的参数消费者，不以隐藏未知项或增加报告条目替代实现。先定位用户真实模型峰值流量/超配置流量路径统计全空的原因，再验证修正和参数链路。当时这两项在真实模型前后报告为 incomplete，而合成细颈模型可得到完整结果；不将未知直接改为通过。B 不再因人工/来源不明键而禁止优化；用户允许调整机料参数，但仍须有有效测量或配置依据，不凭空假定标定值。

基准：Orca feature `2dd0b6b21ff038c50db4b3170e96d45e8bceeafd`，本轮开始仅既有文档、下载工具和 result.json WIP，源码无差异；Blender/PAM/Compare/Web 保留前轮未提交增量，PDS 无本轮变更。已有 Mac arm64 Release 程序 hash `7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`。真实证据 `/tmp/effective-feature-e2e-yi5fDv/user-model3/`，合成证据 `run2/neck/`；最近 PAM 134、Web 245 项通过不证明下一步源码。图谱未覆盖当前变更，按源码核对，不初始化索引。

计划验证：最小回归捕获流量失效原因，必要时只修报告观察，不改变 G-code 指令、运动估时或切片/分盘/修复路径；重建当前目标后比较同参报告和执行指令。保留默认不采集、真正无效/未完成块仍未知。相关 GUI/CLI 边界与本地结果持续记录于本节。尚未改业务源码或开始新构建，不提交、推送、部署、访问业务库或打印。

诊断步骤进行中：已核对 build/arm64 为本仓 Ninja Multi-Config/arm64/MeshLib ON，未发现其他构建进程。临时增加首个无效块诊断及隐藏文件探针，用旧真实 G-code 重放，不重新请求服务或打印。`cmake --build build/arm64 --config Release --target fff_print_tests --parallel 2` 日志 `/tmp/effective-flow-probe-build.log`；诊断代码在定位后移除，不能随生产交付。

根因已确认：真实 support 段 g1_line_id=966，distance=5.00601053238 mm，阶段终点=5.00601100922 mm，float 求差再相加产生一个 ULP 的越界，严格比较使整材料永久 incomplete。已用三个短运动段建立确定性红例（`/tmp/effective-flow-red.log`）。修正只允许与该段长度成比例的两次 float 舍入范围，积分在校验后钳制观察副本端点，不改原梯形/估时/运动；stderr 诊断已删除。新构建/报告全量及真实 G-code 重放进行中：`/tmp/effective-flow-green{,-build}.log`、`/tmp/effective-flow-probe4.log`。首个预览探针因标签模式未匹配无观察，第二次探针因静态配置未知键失败，均不是根因结论；按实际导出静态配置与非BBL标签重放后复现。

流量补丁完成最小原生回归：临时文件探针已移除；正式用例覆盖低于/高于峰值及接近0上限、合法float边界、报告有效性和关闭采集的旧观察逐项一致。Release应用及fff测试重新构建成功，79用例/18402断言通过，日志 `/tmp/effective-flow-final-{build,native}.log`。同参数重切用户原/优化工艺与细颈3例进行中（`/tmp/effective-flow-cli.log`），逐一断言指令、有效配置、估时、耗材和覆盖未变，不复用旧二进制。

第二项实际规则补齐：PAM高瘦/偏心触发独立于小轮廓阈值；相对慢速按原外墙值解析后作为上限，并写回小轮廓绝对有效速度防止二次降低。使用已有B慢速，不猜机料值，A/模板不动。Web显示这两个关联参数及实际触发来源，不把未触发小轮廓记成额外优化。新红例验证原跳过，首次绿测1项失败为测试把A缺值与模板0的合法比较记录也要求清空，改为仅断言不错误归因小轮廓速度；重新全量执行。高瘦真实几何→PAM→Orca验证准备中。所有增量仍未提交或部署。

### 2026-10-21 提交交付（五个业务仓库已推送）

用户已授权将上轮审核通过的改动提交并推送。Blender/PAM/Compare/PDS当前feature分支及Web当前develop分支各一笔业务提交，均已普通push到origin；Orca仅提交本专题交付记录，不含其他文档、下载工具、result.json或引擎源码。操作前六仓已fetch对应分支并与远端HEAD一致，无需合并或改写历史。业务代码与上一轮753项测试、18次真实切片通过时一致，本轮只同步交付状态并做暂存检查，不重复运行未变代码测试；不部署、不操作业务库或设备。

| 仓库 | 分支 | 提交 |
| --- | --- | --- |
| Blender | codex/model-feature-analysis-20261018 | 4bba95df50eaea71d2395384fd7d109f3eeceb13 |
| PAM | codex/model-feature-analysis-20261018 | c3e9239ec5bfb60d5f57931b0d462326e249dcc2 |
| Compare | codex/model-feature-analysis-20261018 | 4b249778a615d02a4ed05b21a4555f5024f5420c |
| PDS | codex/model-feature-analysis-20261018 | c6063374c983ead3c0d55f1822645678b4ea1281 |
| Web | develop | 8296dcf15e64261540cfafb459382837c734e5ce |

五个业务仓库暂存边界逐文件核对且diff --cached --check通过，Blender新增模块和对应测试均已纳入，工作区干净。Orca本专题文档随本次文档提交生效，文档提交/推送身份以Git为准；原无关WIP保留。下方“未提交”仅表示相应历史步骤当时的状态。下一步配套发布测试服务并执行Web交接清单；GUI/浏览器、Linux/目标机型和实印仍待验证。

### 2026-10-21 入库前复核（仅审查，未提交）

用户本轮要求检查是否可以入库，不包含提交或修复授权。核对五个服务当前未提交差异与新增文件、接口字段、参数消费者及打包范围，未发现阻断本批入库的问题；可提交后进入测试环境，不能据此宣称生产/实印验收完成。本轮没有修改业务源码、暂存、提交、推送或部署。

基准未变：Blender feature@02c91e3、PAM feature@086f5f9、Compare feature@c333fe8、PDS feature@f92609a、Web develop@7486391；Orca feature@d1ef0ce99c 的 src/tests/resources/.github 无差异。代码图不能证明当前增量，按差异审查技能回到源码核对，不初始化/复制索引。Orca 其他专题文档、下载工具和 result.json 均不是本批提交范围。

复核重点：完整 B 模板只被有用途的规则覆盖，A 仅用于比较；FEATURE 与 B 切片保持修复终态选定文件；新几何为有界标量，不恢复坐标/重复报告存储；最小层高、首层/legacy/Cura 垫层含义及自适应/树支撑兼容关系保留。PDS 单模型与固定位置命令共用追加方法，未传质量值不启用自适应，非法值只 WARN 后忽略，不钳制或补默认值。Compare 对无效 PAM 响应仍按原契约报错，与截图要求修改的 PDS 方法区分。

本轮重新运行：PAM 123、Compare 219（另 96 个既有集成用例跳过）、PDS 定向 121、Web 223、Blender Java 22/模板 34/新纯几何 11，共 753 项通过，0 失败；Web 生产构建成功，仍有既有大 chunk 提示。日志 `/tmp/precommit-review-{pam,compare,pds,web,web-build,blender-java,template,geometry}.log`。未连接业务数据库或打印机。

使用本轮编译的 PAM/PDS 类及此前同源码生成的 Blender 夹具报告，补跑 9 组前后共 18 次真实 Orca 切片全部通过。普通无调整组执行指令相同；薄片两层、细颈 Arachne、小区域填充/小轮廓阈值、悬空枝支撑、自适应斜面及两类筏的首层语义核对通过。证据 `/tmp/remaining-feature-e2e-oQxdEX/precommit-review-20261021/`、日志 `/tmp/precommit-review-cli.log`。Orca 程序 SHA-256 仍为 `7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`，不重复构建未变引擎。

提交边界提醒：Blender 新增 `print3d-blender/script/print_feature_geometry.py` 及 `print3d-blender/src/test/python/test_print_feature_geometry.py` 尚未跟踪，需与调用方一起纳入；QA/pro/local 镜像现有 COPY script 包含该模块。各仓 diff --check 与 Orca 模块检查（35 模块/0 问题）通过；未暂存，不声称 staged 检查通过。GUI/浏览器、目标 Linux/EP3Plus、服务间真实联调和实印仍待部署后按 Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部清单执行；本地自动化不替代这些验收。

### 2026-10-21 自适应质量无效值忽略（本地完成，未提交）

用户要求PDS截图方法不再抛异常。仅把共享命令追加方法的越界/非有限值处理改为WARN+return，null仍静默跳过，0/0.5/1照常透传；不钳制、不补默认值，不扩展修改Compare上游响应契约校验。单模型/固定位置入口共用该方法。PDS原feature分支及WIP保留，其他业务不改，不提交/推送/部署。补充两入口9类输入的回归，核对报告/输出/修复/固定位置参数保持；图谱未覆盖当前增量，按源码审阅。

Java17离线定向回归8个测试类121项通过，0失败/错误/跳过，日志 `/tmp/adaptive-quality-ignore-pds.log`，包含两入口的null、0、0.5、1、越界、NaN及正负无穷；日志确认无效值实际输出WARN。仅本地mock/进程测试，无业务库/云服务/打印，不重新构建Orca。PDS README已同步降级语义；差异检查通过。

### 2026-10-21 补齐真实检测与参数用途（本地完成，未提交）

用户确认最终诉求为补齐既定检测及参数调整，无新业务选择。保持完整B模板为基础、A仅比较；不猜机料标定值、不增加强度档位、自动重切或判优。目标是有界且有消费者的局部薄壁/截面、局部高度、浮空及小轮廓证据，配套PAM参数策略、报告和实际切片验证，不用报告条目数量冒充完成。

基准沿用此前记录：Orca d1ef0ce99c、PAM 086f5f9、Compare c333fe8、PDS f92609af、Web develop@7486391及其已有WIP；Blender feature@02c91e3进入本批前干净。本批修改Blender/PAM/Compare/Web；PDS保留上轮层高透传，Orca业务源码不改。代码审查、文档与本地验证完成；下一步用户审核后新建实验统一联调，不再以报告项数冒充独立检测。图谱不覆盖增量，以模块文档/源码为准。无提交/推送/部署/业务库/设备操作；其他任务WIP全部保留。

完成范围：确定性几何阳性/阴性/姿态/预算边界、PAM独立策略及人工保护/完整模板隔离、Compare有效机型下限传递、真实Blender→PAM→Orca参数生效与无调整对照。缺标定的尺寸补偿/回抽及未指定的强度/全局填充按用户确认保留机料模板，不新增猜测规则。任意方向浮雕、连续全高度最薄处、跨实例承托及实物承载/质量不属于本次已实现精确检测；有界范围明确输出，不宣称全模型风险已排除。

当前实现：Blender复用墙入口世界三角数组，新增有界截面薄壁/侵蚀分裂细颈、小截面面积、最短外轮廓、水平相邻台阶/相对薄片高度、Z坡面、凹形/孔洞和闭合网格离床lower-star起点。最多32截面/800万三角-截面工作量、原共享期限剩余内最多15秒；单次numpy/GEOS调用非硬抢占。不出坐标，不恢复抽样厚度。多实例起点接触不确认为浮空；开放/绕序错误/预算不足显式未知。PAM接Arachne、小区域实体填充、机型最小层高约束的固定细层/贴床薄片模型首层、小轮廓原档、自动支撑和原生避外墙；已知没有Z坡面不做自适应。全部决策后统一处理有机树兼容并报告最终值。Compare只增传有效最小层高标量；Web只展示该项需要的小指标。

收尾审查修正两处：极薄细节目标层高同时受Orca 0.01 mm绝对下限约束；legacy有筏时 `initial_layer_print_height` 属于筏、模型首层使用 `layer_height`，不能一并改筏首层；Cura筏各相独立，允许细化模型首层。对应单测和两种筏真实薄片切片通过。没有改共享Orca切片/分盘/修复实现或原模板选择规则。

最终验证（不累加中间重复运行）：

| 范围 | 实测结果 |
| --- | --- |
| PAM Java17离线全量 | 123通过，0失败/跳过 |
| Compare Java17离线全量 | 219通过，96既有集成跳过，0失败；未访问业务库 |
| Web Node22单测/生产构建 | 223通过，构建成功，既有大chunk提示 |
| Blender Java真实进程链路 | 22通过，无跳过 |
| Blender模板/新几何/独立墙 | 34/11/17通过 |
| Blender独立质量几何/管线/内存 | 31/9/5通过 |
| 新几何→PAM/PDS类→Orca | 9组前后18次切片通过 |
| 原规则回归→Orca | 7组前后14次切片通过 |

合计694项测试及32次真实切片。新链路 `/tmp/remaining-feature-e2e-oQxdEX/run5/` 包含普通方块、细颈、浅台阶、贴床薄片、小柱、悬空枝、斜面、legacy筏薄片、Cura筏薄片；原规则 `/tmp/continued-optimization-cli-me7gc1/remaining-final2/` 覆盖自适应/混合树/Cura筏、固定Brim、窄细节阈值和完整B模板。普通无调整指令完全一致；薄片实际2层，层高/首层参数按筏类型生效；悬空枝有支撑挤出。报告校验及最终G-code配置核对通过。EP7/PLA合成夹具不代表Linux/EP3Plus或实印质量已验收。

日志：`/tmp/remaining-feature-pam-final4.log`、`/tmp/remaining-feature-compare-final.log`、`/tmp/remaining-feature-web{,-build}-final2.log`、`/tmp/remaining-feature-{geometry,template,blender-java,wall-entry,wall-geometry}-final.log`、`/tmp/remaining-feature-quality-{geometry,pipeline,memory}-final.log`、`/tmp/remaining-feature-e2e-oQxdEX/cli-final2.log`、`/tmp/remaining-feature-existing-cli-final2.log`。Orca复用已核对的Mac arm64 Release程序，SHA-256 `7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`；本批src/tests/resources/.github无差异，不重复构建引擎。服务端生成配置走既有GUI/CLI共享核心，GUI无新增自动PAM入口。

中途失败已处理并保留日志：模板旧31项中3个“输出字段完全相等”断言增加新字段后失败，单独断言新字段并核对旧字段逐值相等后通过；单模型failed汇总被降为limited已修正。首次真实夹具导出未选择对象产生空STL，修改临时夹具导出后重跑；不是生产切片失败。生成的测试字节码移至上述临时证据目录，不纳入源码。

各仓差异/空白审查通过，Orca模块检查35模块0问题。新增文件归于Blender现有script/test目录并在模块文档说明，不改第三方快照。累计发布需Blender JAR+script、PAM、Compare、Web，以及上轮尚未发布的PDS层高透传；无新增SQL。当前完整GUI/Linux/实印交接见Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部，按新建实验执行；本轮不提交/推送/部署，不以本地自动化替代这些验收。

### 2026-10-21 完整匹配模板基础上的优化纠偏（本轮完成，整体指标继续）

用户再次明确：先匹配大类，完整匹配模板的参数就是B基础；随后按特征针对性覆盖。不得只提取支撑/附着字段，也不得把A的其余参数统一覆盖到B。上轮提出的模板字段过滤未实施，本轮保持Compare原整份模板/调试副本路径。A参数仅用于报告比较，不再作为PAM最小阈值、慢速值或缺省值来源。机型/耗材标定值沿用当前配置，差异待用户提供数据确认；不增加强度档位系统。

基准仍为Orca d1ef0ce99c、PAM 086f5f9、Compare c333fe8、PDS f92609af、Web develop@7486391及上轮未提交增量。保留所有既有WIP。本轮补A/B配置隔离回归，修正PAM跨组取值和对应说明，完成本地测试及实际CLI核对；未提交/推送/部署，未连接业务库或打印。使用代码理解/差异审查方法，图谱覆盖不足时仍回源码，不初始化索引。

实现：PAM去掉4个跨组取值helper及其调用，A的读取仅剩输入校验和报告before值；B缺省/显式0/百分比保留，窄细节仅选Arachne、悬垂启用B分档、桥接启用当前耗材已配置风扇。已有高瘦限速、原生层高/混合树、名义壳厚、冷却等针对性覆盖保留。Compare只加边界注释和完整模板回归，不修改整份模板选择逻辑。Web仅更新交接文档；PDS、Blender、Orca业务源码本轮无新差异，未改变普通切片/分盘/修复。

验证：PAM先红5项确认跨组泄漏，修改后全量112通过；Compare全量218通过/96既有集成跳过，新增模板目录和调试副本两种来源及最终B冻结保留回归。Compare首次新增夹具误用JSON库of重载导致测试编译失败，改Map构造后重跑成功，没有修改依赖或生产代码规避。日志 `/tmp/matched-template-pam-before.log`、`/tmp/matched-template-pam-final.log`、`/tmp/matched-template-compare-final.log`、`/tmp/matched-template-compare-final2.log`。本轮330项通过，不累加历史用例；未运行业务数据库集成。

当前PAM编译类和未变的PDS命令函数→本地Orca，7组前后14次实际切片与核心报告校验通过。完整B模板、固定Brim及无调整组执行指令一致；B层高/墙圈数/填充/壳厚/首层速度/桥接参数进入GCode，细节组仅改Arachne并保留50%/90%阈值。自适应斜面、混合树和Cura垫层仍成功。日志 `/tmp/matched-template-cli.log`，证据 `/tmp/continued-optimization-cli-me7gc1/matched-template/`；Orca程序SHA-256仍为`7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`，src/tests/resources/.github与已核对源码一致。本轮不重复构建引擎。EP7/PLA夹具不能替代目标机型/Linux、浏览器或实印。

收尾差异审核与各修改仓库diff --check通过，Orca模块检查35模块0问题。未新增扫描、报告复制、模板系统或自动重切。GUI/测试环境的整模板来源核对清单追加在Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部；先继续总体指标实现，不提前要求用户整体联调。

总体指标工作尚未全部完成：16项报告不等于16项独立检测。局部连接颈、一般薄壁、浅浮雕高度、完整浮空等真实测量与策略仍需逐项核实；缺标定的补偿/回抽保留现有值，不虚构效果。下节“下一步联调”仅为上轮交付建议，已由用户要求“整体指标完成后再联调”取代。

### 2026-10-21 继续既定参数优化（本地完成，待联调）

用户确认开始编码，无需新增业务选择：B 的自适应层高遇到 organic/default 树支撑时统一改 tree_hybrid，不区分人工来源；其他参数保护保持。Brim 已由推荐决定，保留 outer_only/no_brim 和确定的宽度/间隙，删除 PAM 的 auto_brim 重复决策，不动 Raft。沿用修复后选定文件、模板匹配及 8 组 16 项独立报告，不新增几何扫描、颜色存储、自动重切或择优。

基准：Orca d1ef0ce99c（无业务源码 WIP），PAM 086f5f9、Compare c333fe8、PDS f92609af（均 feature 分支），Web develop@7486391；四服务初始干净，Orca 既有文档/下载工具/result.json WIP 保留。CodeGraph MCP 未暴露，CLI 可用；PDS 索引目标方法行号与当前源码不一致，未使用其结构结论；无图谱的服务按源码核对，不自动初始化。使用代码理解/差异审查技能并按上述限制降级。

实施顺序：补 PAM 原生层高与参数规则 → Compare 冻结结果/PDS 显式 CLI 参数 → Web 有用途的调参反馈 → 本地单测、真实 CLI 和普通流程回归。数值优先复用当前机料有效配置，缺少标定依据不猜补偿、回抽、强度或流量上限；不能将模板保留冒充完整检测。共享 Orca 算法和 GUI 控件暂不改，服务端参数通过既有核心执行；目标 Linux、浏览器和实印另列待验。本轮不提交、推送、部署或连接业务数据库。

当前步骤：PAM/Compare/PDS/Web 已完成本批实现、差异审查及交接；下一步用户审核后进入测试环境，不继续扩张规则。PAM默认采用Orca原生自适应质量0.5；Compare冻结一个可选标量，PDS仅有该标量时在单模型/固定位置两命令追加原生CLI参数。Brim不再切auto；窄细节阈值、悬垂/桥接速度复用已有同机料数值，变层高保持名义顶底壳厚。Web只从已加载核心报告给出调试方向，不新增扫描/保存或重切。

真实本地CLI已完成6组前后12次切片：固定层高无调整、自适应斜面、有机树转混合树、Cura垫层配合混合树、窄细节阈值、固定Brim。当前编译PAM优化类及PDS命令函数直接生成参数；全部GCode/报告通过校验，实际变层高/支撑及覆盖参数可见，无调整与固定Brim组执行指令相同。证据 `/tmp/continued-optimization-cli-me7gc1/run3/`；二进制SHA-256为`7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`，Orca源码未改。run1为临时验证器缺少path字段，run2捕获壳厚浮点尾数，改用BigDecimal后run3通过，保留失败日志。EP7/PLA合成夹具结果不代表目标EP3Plus/Linux或实物已通过。

最终本地验证：PAM111、Compare217、PDS358、Web222，共908项通过/0失败；Compare96项既有集成跳过。PDS先357通过/1未启用真实CLI用例，再指定本机Orca及STL夹具重跑全量358通过/0跳过，分阶段修复真实证据在 `/tmp/continued-optimization-cli-me7gc1/repair/`。日志 `/tmp/continued-optimization-{pam,compare,pds}-final.log`、`/tmp/continued-optimization-pds-real.log`、`/tmp/continued-optimization-web{,-build}.log`；Web生产构建成功，仅既有大包提示。四服务diff --check通过；Orca文档检查35模块0问题，src/tests/resources/.github无本批差异，既有WIP保留。图谱按前述限制回到源码复核，不声称完整图谱或服务端到端验证。

发布PDS→PAM/Compare→Web，目标引擎先核对已有变层高CLI；无新增SQL、主服务/Blender改动或Orca重包要求。GUI无新增控件，服务使用原生共享核心；浏览器、目标Linux、EP3Plus和实印待验清单保存在相邻Web `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部。B可修复并更换工艺，按整体打印效果验收，不自动判优；未提交/推送/部署或访问业务库。

### 2026-10-21 服务代码已提交推送（待测试环境部署）

用户授权提交并push，已完成三个Codeup仓库各一笔提交：PAM `086f5f9`（自动选参）、Compare `c333fe8`（修复后检测/选参/切片）、Web `7486391`（模型预览/逐项报告）。PAM/Compare推送到当前`codex/model-feature-analysis-20261018`，Web推送到当前`develop`；远端均正常快进，不合并、强推或部署。服务仓库工作区干净，配套发布顺序PAM→Compare→Web。

本次Orca仅将本批自动选参、修复后模型及入库复核记录按段落提交，业务源码、GUI/CLI、模块规则和指纹未变，无需应用构建或新增GUI操作；服务端与Web的待验清单仍在相邻Web仓库交接文档顶部。其他历史记录、模块文档、下载工具及result.json保留本地，不纳入提交。此前540项本地通过与96项集成跳过的边界不变；下方各轮“未提交”是历史时点，不代表当前服务提交状态。

### 2026-10-21 入库前复核（可进入测试环境，非生产验收）

核对当前累计未提交差异及新增文件：PAM `fa7b4bef`、Compare `13d25176`、Web `f045e101`；Orca仍为`69fe8a45`，无本轮业务源码改动。PDS当前`f92609af`较上轮只将Dockerfile基础镜像更新为v2.0.24，修复API/结果与快照切片契约不变。本轮只审查、运行本地验证并补记证据，没有修改业务代码或提交/合并/部署。

检查通过：修复终态选文件后再检测；推荐与B切片冻结同一文件及单次变换；A沿用原件；修复失败保留文件兜底、不重复修复；产物哈希未知不冒用原件；参数保护、报告来源、前端文件切换和原切片命令入口。源码审查未发现阻断测试环境入库的问题；未执行目标分支合并，不能据此保证远端无合并冲突。

重新运行Java17离线`mvn -o clean test`：PAM105通过，Compare215通过/96项既有集成测试跳过；Node22 `npm test`220通过，生产构建成功，仅既有大包提示。共540通过、0失败；日志`/tmp/pre-merge-review-{pam,compare,web,web-build}.log`。三个服务仓库`git diff --check`通过、无未解决合并条目；Orca文档检查35模块0问题。不连接业务库，不进行真实服务/设备联调或将桩测试当作端到端通过。

累计发布PAM→Compare→Web；无本轮新增SQL或Orca重包要求，PDS须已具备现有分阶段修复接口。测试环境新建实验，按Web交接文档顶部清单核对健康跳过、问题模型文件一致、修复失败兜底、缩放旋转、选定模型跨域预览及A/B产物。旧进行中实验缺少准备步骤时不能直接续用旧特征。保留无关文档、工具与result.json改动，不纳入本功能提交。

### 2026-10-21 使用修复后选定模型（当前结果）

按用户要求先修复再检测：Compare 新实验增加 REPAIR 文件准备，网格健康跳过；否则调用现有 PDS 分阶段修复并等待终态。失败沿用最后保留文件或原件，明确显示兜底。FEATURE 引用选定 URL，RECOMMEND 校验文件一致并冻结小型 modelPreparation 来源；B 切片使用同文件且 repairModel=false，不再重复修复。A 使用原件、原默认配置。Web 进度/预览/颜色缓存随文件切换，A/B 接受有明确原件来源的整体方案对比，未知产物哈希不沿用原件哈希。

本轮修改 Compare/Web 及交接文档，基准仍为 Compare `13d25176` / Web `f045e101`，Orca `69fe8a45` 业务源码、测试、资源、GUI/CLI入口无变更；PDS/Blender干净，PAM原有未提交自动优化不变。不增加新SQL、版本分流、重复报告/模型保存、自动重切或打印。图谱缺失按源码降级审查，不初始化。未提交/推送/部署，无业务库和设备操作。

验证：Compare Java17离线clean test 215通过/96个既有集成用例跳过；Web Node22测试220通过、生产构建成功（既有大包提示），共435通过/0失败。首轮旧URL断言已按新文件契约修正并全量重测。日志 `/tmp/prepared-model-{compare-full,web-tests,web-build}.log`。本轮无Orca算法改动，不重打包或用历史CLI结果冒充服务端到端验证。Orca模块文档检查35模块0问题；GUI/线上对象存储跨域和真实PDS/Blender联调仍待测试环境执行。

部署与交接：本轮更新Compare+Web；累计自动选参未发布时需连同PAM。新建实验验证，旧完成记录不回写，旧已提交任务可以查结果，缺少准备步骤的旧实验不继续混用修复前特征。健康跳过、问题模型文件一致、变换一次、失败兜底、预览/缓存和原A路径清单已存相邻Web仓库 `docs/CORE_REPORT_HANDOFF_20261020.md` 顶部。

### 2026-10-21 自动选参再次复核（当前结果）

继续优化PAM/Compare未提交实现，分支与HEAD仍同下节。已修正有效线宽重复解析（遗漏耗材喷嘴覆盖及零值回退）、Compare提前改墙使自动动作被报成模板、等效开关被规范化后与冻结请求不一致三处问题。PAM直接使用Compare既有解析器得到的线宽标量，不传机器配置；墙只在PAM判断；未改变的值保留原形式。FEATURE测量与ID复用一次查询，完成阶段的原状态检查不删。不增加检测/扫描/存储、版本分流或手填入口，人工优先不变。

最终验证：PAM105、Compare200、Web215项通过，共520通过/0失败；Compare96项既有集成测试跳过。Java17离线clean test、Web生产构建成功（既有大包提示），日志 `/tmp/auto-review-{pam-full,compare-full,web-full,web-build}.log`。新回归先复现后修正；Compare首轮查询计数断言包含正常状态查询，已限定完整结果查询并重跑全量。

当前Compare解析类→PAM优化类→现有Orca：7组前后14次真实切片成功，参数覆盖进入GCode；无调整组执行指令相同。额外4个跨模块线宽边界通过（耗材覆盖、绝对零回退、百分比零、未知线宽）。程序SHA-256仍为下节同值，证据 `/tmp/auto-review-cli-uL1UIy/`。边界临时脚本首轮误写字段名，改为实际evidence后通过；旧日志保留。本地EP7/PLA合成模型，不替代生产EP3Plus标定、Blender准确率、Linux或实物验证。

范围：本轮只改PAM/Compare源码测试及交接文档，Web重新测试但未改业务源码；PDS/Blender干净、Orca src/tests/resources/.github未改，不需Orca重包。累计未提交自动优化仍需PAM→Compare→Web配套发布；没有提交、推送、部署或业务库/设备操作。8组16项中缺依据的8项仍保留模板，没有新增自动测量。下一步按相邻Web仓库的 `docs/CORE_REPORT_HANDOFF_20261020.md` 最后一节核对自动动作、人工保留、线宽及请求显示，原GUI/测试服务验收待执行。

### 2026-10-21 自动选参纠偏（本地完成，待测试环境联调）

用户指出手填/勾选参数不符合自动优化要求，本批直接替换上一版未提交实现，不保留双流程。B原模板/墙建议后自动评估已有证据并合并参数，人工编辑键优先；A、原修复/固定位置/分盘/打印状态机不改。不新增几何扫描、颜色坐标保存、自动重切或版本开关。上一版手动入口/PDS专用变层高透传已移除，不把手填47参数或旧874项结果当自动优化完成证据。

身份：Orca `codex/model-feature-analysis-20261018@69fe8a45`；PAM `fa7b4bef`、Compare `13d25176` 仍同功能分支上的未提交增量，Web `develop@f045e101`；PDS `243ec801` 已无本批差异。Orca业务源码、GUI、CLI及打包文件未改，其他文档/工具/result.json WIP不动。未提交、推送、部署、访问业务库或实印。

8组仅组织16项独立说明：5项复用既有几何、3项采用Orca原生小轮廓/冷却/流量能力；其余8项因缺少可靠检测、质量目标或标定保留模板并说明未覆盖，不冒充全部自动化。真实自动覆盖包括Arachne、已有悬垂降速/桥接风扇/冷却档启用、复用附着建议及既有速度/加速度限制；不猜孔补偿、强度或流量上限。百分比小轮廓不用于全局外墙限速，避免关联二次降速。历史工艺副本来源不明时保守保护，不覆盖人工值。

本轮本地验证：PAM101项、Compare199项、Web215项通过，0失败/错误；Compare96项既有集成测试跳过，不计入通过。Java17离线clean test与前端生产构建成功，最后夹具/文案修改后再运行相关全量测试，Web仅既有大chunk提示。日志 `/tmp/auto-optimization-{pam-full,compare-final,web-final,web-build-final}.log`。未使用业务数据库。

真实CLI程序SHA-256仍为`7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`。直接调用当前PAM编译类生成参数，再用本地EP7/PLA/0.4合成夹具执行7组前后对照（14次切片）及额外桥接风扇2次；全部GCode/报告成功并校验。无调整组执行指令完全相同，其余对应行为变化；独立桥接风扇20%→80%出现M106 S204/恢复S51，冷却样例估时7m08s→8m16s，Brim计划面积0→58.9137mm²。不证明EP3Plus标定、Blender特征准确率、实物质量或速度收益。完整证据 `/tmp/auto-optimization-cli-sJwHfk/`，首轮临时脚本缺path失败留存，修正校验适配后通过。

本批配套部署仅PAM→Compare→Web；无需SQL、PDS、Blender、主服务或Orca新包。此前尚未上线的精简/模型功能仍按原批次部署范围核对。GUI/测试服务/Linux/实印尚未验证；自动选参仅是服务端B组能力，Orca GUI无新增入口，相同有效配置仍复用原生核心。具体规则、人工键/历史副本、失败重试及GUI验收清单在相邻Web仓库的 `docs/CORE_REPORT_HANDOFF_20261020.md`，不以CLI通过替代现场验收。

收尾核对：受影响仓库git diff --check无问题，新增未跟踪源码逐项空白检查无诊断；Orca模块检查35模块/0问题。PDS/Blender工作区干净，Orca src/tests/resources/.github无新增差异。既有其他任务WIP保留，未暂存或提交。

### 2026-10-21 Orca 累计精简交付（审核通过，本节随提交生效）

用户授权先确认 Orca 是否还需补改，审核验证后本地提交；不推送、合并、部署或改其他服务。基准为 `codex/model-feature-analysis-20261018@bc6684f18026721b6d19348d509e1e8c780a11e8`，源码与相关测试累计 diff SHA-256 为 `b654d7a6c2fe0497c4d3c137964da3987f88811015537d698e256988ef018d68`，与上一轮完整构建验证身份一致。保留下载工具、其他专题文档及 result.json 等非本任务 WIP，混合模块文档按实际暂存范围分离。

根审核调用链与提交边界，三路独立只读复核报告算法、共享 G-code 解析器/构建和测试覆盖，均未发现本批阻断，不再追加业务代码。CodeGraph MCP 未暴露、CLI 可用但索引报告 8 个修改/2 个删除；旧图谱不能证明当前 WIP，以当前源码为准。删除范围是报告专用三维集合/深度/投影诊断、角色运动观察及重复输出，不删除 CGAL 修复、MeshLib 或平台依赖。保留二维孔缝/窄宽/覆盖、床面积、成本、材料峰值/超 cap 距离和冷却；normal/reset/unknown/取消与默认不采集继续保留。普通切片、分盘、修复路由未改，CLI 唯一源码差异为报告紧凑序列化。

本轮实际补跑结果（全部进程退出 0）：

| 范围 | 结果 |
| --- | --- |
| 原生报告 `[SliceAnalysis]` | 78 用例 / 18,009 断言通过 |
| 原生修复、阶段、重建 | 207 用例 / 9,658 断言通过 |
| Python 报告验证器 | 83 项通过 |
| CLI 报告开关/保护 | 10 项通过；默认/关闭/开启时的执行指令、有效配置和层数一致 |
| 特征真实切片 | Classic/Arachne、孔缝/细笔画、普通/树支撑、筏等 40 场景通过 |
| 固定位置/多盘 | 64 项通过，含无摆盘、禁用/空盘、越界拒绝和报告开关交叉 |

证据为 `planning/slice-analysis-20261021-submit-{native,repair,python,cli,features,fixed}.log` 及同前缀 CLI 目录中的逐条命令、输入/输出和程序身份。本次没有重复编译：复用 10 月 20 日已对当前累计源码构建的 Mac arm64 Release、MeshLib ON 应用/测试，确认源码 diff 摘要及三份二进制 SHA-256 均未变化。应用为 `7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`，fff 测试为 `389d7bc510860649f7565bd9502c5791e66dca0ce596435262be0ab9d2b4efac`，libslic3r 测试为 `3dfaa448e71bb1f55592527be5c36da6d1e385bf5f17589c44666c82e6aebe84`。根 result.json blob 仍为 `7a157845db6c198b1e6b7e8db249ba1e995f2919`，未作为测试输出或提交文件。

提交仅包含 Orca 报告精简的源码、相应测试、模块说明和相关实施记录；混合文档按段落暂存，工作区原文保留。其他服务、下载工具和无关专题记录不纳入。最终暂存文档指纹独立核对，不用含未提交下载工具的工作区指纹替代。工作区 `--all` 与暂存 `--staged` 检查均为 35 模块 / 0 问题，`git diff --cached --check` 通过；18 个暂存文件的源码/测试摘要与本轮实测身份一致。未推送、合并到 master、打 Linux/Windows 新包或部署。

GUI/目标平台交接仍待执行：启动本仓 `build/arm64/src/Release/OrcaSlicer.app`；导入 `planning/cache/cli-assets/models/stl/cube.stl`，按本轮 `cli/enabled/command.json` 的机料/工艺核对有效配置，普通切片并查看路径、时间、耗材，无新增分析入口；保存 G-code 供同参 CLI 对照。再打开 `planning/slice-analysis-20261021-submit-fixed/two-plate-seed/output/model.3mf` 逐盘切片、保存重开，盘成员/坐标应保留；打开同证据根下 `disabled-partly-outside.3mf`，第二对象禁用时可切第一对象，启用越界对象后应拒绝，重新禁用后恢复。修复界面仍按下方既有高级修复/候选取消步骤核对。回传截图、日志及产物，不以自动化代替 GUI 或实物打印验收。Linux 测试服务应重新打包并配套已精简消费者后联调，旧包不会获得本次优化。

### 2026-10-21 三项定向去重复（本地完成，待发布联调）

用户明确批准：同姿态预览不重复扫描/更新颜色；切片等待不读无用配置，查询投影必要字段；无桥接时复用悬垂连通分组。仅改Web、Compare/PDS、Blender及对应测试/文档，不改Blender启动恢复、不增加着色持久化、不改PAM规则或Orca切片/分盘/修复。现有各仓WIP保留，根分支仍`codex/model-feature-analysis-20261018@bc6684f180`；不开新分支、不提交推送/部署、不访问业务库或打印。

三路独占仓库实施并先存真实WIP基线，根复核调用方、协议与本轮差异。使用understand-diff方法，过期/缺失图谱以当前源码替代；不以历史通过证明新代码。验证同值form替换与真实姿态变化、模型更换，等待/失败/首次提交/成功与版本冲突，以及无桥分组次数和有桥/临界面积结果。Blender仅复用连通关系，剩余面积与阈值筛选保留，关注集合遍历及浮点累加顺序；Web全单测/构建、Compare/PDS本地mock、Blender真实几何及PAM结果对照。Orca源码不变不重新打包，真实浏览器/Linux联调单列待验证。

实施结果：

- Web复用现有有效变换归一化，监听scale/XYZ四个标量；同值form替换、非姿态字段和等效数字字符串/缺省值不再触发尺寸扫描或颜色更新。首次载入、真正变换、同姿态换模型与同tick换模型/姿态仍执行原流程。不新增持久化、全局缓存或修改可见页的渲染循环。正式SFC回归修改前捕获多读36个夹具顶点，修改后同值刷新为0；不是大模型/GPU性能数据。
- Compare只在首次提交及成功有GCode后读取Variant，等待/失败/查询异常不读完整snapshot；不缓存配置跨异步阶段，保留完成短事务、任务租约和Variant乐观版本冲突。PDS新增内部状态查询投影，仅此入口改用；包含SliceResult全部来源字段及sn行身份（防止可空字段全空时ORM将存在行视为null）。共享getJobBySn/saveSliceJob仍全量读取，公开接口、JSON字段、历史null和任务不存在错误不变，无SQL迁移。
- Blender仅在无确认桥面且原/残余集合遍历顺序一致时复用两组原始连通分组。有桥或顺序变化仍重建；残余面积求和、阈值筛选、统计和射线测量不变。典型无桥夹具4→2次分组，有桥仍4次，不保证所有无桥输入减半，也不代表任务耗时减半。

本轮本地验证（均实际执行，未访问业务服务/数据库）：Web **209**项与生产构建通过；Compare **99**项、PDS **111**项隔离单测通过；Blender模板 **31**项、独立墙入口 **4**项、墙几何 **13**项通过。8组真实Blender几何完整features与真实修改前WIP逐值相同；112组最终PAM推荐结果相同，PAM源码未改。新增回归覆盖归一化、异步模型替换、无用配置零读取、投影JSON一致、保存全量查询、租约/版本冲突、桥接重算、集合顺序回退和浮点阈值。Blender首轮顺序夹具构造不同而失败，仅修正测试建集方式后通过，保留原失败日志。

独立审核：根核对本轮增量与消费者；Web/Blender实现者互审对方改动，未发现本轮阻断。验证使用当前源码及真实WIP快照，不与旧分支或旧二进制混用。四仓git diff --check通过；Orca源码/相关测试diff摘要仍为`b654d7a6c2fe0497c4d3c137964da3987f88811015537d698e256988ef018d68`，本轮未改Orca业务代码、不重新打包。模块文档检查35模块/0问题。

证据：Web `/tmp/compare-web-pose-20261021.9vt4in/`（前后快照、增量diff/hash），测试/构建 `/tmp/compare-web-pose-{tests,build}-20261021.log`；Compare/PDS修改前快照 `/tmp/compare-pds-query-before-20261021-DrtTSu/`，测试 `/tmp/compare-deferred-variant-tests-20261021.log`、`/tmp/pds-query-projection-tests-20261021.log`；Blender `/tmp/blender-region-reuse-20261021.54HQG2/`（真实基线、测试、8几何/112推荐对照及增量diff/hash）。各仓现有交接文档已同步本轮范围和验证。

Compare/PDS最终快照 `/tmp/compare-pds-query-after-20261021-ckA4Qw/`，本轮增量 `/tmp/compare-pds-query-increment-20261021.diff` SHA-256 `8b94950d37781f67c7d5d97fff25686f216ac5988f98cb8937e90f79ae86b629`。当前生产文件指纹：Web ModelPreview `6b56ae1a35ebdcf107c788380a395331f34f370f8dbf30a7e8838f54a0cb0008`；Compare Worker `a6ee7e238fdb88c24a2e169c7a487a7a8df618d3c7fc3f31d2a53b057ae90142`；PDS SliceJobServiceImpl `581f3b9e5fb5355ccb8e7c1f354ea4bf6a3ce5d620f03ee96b8b8a26ddca9eb3`；Blender模板脚本 `d7a65c606d675a98b38694215638a6457d6614fc5deb854971457f37dfcac31a`。服务基准HEAD仍为Web develop@87c891f、Compare功能分支@94fcb58、PDS@243ec80、Blender@4d550a7，保留此前各仓WIP。

下一步：发布Web、Compare、PDS和Blender后，用同模型/姿态/线宽核对检测与参数选择不变，刷新详情不重复预览计算，真旋转/缩放和换模型正常；等待→成功/失败与GCode/报告链接正常。具体GUI步骤在Web `docs/CORE_REPORT_HANDOFF_20261020.md` 第10项。真实浏览器WebGL、Linux复杂模型耗时和测试环境端到端联调尚未执行，不承诺固定加速比例。用户暂不处理的Blender启动恢复逻辑保持原样；未提交、推送、部署或发起打印。

### 2026-10-21 预览缓存成本与悬垂分组说明（只读分析）

用户先分析、不编码。核对Web仍develop@87c891f、Blender仍功能分支@4d550a7及既有WIP。检测指标/推荐业务结果仍保存；不持久化的是预览着色数据。当前同模型/有效姿态/阈值已有单次详情内存缓存，首次选悬垂/底面才在Worker计算；待优化watcher导致的是尺寸全顶点扫描及颜色缓冲更新，不是每次重新执行着色Worker。离开详情清理，整页重载需重新载入；STL下载/解析/法向/上传显卡成本独立于着色是否保存。

本机Node直接调用生产classifyModelFaces+localHintColors，合成10万与100万有效三角面各3次（变换scale1、X12/Y5/Z7°，先1万面暖运行）：10万面总18/21/21ms，100万面170/176/176ms。不是用户真实模型或浏览器性能；不含网络、STL解析、Worker启动/顶点复制、GPU上传/渲染，不据此承诺页面加载时间。原始分类每面1字节，当前两份RGB缓存合计每面18字节，100万面分别约1MB和18MB（十进制、未压缩）；若未来持久化，可考虑分类而非重复坐标，但需绑定模型/姿态/阈值和拓扑身份，且只省分类计算、不省原模型加载。目前没有为这份轻量提示新增持久化的收益证据。

分组建议收窄：第一次原始悬垂分组用于原始指标；确认桥接后扣面，第二次用于支撑证据。没有确认桥面时可考虑复用原始连通关系；不能未经验证直接复用effective_regions及全部统计。overhang_area按mesh顺序、residual_area按set求和，临界阈值可能因浮点累加顺序不同而变化；应保留剩余面积/筛选/统计原路径，并验证顺序和边界。桥接承托射线、局部悬臂距离与有桥接后的重新分组仍必须保留。此处仅解释和收窄未来实施范围，未修改业务代码、存储或部署。

### 2026-10-20 加载/读写优化后复查（只读完成）

用户询问是否仍需优化，本轮仅分析，不继续改业务代码。基准仍为`codex/model-feature-analysis-20261018@bc6684f180`及上一节优化后的服务WIP，保留并行文档、工具和result.json。分别核查Orca报告成本、Web加载/内存、Compare/PDS调度与重复工作、Blender/PAM核心计算；不重复提出已完成事项，不为精简删除仍有消费者的指标。使用understand-diff影响审查方法，历史图谱不覆盖当前功能，以源码为准。输出按必须修、值得优化与需量测后再定区分；不部署、提交、访问业务数据库或打印，不重复全量构建测试。

结论：未发现上一轮加载/读写优化引入的新阻断，但发现一项既有的条件性恢复任务风险和三类可继续减少的重复操作，均未实施。

| 优先级 | 当前证据、触发条件与建议 |
| --- | --- |
| 多实例/滚动发布前处理 | Blender `TemplateFeatureAnalyzeJobService.java:44–62`在启动时选全部processing任务，无过期/执行归属条件；100–108仅updatedAt竞争，163–176完成/失败仅按jobId回写。若另一实例仍在执行，新启动实例可再次运行并相互覆盖结果。该文件与HEAD一致，并非本轮引入；未确认线上副本/滚动方式，不能断言为当前慢的原因。应只恢复确实中断的任务，并防止旧执行覆盖新执行结果；本轮不擅自设计或实现分布式调度。 |
| 可直接小改 | Web `ModelPreview.vue:205–207`的watch getter返回新数组，详情更新替换input/form时，即使4个变换值相同也调用applyTransform，经`modelPreviewTransform.js:14`精确遍历全部顶点，已开提示时再次标记颜色缓冲。手动刷新/真实阶段变更/旧服务轮询均可触发，已访问但隐藏的流程页也执行。建议改逐项标量监听或有效变换比较，不改坐标、尺寸、颜色算法。根与子代理本地Vue最小诊断均确认同值新对象触发一次；没有下载或访问线上。 |
| 可直接小改 | Compare `ExperimentWorkerServiceImpl.java:419`每轮切片查询都取完整Variant及snapshotJson，真正消费者只在首次提交438–439、成功保存462–466；WAITING/失败/查询异常不用。可延后至实际消费者读取，保留成功版本校验。PDS `/query`经`SliceJobServiceImpl.java:122–125`全行读slice_job，响应不使用params/stl_url等；可独立投影结果字段，不能缩窄saveSliceJob共用读取。收益是减少轮询数据库数据量，不是减少切片次数。 |
| 有限复用，须逐值回归 | Blender `template_feature_analyze.py:411/443`先分组悬垂/直下面，458/465再分组扣除桥接后的面；无确认桥接时两对集合相同，可复用分组及对应离散度/复杂度。有桥接必须重算。既有32面悬臂夹具四次分组面数[2,2,2,2]、两对集合相同；64面桥接夹具为[2,2,0,0]，支撑BVH仅构建一次。当前未测复杂模型，不能承诺总体提速；后续需保留排序/浮点累加结果。 |

先量测而不继续改的部分：可见静止预览仍逐RAF绘制（ModelPreview.vue:43–48），但改按需绘制需保留拖动阻尼/颜色/resize；PDS完整stdout/stderr用于cmdTest诊断，不凭猜测截断。Orca`SliceAnalysis.cpp:493/513/531/535`已判coverage不完整时仍先计算将输出null的交/差集，可以短路但仅节省异常层成本；整盘底面积再次遍历路径、G-code扫描材料身份有复用空间，必须保留全层合法性、路径自身底高和无耗材记录材料语义，先测再动。正常层的孔缝、漏路径、实体覆盖和窄宽等核心几何仍有消费者，不直接减少层数/精度或删除。报告JSON dump的临时字符串、少量面积标量不是当前已证瓶颈。

端到端核查：QUALITY/FEATURE/RECOMMEND各为实验级单任务，A/B共用检测结果；两组切片是必要对照，仅B按QUALITY决定修复，未发现应直接删除的重复远程计算。报告流式校验→文件流上传没有重新恢复全量JSON树，G-code头尾统计仍有界；同次多模型相同URL已下载一次再本地copy，不为跨服务复用引入新缓存架构。Blender顶层指标用于评分、每模型支撑/悬臂证据用于支撑决策；墙的投影/栅格/骨架分别有消费者，不因“看着重复”删除。

本轮验证为当前源码与上轮最终快照核对、Web最小响应性诊断及两例Blender调用计数；没有重跑完整应用/服务测试，上一轮通过数不冒充本轮结果。Orca源码/相关测试diff摘要仍为`b654d7a6c2fe0497c4d3c137964da3987f88811015537d698e256988ef018d68`。各服务分支/HEAD不变，只更新本记录；无新提交、部署、数据库操作或实际打印。下一步建议先确认Blender部署是否存在实例重叠，再决定上述小范围优化；Linux原复杂模型分阶段耗时/内存才是进一步算法优化依据。

### 2026-10-20 本地 HTML 截图阅读版（完成）

用户要求生成本地 HTML 自行截图，解决截图区域不合适的问题。以飞书 revision207 的已核对 `planning/exports/model-feature-guide-20261020/guide.md` 为内容基准，保持正文、31张已有图与3张流程图的含义，不新增人工处理内容、不回写飞书。将图片内嵌、复用已有SVG；白板预览存在大空白画布，按原节点和连线重新排版。提供章节目录、单图完整查看及原图保存，离线不依赖网络。当前仓库仍为 `codex/model-feature-analysis-20261018@bc6684f180`，不动其他任务WIP。完成后校验正文/表图数量、页面溢出、单图切换与尺寸，实际浏览器渲染抽查；不构建应用、不提交。

交付：`planning/exports/model-feature-guide-20261020/model-feature-guide.html`，单文件663,544字节，内嵌34张SVG，保留19表及50个标题。正文截图可隐藏界面；单图默认完整适应窗口，支持原尺寸、前后切换、H隐藏按钮及SVG保存。3张流程图按原节点连线重排，不再嵌入旧白板带大空白的预览。

验证：初次Playwright默认浏览器缺失，改用本机Chrome独立无头进程验证（不访问用户会话）。所有图片加载、34图窗口内完整显示、390/768/1024/1440宽度无页面横向溢出、SVG文字无越界；切换/原尺寸/隐藏/退出均通过；无页面JS错误、无HTTP依赖。目视阅读页、参数流程图及长图，修正Raft/Arachne单词断行。证据为同目录`html-verification.json`与`html-qa-*.png`。纯阅读版检查，不代表业务算法或实物打印验收；不回写飞书，不变更业务代码。

用户截图步骤：用浏览器打开HTML；正文通过章节下拉跳转；图例点击“单图查看 / 截图”，按H隐藏按钮后截图，Esc返回。长图如需较大字号可选“原尺寸查看”分段截图，或“保存SVG”取得完整矢量图。本机Chrome已验证；用户实际窗口/浏览器仍可按上述方式自行确认。

### 2026-10-20 旧文档有效内容融入当前图解（完成，按用户收敛为已有自动流程）

最新范围调整：用户明确“暂时先不增加人工处理的，没用就先不增加”。此前7张人工细调/案例图仅在本地生成，尚未向飞书上传或替换；本轮不发布这些草稿。收敛为仅优化现有自动模板/支撑/附着/条件Arachne及核心报告的具体作用、效果、代价；不新增人工流程、参数清单或检测能力。原有人工边界说明保留，不扩写。

用户授权将两篇旧飞书中的案例、参数方向与代价融入当前特征指南，不另堆重复章节。仅修改 Wiki `Kn6fwWsYWinZy2k1vvqcYyKknod`、本地同步稿、静态矢量/PNG及本记录；不修改两篇参考文档、业务代码、任务、部署或数据库。基准为 `codex/model-feature-analysis-20261018@bc6684f180`，保留既有消费者精简等 WIP。

最初计划（后按用户要求收敛）：先回读当前修订与两篇来源；保留一张特征总览、16类详细指标及后续章节结构。拟将薄片/细杆/噪声误判案例、支撑接口、小底座附着、细节路径、封顶与冷却的具体参数作用融入对应段落和原位图。明确已接入自动建议、人工试参、实物验收三者；旧文档数值不成为统一阈值，失效配置键不照搬，不恢复评分或服务端着色大数据。静态图沿用已有SVG生成器，不新增FigJam或AI位图。

验证：逐图渲染和目视，按块带revision更新，云端回读核对图片/章节、参数口径及非目标块不变；同步本地阅读稿，保留原快照和原图。仅文档工作，无应用构建、业务测试、提交或部署。

完成结果：目标飞书 revision196→207，10个原有正文/表格块更新、2张图片原位替换。第3.1图说明已有支撑启停/类型、附着、条件Arachne的具体问题、对应参数、作用和代价；第5.2图解释已有流量/冷却/成本报告，不将只读报告写作再次自动调参。借用旧案例帮助区分薄片/细杆、局部噪声与真实风险，不声称新增语义识别或全表面检测。未增加人工步骤、调参项目、阈值或自动规则。

核验：全文仍31图片、3原生白板、19表；第2.1仍1总览，第2.2仍16指标图。147个非目标块逐字保留；两篇来源内容和revision171/22未变。2图本地目视已修正图文重叠，云端回读校验token/尺寸/位置与自动范围；本地guide.md由最终回读生成。7张先行人工细调草稿未上传/采用，仅保留本地，不计入交付。静态新图681,398字节，与业务模型报告无关。浏览器整页排版未验收，不声称通过。

证据：`planning/exports/model-feature-guide-20261020/integration-{before,source-cases,source-parameters}.json`、`auto-integration-{after,verification,responses,uploads}.json`、`auto-integration-plan.cjs`、`publish-auto-integration.cjs`及`figures/render-auto-integration.cjs`。本地同步稿与原图/原文快照保留可恢复。模块文档检查35模块/0问题、目标MD空白检查及发布/绘图脚本语法检查通过。下一步为用户阅读当前飞书第3.1与5.2节；不改业务代码、不运行打印或应用测试、不提交/部署。

### 2026-10-20 复查后的加载/读写优化（本地完成，待发布联调）

用户批准上一轮复查建议。沿用 Orca `codex/model-feature-analysis-20261018@bc6684f180` 和各服务当前 WIP，不提交、推送、部署，不访问业务数据库或发起打印。本轮不改 Orca 切片、分盘、修复或测量算法；实施范围为 Web 报告超时/独立加载、仅保留指标及必要元信息、详情页模型预览复用；Compare 降低等待状态重复详情读取并保留并发控制；Blender 复用对象边界/只保存所需面法线；PAM 单次判定复用和 PDS 有界错误摘要。

分工：Web、Blender/PAM、Compare/PDS 三路独占各自仓库，根负责轮询协议设计审核、最终差异交叉复核和本记录。必须保留历史报告/实际喷嘴对照、未知值、任务重试/UNKNOWN/结果变化可见性、预览变换与离页释放；不加数据库列、泛化缓存框架或自动择优规则。历史图谱不覆盖当前功能，按 understand-diff 方法核对当前源码消费者，不依赖过期图谱结论。

验证计划：先存当前 WIP 对照、增补超时/并行/模型生命周期及调度变化回归、本地单测和 Web 生产构建、Blender 真实几何/保留指标及 PAM 推荐结果等价。Orca 源码不变不重复打包，不以旧回归冒充本轮服务测试。GUI 页签切换/报告重试、Linux 大模型端到端性能仍单列待验证。关键设计、每批实现及测试结果在此续记。

最终实现与边界：

- Compare保留数据库version、租约、状态与重试流程；progress只补已有小字段。结果/错误/补查期限未变的WAITING回写不推进updatedAt，仍正常递增version、写结果、释放租约并续排任务；Web仅对双方均为已提交RUNNING/WAITING的任务按可见字段判断。首次提交、结果变化、UNKNOWN、终态与重试仍刷新详情；旧progress缺status时仍按version比较。现有WAITING轮询至少间隔5秒，人工刷新仅限UNKNOWN，与数据库秒精度时间配套；未来若更改该调度前提须重新审查。不加数据库列、不对大JSON做摘要运算、不传无消费者的nextRunAt。
- Compare调度候选及依赖状态查询只选所需字段，领取任务仍按原id/version/lease校验后读取完整记录，不裁剪真正执行业务所需的请求/结果。
- Web报告网络/响应读取90秒预算，A/B独立并行；超时进入可重试错误，取消及迟到响应不得覆盖新请求。缓存核心metrics/实际喷嘴元信息，不再额外保留原始报告树；保留已知喷嘴不一致的对照拒绝和未知值语义。该预算不能中断同步JSON解析或指标计算，也不能消除首次解析的内存峰值。
- 预览首次进入流程页才挂载，同一详情切页复用一个STL；隐藏页签或浏览器后台暂停渲染，恢复不重置视角。模型输入变化、离开详情及迟到下载仍正确释放；不引入跨实验全局模型缓存，不修改模型变换或颜色判断。
- Blender单对象复用6个原始边界标量，保留原舍入，只存后续消费的悬垂面法线，不增加跨阶段全量顶点缓存。PAM单次推荐复用支撑/附着判定，保留输入校验、候选顺序、规则、分数与理由。
- PDS有界构建额外诊断摘要，最多保留2001字符前缀以判断原2000字符截断；保留stderr优先、成功时stderr警告、空白处理及完整cmdTest.msg。不截断原命令输出采集，也不修改执行命令、超时或切片结果协议。

本轮本地验证：

| 范围 | 最终结果 |
| --- | --- |
| Web | 207项测试通过；生产构建通过；既有chunk大小警告保留 |
| Compare | 90项离线mock测试通过，0失败/错误/跳过 |
| PDS | 104项离线测试通过，0失败/错误/跳过 |
| PAM | 84项测试通过 |
| Blender | 模板27项、独立墙4项、墙几何13项通过 |
| 等价对照 | 8组真实Blender几何全量features、112组最终PAM推荐结果与本轮WIP基线精确一致 |

PAM支撑/附着运行计数从各672降至112，即每请求各6次降至1次。19,200面小立方体的内部诊断中，法线记录19,200→0，3次暖运行中位数0.073864→0.066099秒；这不含导入/墙分析，不是RSS、Linux或端到端提速结论。复杂模型总体耗时仍待目标环境测量。

根独立复核Web和Blender/PAM增量，并由另一代理交叉审核Compare/PDS并发、状态刷新与摘要语义，未发现本轮阻断问题。Web生命周期测试执行生产Vue组件和真实Three几何、模拟WebGL/网络，不冒充真实浏览器验收。PDS首轮一个早停夹具被测试框架DEBUG日志调用toString提前遍历，已修正夹具并保留初始日志；最终104项通过，不隐藏中间失败。

证据：

- Web：`/tmp/compare-web-followup-{tests,build}-20261020.log`；`/tmp/compare-web-followup-20261020.9u8cKd/`含最终13文件SHA256、本轮增量及快照说明。部分改前文件由保存内容逆推，仅为语义对照，不冒称全部为逐字原始快照。
- Compare/PDS：`/tmp/compare-progress-projection-tests-final-20261020.log`、`/tmp/pds-bounded-summary-tests-final-20261020.log`；改前`/tmp/compare-pds-polling-before-CzONhP`，改后`/tmp/compare-pds-polling-after-yHI3Hp`，本轮增量`/tmp/compare-pds-polling-increment-20261020.diff`。
- Blender/PAM：`/tmp/blender-pam-reuse-20261020.7KMBiS/`含增量、测试、8例/112例原始对照及运行计数。
- Orca源码/相关CLI脚本与测试WIP diff摘要仍为`b654d7a6c2fe0497c4d3c137964da3987f88811015537d698e256988ef018d68`，与本轮开始一致；本轮没有新增C++改动，不重建，不把上一轮应用回归当成本轮服务测试。

待发布后验收：同一实验切换流程/切片/打印页，确认STL不重复下载、隐藏不渲染、视角保留且离页释放；模拟报告超时后A/B可分别重试，正常G-code下载不受影响；处理中的纯等待只查轻量进度，首次提交/结果变化/终态/UNKNOWN/重试能刷新详情；同模型核对原模板、支撑、附着、墙建议及App修复回退。详细步骤见`Print3DCompareWeb/docs/CORE_REPORT_HANDOFF_20261020.md`，Linux原复杂模型记录阶段耗时与峰值内存。真实WebGL、低内存、跨域网络、业务数据库及服务环境均未在本轮验收。

本轮未提交、推送、部署、执行SQL或实际打印。原切片、分盘、修复命令及调度锁保留；根目录result.json、无关工具和并行文档任务不动。6仓差异空白检查通过，Orca模块文档检查35模块/0问题；各仓分支/HEAD未变，不删除历史业务数据。

### 2026-10-20 精简后第二轮复查（只读完成，建议已获批准）

用户要求继续检查是否还有值得优化的部分，本轮仅做源码/消费者核查，不编码、不提交/部署，不把“是否需要优化”当成继续扩改授权。基准仍为`codex/model-feature-analysis-20261018@bc6684f180`及下节最终WIP；现有未提交源码与其他文档任务保留。四路分别检查Orca残余遍历/完整性、Blender/PAM实际输入与输出、Compare/PDS读写调度、Web报告/Worker缓存。历史图谱早于本功能，使用当前源码；上一轮测试结果只在源码身份未变范围内引用，未知的Linux性能与GUI状态不写成通过。结论按实际问题、可选优化与不应删除的核心计算区分，避免为了减少指标改变判断。

复查结论：在本轮检查范围内未发现新的切片、分盘、修复入口或结果协议阻断问题；仍有以下可优化项，均尚未实施、未测耗时收益。

| 优先级/位置 | 当前证据与建议 |
| --- | --- |
| 优先：Web报告加载恢复 | `sliceReportLoader.js:47–68`没有有界超时，加载中不能重试；`ExperimentSliceComparison.vue:18–21`顺序等待A、B，A挂起则B不开始。建议报告请求超时后明确进入可重试失败；不能照搬当前abort静默返回路径而遗留loading。原G-code下载/打印独立，不据此判断切片失败。 |
| 优先：Web重复STL加载 | `ExperimentDetail.vue:94`通过v-if卸载流程页预览，返回时`ModelPreview.vue:104–112`重新下载/解析/计算法向。仅着色提示缓存还在。建议同一详情保留一个模型预览，隐藏时暂停渲染、离页释放；不要仅改v-show而继续后台逐帧渲染。 |
| 优先：Compare轮询重复详情 | Worker领取及回写都增加job.version（147/558）；Web处理中的15秒progress轮询按版本判断是否重取detail。detail重新读取所有任务/参数快照并解析JSON，即使只是等待。建议区分用户可见结果变化与租约调度变化，不移除并发控制版本。独立URL报告已缓存，不是每轮重新下载报告文件。 |
| 内存：Web原始报告 | loader同时保留完整原始JSON和提炼指标；原始树后续仅供resolvedJob传递实际喷嘴元信息。可提取必要比较元信息后释放原始树。只能减少常驻内存，不能宣称消除首次下载/JSON解析峰值。 |
| 计算/内存：Blender模板特征 | `object_size_mm`及多个后续步骤重复扫描同一世界坐标包围范围，可复用六个原始边界标量并保留原舍入；`analyze_overhangs:396`存全部面法线，后续复杂度只读取悬垂面，可只保存这些面。不要重新引入跨阶段全量顶点缓存。 |

低优先级：PAM五个候选及最终输出各重复同一支撑/附着判定，可在单次推荐内复用（保留输入校验）；Compare只判状态的`succeeded/latest`仍取完整JSON列，可使用必要字段投影；PDS成功执行仍无条件全量拼接错误摘要再截断，可改有界摘要，但`cmdTest`完整诊断输出不能截断。这些不能解释或承诺消除分钟级耗时。

Orca仍有报告专用的整盘贴床路径二次遍历、G-code材料身份遍历，以及不完整层先做布尔运算再丢弃结果的小优化空间。需先量测再决定，不能简单只取第一层或只取耗材统计map而丢失垫层/材料/unknown语义。二维孔缝、遗漏、窄宽、实体覆盖、贴床、成本、流量/冷却、桥接与局部悬臂证据均有消费者，不建议再删。报告流式校验、上传、G-code头尾读取和独立质量/墙接口也保留。

本轮仅更新本记录并检查文档/差异；未运行应用构建、全套测试、GUI或远端性能测试。上一轮本地回归不是新优化的验证证据；上述建议实施后还需对应本地测试及目标Linux复杂模型耗时/内存测量。

### 2026-10-20 按实际消费者去冗余（本地完成，待发布联调）

用户批准保留选参和打印风险核心指标，删除无用计算及重复存储，并回归原切片、分盘、修复。基准仍为 Orca `codex/model-feature-analysis-20261018@bc6684f180`，以上一轮核心精简 WIP 为起点；不覆盖并行文档任务、其他工具和 `result.json`。本轮不提交、推送或部署。

范围：Orca 删除无消费者的独立顶/底覆盖差集、截面交集重复面积、逐层时长数组及重复统计输出；保留二维遗漏/孔缝/窄宽、实体覆盖、贴床面积、成本、流量/冷却和完整性。Blender/PAM配套清理无规则消费者的底面区域数和旧跨度分支，复用计算且保持选择结果；Compare停止报告重复新写、精简无用调度查询，PDS只收敛日志；Web保留核心证据并补人工核查方向，不评分、不自动改参。独立质量/墙接口、切片/分盘/修复命令和异常语义不变，历史数据不删除。

验证计划：各仓本地单测、Web构建、最小Orca应用/测试重建；同模型保留字段/参数选择及G-code对照、固定位置/多盘和修复回归。上一轮二进制仅作改前基线，尚不证明本轮源码。根负责Orca，三路分别独占Blender/PAM、Compare/PDS、Web；源码影响审核使用understand-diff方法，旧图谱不覆盖本功能，回到当前源码。GUI/Linux与复杂模型耗时仍须目标环境验收。

实施进度：

- Orca进一步删掉报告无消费者的角色运动观察器/容器、名义速度流量范围、接缝计数及G1非挤出段额外距离；normal估时器用内部bool继承原报告开关，不改原估时器和G-code生成。峰值/cap距离/冷却保留；逐层时长内部数据保留，仅停止重复输出数组。已独立审核保留scalar、材料身份、默认关闭、取消与失败语义。
- Compare不再把质量/特征报告重复写到实验表及推荐请求；业务消费者仍读取job.result。历史数据库列/值不删，不加SQL。WAITING不重复做五阶段汇总，终态/UNKNOWN仍汇总；轮询关闭无消费者COUNT。PDS仅精简常规日志，测试诊断明细仍保留。
- Web仅展示7项核心对比，加孔缝/窄宽、床面、流量和冷却参考；无报告/未完成不能显示安全。时间来源明确，CSV不为导出额外下载未加载报告；没有自动评分、自动改参或新增阈值。197项测试及生产构建通过，既有chunk大小警告不作为运行失败。日志`/tmp/compare-web-practical-{tests,build}-20261020.log`。
- Compare79项、PDS100项本地mock测试通过；Python报告验证83项通过。分别见`/tmp/compare-workflow-pruning-tests-20261020.log`、`/tmp/pds-workflow-log-pruning-tests-20261020.log`、`/tmp/orca-core-cli-validator-20261020.log`。
- Orca第一轮最小构建成功，原生87项中86项通过；唯一失败是新床面积断言仍沿用已删逐实例helper的非法层口径。现按原整盘规则修正为“非贴床为0”，NaN/非法路径仍unknown，不修改生产床面积算法。日志`planning/slice-consumers-20261020-native-initial.log`保留。同期修复207项/9658断言通过；误写的不存在tag `[ModelCheckWorkflow]`不算覆盖，工作流检查由`[ModelRepair][Inspection]`实际用例覆盖。
- 删除共享运动容器后再次重建应用及两测试目标，当前日志`planning/slice-consumers-20261020-build-final.log`；需以此后最终回归替代上述中间结果，不使用旧二进制证明新源码。

最终结果（替代本节中间状态，不覆盖下文其他任务的历史记录）：

| 范围 | 本轮结果 |
| --- | --- |
| Orca Mac arm64 Release 应用、fff及libslic3r测试目标 | 最终重建退出0；未打其他平台包 |
| 原生切片报告 | 78项、18,009断言通过；包含开关/reset、全部move距离、取消/unknown及只读验证 |
| 原生修复/阶段/重建 | 207项、9,658断言通过 |
| 实际CLI | 报告开关/保护10项；特征40场景；固定位置/多盘64项全部通过 |
| 修复真实入口 | 阶段报告34/34（MeshLib enabled）；MeshLib 42/42，0失败/跳过 |
| Python协议验证 | 83项通过 |
| Blender / PAM | Blender109项、PAM81项通过；最终仅撤回跨阶段全模型顶点缓存后，重跑模板25、独立墙4、墙几何13及8例几何对照全部通过 |
| Compare / PDS / Web | 79 / 100 / 197项本地测试通过；Web生产构建通过 |

Blender/PAM保留字段：8例几何的全部剩余特征逐值相等；8组新旧报告加20份既有真实报告，在Orca/Cura及模板映射/回退的112组最终选择相等。最后撤回顶点缓存不改输出，避免为少算一次顶点变换保留全部模型顶点；没有声称降低峰值内存的实测比例。独立质量、独立墙接口保留。日志与临时对照工具见`/tmp/blender-feature-slim-20261020.4B6N6z/`及两仓文档。

Orca最终证据：`planning/slice-consumers-20261020-{native-final,repair-final,cli,features,fixed,stage,meshlib}.log`；逐条命令、模型、报告及保护哈希在同名前缀目录。应用SHA256为`7ce64d08fbcd7ffb2e97326e0c20c39894d0b916853d7a13dc14eab893639834`，fff为`389d7bc510860649f7565bd9502c5791e66dca0ce596435262be0ab9d2b4efac`，libslic3r为`3dfaa448e71bb1f55592527be5c36da6d1e385bf5f17589c44666c82e6aebe84`。

40份报告与上一轮`planning/slice-core-20261020-final-features`严格比较：22,384个保留标量中22,383个逐值相等，只有树支撑Arachne夹具第20层实体未覆盖面积从`1.9402728201800006`变为`1.9402728201800008 mm²`，差1 ULP（约2.22×10⁻¹⁶）。同一新二进制复跑全部40例，该项又为`1.9402728201800006`，其余字段不变。未放宽比较、未修改面积算法；确认同二进制也存在末位浮点波动，未证明具体区域排序原因，不能写成全部标量严格相等。首次与复跑的所有可执行G-code、有效配置、层数、耗材均与基线严格相同。证据`planning/slice-consumers-20261020-{equivalence-details,repeat-equivalence}.log`，复跑目录`planning/slice-consumers-20261020-features-repeat/`。

40份报告总字节由1,360,087降到775,309，再减少约43%；不把文件体积减少当CPU耗时减少。此次没有受控性能压测，原复杂模型和Linux服务耗时/内存待测。改动经过跨代理独立审核：切片路由、分盘、修复、参数阈值与原G-code生成未改；PDS只缩减日志，完整执行结果仍返回。没有发现本轮阻断性代码问题。

发布与界面交接（尚未执行）：

1. 先部署PAM与Compare，再部署配套Blender JAR/脚本，随后配套Orca/PDS版本和Web。PAM必须先于Blender，否则旧必填字段校验会拒绝精简报告。本轮无需新SQL、不删历史列/记录/模型/附件。
2. QA同一原模型/姿态/机料参数新建实验，确认FEATURE→RECOMMEND→A/B切片完成，模板/支撑/附着/墙建议未漂移，普通App修复任务可正常完成与回退；不下发打印。
3. Web不开悬垂/底面提示时不计算颜色；首次打开才运行Worker，同一实验切换复用。切片/流程/打印页共享报告加载状态，缺失/未完成不能显示0或安全，历史报告仍可读；CSV不额外请求未加载报告。详见CompareWeb的`docs/CORE_REPORT_HANDOFF_20261020.md`。
4. 原生GUI使用相同STL/OBJ和多盘3MF，不启用报告，核对切片、预览、盘号/禁用盘、保存和手动修复；与当前CLI结果对照。GUI没有此报告入口，本轮没有声称GUI人工验收完成。
5. Linux用已重新打包的实际镜像复测小模型和原复杂模型，记录各阶段时长/峰值内存、最终G-code与报告大小；不使用本机小夹具耗时承诺服务器提速或打印质量。

本轮未提交、推送、部署、写业务数据库或实际打印。源码删除可由Git恢复；历史数据保留。所有仓差异空白检查通过，Orca模块检查35模块、0问题。

### 2026-10-20 参数匹配及G-code问题—调参—效果图与全文核对（完成）

继续同一飞书文档，用户要求第3章及之后也用具体问题、对应参数、调整后的可能效果解释，不能只是流程框和文字清单。保留第2章一张总览及16张详细图，对后文采用场景图并核对全文重复、自动/人工边界、计划路径/指令/实物的区分。结合当前精简后的实现口径清理已停用诊断与着色附件的过时描述，不以历史交付范围冒充当前实际输出；部署状态仍需单列。

基准为`codex/model-feature-analysis-20261018@bc6684f180`及现有精简WIP，仅编辑文档与静态图资产，不触碰业务源码、用户改动或部署。先回读云端最新revision并备份，核对保留指标及参数规则，再渲染/目视图解，按块更新并回读检查非目标块、图片位置与能力边界。参考修复文档不改；无应用构建、业务测试或提交。

阶段记录：已重绘参数匹配、人工优先、墙建议复核、缺路径/孔侵入/实体目标、桥与屋檐、流量/冷却/人工运动方向、单因素试参、A/B代价、Web定位与交付/存储边界13张场景图；第2章同步修订6张指标图的当前输出口径。19张替换图已逐张目视。核对期间发现并行消费者精简继续移除了角色/运动/接缝明细、逐层时长数组、独立顶底差集和底面块数；已按当前源码及Blender/Web交接记录修订全文实现表，保持16类工艺方向但明确人工预览项，不以旧字段冒称现有输出。云端以revision154为基线按块更新，保留原文/原图快照；每次写入前校验revision和内容以防覆盖并发修改。浏览器读取超时，本轮以本地图形目视和云端块回读核对为准，不声称整页浏览器验收。

完成：目标飞书revision154→196，40个原有正文/表格块更新、19张图替换（13张后续/状态场景图＋6张既有指标图口径修订）；最终回读又修正一处桥接角色明细旧说法。全文31张图片、3张原生白板、19张表；第2.1节仍为1张特征总览，第2.2节仍为16张详细指标图，99个非目标块（含用户前言、原生白板与操作列表）逐字保留。核验图片token、尺寸、顺序和章节计数；本地guide.md已从回读结果同步。新增静态说明图片合计2,849,466字节，不是业务报告或服务端着色数据。

证据：`planning/exports/model-feature-guide-20261020/followthrough-{before,after,verification}.json`、逐操作响应/进度、`followthrough-plan.cjs`、`publish-followthrough.cjs`和figures生成器/源图。旧快照和图片均保留，可恢复。首次本地导出因无id的ol节点校验停止，未影响远端写入；修正解析并逐字校验操作列表后，恢复执行未重复远端更新。

检查结果：19张替换图目视、云端回读及本地文档图片引用完整性通过；目标MD差异空白通过。全仓模块检查为35模块/3项指纹待复核，涉及cli、gcode-preview、test-tools，属于同工作区并行源码精简尚在更新的模块；本轮不改这些模块指纹，也不冒称全仓检查已通过。仅文档工作，不构建、运行接口/打印、提交或部署。后续由用户在飞书按第3章匹配→第5章具体检查→第6章对比阅读，实际服务能力仍以配套部署和联调确认。

### 2026-10-20 特征总览与指标—参数—效果图重构（完成）

用户明确本轮要“一张特征总览＋具体指标到切片参数再到效果的详细图示”，不是两套几乎相同的结构图。仅修改目标飞书及静态说明资产：用一张汇总图替换第2.1节四组特征图；第2.2节按16类展开测量位置、参数作用和调整前后机制，保留风险/代价与自动或人工的区别。原实现表不以示意图代替，后续章节已有独立图解保留。原文、原图及token保留可恢复，逐张目视和远端回读后记录结果。

仓库仍为`codex/model-feature-analysis-20261018@bc6684f180`，存在另一进行中“核心指标精简实施”的源码改动，属于该任务，不覆盖、不构建、不提交；本轮图中的测量/调参关系作为工艺解释，不把尚未测量、人工尝试或已停用诊断冒称当前自动功能。若云端已有更新，以最新块为准，不用旧整篇稿覆盖。

已按块完成目标飞书revision=96→154：第2.1节合成一张六组特征总览；第2.2节改为16张“指标测量位置→参数改变路径/工艺的方式→前后可能效果”图。具体画出Arachne窄笔画路径、墙与填充分布、分层台阶、支撑承托、桥接方向、Brim贴床范围、孔补偿、封顶、冷却、运动、接缝和流量等机制，不再把两套特征分类图重复摆放。效果标为机制示意而非实测或保证，每张保留自动/人工范围、增时增料或尺寸/材料风险；相邻组附Orca官方功能说明链接。实现表保留原接入记录，在导读明确精简后实际输出及部署状态需对应版本报告和验收记录确认，不把文档16类解释当成每任务必跑清单。

验证：17张静态图逐张目视并修正标注遮挡；新PNG合计2,429,861字节，仅为说明文档素材。云端回读确认第2.1节1图、第2.2节16图，全文31张图片/3张原生白板/19张原表，114个非目标块逐字保留；16张新增图片均在各自标题后，尺寸1600×740，总览1600×980。移除7张重复旧图及对应2.1分组标题/段落，原图、token、revision=96快照及生成器仍保留可恢复。后续章节、参考模型修复文档和用户前言未覆盖。

证据在`planning/exports/model-feature-guide-20261020/effect-story-{before,after,verification}.json`、操作响应、`figures/render-effect-stories.cjs`及SVG/PNG；本地`guide.md`同步对应章节。API顺序/内容核验与本地图形目视通过，浏览器整篇排版未验收。模块文档检查35模块/0问题，目标文档差异空白及本地图路径检查通过。本轮不改业务代码、不构建、不运行业务任务、不提交或部署。下一步由用户从第2.1总览进入第2.2任一具体指标，核对“为什么调、调哪里、预期改变什么、代价是什么”的阅读效果。

### 2026-10-20 核心指标精简实施（本地完成，待目标环境/界面验收）

用户已批准编码，并要求保留可检测且可用于优化参数的指标。保留清单：切前匹配全部实际输入；切后二维漏路径/孔覆盖、窄宽/间距、床面足迹、成本及流量/速度/冷却聚合。停止三维名义连通全套精算与汇总、逐片坐标和无消费者的深度/跨层诊断；模板业务停止附加厚度与服务器着色附件。前端按需生成悬垂/底面示意，当前实验内存复用。修复、参数决策、切片/分盘和独立质量接口不变，不新增版本开关或调度架构。

基准Orca`codex/model-feature-analysis-20261018@bc6684f180`；现有文档/result.json/工具WIP保留。Web`develop@87c891f`，PDS`243ec801`，Compare`94fcb58`、Blender`4d550a7`含本线程前轮区域文件化WIP。根负责Orca及集成，三路分别负责Web、Blender、Compare，禁止交叉改文件、提交、推送、部署或外部业务测试。PDS现有顶层校验兼容精简报告，无需新SQL/DTO。

验证：各仓指标/接口回归，新Orca应用及测试目标构建，CLI报告/关闭报告对照与固定位置/分盘保护已完成；GUI/浏览器实际交互及Linux服务耗时仍待验收。旧二进制仅作改前基线。CodeGraph MCP未暴露，历史Understand图早于本功能，以模块文档和当前源码定位；按understand-diff影响复核方法检查当前差异，不把旧图当最新依赖证据。

实施进度：

- Orca 已移除三维连通专用实现/CMake条目和不参与业务的深度/跨层/源面计算；保留逐层二维口径，只保存当前层临时足迹，不留全模型诊断缓存。接缝和床面分区坐标不再输出，CLI仅把JSON改为紧凑格式。旧专属测试随功能删除，混合测试保留有效断言，并加入禁止产生废弃字段的测试。
- 改前同一应用已完成10项CLI开关/输出保护基线，证据 `planning/slice-core-20261020-before`。最小所需应用与 `fff_print_tests` 正在 `build/arm64` 构建，日志 `planning/slice-core-20261020-build.log`；原有CMake生成重编了共享依赖，未启动其他平台打包。
- Blender 模板20项、Java22项（含真实Blender，无跳过）、独立墙4+11项和原质量31+9+5项通过。8组模型/姿态剔除停止字段后，全部匹配输入与 Git HEAD 旧实现逐字段相同。证据 `/tmp/blender-feature-slim-20261020.4B6N6z`；不改独立质量/墙算法。
- Compare 58项本地mock通过；不再请求/保存颜色附件。历史GET保留，旧生成/重试410，在途纯着色任务本地结束，不再调Blender。PDS无需代码/SQL改变。日志 `/tmp/compare-feature-pruning-tests-20261020.log`。
- Python报告校验87项通过。40个CLI模型/工艺场景及输入生成函数保持，停用字段禁止输出，尚待新二进制真实运行；日志 `/tmp/orca-core-report-python-tests-20261020.log`。
- Web 首轮核心收敛后191项测试及生产构建通过；旧大诊断解析器/专属测试一并删除，复用报告loader，零基线百分比改为不适用。本地Worker颜色测试不代替真实浏览器验收；日志 `/tmp/compare-web-core-{tests,build}-20261020.log`。
- 独立源码复核确认保留的宽度/间距、床面面积、配置与流量/冷却算法与HEAD一致；CLI分盘/修复/切片控制无改动。静态审核不替代正在进行的新二进制回归。所有历史日志和附件保留，不提交/推送/部署。

最终验证（下列结果替代上述中间运行状态）：

- Mac arm64 Release 最终应用和测试目标构建退出0：`planning/slice-core-20261020-final-build.log`。应用SHA256 `e59b84abcb43c3c6e6b7ef06c047dccdf7c0f6f502c2ffa55cf1565da19aeb0b`；fff测试SHA256 `693d474f04a4a4d949b86bc5c5ebbe285333254156461d9dbae006f99e44b20a`。
- `[SliceAnalysis]` **92项/21,528断言通过**，日志 `planning/slice-core-20261020-final-native.log`。独立审查补回被纯诊断块夹带删除的3个有效测试：配置百分比原样保留、孔内岛床面面积、共享筏不读备份；对应退休坐标断言删除。新实现仍通过直接/嵌套路径第17次offset取消、开关/reset及只读验证。
- 最终二进制 **10项报告开关/保护、40项特征真实切片、64项固定位置/分盘回归均通过**。命令分别为既有三个脚本；参数及逐条命令保存在 `planning/slice-core-20261020-final-{cli,features,fixed}/`，日志同前缀 `.log`。覆盖原STL/OBJ固定位置、越界拒绝、多盘、空/禁用盘、报告on/off和非切片工程导出；不发起打印。
- 40份最终报告与 `planning/orca-merge-review-20261020/analysis-features` 对照：全部 **42,253个保留标量字段严格相同**（仅排除文案limitations），未使用浮点容差。全部可执行G-code、完整有效配置、层数、耗材严格相同。40份报告由 **16,421,163字节→1,360,087字节**，约减少91.7%。本轮即时cube旧/新基线3种开关的G-code同样一致，报告 **1,693,547→101,829字节**。此为样本文件体量，不冒充CPU提速比例。
- 单次cube观察旧enabled 0.931秒、新enabled 0.195秒；新测试存在并发且非受控基准，不作为总体耗时/服务器吞吐结论。QA原复杂模型未在本轮重跑，Linux目标内存/耗时待测。
- Web 最终 **190项测试、生产构建通过**（中间191数量不再沿用）；交叉审发现彩色STL原Float32颜色属性不能复用Uint8缓冲，已删除原色并固定normalized Uint8；剩余墙P10/骨架明细解析也已删除并复测。Worker、缓存/失效、取消/重试、报告状态接线、零基线均有离线测试；不冒充浏览器WebGL已验收。
- Blender **102项测试**（包括真实Blender联合链路）及8组新旧匹配输入严格一致；Compare **58项mock**、Python **87项**通过。PAM消费者只读复核确认21项必需特征与支撑/悬臂、墙status/scope/risk/lineWidth保留，无thickness/颜色/骨架消费者被遗漏。PAM、主服务及PDS不需要新增代码、SQL或配置。
- 全仓差异空白检查通过，模块文档检查 **35模块/0问题**。原有 `result.json`、无关文档/工具WIP及历史附件未清理。不自动提交、推送、修改测试服务或飞书。

界面/部署交接（均待执行）：

| 场景 | 具体操作与预期 |
| --- | --- |
| 原生普通切片/预览 | 启动本仓 `build/arm64/src/Release/OrcaSlicer.app`，导入 `planning/cache/cli-assets/models/stl/cube.stl`，加载同目录 profiles 下 EP7 0.4/standard 0.20/Generic PLA 配置后切片并查看预览。应正常生成路径、时间和耗材，没有新增报告菜单或后台报告工作；保存G-code供同参CLI核对。 |
| Compare核心展示 | 配套发布新Orca包、Blender Jar及Python脚本、Compare服务和Web；PDS/PAM本轮无需重新改动。选择同一测试模型重跑实验，核对模板/支撑/附着/墙建议与旧结果，流程/打印/切片三个页签加载同份报告，未加载/失败不误写未提供，不出现退休诊断表。 |
| 本地着色与缓存 | 在同一实验首次展开模型提示、切原模/悬垂/底面、切页返回。仅首次计算，不生成区域附件或重跑Blender；改模型/姿态重新计算，离开详情释放。含原色二进制STL仍显示正确底色，加载失败可重试；底面是最低位置示意，不是精确分区/桥接证明。 |
| 大模型服务成本 | 使用原QA慢模型，记录特征、切片/报告阶段实际耗时和报告字节；检查任务最终成功、无旧着色文件和三维未覆盖明细。Linux资源与浏览器内存实测单列，不沿用Mac简单样例比例。 |

Web详细步骤见相邻仓 `Print3DCompareWeb/docs/CORE_REPORT_HANDOFF_20261020.md`，Blender步骤见 `Print3dBlenderService/docs/model-feature-analysis.md`，Compare退休区域入口语义见 `Print3DCompareService/docs/FEATURE_REGIONS_FILE_HANDOFF_20261020.md`。回传实验ID、时间段、目标镜像/构建身份、日志和G-code/报告即可复核；未执行真实打印，本轮不声称打印质量验收完成。

### 2026-10-20 按章节补齐特征、指标、选参及问题图解（完成）

用户澄清上一轮合并图表的方向不符合要求：需要先认识模型特征，再用独立图解理解测量指标；参数匹配、Arachne、切片／G-code问题、对比与验收也应在各自章节配图，而不是集中放一个汇总。以目标飞书revision=50、Orca `bc6684f180`为基准，保留原正文中的业务边界、16类和用户前言，重新组织第2章并逐章补图。特征图展示结构及关注点；指标图展示测量范围、数值／状态的读法；选参图展示证据到工艺的关系；检查图展示正常情况与待核查问题。示例数字明确非实测、非业务阈值。仅改文档及静态素材，不更改或声称新增功能、业务存储、部署状态。

验证计划：所有图本地渲染并逐张目视检查，按块更新目标飞书、保留原生白板与非目标内容，回读核对章节位置、图片身份及全文边界；本地记录生成素材和快照，保持可恢复。不修改参考模型修复文档。

已完成目标飞书revision=50→96：4张特征识别图（16类）、4张测量指标图（16类），以及抽样读数、参数匹配、Arachne条件、结构缺失／孔缝、连接承托、G-code关注点、参数复核、A/B读数、页面入口、交付验收与两类文件说明，共20张新图。全部逐张目视，修正抽样点与表面关系、统计量排序标注、检查图的单位及验收图措辞，并复看修正图片。16张按章节就近插入，现有4张混合图原位换为指标图，其余2张专项图和3张白板保留。第2.1节恢复独立特征认识，第2.2节解释测量和读数，第2.3/2.4节保留匹配证据及观察指标；第3章拆开“特征到工艺”和“模板／建议／人工参数来源”。没有把特征删除并入指标，也没有单设集中图册。

回读验证：7个主章节均有就近图解；共22张图片、3张原生白板，原19张表和88个非目标正文／表格／白板块逐块保留。新增图token及所在章节、4张替换图身份与尺寸核对；不以文档示例数字冒充实测、自动阈值或新增能力。SVG/PNG、生成器、前后快照和操作响应在`planning/exports/model-feature-guide-20261020/`；最终结果见`chapter-visuals-verification.json`，本地`guide.md`同步对应正文和图路径。20张静态PNG合计2,733,652字节，不涉及每任务计算或报告存储。

模块文档检查35模块／0问题，目标跟踪文档差异空白检查通过。浏览器整篇排版未验收，不以API回读冒称网页目视通过；参考修复文档、业务代码、数据库、部署和任务未操作。纯文档改动，不构建应用、不测试业务、不提交。下一步为用户按第2.1→2.2→3→5章阅读，分别核对结构认识、指标读法、选参理由和切后问题。

### 2026-10-20 按业务收益精简的替代方案（待用户审核，未编码）

用户进一步明确：不接受把无用明细改成汇总继续算/存，仅保留参数匹配所需及对打印有明确价值的证据；展示着色优先前端识别、单次缓存。此要求替代上一轮“保留全量诊断能力并另存明细”的建议，也不再将16类研究指标作为每任务必跑清单。本轮只出方案，审核后编码；基准与工作区业务状态未变，不操作QA、部署、任务或历史附件。

待审范围：

- 保留匹配必需的悬垂/桥接/局部悬臂证据、底面与稳定性/形态指标、水平窄细节风险及其外墙线宽依据。`supportAnalysis`与`cantileverRegions`中的面积/距离确有规则消费者，不是颜色坐标，不得误删。尺寸、法向等仍是计算基础；校验字段与旧回退的调整须与PAM一起核对，不单端删响应字段。
- 本流程停止附加三维抽样厚度，不保留它的观测汇总；独立质量检测原功能不变。停止新任务生成/编码/上传着色三角面附件，前端复用已加载STL，首次查看时计算悬垂提示及最低处底面候选，当前实验内存缓存，模型版本/姿态变化失效，离页释放。不以预览Z=0冒充打印床，不声称与后端修复/分区等价。取消不能由轻量法向分类支持的紫色桥接、精确区域计数/面积和整块区域选择承诺；后端桥接小指标继续用于匹配。
- 停止完整`nominal_target_path_connectivity`精确三维计算及其全部输出，不保留未覆盖体积等替代汇总。此前95%为样例紧凑JSON体积占比，不是耗时或缺陷比例；目标减路径包含正常稀疏填充留空，不适合作为直接质量结论。停止本流程无消费者的逐片坐标、接缝坐标、闭环周长转向、完整跨层连接/深度/投影等工程诊断。
- 切后仅建议保留：原有预计时间/耗材及支撑用量；少量有明确核对动作的二维证据（有目标却完全无路径的独立截面、闭孔被计划路径完全占据）；利用既有处理器数据核对已配置流量上限和层冷却时间。后两组仅提示核对，不等于实物缺陷/打印合格，不新增未经确认的阈值。必要指标的计算范围/完成状态保留；不为缺测造零，不再做满屏通用统计或空值表。
- 业务页只显示匹配原因、参数变化、时间/耗材/支撑成本和上述少量待核对项；共用现有报告状态，修复跨页“未提供”。不引入新旧版本切换、通用缓存服务或新调度框架；颜色不阻塞业务。不为缩小输出而保留全部旧计算，更不将大量几何精算搬至浏览器。

审核通过后的验证边界：同模型/姿态/配置的匹配与最终G-code保持原有结果；普通切片/分盘、修复和App接口不被旁路修改；新任务不再生成旧颜色附件及三维未覆盖明细/汇总；前端切页不重复下载/识别，变换改变不复用旧色；未完成检查不报通过。按阶段记录改前/后耗时、文件大小和内存再报告收益，不能预先承诺提速95%。此次仅源码分析与方案记录，不构建、不宣称测试通过。

### 2026-10-20 图例与指标解释去重（完成）

用户指出特征概览与16类图例看起来重复。本轮以目标飞书文档revision=44为基准，合并为一套16类图解：图只说明结构和测量位置，表说明指标口径、参数用途与当前能力边界。移除已被完整图解覆盖的四例概览，保留全部指标表、后续章节和用户内容，不改业务或存储代码。原图、生成器与文档快照保留用于恢复；先本地渲染目视检查，再原位更新四张图和相关导读，最后回读核对非目标块未变。

已完成revision=44→50：原位替换4张图，图中去掉重复的自动化状态及“测量／对比／边界”三段文字，保留结构、测量标注与必要读图说明；奇数图组最后一项居中，不用说明卡重复凑格。导读和第2.1节统一为“16类模型特征：图解与测量指标”，按相同编号查图和表；删除旧四例概览，原素材及token已留档，可恢复。全部16类、19张表、3张原生白板、另2张专项图片及92个非目标正文／表格／白板块逐块核对保留。4张新图合计682,927字节，属于文档静态素材，与每模型报告存储无关。

本地4张PNG逐张目视检查，API回读验证图片token、尺寸和位置；浏览器整体排版未验收，不冒称页面目视通过。证据在`planning/exports/model-feature-guide-20261020/dedup-{before,after,verification}.json`及对应上传／替换响应，本地`guide.md`同步目标段落与图路径。模块文档检查35模块/0问题，目标跟踪文档空白检查通过。无应用构建、业务测试、提交或部署。下一步为用户在飞书第2.1节查看合并后的图解。

### 2026-10-20 五处报告展示与冗余开销复核（诊断完成，未实施精简）

用户反馈等待较长、五处页面内容难理解及“未提供”不一致。本轮仅核对当前源码、上一轮已读取的同一实验报告与截图，不改业务计算、接口、检测范围或参数规则，不重跑实验、不部署。Orca仍为`bc6684f180`；Web`develop@87c891f`干净，PDS`243ec801`干净，Compare`94fcb58`保留既有区域文件化WIP。历史图不覆盖本轮报告功能，以当前源码为准。

- 图2/5是前端接线遗漏：`ExperimentDetail`已有`sliceReports`只传给切片文件页，`WorkflowStages`和`ExperimentResults`内的`AutoReport`没有`reportLoader`。因此它们仅识别内嵌报告，URL报告即使已在另一页加载仍显示“未提供”。应复用同一加载器/缓存，不新增store或重复下载；区分未加载、加载中、失败、已加载、确实无报告。
- 图1上半是朝上水平表面的窄细节检查，下半是三维表面抽样对侧距离，两者不是同一测量。1200/1200为有效样本/抽样面数，1/1为抽样覆盖壳体/检出壳体；0.0004mm只是被接受样本中的最小值，不能据此认定模型真实最薄壁或打印精度。XYZ是变换后模型样本表面位置，不是打印机执行坐标。模板分析当前默认附加厚度观测，但PAM不消费它；水平窄细节风险与Arachne建议另有独立消费者，不能混删。
- 图3技术明细当前只供Web展示、A/B人工比较和CSV导出；自动匹配读取切前FEATURE，并非切后这批指标。图4应优先解释时间、用料、支撑与路径覆盖差异，不能把层序列数、路径长度、接缝记录数当质量分。A=0时百分比正确不计算，但通用格式化显示“未提供”，应标“基线为0，不适用”；本实验B尝试修复而A没有，不是纯参数对照。
- A/B共享一次网格检查、一次特征分析和一次推荐，各自切片/报告是必要对照。Orca报告计算/序列化及PDS完整流式校验、上传均在成功返回前；Compare后台只保存报告URL，没有再读取整份大报告。G-code基础时间/耗材解析只读头尾有限范围。未测本实验分阶段耗时，不能把等待全归因于某算法。
- 展示用特征附件保存位于FEATURE完成前，后续切片等待FEATURE；当前WIP又加入OBS下载/COS转存，不能把尚未发布的下载路径解释成本次QA耗时原因。按需“重新生成特征标记”会复跑Blender，正常A/B没有因此重复运行。

建议顺序（待实施）：先修跨页报告状态与零基线文案；流程/打印页只留业务摘要和诊断入口，技术面板集中切片页；保留指标口径，普通产物以摘要为主，评估将14.8万条逐片定位明细改为诊断用途而非普通任务必存内容。JSON紧凑化只降低写盘/校验/上传/解析成本，不宣称减少几何精算耗时。展示附件不应阻塞核心匹配与切片；默认厚度观测和重计算是否按需，应结合耗时与业务收益明确后再改。不擅自取消旧着色、16类范围或质量检查，不新增自动择优阈值。下一步待用户确认精简范围后编码及针对性验证。

### 2026-10-20 QA 小模型切片等待与报告体量核对（只读完成）

用户询问新增测量、切片特殊处理及截图A/B一直“处理中”。本地Orca仍`bc6684f180`，Compare`94fcb58`含既有区域文件化未提交改动，Web`87c891f`、PDS`243ec801`。只读当前测试实验`01b2632b-075f-40e5-acfb-7cf8d0a4a8c6`：创建17:47:32，检查/特征17:47:34、匹配17:47:57，A/B提交17:47:57/58；现场读取时两组均已成功，G-code已生成，参数与切片3/3，未下发打印。截图时间为提交时间，不是结束时间或最近心跳。A预计打印202.62分钟/23.89g，B233.35分钟/22.66g，不能当作切片计算耗时。

“切片流程”页的“切片报告指标·未提供”未区分URL报告尚未加载；“切片文件”页有加载入口。用户加载后两份analysis JSON均HTTP200，页面指标和差值可读；网络资源大小分别72,314,209及约44,913kB。只读下载第一份到`/tmp/orca-qa-slice-report-audit-20261020.XlGcS8/report.json`分析：紧凑JSON为24,343,949字节；`objects.groups[0].nominal_target_path_connectivity`为23,016,433字节（约94.5%），75个目标连通分量下合计148,053条`missing_slab_regions`，每条含bounds及volume，不是原Blender着色三角坐标，也不等于148,053个模型缺陷。CLI当前`report.dump(2)`缩进进一步放大文件体积。

当前代码链：Compare A直接切片，B因本例检出10组自交传repairModel=true，在同次Orca命令追加`--auto-repair-model 1 --repair-model cgal`，不进入独立MeshLib分阶段重建。PDS按`source=compare`自动启用报告，无需collectAnalysis请求字段；CLI在G-code生成后调用`make_slice_analysis_report`，PDS校验、上传报告后才完成任务。因此处理中涵盖切片和额外报告工作，不能从尺寸小推断成本低（本例236,130三角面、A360/B388条G-code层序列）。未读取本次阶段日志，未测分阶段耗时，不宣称等待主要由报告或CGAL造成。后续应评估分析计算与持久化明细的必要性、精简报告及修正未加载文案；本轮只诊断，不修改业务、重试、部署、数据库或发起打印。新增切前墙/厚度与旧悬垂/底面/着色分开解释，不把文档16类范围说成全已自动调参。

### 2026-10-20 完整16类配套图例（完成）

用户指出第2.1节16类只有表格、原示意图仅覆盖局部。此次只为同一飞书目标补齐16类几何／路径机制图例，按A—D四组就近放在指标表前；保留既有流程图、正文和用户内容，不修改检测、区域存储或业务代码。基准Orca仍为`bc6684f180`，既有未提交内容保持。图例复用既有SVG说明图风格，明确示意而非实测、当前证据及自动／人工参数边界；不为静态说明图生成模型三角面数据或变更可视化存储方案。

实际读取时目标revision=35，用户已新增前言并拆分开篇段落，保留该最新版本，不用旧稿整体覆盖。生成4张图组，共16个编号条目，每项包含几何／路径示意、测量内容、参数对比方向及能力边界；本地逐张目视检查后修正截面宽度标注遮挡、窄区关注位置、开放槽轮廓及固定XY位置标注。4张PNG合计约1.18MB，为文档静态说明素材，不随业务任务重复生成。首次媒体上传因CLI要求相对路径而在文件校验阶段失败，未插入图片；改用目标目录内相对路径后完成，无重复上传块。

写入并回读revision=44：4张新图分别位于A/B/C/D标题与其指标表之间，16类编号完整；原3张图片、3张原生白板保留，95个原正文／表格等块完全未变，仅更新原四例总览的导读图注。证据为`planning/exports/model-feature-guide-20261020/illustrations-{before,after,verification}.json`及插入响应，SVG/PNG与可重现生成器保存在其`figures/`。浏览器页面读取再次超时，网页整体目视验收未完成；本地图形目视与API图文位置校验通过。模块文档检查35模块/0问题；不构建应用、不测试业务、不提交或部署。下一步为用户在飞书第2.1节阅读新增图例。

### 2026-10-20 特征业务文档完整范围整理（完成，待用户阅读）

用户要求将飞书说明优化完整，纠正9组匹配摘要看似代替原16类范围的问题。只更新目标Wiki `Kn6fwWsYWinZy2k1vvqcYyKknod`、本地同步稿和本记录；不改业务代码、不发布或重试任务，不修改模型修复参考文档。已回读目标revision=15，与原导出一致，现有3张原生流程图及3张机制图保留。Orca仍为 `bc6684f180`，Blender/Compare上轮区域文件化修复为未提交工作区改动，不能写作线上已具备。

已按当前16类方案、已核对源码及两路只读复核补齐：完整指标/单位/范围、原有与本次新增、自动匹配与人工对比方向、未实现算法、未接通规则、待联调和实印验收。16类分为细节与表面、承托/落床/稳定、孔缝/封顶/冷却、运动/出料/成本四组；原9组摘要改名为“切片前，哪些证据已参与匹配”，不再代替完整清单。补A/B差值示例、Web查看位置、典型模型与>4MiB区域文件验收用例；明确模板层高变化不等于厚度自动调层高，原生冷却/流量规则仍生效。第7章区分已实现交付、仍缺的软件能力、目标环境联调与实印标定，以及两种大JSON和不同失败边界。

目标文档现为revision=30，回读核对7章、16类、19张表，原3张白板的源码及3张图片token均保持；参考修复文档未执行写操作。按块更新过程中，整段替换第7章一次未匹配且未改变revision，随后按已确认的旧表格/段落块分别替换并删除已被展开内容覆盖的旧验收摘要，没有整篇覆盖。前后快照、更新响应及`complete-verification.json`保存在`planning/exports/model-feature-guide-20261020/`。浏览器只读预览在DOM读取时超时，未完成网页排版目视验收，不以CLI回读冒称浏览器验收通过。

本地同步`doc/bmr-print-optimization.md`的范围说明及飞书入口，保留技术16类原始指标表。两路独立复核后修正长度单位、区域附件补来源/变换后保存的语义，以及大文件验收案例。模块文档检查35模块/0问题，本轮两份跟踪文档差异空白检查通过。业务代码、任务、数据库、部署均未操作，不做无关构建/测试或提交。下一步：用户阅读飞书第2.1节完整指标及第7章能力边界；区域文件化代码的发布顺序与待联调事项仍以下一条记录为准，文档完成不代表这些功能已上线。

### 2026-10-20 特征区域文件化修复（本地完成，待发布联调）

用户授权优化前述大报告入库失败。范围：Blender 模板分析将 `visualization.regions` 写为独立 JSON，在清理临时目录前复用 OBS 上传，结果保留检测指标和小型文件引用；Compare 读取该文件、补齐原有模型/变换身份并沿用 COS 附件及网页 `assetId` 契约。不改 Orca、参数匹配规则、独立墙特征接口、数据库表/参数或部署，不新增业务开关/版本。可视化文件失败只降级区域显示，不清空已完成的检测指标。

基准：Blender `4d550a7`、Compare `94fcb58`，两仓开始时干净；Orca `bc6684f180` 仅更新本记录，原有其他改动保留。验证计划：大于 4 MiB 的区域文件不得进入 Blender/Compare 任务结果 JSON；指标保持一致；上传/下载失败降级；原内联区域响应和不请求可视化场景保持；临时文件清理及网页原读取契约。复用已授权本地单测，所有外部存储/数据库 Mock 或本机 HTTP fixture，不使用线上服务、设备或数据库。完成后独立审查差异；不自动提交、推送或重试实验。

已完成：

- Blender Python 原样保存独立 `template_feature_regions.json`，报告不再内联坐标；Java 在原临时目录清理前上传 OBS，只将 `visualization.url` 等小型信息入库。生成或上传失败保留原检测指标，区域显示为 unavailable；不请求可视化的路径不上传。同步 OBS 文件上传流及时关闭。
- Compare 复用原 AssetService/COS，通过单个流式 JSON helper 读取新 URL 或原内联结果；保留原区域内容，按实验快照添加模型来源/变换，存成原附件。数据库与网页继续使用 `assetId` 小引用。沿用原文件大小上限，补基础下载超时和文件格式校验，无新配置或新业务流程。
- 独立审核核对 Main/PAM 仍只消费原 features，匹配/支撑/附着/墙建议未变；Compare 文件失败隔离、两处结果小引用以及区域重新生成不覆盖主报告均成立。未发现新增阻断，不把审核当作线上验证。

本轮本地验证：

| 范围 | 实测结果 | 证据 |
| --- | --- | --- |
| Blender Java联合/独立入口 | 24/24，0跳过；真实Blender、本机模型HTTP、存储/数据库Mock | `/tmp/blender-feature-regions-java-20261020.cxhqck/tests.log` |
| Blender模板脚本 | 32/32；>4MiB区域与<2KiB夹具报告、坐标保留、文件失败不改变指标 | `/tmp/blender-feature-regions-python-20261020.86Uq8E/tests.log` |
| Compare文件/任务/原上传回归 | 59/59，0跳过；>4MiB真实HTTP文件→Mock COS→两处<1KiB夹具报告，旧内联、新引用、上传/下载失败、超时、清理与重新生成 | `/tmp/compare-feature-regions-tests-20261020.log` |

Compare首轮新增空URL用例发现校验空指针，修正后四类测试完整重跑通过；历史禁用集成测试不计入59项。Blender和Compare差异空白检查通过，Orca文档检查35模块/0问题。夹具大小是验证结果，不是对所有模型报告大小的上限承诺。Orca业务源码未修改，无需应用构建/GUI回归；本轮未修改数据库参数、运行QA重试、发布、提交或推送。

**下一步：先发布全部 Compare 实例，再发布配套的 Blender JAR 和脚本，重试原失败实验。** 无需为本问题重新打包Orca、发布Web/PAM/Main/PDS或扩大数据库包限制。已成功附件无需迁移，失败任务不会自行恢复。人工验收：检测成功且指标可读；区域图可加载并对齐模型；Blender结果无regions，Compare报告只有assetId；区域不可用时指标和后续匹配仍可用。详细命令、发布顺序和验收见 Blender `docs/model-feature-analysis.md` 与 Compare `docs/FEATURE_REGIONS_FILE_HANDOFF_20261020.md`。对象存储真实权限、跨服务网络及QA原模型仍待发布后确认。

### 2026-10-20 QA 特征检测失败排查（根因已定位，未修改业务）

用户要求分析实验 `20e419ff-b11c-4bd8-8774-6f0a4e46197c`（模型 `1544439115497701376`）特征检测失败原因。本轮只读实验页面、QA Grafana 日志与当前源码，不重试业务任务，不修改服务、数据库或部署。源码基准仍为 Orca `bc6684f180`、Compare `94fcb58`、Blender `4d550a7`；保留已有未提交改动。

现场证据：`print3d-blender-qa` 中 `2a7916463262423488b790b164ec8728` 于 14:45:10.759 开始，14:45:26.974 命令退出 0；14:45:27.218 在 `TemplateFeatureAnalyzeJobService.updateSuccess` 保存 `template_feature_analyze_job.result_json` 时抛出 `PacketTooBigException: Packet for query is too large (4,776,046 > 4,194,304)`，随后任务标为 failed。失败是结果入库超过当次 MySQL 4 MiB 包限制，不是 Orca 切片超时，也不是网格缺陷检出。Compare 页面固定错误文案掩盖了底层原因，失败会阻止后续匹配与切片。

现有模板任务将完整报告（含区域可视化数据）一起序列化入库；Python 可视化以 float32 坐标的 Base64 表达区域三角面，只有 300000 面数量上限，没有匹配数据库包大小的字节预算。具体字段占比尚未拿到完整失败报告，不能把总 SQL 包大小当成某一字段的实测大小。下一步待用户决定修复：适当调整数据库包限制可解除当前阻塞；代码应隔离大可视化数据与小指标结果，并向页面传递可理解的失败原因。此轮不擅自执行修复或重跑。

浏览器扩展读取响应体超时后，改由现有 Chrome 原生界面查看 Grafana 的失败日志上下文；没有读取凭据或绕过鉴权。原查询 `print3d-slice-qa` + `modelTransforms` 不属于 FEATURE 调用链，查不到日志不代表 Blender 无异常。GUI/CLI 构建及打印验证不适用本次只读诊断。

### 2026-10-20 飞书业务说明同步（完成，待用户阅读）

用户要求参考“模型修复业务执行流程”，用已有飞书 CLI 将模型特征检测、参数匹配和 G-code 检查写入目标 Wiki `Kn6fwWsYWinZy2k1vvqcYyKknod`，多用图解释。本轮只修改该目标文档及本地说明/验证产物，不改业务、部署、数据库、打印任务或参考页。目标已读确认为空文档，真实 document_id 为 `FssPdyIdooqizOxlf17cKQmsnQd`，初始 revision=1。

Orca 基准为 `codex/model-feature-analysis-20261018 @ bc6684f18026721b6d19348d509e1e8c780a11e8`；保留工作区原有模块文档、result.json 和未跟踪文件。已有 lark-cli 位于 Node18 的 bin，不在当前 Node22 PATH；使用绝对路径，不安装或升级工具。Understand 历史图早于当前功能，CodeGraph 状态不作为完整覆盖依据；通过相关服务当前/固定提交源码复核。

结果：已写入并回读 [模型特征检测、参数匹配与 G-code 检查](https://ver133tlfd.feishu.cn/wiki/Kn6fwWsYWinZy2k1vvqcYyKknod)，revision=15；7章、10张表、3张飞书原生白板和3张机制示意图。参考页 revision=359 前后未变。正文、Mermaid、SVG/PNG、CLI响应与回读验证保存在 `planning/exports/model-feature-guide-20261020/`；`verification.json` 确认章节、表数、白板源码、图片token及关键边界。

分工核对并独立复审完整特征清单、模板/参数决策、切后报告与 A/B 比较，修正文案中悬垂不等于已证无承托、抽样厚度不等于全局最薄、计划路径不等于最终G-code几何、Compare匹配回退阻止B切片、缺配置上限仍可展示峰值、网页不含G-code路径查看器等边界。3张本地图片及3张白板导出预览已目视检查。浏览器页面核对尝试超时，不将其写作完整网页交互验收；CLI回读、嵌入图片身份和白板内容检查通过。

代码依据：Orca `bc6684f180`、Blender `4d550a7`、PAM `b64fc3c`、Compare `94fcb58`、PDS `243ec80`、Web特征版 `87c891f`、Main `d09374371d`。读取期间Web工作区被外部切到develop，后续固定feature提交阅读，没有切分支或把本地切换当作线上状态。明确“检测到指标”不等于“已自动调参”，Compare 原有切片内 CGAL 修复不等于 App 的分阶段 MeshLib 修复。未修改业务代码、运行测试/打印、提交或部署；线上部署一致性和实物效果仍待用户联调。下一步为用户阅读目标页并按第6、7章进行新任务验证。

### 2026-10-20 合并提交交付（本节随合并提交生效）

用户授权提交已审查的整合结果。本次在 `codex/model-feature-analysis-20261018` 完成合并提交，父提交为本分支 `40d8be288d` 与本地 master `3239eab859`；不移动 master、不推送、不部署。纳入已审查的 master 切片/分盘修复、Linux MeshLib 打包、阶段报告精确顶点共享及对应回归/文档，保留工作区无关改动和定向 stash，不使用全量暂存。

业务源码、测试及打包配置与下方已验证候选保持一致；本轮更新交付状态，并从暂存版 test-tools 文档排除遗漏的 Thingi10K 多行测试记录，工作区原文完整保留。复核暂存范围、未解决冲突和文档/空白检查，不重复此前已通过的构建与回归。GUI 手测、原生 Linux 验收和已知历史限制仍按下方清单保留，不将提交等同于完整产品验收。

Linux 构建状态更新：只读核对 `integration-20261020-j6rPoD/build-exit-code.txt` 为 `0`，原构建日志末尾显示 AppImage 封装成功；因此此前“后台构建中”的记录已结束。新包的隐藏 SDK 搬迁、固定位置/分盘、报告及修复专项仍未验收，不能据此直接部署。下一步是按需推送本分支并在 GitHub 使用该分支的工作流打包，或继续对本机 Docker 新包执行既定验证；本次提交不触发这些外部操作。

### 2026-10-20 Orca 合并与提交前审查（代码可提交，Linux新包仍待验证）

**本轮结论：Orca 合并工作区已完成、冲突全部解决，独立代码审查及新Mac构建回归未发现新增阻断，可提交此整合候选供测试分支使用；尚未执行commit/push，也未部署。** 只把master修复合入当前feature，不移动master。Linux完整新包仍在本机Docker后台构建，不可将代码准入等同于新包可部署；GUI及原生Linux验收仍待执行。已知历史repetitions限制继续保留，不额外扩展本次修复范围。

用户授权先整合 Orca 并检查是否可提交合并。本轮仅将本地 master `3239eab859` 的两笔修复合入当前 `codex/model-feature-analysis-20261018 @ 40d8be288d` 工作区，使用 `--no-commit`，不推送、不部署、不修改其他服务仓库。现有 Linux/MeshLib 与阶段报告 WIP 保留；result.json、下载工具及其他任务文档不纳入本次提交范围。重叠的未提交文件先建立可恢复的定向 stash，解完合并冲突后恢复并逐项核对。

验证计划：审查 CLI 导出冲突同时保留报告开关和非空 G-code 检查；核对分盘/镜像变换与修复阶段报告；复用当前工具链最小增量构建，重跑固定位置、分盘、只读报告和修复专项；最后检查模块正文/指纹及差异。旧包和旧测试不作为合并结果通过的依据。GUI 手工验证与原生 Linux 部署状态另列，不以静态审查替代。

合并处理：定向 stash `7175617a2f1a7f0503e2e3b9716a0fdf035f51c9` 保存7个重叠文件，应用恢复后保留该备份，不删除用户其他stash。已清除两轮冲突，MERGE_HEAD 为 `3239eab859`，HEAD 尚未移动。CLI 唯一业务冲突合并为带 `slice_analysis_requested` 的导出调用加非空文件校验；报告失败仍报错并保留 G-code。工作流同时保留无 SDK 的 MeshLib 搬迁验证和 master 的固定位置/分盘回归；文档合并不沿用旧指纹作为新代码通过。独立审查指出旧测试缺少“固定位置/禁用盘与报告开关组合”，正在补入既有脚本，不改业务实现或旧断言。

构建/测试进行中：Mac 复用本目录 arm64 Release/MeshLib ON 缓存，运行 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests fff_print_tests --parallel 6`，日志 `planning/orca-merge-review-20261020/build-macos.log`。本机 Docker Ubuntu24 amd64 私有 `/work/orca` 同步本轮源码后复用依赖，使用原 `build_linux.sh -s -i -j6` 及 `ORCA_MESHLIB_LOCAL_EVALUATION=ON`、SDK `/probe/sdk`、`ZLIB_USE_STATIC_LIBS=ON`；日志 `/tmp/orca-meshlib-linux-leXC1x/full-build/integration-20261020-j6rPoD/orca-appimage-build.log`，不覆盖旧版本包，不触及远端服务器。Linux 配置/打包工具53/53通过；Mac同53项50通过3专属跳过，尚不代表完整应用通过。

提交范围：已建立只包含本轮整合的暂存候选；未纳入 result.json、Thingi10K下载脚本/测试/模块说明、GUI启动分析及其他文档任务。共享模块清单和test-tools/arrangement文档采用部分暂存，工作区原有文字完整保留；因此test-tools工作区与暂存指纹不同是范围差异，不是漏测。首次暂存检查指出下载工具指纹和无关GUI链接，已按范围剥离，不通过补收无关文件来规避检查。当前 `--all` 与 `--staged` 均35模块0问题、差异空白检查通过；尚未提交，后续结果落盘后重查。

Mac三目标构建已完成，退出0；应用 SHA256 `88aa9af3e6b4ed0bf692a0b063e1244579c77ae394cf33f39d3b1f4daacfd46b`，libslic3r_tests `a47fd318656a999250b2995da5f33fe22c46efcfc66fbef7992c84ef2b258ee7`，fff_print_tests `2f3f38eef87caabe2a6bf46e9d8e72f856c43b34f740aa4746540bb8c0b9ef77`。身份保存于 `planning/orca-merge-review-20261020/binary-identities.sha256`。已启动真实 fixed-position（含报告新增6例）、arrangement、原生3MF/Arrange、修复与分析专项，使用新目录及独立进程cwd，结果随后回填。编译成功不先写测试通过；Linux仍在本机隔离容器构建。

最终新Mac构建结果（证据根目录 `planning/orca-merge-review-20261020/`，各进程退出0）：

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 固定位置/报告交叉 | 64/64；成功场景开启/关闭报告的全部执行指令、完整有效配置及层数相同；禁用盘不出报告，越界/全禁用失败无报告 | fixed-position/validation.json |
| 单/多盘、镜像/定向JSON与3MF | 154/154，含JSON开关的实际路径一致性 | arrangement.log、arrangement/ |
| 3MF与分盘原生 | 58用例/3826断言 | tests.log |
| 修复/证据/重建/阶段报告原生 | 229用例/9842断言，含百万面与MeshLib取消/异常join | repair-native.log |
| 分析/G-code原生 | 242用例/54665断言 | analysis-native.log |
| 阶段报告、Alpha Wrap、MeshLib、旧check | 分别34/34、36/36、42/42、114/114 | repair-{stage,alpha-wrap,meshlib}/summary.json；repair-check/orca-model-check-ob19ftwa/summary.json |
| 分析报告/特征指标CLI | 分别10/10、40/40，输入完整性通过 | analysis-{cli,features}/summary.json |
| Python分析验证器 | 170/170 | python-slice-features.log |
| Linux配置/打包工具 | Ubuntu24容器53/53；Mac50通过3平台跳过 | 容器integration目录python-linux.log；/tmp/orca-meshlib-integration-review-qVJHg0/python-meshlib.log |

不同过滤器不合并累计；百万面只验证读取至截面预算，不代表百万面完整修复吞吐。Alpha Wrap实际观察到限时退出；MeshLib CLI未运行可选慢模型限时项，`cooperative_timeout_observed=false`，不冒称CLI真实耗时超限已通过。原生3DBenchy存在与历史一致的只读备份目录告警，不算GUI备份验收。测试前后源码/程序/输入身份一致，根result.json Git blob保持`7a157845db6c198b1e6b7e8db249ba1e995f2919`。

Linux续接：容器 `orca-meshlib-linux-40d8-probe` 的现有构建session `51057` 保持运行；10:08核对为234/515、无编译错误。日志目录 `/tmp/orca-meshlib-linux-leXC1x/full-build/integration-20261020-j6rPoD/`，完成退出码写 `build-exit-code.txt`。先核进程/退出码，不重复启动构建；成功后再验证新 `2.3.2.3-Release` AppImage/完整AppDir的真实运行，执行新版64例fixed、154例arrange及报告/阶段/MeshLib专项，隐藏SDK并核依赖身份。此批最终包运行尚未执行，旧包未覆盖，不访问远端服务器。

GUI 交接（待手测，不宣称通过）：使用本轮完成构建后的 `build/arm64/src/Release/OrcaSlicer.app`，不要打开旧打包副本。CLI夹具位于 `planning/orca-merge-review-20261020/fixed-position/`；尚未生成时先等待下方CLI结果。使用该目录的machine/process/filament参数作对照，或明确记录GUI选用的有效参数。

| 场景 | 操作与预期 | 状态 |
| --- | --- | --- |
| 禁用实例部分越界 | 打开 `disabled-partly-outside.3mf`，确认第二对象关闭打印，切第一盘；应正常生成第一对象路径，禁用对象可显示越界但不阻止切片。再启用第二对象，应拒绝部分越界；关闭后重新切片恢复 | 待GUI |
| 多盘与切片包 | 打开 `two-plate-seed/output/model.3mf`，逐盘切片；保存工程后重开，应保留两盘成员/坐标。另开 `first-disabled.3mf`，只第二盘有可打印模型，导出的切片包应只包含实际成功盘G-code | 待GUI |
| 原有修复入口 | 导入本轮stage夹具缺面模型，执行既有高级修复并导出重开；手动Alpha Wrap生成候选后取消，原件保持。GUI不应出现新报告开关或自动MeshLib选项 | 待GUI |

保留操作日志、导出的3MF及截图回传；GUI通过也不等于实机打印验收。当前已知旧 `--repetitions` 多实例复制数量问题未纳入本次修复；此前偶发备份异常未在master加测中复现，不写成根因已消除，详见下方既有记录。

### 2026-10-20 测试分支合并与服务联调准入审查（完成，先整合再部署）

用户要求评估 Orca 及前后端当前能否合入 devlope 并部署测试服务；本轮仅分析，不合并、切分支、提交、推送、迁移数据库、部署或启动真实业务。Orca 当前仍为 `codex/model-feature-analysis-20261018 @ 40d8be288d` 加前轮 WIP，master 为 `3239eab859`；必须区分已提交功能、未提交 Linux/阶段报告修正、目标分支与已有包身份。并行核对 PDS/Main、Blender/PAM、Compare/Web 的真实分支、接口、依赖和既有验证；原生主代理核 Orca/master 整合边界及 Linux 包。先核各仓实际存在的 develop/devlope 引用，不猜测 Orca 同名分支。只使用匹配源码的图谱；历史图谱不足时降级源码，不新建索引。

**结论：可以进入合并收尾，不可直接拿当前分支/旧包部署整体测试。** 六个服务/Web仓库真实目标名均为 `develop`，不是 devlope；均在功能分支且干净。通过 `git ls-remote` 实读最新目标 SHA，发现 PDS/Main/Compare 比本地缓存新；仅用 `git fetch --no-write-fetch-head --no-tags --no-recurse-submodules origin <SHA>` 取缺少的提交对象，不更新分支、工作区或 FETCH_HEAD。以下 merge-tree 对明确 SHA 预演，不是实际合并。

| 仓库 | 功能提交（待合增量） | 本轮核实目标 | 预演结果与条件 |
| --- | --- | --- | --- |
| Orca | `f653f0a47f`、`40d8be288d`，另有已验未提交 Linux/百万面修正 | 本地 master `3239eab859`；codeup/master 仍 `4df6d516be`，没有同名 develop/devlope | 6个冲突文件：5文档+CLI。必须保留 master 固定位置/无G-code失败/镜像定向JSON修复，并保留 feature 的按需报告采集；不能任选一边。只合HEAD会漏WIP |
| PDS | `b4a0380`、`b1e8c40` | `3119dd219df2dc1286453348fce24b0b93068fdd` | 无内容冲突；最新目标 Dockerfile 已升级基础镜像v2.0.22，未验该镜像内Orca身份；需先加报告URL列 |
| Main | `26dd94c51`、`d1476c15a` | `e99140d24645ff1294cfff2df5aaa0673f5a5b62` | 5文件冲突，3业务+2测试；须保留目标多模型advanceModels、PLATE排除与设备场景策略，再叠加墙宽/建议及最终模型消费 |
| Blender | `4d550a7` | `70b5c53d9a918e367e8b71b3241e1a425eb71fe8` | 无冲突；合并树保留目标OBJ朝向/超时更新，完整部署共享Python脚本，合成树尚未重测 |
| PAM | `b64fc3c` | `90c70c9ec923fcaffc00a2046f6a59f3e7e91c87` | 无冲突；只增加有限Arachne墙建议，无新SQL |
| Compare | `94fcb58` | `d0ebd6b647298882e0325c9893606b00b194507f` | 无冲突但非快进；合并树等于功能HEAD。旧Jasypt/初始化SQL已在远端，不是本次增量 |
| Web | `87c891f` | `02e30f17db0da0c8b7b59a94a352f4a99440332d` | 可快进；完整报告按需读取，真实存储CORS/复杂报告性能仍待联调 |

Orca预演树 `50c57072c3549430094492b481f19c60ce5a7456` 的唯一业务冲突在 G-code 导出：master要求有效非空文件，功能分支增加 `slice_analysis_requested` 参数；两者都必须保留。Main冲突为 `AdvancedSliceParamMatchServiceImpl`、`DevicesServiceImpl`、`PrintJobServiceImpl`、`ImportModelPrintabilityProcessStepHandlerTest`、`DevicesServiceImplAdvancedSliceParamConfigTest`。PDS预演树 `e6da7b2c5a0b13299ceb70d8cdfe4dd557a411e2`、Main冲突树 `3d9687c016b5ba1f531c9cb05f592c4e1b959af1`；这些树仅供审核，不是可部署提交。不能用功能分支已有单测证明解冲突后的代码通过。

**部署前必须完成：**

1. 整理 Orca 当前已验 WIP 并与 master 两笔修复整合；不得夹入 result.json、下载工具和无关文档。重新构建最终 Linux 包并验证普通固定位置切片/分盘、报告开关、阶段修复和缺库隔离。现存 v2 AppImage SHA本轮复核仍为 `0e855633231e941573bb4cb117add84ad699acfdf1031744a8d176ebd8131127`，对应 feature+WIP而非master修复，不能直接用来验收整条业务。保持完整AppDir依赖，不只复制ELF；目标须是匹配Ubuntu24用户态，v2.0.22实际内容本轮未读取。
2. 主服务手工解冲突后跑对应回归；PDS/Blender/PAM interface 都是 `1.0.0-SNAPSHOT`，先发布/刷新匹配新代码的接口产物，再构建消费者，不能复用旧同名缓存。
3. PDS先核表结构并执行一次 `print3d-slice/src/main/resources/db/migration/V20261018__add_slice_analysis_to_slice_job.sql`。代码直接映射 `slice_analysis_url`，缺列会影响普通任务查询，不只是Compare报告；项目未配置自动迁移，不能仅部署Java或重复跑初始化SQL。保持原任务升级前排空策略，检查模型修复和普通切片各自的Orca命令路径。
4. 先单任务测试健康/高级/重建/失败兜底，以及原APP固定位置/分盘；再验特征→模板→Arachne建议（人工值优先）→Compare报告加载与A/B。确认文件存储跨域GET；报告任务预算1800秒、普通任务180秒隔离保留。复杂模型在目标容器测实际内存/耗时/完整报告上传读取，不以延时替代内存限制。当前PDS正常Java命令不使用 repetitions，前述上游多实例重复问题不作为该正常入口阻断；手动自定义命令另行约束。

本轮只读接口/源码、Git整合及已有证据：PDS105、Main126、Compare47、Web384及构建、Blender/PAM等历史结果见各仓交接/上文；未重新运行它们，不将未绑定完整源码hash的旧日志视为合并树验收。Orca当前包已有MeshLib42/阶段34/切片报告10、master另有切片58/分盘154及间歇100次证据，仍不替代合入后的同包回归。CodeGraph MCP未暴露、CLI可用；本目录status提示1文件变化，但系统sqlite只读打开失败，未把status当覆盖证明；Understand图仍旧，使用模块和当前源码，不写历史图覆盖。业务代码/分支未修改，没有部署、访问数据库或发起打印。

### 2026-10-20 第二项间歇异常补核（来源与最新复测状态）

用户追问第二项。只读复核 master `3239eab859` 的历史日志、当前源码及已有加测证据；未重新运行应用、构建、修改业务或操作服务器。该项包含两个独立现象，不把 OBJ 超时与备份删除异常合并为同一根因。

- **备份清理崩溃：相关代码及生命周期链均为上游继承，尚无证据证明本次定制引入。** 失败日志 `../orca-slicer-master/planning/cli-fixed-position-20261019/mirror-json-fix/slicing-serial/project-new-stl-0-outside/run.log` 显示同一 Model ID、同秒、同目录先排队 RemoveBackup，随后合并阶段 `get_backup_path()` 删除旧目录抛出 Directory not empty。`get_backup_path`、析构与后台 `process_task` 同官方 v2.4.2；官方同样存在 vector<Model> 扩容时可复制保留 ID、不复制 backup_path，以及多输入合并调用。已在线核对 [Model.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/v2.4.2/src/libslic3r/Model.cpp) 和 [bbs_3mf.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/v2.4.2/src/libslic3r/Format/bbs_3mf.cpp)。删除语句可追溯到 `1555904beff`；本次新增盘成员刷新尚未执行，合并后的实例映射也尚未执行。目录生命周期与后台删除/重建交叉是有日志支持的原因方向，但缺少官方成品运行、现场线程栈/系统调用证据，不能写成完整并发根因已经证明。
- **OBJ 120 秒超时：仍无法归因。** 原日志停在 Print::process 调用前准备阶段；无现场栈，未请求 JSON，不能认定由镜像 JSON 修正引入，也不能因另一例备份异常就归为同一原因。
- **更新复测状态：已有 100 次加测未再出现两个异常。** 本轮读取 master 另一轮已完成的 `planning/cli-fixed-position-20261020-repeats/results/{identity,results,summary}.json`：两个场景各 50 次（各 25 串行、25 两路并发），总计 100/100 符合预期，身份为当前 `3239eab859` / 应用 SHA `cf0678709d362c2991e9e2be5e2a2a9e8b61c78e9e5f756c299625416694475d`。OBJ 输出有效 G-code；越界混合输入正常返回 -50，无崩溃/缺结果。维持 120 秒超时，未达到 10 秒采样阈值。这里复核已有证据，不算本轮新增测试；仅说明 macOS 未再复现，不声称根因已修复或 Linux 通过。

### 2026-10-20 多实例 repetitions 来源核对（完成，仅溯源）

用户本轮仅要求判断第一个问题是定制引入还是官方已有；第二个历史间歇问题不再展开，不将本轮未调查写成根因已修复。本轮未修改业务代码、构建、重跑应用、提交或操作服务器；开发目录仍为 `40d8be288d` 加已有 WIP，独立 master 仍为 `3239eab859`。

**结论：官方继承的 CLI 存量缺陷，不是本次定制引入。** 最早来自 BambuStudio 的 [`99bbce0f0f20ae8f55e76483f26e972ce4af6e4c`](https://github.com/bambulab/BambuStudio/commit/99bbce0f0f20ae8f55e76483f26e972ce4af6e4c)（2023 年 7 月，加入 repetitions）。`f6ac8b60b71` 后续修正 skip/loaded_id，但保留按实例遍历、每次克隆整个对象的逻辑。官方 [OrcaSlicer v2.4.2 PartPlate.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/v2.4.2/src/slic3r/GUI/PartPlate.cpp) 仍包含该实现；本地同名引用的完整函数与 `4df6d516be^` 逐字一致。另通过网页工具核对官方公开源码的同一循环、整对象复制和 CLI 调用；终端直取 raw 文件超时，因此没有宣称远端文件哈希一致。

官方可达链也一致：`OrcaSlicer.cpp:4287–4307` 将 repetitions 转为 count−1，`:4399–4410` 允许 arrange，`:4948` 调用复制函数；原生 3MF 导入 `bbs_3mf.cpp:4198–4208` 本就支持同一对象的多个实例，`Model.cpp:503–505,1151–1155` 克隆全部实例。这些行号对应本地 `upstream-v2.4.2`。因此同对象两实例要求重复两份时，原有 2 份加两次整对象克隆各 2 份，得到 6 而非 4；不需要定制导入才能触发。近期 `4df6d516be` 在该函数仅增加原/新 ObjectID 的收集和返回，`3239eab859` 不改复制数量。

验证边界：当前 master 的 2→6 已由上轮真实工程和 G-code 复现；本轮对官方只做源码与调用链核对，未运行官方成品。仅确认同盘同对象多实例、CLI `--repetitions > 1` 的这类问题，不扩大为普通单实例重复、GUI 复制或跨盘变体已复现。后续如修复，应单独补这组数量回归；本轮只记录来源。

### 2026-10-20 master固定位置修复核对（完成，另确认重复份数旧缺陷）

用户要求确认master是否已经修复固定位置空盘，并检查有无其他问题。本轮只读审核实现与隔离验证，不合并、修改业务、提交、推送或部署。独立master工作目录已干净，HEAD为 `3239eab859`，包含 `4df6d516be` 固定位置切片／分盘一致性及 `3239eab859` 镜像／定向JSON修正；本工作目录仍为 `codex/model-feature-analysis-20261018 @ 40d8be288d` 加前轮WIP，两笔提交均不是本分支祖先，昨日Linux v2包也未包含。不能继续把旧缺陷称为master当前未修，也不能把master修正视为开发分支已获得。

分别核对最终源码、程序身份与测试证据；并行审核盘成员／切片／导出边界和与阶段报告／切片分析的交叉。master没有CodeGraph/Understand索引，不初始化或复制其他目录索引；旧开发目录Understand图仅为历史导航，不作为master事实。审核期间master仅验证文档被外部工作追加，本轮未改该目录文件；业务源码、脚本、应用及HEAD保持身份一致。

**空盘结论：master已修复、开发分支尚未同步。** master原始网格导入逐实例登记，最终几何变换后刷新归属；空盘跳过前有真实成员，最终没有G-code时明确失败。还包含启用实例越界防漏切、禁用实例不阻塞、实际盘号重验、assemble后3MF成员与导出引用保护。`3239eab859`另修镜像／定向JSON，不改变实际模型几何。两笔修复的审核范围内未再发现新增确定回归；不能将下述历史重复入口问题归因于本轮修复。

本轮在master的Mac arm64 Release／MeshLib OFF最终程序上串行各跑一轮：58/58切片检查、154/154分盘／变换检查通过，保留默认120秒超时和既有断言，没有反复刷绿。证据 `/tmp/orca-master-cli-20261020-BTEueU/`；应用SHA256 `cf0678709d362c2991e9e2be5e2a2a9e8b61c78e9e5f756c299625416694475d`，CLI源码 `ccd31ccb9e29680a3c912ee749b61be37746f653a7e95673adb1a1f6bb5a1ac8`，PartPlate源码 `5f26ae6a34320a0c991dc242ccaba5714070dcc07cbe1f169203dc23ded9e5b1`。本轮复用匹配构建，没有重新编译、修改服务器或生产数据。

**另确认P2旧缺陷：多实例工程的repetitions会多复制。** `master/src/slic3r/GUI/PartPlate.cpp:2274–2301`逐盘内实例遍历，却在2295行每次复制整个对象及其全部实例。同一对象有两个启用实例、同盘，`--auto-plate 0 --arrange 1 --slice 1 --repetitions 2`应为4份，实际为6份，命令仍返回0；repetitions1对照为2份。新增定向两次运行分别核对真实3MF build/盘成员、JSON及G-code：重复2时有6个互不重叠的模型区域，每区284条正E/XY挤出命令，不只是JSON多行。证据 `repetitions/physical-path-summary.json`、`seed.json`和两组command/run/output；没有改正式回归脚本。G-code对象注释标签重复，不能按注释键数统计实例，已由物理边界与实际挤出匹配确认。Git追溯循环/整对象复制来自`99bbce0f0f2`等历史代码，`4df6d516be`只增加返回ObjectID；现有154项只覆盖单实例重复，未覆盖此组合。未验证跨盘变体，不扩大复现结论；本轮仅报告，不修复。

**历史稳定性风险尚未关闭：** 前一轮`fixed-obj-no-json`曾120秒超时，`project-new-stl-0-outside`曾因`Model::get_backup_path()`中的remove_all抛出Directory not empty而SIGABRT。本轮两例均通过，但没有定位并修复根因，不能用复测通过消除风险。已核对失败日志及当前`Model.cpp:1318`和`bbs_3mf.cpp:8692`相关清理路径仍在；线程交叉访问只是调查方向。异形床旧问题按已有业务范围未扩展修复，本轮不重新验证。

**后续同步边界：** 开发分支的报告预检查`:7124`也依赖盘成员，master提前登记后前提可一并补齐；合入时必须保留`export_gcode(..., slice_analysis_requested)`及报告失败返回，接上非空G-code与零产物失败检查。ModelStageReport仍在check/reconstruct入口提前返回，SliceAnalysis读取真实Print/G-code，不消费model_transforms，未发现JSON投影污染分析指标。两边各自测试不替代合入后的固定位置＋报告开/关/已有报告、禁用/空/多盘报告数量、镜像定向与实际G-code组合测试。GUI、原生Linux/Windows、服务器及合入后最终包仍待验；本轮没有合并或提交。

### 2026-10-19 复审修正完成（当前交接状态）

基准仍为 `codex/model-feature-analysis-20261018 @ 40d8be288d`，保留工作区原有改动，不提交／推送／部署。按用户要求暂缓固定位置空盘／分盘问题；本轮只修正阶段报告百万面预算口径，以及 Linux MeshLib ON 的构建依赖与运行库隔离，不改切片算法、分盘逻辑、业务阈值或 GUI 入口。

- 阶段报告读取 STL 时只共享精确相同顶点，保留每个三角面的坐标、顺序、朝向及原文件 SHA；新增真实 1,000,012 面回归，旧重复角点表示触发预算，新表示能完成指定截面。不是扩大预算，也不宣称百万面完整修复性能通过。
- Linux ON 补齐静态 ZLIB 参数；首次完整包审计另外发现 TBB 头定义未被 archive 隐藏覆盖，已增加精确命名空间局部链接脚本与空格路径引用。OFF、Mac、Windows链接路径不变。真实合成 ELF 测试及独立审核通过。
- Mac 最终构建后：213 原生用例／9,272 断言、34 阶段报告、36 默认 Alpha Wrap、10 切片报告 CLI 均通过；跨补丁默认 G-code／配置一致。最后合并 Python 回归为 223 项，220 通过、3 项 Linux 专属跳过（`python-tests-final.log`）。
- Linux 修正版 **v2** 已增量重链并重新封装。SDK 隐藏、清空额外库环境、原字节解包并搬迁后，依赖／哈希、42 MeshLib、34 阶段报告、10 切片分析及 AppRun help／实际重建通过。`LD_BIND_NOW=1` 和实际重建中 SDK→主程序 TBB 绑定均为0，原14个冲突符号均使用 SDK OpenVDB 自身定义，不以首次包通过代替本次验收。
- Linux 新旧包普通切片对照：G-code指令、有效配置、90层、2.79g耗材及排除墙钟字段的旧result一致。Linux工具回归53/53；本轮未放宽任何旧断言。新包证据在 `/tmp/orca-meshlib-linux-leXC1x/full-build/final-appdir-v2-xyAnjI/`，重点文件为 `tbb-binding-isolation-summary.json` 和 `pre-post-symbol-isolation-slice-comparison.json`。Mac证据为 `/tmp/orca-20261019-fixes-fNQ04T/`。
- 独立只读复核两份实际绑定日志各50,403条，SDK的3,939条TBB相关绑定全部落在SDK库，没有绑定到Orca主程序；检查覆盖全部TBB名字而非仅原14项。主程序797行动态定义符号中TBB定义为0。代码审核及最终包证据复核未发现新增阻断。

修正版包：`/tmp/orca-meshlib-linux-leXC1x/full-build/OrcaSlicer_Linux_V2.3.2.2-Release-meshlib-40d8be288d-v2.AppImage`，SHA256 `0e855633231e941573bb4cb117add84ad699acfdf1031744a8d176ebd8131127`；包内ELF为 `edc52d0defc6ccfd74d9cef2836f32937fd43414135a1172e348d90f39b367c5`。SDK已恢复，容器无活动构建／测试，原包和失败证据保留。当前源码与容器私有副本一致，根result SHA仍为 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。

**仍未覆盖：** 本机 ARM/Rosetta 无法直接启动原 AppImage 外壳，原生 Linux x86_64 入口、服务器、Windows和GUI手测仍待验证；已验证的是原字节包内程序。本分支暂缓的固定位置空盘缺陷仍是发布阻断，不据本轮回归允许直接替换线上镜像。Orca自身第三方TBB的既有ODR编译警告保留，详见[Linux打包记录](2026-10-19-linux-github-packaging-review.md)。最终模块文档检查35模块／0问题，`git diff --check`通过；没有自动提交。

### 2026-10-19 复审问题修正过程（历史：首次包与隔离缺口）

**最新阻断（首次完整包验收后发现）：** 下表功能测试虽然通过，但进一步 `LD_BIND_NOW=1` 证实包内OpenVDB10.1会把10个TBB内联函数及4个RTTI绑定到Orca自身旧TBB定义，现有 `--exclude-libs` 未覆盖主程序/Orca静态库中实例化的头定义。部分同名函数实现长度不同，不能因小模型未崩溃判定兼容。正在Linux MeshLib ON链接边界补局部符号隐藏及真实ELF回归，不改OFF、Mac、Windows、算法或业务阈值。首次AppImage与绑定日志保留作为修前证据；新包重链后须另做完整包内验证，不能直接交付首次包为已验收版本。

用户授权修正上述问题，并明确暂缓问题1（固定位置切片／盘成员）。本轮只处理阶段报告原始STL顶点表示造成的预算不一致，以及Linux MeshLib ON缺少静态zlib选择和相应漏测；不改分盘/切片逻辑、业务阈值或后端接口，不提交/推送/操作服务器。基准仍为 `40d8be288d`，保留已有工作区改动。

已在保持三角面实际坐标、顺序、朝向和文件SHA不变的前提下，让显式阶段报告读取共享精确重合顶点，新增真实百万面截面预算回归；普通重建解析默认仍不共享，GUI和普通切片不走该阶段报告入口。Linux只对ON构建参数补 `ZLIB_USE_STATIC_LIBS=ON`，测试真实依赖查找而不再只模拟Crypto/TBB。独立源码复核未发现新增阻断。

本机Mac应用与libslic3r_tests已按最终代码重新构建通过；容器 `/work/orca` 独立副本复用已完成的第三方依赖，完整根CMake默认配置已真实复现共享zlib拒绝，显式静态配置成功，CGAL目标25步及完整应用／AppImage构建均通过。新证据目录 `/tmp/orca-20261019-fixes-fNQ04T`；Linux完整构建日志继续存 `/tmp/orca-meshlib-linux-leXC1x/full-build/`。实际结果按构建、包内程序和原始AppImage启动分开如下。

| 本轮验证 | 结果与边界 |
| --- | --- |
| Mac应用与libslic3r_tests最小目标重建 | exit 0；arm64 Release、MeshLib ON；日志macos-build.log |
| 原生模型修复、检查、重建、截面依据及同进程MeshLib回归 | 213用例／9,272断言通过；含新增百万面和精确顶点两例，native-repair-complete.log；此前单独2例／29断言及54例均为子集，不累计 |
| 阶段报告真实CLI | 34/34通过，含真实MeshLib候选、完整复检与导出回读；model-stage/summary.json |
| 默认Alpha Wrap独立重建CLI | 36/36通过、无跳过，沿用原断言／超时；含300万面输入、256MiB边界、证据开关候选一致及实际1秒预算超时受控返回；alpha-wrap/summary.json。大面数夹具为重复几何，不代表复杂大模型修复性能 |
| 普通切片报告开关真实CLI | 10/10通过，默认／显式关闭／开启的指令和有效配置相同；slice-report/summary.json；使用摆盘场景，不覆盖暂缓的固定位置空盘缺陷 |
| 跨本轮补丁的默认G-code对照 | 与前轮默认关闭产物相比，7,218条指令、565行有效配置、90层、耗材元数据及剔除墙钟后的旧result一致；default-gcode-before-after.json |
| Mac Python | 222项中220通过、2项Linux专属跳过；python-tests.log |
| Ubuntu24 amd64配置／打包工具回归 | 52/52通过；config-package-regression.log，不代替完整应用运行 |
| 最终Linux包内程序（本机amd64仿真） | 原字节解包并搬迁AppDir，隐藏SDK且清空额外库路径后，依赖／哈希校验、42/42 MeshLib、34/34阶段报告、10/10切片分析及AppRun help／实际重建均通过；final-appdir-ZVzZoh/ |
| 原始AppImage入口 | 本机Rosetta/binfmt返回Exec format error，未通过原脚本；失败证据保留于final-package-15L6F2/，原生Linux x86_64启动仍待验，不能以手工解包测试覆盖此状态 |

新应用SHA256为 `ddcaaa45ce0e5bde659655474ee175a3b91a0844b2a137f0e5056bd9713beed1`，测试程序为 `fdee7266c2be68739b72e297393bc043e0ff6704764f536cd4ecb1d5e1ea9dc3`。实现／头文件／新增测试源码哈希及CLI脚本身份见 native-identities.txt、cli-source-and-binary.sha256。验证前后源码、程序、输入资源及根result哈希不变。首次宽选过滤中的 `[ModelInspection]` 无匹配，已改为实际 `[ModelRepair]` 重新完整运行213项；没有把空过滤计为通过。新百万面用例约0.81秒，只测读取到指定截面预算，不作为百万面完整修复吞吐结论。

GUI无新入口／文案，本轮回归包含共享修复核心与导入保色；GUI点击操作、Windows、原始AppImage在原生Linux的启动和目标服务器运行仍未验，继续按既有GUI清单交接。模块文档和复核指纹已更新，`check_ai_module_docs.py --all` 为35模块／问题0，`git diff --check`通过。问题1保持未修复，不以本轮通过覆盖该已知发布阻断；本轮不提交或部署。

最终AppImage保留在 `/tmp/orca-meshlib-linux-leXC1x/full-build/OrcaSlicer_Linux_V2.3.2.2-Release-meshlib-40d8be288d.AppImage`，SHA256 `38ef077b5e3834c033ee5452082e66c866960867825b9d785997b9c3d25b0bbe`。未改其AppImage magic或其他字节；仅在本机隔离容器安装squashfs-tools，验证原文件offset 944632的SquashFS4.0后手工解包。最终运行使用搬迁后的包内ELF而非构建树，SDK由trap恢复；详细库／符号证据与TBB依赖自身ODR警告见[Linux打包记录](2026-10-19-linux-github-packaging-review.md)。这不是向GitHub或服务器发布的包，也不能直接替换含已知切片问题的线上镜像。

Mac构建结束后只读资源检查确认容器8核中4核使用、可用内存15.54GiB且无swap；按用户此前提速授权，将Linux构建从4并发增量调为6。旧Ninja在346/545收到定向SIGINT，原脚本exit2属于主动中断，不是编译错误；确认无活动编译进程后启动同配置续编，日志另存orca-appimage-build-j6.log，不覆盖原日志、不清依赖或缓存，也不改源码／验收标准。

### 2026-10-19 模型修复／特征检测交付复审（完成，有待修问题）

用户要求继续检查当天功能，不授权本轮修改业务实现或提交。基准为 `codex/model-feature-analysis-20261018` / `40d8be288d`，覆盖阶段报告 `f653f0a47f`、切片指标及按需统计 `40d8be288d`、现有 Linux MeshLib 打包 WIP，并按实际分支核对关联服务。保留已有文档、下载脚本和根 result 改动，不合并发布分支修复、不操作服务器/数据库/打印机。

本轮并行审阅修复交付、特征/几何统计、Linux 链接打包和 PDS 兜底编排；已核对本机应用 SHA256 `87a61cd565e1e4e35db2f4c39db7478d7616b2d39caa289588c41fca9cca4e60` 与冻结记录一致，fff 测试程序 SHA256 `565e5bd3d0be23b66f3841a893af4830e921fd0ffe5822b61ae674162b0cdd36` 及 Processor/测试源码也匹配。历史 Understand 图未覆盖新模块，不写覆盖图；源码和分仓身份为准。现有 libslic3r_tests 为更早构建，未用于证明本轮修复实现通过。

**发现与下一步（本轮不修复、不提交）：**

1. 新增 P2：阶段报告使用 `read_bounded_model_stl` 保留实际导出坐标（`ModelCheckWorkflow.cpp:317,376`），但该解析器每面存三个独立顶点（`ModelReconstruction.cpp:319–328`）；截面预检在 `ModelRepairEvidence.cpp:325` 按存储顶点数限制 300 万。因此 1,000,001 面即有 3,000,003 个存储顶点，即使唯一顶点数在预算内也会拒绝。触发前提是高级修复后仍有缺陷、无直接重建依据且需要截面观察；随后 `require_completed_evidence` 失败，阶段报告不发布，业务保留前一份模型。普通切片不走此路径。该问题由 `f653f0a47f` 的原始 STL 回读接入引出，已按源码确认预算口径不一致，**未运行百万面完整业务复现**。应保持三角面坐标不变，精确合并重合顶点或统一预算口径，并补报告开关的稠密网格边界对照；不直接放大内存上限。
2. Linux MeshLib 构建 P1：`.github/workflows/build-linux.yml:104` 和 `src/meshlib/README.md:58` 的 ON 配置未传 `-DZLIB_USE_STATIC_LIBS=ON`。Ubuntu 24 的 CMake 探针默认找到 `libz.so`，与 `src/meshlib/CMakeLists.txt:249–250` 的静态依赖要求冲突；显式参数后找到 `libz.a`。证据为 `/tmp/orca-meshlib-linux-leXC1x/full-build/zlib-probe-{default,static}.log`。仅需在 ON 构建选项补齐，不能宣称全应用打包已经成功。ON 包的普通进程启动也加载 MR 动态库，最终包还必须验证普通切片／分盘与运行库共存，不能只测重建。
3. 继承的发布阻断仍在：当前分支的 `OrcaSlicer.cpp:7196` 在 STL/OBJ 固定位置、全盘切片中把未登记盘误判为空盘。本轮用独立目录重新跑默认关闭报告的 STL，仍退出 0、跳过盘、只写 result.json，无 G-code。前述盘内变换 JSON 错位已知问题也未合入修复；另一个 master/release 工作目录的通过不能替代本分支。需要先按最终目标提交整合已验证修正，再复跑完整切片／分盘回归。

**本轮实际验证：**所有产物使用 `/tmp/orca-20261019-audit-4HEcRh/`，没有覆盖根 result 或旧证据。

| 验证 | 本轮结果 |
| --- | --- |
| 当前 fff_print_tests `[SliceAnalysis],[LayerCount],[PrintApplyCache],[GCode]` | 242 用例、54,665 断言通过，见 native-tests.log |
| `scripts/test_cli_model_stage_report.py --meshlib enabled` | 34/34 通过，输入完整性检查通过；小模型，不覆盖上述百万面边界 |
| `scripts/test_cli_slice_analysis.py` | 10/10 通过，含默认／显式关闭、开启、输出保护与指令一致性；自动摆盘场景 |
| `scripts.tests.test_cli_slice_features`、`test_meshlib_platform_config`、`test_package_meshlib_linux` | 共 220 项，219 通过、1 跳过（Mac 不运行 Linux 原生 ELF 项）；不是 Linux 全应用验收 |
| STL 固定位置 `--slice 0 --auto-plate 0 --arrange 0` | 重现旧缺陷，exit 0 无 G-code，见 fixed-position/run.log 和 output/result.json，不计入通过 |

**跨仓范围：**只读抽查 Blender `4d550a7`、PAM `b64fc3c`、主服务 `d1476c15a`、Compare `94fcb58` 对应特征提交中的分析、线宽匹配及参数传递；Web 只核对 `87c891f` 的分支／提交范围，未重新逐项审核前端。这些目录仍在特征分支。PDS 实际 checkout 已为 `feature/f_分盘优化_20261019 @ bf2c184`，本轮用 `git show codex/model-feature-analysis-20261018` 审阅 `b1e8c40` 及其修复基础，不切换用户工作区。部署必须先对齐这些功能提交，不能把当前 PDS checkout 当作上述功能版本。没有重跑服务侧测试或启动服务／数据库／OBS调用。

PDS 入队前原件兜底、上传成功后 checkpoint、仅 processing 可更新、异常重读已持久化结果、子进程退出后释放执行资源，以及 CHECK 与普通切片不进入分阶段修复均已按源码复核，未确认新增阻断。旧修复任务恢复只安装截止监控、不续跑，仍遵循文档发布前排空旧任务的边界。主服务和 Compare 的 Arachne 推荐仍限制在匹配的模型／线宽上下文并保留显式用户选择，没有扩大为任意自动改参或自动择优。

特征／G-code 侧未发现新的确定缺陷：新增统计仍由显式报告开启；普通 GUI/CLI 保留原指令、估时与耗材路径，Cura 首／第二模型层间隙未改；三维指标是计划几何而非真实打印质量保证。复杂报告已有的约 388 秒、3.2 GB 峰值、228 MiB 文件成本仍成立，未新增低配 Linux 性能结论。本轮不包含完整 Linux AppImage、Windows、GUI 手工操作及真实后端联调；沿用集中 GUI 交接清单，不能以本机自动化替代。全量模块文档检查为 35 模块／问题 0，`git diff --check` 通过；复核后源码／应用／fff 测试哈希及根 result 哈希保持不变。

### 2026-10-19 master 切片／分盘扩展审查（完成，只读审核及隔离验证）

用户追问 master 除既有空盘/JSON问题外是否还有其他问题。本轮仍在功能分支记录，不切换或修改 master 工作目录。
master 已提交基准为 `ba979559b0`；实际工作目录 `../orca-slicer-master` 已有未提交的固定位置盘登记、切片全空失败、单/多盘JSON区分及禁用实例过滤补丁，另含Linux工作流和版本修改，均属于其他工作，不修改或提交。
审查分别覆盖已提交源码和这些待提交补丁，重点补查多盘导出、修复后共享缓存及CLI边界；不得重复把已被补丁改正的问题称为当前工作区缺陷。
主干没有最近两笔阶段报告/切片分析提交；现有功能分支程序不能作为 master 实测证据。主干文档记录的40项切片/36次JSON仅作既有证据，尚未复跑或确认本轮新增边界。
CodeGraph MCP未暴露，CLI可用；master工作目录无独立索引，旧Understand图与分支不符，按本分支模块和源码审阅，不初始化/复制索引或覆盖图谱。

独立复跑期间检测到 master 的外部并行工作扩展测试并更新实现/程序，需按每轮身份分开：最初 `4531efe8...` 对应CLI源码 `ac6382fa...`；切片脚本已扩到43项，41过、2个 `--assemble` 只导出工程用例失败（3MF盘成员引用不存在的object_id=0），记录 `planning/master-audit-extra-20261019/independent-slicing/`。这是旧补丁缺口，不把它当最新外部补丁仍未修复。新源码已扩大重建盘归属范围到原始输入/assemble，不再只切片时刷新；多盘JSON还补查实际构建体积。新程序已变为 `3c2db788...`，分盘复跑44次通过，见同目录 `independent-arrangement/`。
准备再次核对新源码/程序身份并单独复跑切片，不用前一轮起始SHA覆盖并行变化；本轮没有修改master实现、测试或构建。两个额外疑点已撤销：copy/split参数不可达（解析拒绝）；无切片直接导出min-save的正常/越界对照都因没有加载有效G-code失败，不证明静默漏盘。

最终复跑完成：`independent-slicing-final/validation.json` 为44/44通过（含合并原始输入/已有3MF、重开再切片、实际G-code与工程盘引用检查），`independent-arrangement/validation.json` 为44次CLI通过。两者程序SHA均为 `3c2db788536353c6789613a6055ef8069ea71d498f5550ba7ef5973f958a5c5e`；复跑前后源码 `OrcaSlicer.cpp` SHA保持 `5c5a181cdd9dd6f79bb785ad954b0adc5f201f3125462c8aa5a09ae2be4ff28e`，程序SHA亦保持；切片脚本SHA `2689fbea96ae4a4d51ae9213699e564629a1864c625a7d6c58b38855298e3045`。先前41/43红灯保留，不混计或覆盖。

master HEAD除已知空盘/JSON外的已确认问题：原始混合输入完全越界/摆放溢出会静默漏切；禁用实例的部分越界和面数统计仍阻塞正常打印；最终选盘索引缺少准确检查；已有工程 `--assemble` 后只导出没有更新盘引用。后者从旧对象索引进入 `bbs_3mf.cpp:7976–7992`，无效对象只告警却继续解引用find结果，本次实际写出object_id=0，不能断言所有读取器必定打不开。原始网格assemble场景由本次初始成员登记暴露，已有3MF场景属于原有缺陷，不能混同来源。当前master未提交补丁已针对这些业务路径修正；本轮未再确认新阻断问题，不等于整个master无缺陷。所有结论不包括推送/发布状态，master提交仍为ba979559b0。

独立源码审阅覆盖修复/重建采用的新volumeID、凸包与二维投影失效、PrintApply变换缓存、GUI切片/盘刷新、TreeSupport告警筛选，以及GCode层数统计、3MF单/全盘选择、ZIP失败回滚、当前盘发送解析；未发现额外可确认的正常业务回归。共享导出快照不能区分全部禁用与全部越界的防御层测试缺口已留意，但现有GUI门控及CLI缓存加载阻断所尝试的触发路径，不作为已复现线上故障。
GUI重建/修复后首切、二切、撤销、保存重开仍需按已有清单手测；Windows/Linux原生及线上均未验证。参考master工作区的固定位置切片交接记录，禁止用本机通过替代其发布包验证。本轮仅更新此审查记录及忽略目录内的诊断产物，没有修改master代码、提交或部署。

### 2026-10-19 普通切片与分盘兼容性复核（完成，发现发布阻断，未修复）

本轮仅分析、运行隔离本地验证，不修改实现、不提交。基准为 `codex/model-feature-analysis-20261018` / `40d8be288d`，分别审核模型阶段报告 `f653f0a47f` 和切片指标 `40d8be288d` 的增量，同时检查继承的空盘判断；工作区已有文档、下载工具和根result改动不纳入。用户提供的事故场景为 STL/OBJ 配合 `--slice 0 --auto-plate 0 --arrange 0`，需同时核对退出码、G-code实际产物及盘成员注册，不能以自动摆盘场景代替。

结论：当前分支不能按原后端固定位置命令直接发布。使用与下文冻结SHA一致的Mac arm64 Release应用，已实测复现两项继承问题；不是 `f653f0a47f` / `40d8be288d` 新增的普通入口回归，但当前版本仍会携带，不能以新增功能默认关闭为由忽略。

| 检查 | 本轮实际结果 | 来源与影响 |
| --- | --- | --- |
| STL、OBJ，`--slice 0 --auto-plate 0 --arrange 0` | 两者进程退出0、result.return_code=0，但无G-code且无sliced_plates | `OrcaSlicer.cpp:7196` 提前按成员列表empty跳过；`b05085f7c` 引入的旧问题仍在 |
| 同一STL，报告显式0／1 | 都退出0且无G-code；开报告也无analysis.json | 新预检`:7124`复用相同判断；末尾`:8632`成功出口不核对实际产物 |
| STL、OBJ，`--arrange 1 --slice 0` | 均生成1份G-code | 该路径重建了盘成员，仅证明本组小模型正常 |
| 同一STL，不摆盘但显式`--slice 1`，报告关／开 | 两者正常生成G-code；开启时也有报告 | 显式盘号绕过空盘跳过条件，仅作诊断对照，不是多盘通用修复 |
| 20mm盘内STL指定底面中心(30,40)，不摆盘，导出变换JSON | 日志确认位置已应用，JSON却返回plate_index=1、position=[90,90,0] | `:6595`依赖尚未建立的成员归属，`:6653–6675`误走无归属虚拟盘；返回投影来自`4e50224aca`，未实际移动模型 |
| 同一模型摆盘，以及盘外/超尺寸JSON对照 | 摆盘返回真实盘0；真正盘外与超尺寸返回虚拟盘1 | 后两者符合既有虚拟返回盘约定，不把虚拟盘当真实切片盘 |

共完成12条有效业务对照（8条切片、4条JSON），不是“12项全部通过”。首轮4条JSON测试命令错误导致参数拒绝-2，原失败保留，修正测试命令后在新目录复跑，未将参数拒绝当产品缺陷。证据位于 `planning/bmr-commercial-design-20261018/verification/slicing-plate-audit-20261019/summary.json` 和 `slicing-plate-audit-20261019-json/summary.json`，每个case保存完整command.json、run.log、独立data/cwd/output。盘内JSON日志第17行确认(30,40,0)，第86行返回(90,90,0)；固定切片日志明确`Skip non-printing plate 1`后仍完成成功。根result SHA保持`aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。

两笔增量的独立源码审阅：阶段报告只由显式check/reconstruct入口执行，未改普通导入、修复核心、模型变换或盘归属；报告关闭时保留旧检查路径。切片指标关闭时跳过新增采集和几何分析，开启时只读Print/模型/路径；Arrange、ModelArrange、PartPlate、ArrangeJob、墙/填充/支撑生成没有这两笔变更。GUI普通导出沿默认false，GUI高级修复/重建入口保持，不自动采用候选。未发现这两笔新增的默认打印指令或排布算法变化，不等于整包无风险。

显式开启分析的任务边界仍存在：G-code导出后才算报告，报告异常或硬链接发布失败会返回`CLI_RESULT_WRITE_ERROR`并停止后续盘/最终3MF导出；已生成G-code保留。分析耗时计入原每盘超时（`:7763`），复杂模型可能因此超时；不把“只读”解释成不影响耗时和任务结果。模型阶段报告也会增加读取/比较和报告失败出口，仅影响显式请求。

本次定位出的回归缺口：`scripts/test_cli_slice_analysis.py:630` 和 `scripts/test_cli_slice_features.py:880` 统一使用`--arrange 1`，此前40场景/10保护不能证明关闭摆盘正常。修复应只补盘归属初始化（保留用户坐标、不强制重新摆盘）、共享的目标盘判定与无实际产物的失败检查，并覆盖STL/OBJ固定位置、报告开/关、盘内JSON、正常多盘/空盘/禁用盘/越界模型；不能简单删除所有空盘保护或把后端统一改成切第1盘。

本轮未改生产代码、未提交、未重新编译；已有程序身份一致故直接复用。GUI手测、Windows/Linux与线上环境未执行。修复后的GUI交接需在匹配新构建中验证：导入两件模型→自动分盘→逐盘切片/导出；再加入空盘、关闭其中一件打印→切所有盘；核对盘数、位置、实际文件和提示，保存重开后重做。CLI应同时断言退出码与产物，不用GUI结果替代后端固定位置命令。

工具边界：按understand-diff检查增量及调用关系；Understand图谱为2026-08-28旧快照，未作为当前事实或写覆盖图。CodeGraph MCP未暴露，CLI/索引可用，抽查CLI/PartPlate/ModelCheckWorkflow内容匹配；Processor过期后一次增量sync完成6文件并复核哈希。部分报告符号未检索到，使用当前源码；不宣称图谱完整。

### 2026-10-19 报告统计按需采集（完成，与切片指标功能合并交付）

用户要求未开启报告时不做新增统计。本轮基于 `codex/model-feature-analysis-20261018` / `dbcd9a9e2a`，只调整 Orca：沿现有 CLI 报告开关传到导出器/解析器，默认 GUI、普通 CLI 和独立预览不采集分析元数据；原估时、耗材、预览和指令生成保留，不新增用户参数。实例内固定采集开关，reset 清理统计并保留实例用途，避免复用时串数据。

已接入：`Print::export_gcode` 末尾默认false的内部参数 → `GCode(bool)` → `GCodeProcessor(bool)`；只由 CLI 的 `slice_analysis_requested` 显式开启。未启用时不初始化角色统计 vector/材料上限 map，不记录块分析字段/每层用料，不做新增峰值/超限距离计算；G1空移距离的额外计算也随开关关闭。分析字段结构仍存在，不宣称结构尺寸或条件判断为零成本。

验证完成：Mac arm64 Release 应用及fff_print_tests均已更新。先分别构建应用/测试，再对新增块局部clang-format后重新构建两目标；最终命令 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6` exit0。最终程序重新运行 `[SliceAnalysis],[LayerCount],[PrintApplyCache]`：235项/45687断言通过；另跑`[GCode]`：7项/8978断言通过。默认/off/on及输出保护10项、40真实CLI通过；40份G-code报告统计、有效配置、全部可执行指令、层和用料与修改前基准严格相同。独立只读审核未发现阻断；35模块文档全量检查和diff检查通过。没有新增用户参数、业务分支、后台接口或自动改参。

完整报告严格比较保留一个FAIL：`new-island-arachne-support-off`的两处原二维面积各差1ULP，分别为层20的`solid_fill_uncovered_area_mm2`（1.9402728201800008 vs 1.9402728201800006）及层25的`section_uncovered_area_mm2`（0.33141426921799966 vs 0.3314142692179997）。本轮未改任何报告几何计算；用同一最终binary、同一输入/配置再跑3次，层25仍在这两个尾数间变化，复现已知浮点稳定性限制。原失败保留，不放宽断言、不把整份报告写成40/40严格一致；本次门控涉及的完整`gcode`块和打印指令40/40相同。

证据根目录为 `planning/bmr-commercial-design-20261018/verification/`：`build-analysis-optin-{app,test,final}-20261019.log`、`unit-analysis-optin-final-20261019.log`、`gcode-analysis-optin-final-20261019.log`、`analysis-optin-final-{cli,features}.log`及同名结果目录；严格全报告差异为`analysis-optin-cross-build-20261019.log`，相关统计严格等价为`analysis-optin-metrics-equivalence-20261019.log`，同binary复跑为`analysis-optin-repeat-20261019.log`及目录。初轮通过不重复计数；日志/planning不入库。

冻结身份：Processor.cpp `0a1bfb3048ffb0bd2351d2f22d935ec5927388e3be433c307951536ada060c30`、Processor.hpp `c86cc766cd85ffc13d01bbda81ca5df6bbef37a11bef6bcce47a801fb43d59cd`、测试 `72f51632af1dce3f8411238d16c2ed40654af9a15bfa32bc9e6a7f73637f30e0`；应用 `87a61cd565e1e4e35db2f4c39db7478d7616b2d39caa289588c41fca9cca4e60`、测试程序 `565e5bd3d0be23b66f3841a893af4830e921fd0ffe5822b61ae674162b0cdd36`。根result仍为`aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`，初始无关改动保留。旧打包副本不代表当前程序。

交付整合范围：按用户要求将本批代码、测试及对应文档并入原 `dbcd9a9e2a` 的切片指标功能提交，父提交仍为 `f653f0a47f`，此前模型修复及构建修复保持独立。仅本地整合，不推送、不部署；无关文档、Thingi下载脚本、根result和构建/日志产物不纳入。重新核对源码与已通过测试的程序身份一致，复用上述运行证据，提交前执行全量及暂存文档检查和暂存差异检查。

GUI兼容仍待用户操作，Windows/Linux未执行本轮原生验证，也未新增性能数字。当前应用为仓库内 `build/arm64/src/Release/OrcaSlicer.app`：

| 操作 | 预期与回传 | 状态 |
| --- | --- | --- |
| 用当前应用打开 `planning/bmr-commercial-design-20261018/verification/analysis-optin-final-cli/default-off/output/plate_1.gcode`，切换层/速度/耗材预览；再打开同目录`enabled/output/plate_1.gcode` | 两份均90层，预览路径、预计时间及耗材一致；不要求界面有报告指标。回传截图/日志 | 待GUI验证；CLI两份指令/配置相同 |
| 按[集中测试交接](../../../doc/bmr-print-optimization.md#71-原生-gui-与-cli)的普通切片步骤，在当前应用导入raised-stroke.stl，切片、导出、保存重开再切片 | 普通GUI不输出analysis.json，预览/估时/耗材正常，无新增开关；提供G-code和有效配置核对 | 待GUI验证；默认解析器及重复reset已自动化覆盖 |

代码图：CodeGraph MCP 未暴露，CLI `/Users/liwei/.local/bin/codegraph` 可用；抽查Print/GCode.hpp/CLI哈希一致，Processor哈希不一致，执行一次增量sync完成18项。Understand图为2026-08-28/60148c11，未覆盖新统计，直接读当前源代码与相关模块，不新建图谱。

### 此前提交基准

2026-10-19 提交基准已冻结：按用户要求整合模型特征／切片报告改动，每仓一笔本地提交供审核；保留此前模型修复和共享墙特征基础提交，不改写历史，不推送、不部署、不迁移数据库。Orca本轮未改几何算法或精度，使用下方已经核对的当前源码／二进制。下文旧批次中的“未提交”“180秒阻塞”等描述保留为历史，不代表本节当前状态。

### 本次交付与超时策略

- PDS只对`source=compare`的Orca报告任务使用`print3d.common.slice-analysis-process-timeout-seconds`，默认1800秒，可按测试机配置；普通APP／无报告任务继续使用`print3d.common.slice-process-timeout-seconds`，默认180秒。两者分别计整个Orca进程，不含排队、下载和上传，不修改Cura／模型修复的独立超时。
- 超时仍终止进程树、返回124并按原任务流程失败，不返回成功的半成品。Compare单次HTTP请求60秒、实验等待1小时及后续UNKNOWN对账均未改，不增加无限等待或自动重试。
- 完整报告上传文件存储，PDS任务只保存`slice_analysis_url`，查询与Compare透传`sliceAnalysisUrl`，Web按需读取；不内嵌完整JSON，不截断明细。总方案、PDS README、Compare及Web说明已同步。
- 1800秒只是可调整的功能测试预算，不是性能验收。复杂模型本机完整耗时388.466857667秒、峰RSS3209363456B、报告238892219B；低配置Linux须先单任务实测耗时／内存／上传与读取，延长超时不能解决内存不足。

### 分仓审核入口

各仓分支均为`codex/model-feature-analysis-20261018`，直接查看各仓最新提交相对父提交的差异即可；七仓不能组成一个Git提交。

| 仓库 | 本次提交 | 范围 |
| --- | --- | --- |
| Orca | 本记录所在的功能提交；父提交`f653f0a47f` | 默认关闭的CLI只读报告、精确名义几何与G-code观察、测试和交接文档 |
| Print3dBlenderService | `4d550a7` | 在既有模板分析复用共享墙特征与抽样厚度 |
| Print3dParamAutoMatchService | `b64fc3c` | 按明确墙证据返回有限工艺建议 |
| Print3DService | `d1476c15a` | 线宽／变换条件下采用建议，保护模板与用户参数 |
| Print3DSliceService | `b1e8c40` | 线宽解释、完整报告文件／URL、Compare独立超时 |
| Print3DCompareService | `94fcb58` | 候选建议与报告地址透传，保留原等待流程 |
| Print3DCompareWeb | `87c891f` | 按需读取、展示和A/B对比，保留全部明细 |

### 最终检查与测试边界

本轮重跑：Orca `[SliceAnalysis]` 231项／25126断言，加既有`[LayerCount],[PrintApplyCache]` 2项／2487断言，共233项／27613断言通过；Python170项、PDS105项、Compare47项、Web384项和生产构建通过。对应日志为`verification/{unit,regression,python}-consolidation-20261019.log`，以及`/tmp/model-feature-pds-analysis-timeout-tests-final-20261019.log`、`/tmp/compare-consolidation-final-20261019.log`、`/tmp/compare-web-precommit-{tests,build}-20261019.log`。原生源码和程序身份仍与下方冻结值一致，无需重复构建。

身份核对后复用：40真实CLI／10保护、40新块严格等价及20A/B／CSV、复杂报告完整合同；Main126、PAM79、Blender20Java／29模板Python／31+9+5旧质量和21组旧输出严格等价的既有证据。本轮未重跑这些未改代码，不把历史用例重复累计。独立审核未发现新提交阻断；模块正文／review、35模块全量与暂存检查及各仓`diff --check`纳入提交前检查。无关文档、Thingi下载脚本、根`result.json`、日志／planning／构建产物不入此次提交，原工作区内容保留。

此提交时的下一步是测试环境联调，不再扩功能：先核对并执行一次PDS加列SQL，配套发布接口依赖、Orca与服务／Web，检查文件存储跨域和Blender依赖。未连接业务数据库／云存储／线上服务或打印设备；Linux／Windows原生、普通GUI兼容与真实通知／打印仍待验。当时GUI共用的新增统计尚有计算/内存开销；该点已在顶部“报告统计按需采集”批次修正，当前状态以上方为准。具体步骤见[集中测试交接](../../../doc/bmr-print-optimization.md#7-集中测试交接)。未新增自动择优或质量阈值，16类指标的通用算法缺口仍以总方案范围表为准。

## 提交整理前的冻结结果（历史）

2026-10-19 本轮精确候选过滤、完整报告文件承载、独立审核与本机验证完成；整体业务验收仍受下方性能和环境边界限制。Orca分支 `codex/model-feature-analysis-20261018` / HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`；PDS `b4a0380`、Compare `d934917`、Web `02e30f1` 同功能分支。保留原有WIP；不提交/推送、不连接业务服务/存储/数据库、不打印。根result SHA保持`aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。

| 本轮范围 | 结果与边界 |
| --- | --- |
| Orca精确计算 | D只排除精确C为空的M；C跨Z只按已经连接的T/M父根排除不可能候选，同根仍精确求交。复用已有Association，不复制关系或增加缓存；不改精度、几何定义、JSON明细、打印路径或配置 |
| 原生与CLI | Mac arm64 Release最小应用/测试构建通过；233CASE/27613断言、170Python、40CLI、10保护通过。新2CASE验证父根条件矩阵、有效覆盖与空/边/点候选混合。40新块完整JSON含顺序严格等于上批，40旧字段/指令及20A/B/CSV本轮也严格相同；上批2项1ULP失败仍保留，不以本次通过覆盖历史 |
| 复杂模型 | 112层完整运行exit0：388.466857667秒，峰RSS3209363456B，238892219字节。报告与上批562秒版逐字节相同：592目标、501352Missing、43跨目标连接；完整合同及G-code全部指令/配置/层和耗材标记通过。并行测试下的本机耗时，不是隔离吞吐承诺 |
| PDS承载 | 已复现旧Fastjson writer容量错误。改用已有Jackson流式校验完整JSON与头部合同，原文上传现有文件存储；DTO/数据库/查询仅保存`sliceAnalysisUrl`，重跑清旧地址。报告缺失/非法/上传失败仍失败，普通APP/Cura/固定G-code不增加报告字段。97项离线mock/临时文件测试通过 |
| 真大报告校验 | 实际约228MiB文件调用新生产validator，在128MiB Java堆下通过，约0.285秒、峰RSS215744512B；后者不等于128MiB进程内存。未增加writer限制、业务超时、版本开关或数据截断 |
| Compare与Web | Compare生产通路仍直接透传JsonNode，只补地址保存测试，47项通过。Web详情内按需裸GET，共享单份报告/解析引用、同URL合并、离页取消、失败显式重试；CSV只加载记录绑定的切片尝试。Missing保留原数组，仅当前20条映射；384项Node及生产构建通过（原有大包提示） |
| 实际消费 | 单大报告本机mock fetch仅读取/解析一次，592目标、501352明细、43关联、25642页逐字段检查通过，峰RSS1045348352B。另起进程用两个URL各自解析同一文件为独立对象，188行A/B与原入口/CSV严格相同，约1.37秒、time墙钟1.45秒、峰RSS1345257472B；不是两份实际不同候选或浏览器/CDN测试。40小报告/24222明细/1238页及20A/B/CSV对照归档旧源码SHA严格一致，无structuredClone |
| 审核与范围 | 独立审核未发现本次代码确定阻断；Orca模块35项0问题、各仓diff检查通过。GUI无报告入口，本轮仅显式CLI与Compare报告消费；普通GUI兼容手测、Windows/Linux原生和实印仍待验。16类指标剩余通用算法仍见范围表，不能称全部功能完成 |

**下一步阻断：复杂计算388秒仍超PDS默认180秒，默认业务链未通过。** 保持准确度和既有预算继续优化计算；文件承载修复不替代该性能验收。部署前执行本轮尚未部署的PDS `slice_analysis_url`加列SQL，配套接口依赖/服务/Web并核对CDN跨域GET，未实际执行迁移或上传。具体GUI/业务手测按[总方案交接](../../../doc/bmr-print-optimization.md#7-集中测试交接)和下方既有清单执行。

冻结身份：主CPP `e0e9da6fa0296dd7dd57725db736d1fddea823c752cd9b82d69b96f247008827`，helper仍`2204213b…`；tests `13046ef2b3fd6e5fde8f1f75f7ab12ba5a2a925fcf90369f5f90c2037c3cf622`；app `ebec8eb4458e587d79501b1c15785537dfbbf055e8f9ce4168554077af768ba3`；test binary `ab4cd07716a6e3c19d255d22e59ad12114ea84937fd2e735df844b61050d640d`。大报告SHA `81c6be405a2d35b53b2888711a1ec77c6061de642e9de73726cd19d059934349`。PDS核心 `9cb3e946…`、Web loader `1479750c…` / metrics `7ac35bdc…` / composable `1a2dd337…`。

证据：本目录对应`planning/bmr-commercial-design-20261018/verification/`下的`build-nominal-parent-filter-{app,test}-20261019.log`、`unit-nominal-parent-filter-20261019.log`、`nominal-parent-filter-{features,cli,cross-build,consumer,exact-equivalence,dragon-contract}.log`、`nominal-parent-filter-dragon-accuracy-20261019.log`、`docs-nominal-parent-filter-20261019.log`、`report-carriage-java-tRfuH1/`与`compare-lazy-report-o724ig/`。PDS/Compare/Web离线日志分别`/tmp/model-feature-pds-report-url-tests-20261019.log`、`/tmp/compare-report-url-20261019.log`、`/tmp/compare-web-lazy-report-{tests,build}-20261019.log`。旧Fastjson失败是同依赖/同文件的本机重现（2GiB JVM尚有885096448B空闲堆），不是伪称线上数据库故障。

收尾文档检查曾发现总方案章节锚点写错，已改成实际“7. 集中测试交接”后复查；失败保留`docs-nominal-parent-filter-final-20261019.log`，修正后结果`docs-nominal-parent-filter-final-links-20261019.log`，不覆盖失败日志。当前没有本轮构建/切片子进程遗留；用户已有GUI保持不动。

## 上批冻结基准（历史，以顶部当前状态为准）

2026-10-19 本批完成D末端精确汇总的最小改动与本机验证。分支 `codex/model-feature-analysis-20261018`，HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，无暂存/提交/推送、无业务服务/数据库/打印机操作。根result SHA仍为`aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。

收尾格式检查发现新增CASE的指针声明对齐未满足组合范围clang-format，已仅修该空格，tests SHA更新为`b15e0c01b4838fafd96153b3f385abc1fbd3065237941a6a08c118531deffe62`；源/应用不变。最小测试目标已重建exit0，生成同SHA测试二进制，重新运行仍231CASE/27325断言通过，`build-nominal-balanced-test-formatted-20261019.log`及`unit-nominal-balanced-formatted-final-20261019.log`；当前范围format检查通过，不以旧二进制替代重建验证。

| 项目 | 当前结论 |
| --- | --- |
| 最小改动 | 仅D最终汇总按最终目标根归组，再二分精确求和；稳定指针引用已有Missing，不复制另一套区域。T/M/C/D几何、目标体积、DSU、每条明细顺序、JSON转换及接口不变，无新参数/质量阈值/自动择优 |
| 正式身份 | 主CPP `fbe32181c6e8bd11d8226246fb653c4115042a72b8118117d915234a9d2c3164`；tests `b15e0c01b4838fafd96153b3f385abc1fbd3065237941a6a08c118531deffe62`；app `0645fe96ab130ea0a25696b3460bc07480c3d6d013eb2407a5f66c435a5c2476`；test binary `27850140cd3c67474a43626d21a09f672f3defc142680cf57a7ca04aa057f9b1`。临时探针已移除、调用顺序不变 |
| 普通场景 | 231原生CASE/27325断言、170Python合同、40真实CLI、10保护通过。40新块与上批串行版完整JSON严格相同；新增跨层后合根、远目标隔离、7项互素分母串行真值回归，未放宽断言 |
| 复杂dragon | 正式112层输入首次完整exit0/complete；562.284761916秒，峰RSS3459776512B，报告238892219B。完整合同通过：592目标、501352条Missing、43跨目标连接；这些是名义集合观察，不是缺陷/打印失败结论。112层G-code全指令、完整配置和用料与旧基准严格相同 |
| 兼容性边界 | 40实际指令/配置/G-code指标均严格不变；旧二维字段跨构建2处各1ULP尾差，使旧几何/消费者39/40、原A/B/CSV19/20严格相同，失败保留。同一正式app额外五次复跑均与旧数值基准一致、新块不变，确认同binary也存在该尾差；具体多边形顺序原因未确诊，不静默放宽测试 |
| 审核与文档 | 三路独审未发现本次改动的确定阻断；取消点/指针存续/最终根归属/数学真值已核对。35模块检查0问题、diff检查通过；私有求和阶段取消仅静态审核，不冒称动态注入通过 |
| 业务接入仍受限 | PDS默认整个进程超时180秒，本例在相近性能且默认值下不能完成；报告去格式空白保留原数值拼写仍79193284字符，大于本机Fastjson2默认writer 67108864字符容量，存在明确序列化风险（未实际运行Java大报告保存，不冒称已复现落库失败）。未提高业务超时/大小上限或删数据，尚非商用就绪 |
| 实际Web数据消费 | 现有sliceMetrics对该大报告592目标/501352明细/43连接逐字段映射、1188个首末分页检查通过。单份JSON+单次映射的本机Node总1.52秒、峰RSS1130004480B；不是浏览器、HTTP或数据库E2E，不能据此认定内存/传输可接受 |
| GUI及后续 | 该报告仅显式CLI调用，原生GUI没有对应入口，不新增控件；普通GUI兼容、Windows/Linux原生、业务整链和实印仍待集中验收。后续先处理复杂报告的计算/数据承载问题，保持精确语义与现有业务预算；不扩成自动择优或擅自添加指标规则 |

当前证据在`planning/bmr-commercial-design-20261018/verification/`：`build-nominal-balanced-{app,test}-20261019.log`、`unit-nominal-balanced-final-20261019.log`、`python-nominal-tail-20261019.log`、`nominal-balanced-{features,cli,consumer,cross-build,exact-equivalence}.log`、`nominal-balanced-dragon-{accuracy,contract,gcode}.log`、`docs-nominal-balanced-final-20261019.log`。大模型通过是完整性/合同和独立小例真值的联合证据，不是对每条真实区域都有外部精确oracle，也不与未生成报告的诊断版做虚假的逐项等价证明。所有失败和人工终止诊断保留下文。

接入边界只读核对：PDS `b4a0380` / Compare `d934917` / Main `26dd94c` / Web `02e30f1`仍在同功能分支，未改产品或连接服务。`Print3DSliceService/.../GeneralConfig.java:32`默认180秒，`OrcaSliceServiceImpl.java:697`对整个进程waitFor超时后终止进程树并返回124，报告阶段也计时，已有G-code不进入成功上传分支。报告由`:439`整份读入；`SliceJobServiceImpl.java:223`使用Fastjson2 `WriteMapNullValue`整份序列化，未开LargeObject。本机已验classpath的2.0.18字节码显示Java17该入口UTF16默认容量67108864，超过扩容上限抛OutOfMemoryError；当前仅有实际紧凑字符计数与实现核对，不替代Java保存/DB包大小测试。Compare整份写LONGTEXT、详情整份解析，1小时任务等待与60秒HTTP读取不能抵消PDS子进程180秒限制。实际Web本机证据`compare-dragon-consumer-I4lVwv/{result.json,time.log,verify.mjs}`；无产品修改、无整份报告structuredClone、无浏览器或业务E2E验证。16类指标的剩余通用算法和不做项仍以[范围表](../../../doc/bmr-print-optimization.md#21-当前实现范围仍是-16-类不合并删减)为准，不将本批完成称为全部功能交付。

## 本批诊断与前批续接记录（历史阶段，以顶部最终状态为准）

**最新正式复杂模型已完成**：`candidate-final-balanced-accuracy` exit0，墙钟562.284761916秒、峰RSS3459776512B、报告238892219B（约227.83MiB），新块complete=true，592目标/501352条Missing；源/app/input/root result保护hash均不变。此前串行汇总900秒无报告的阻塞已缓解，但耗时/体积并不满足商业部署的当然结论，正复核完整合同及现有后端限制，不增加业务超时/文件上限。完整112层G-code指令/配置/用料与旧基准严格相同，`nominal-balanced-dragon-gcode.log`。诊断版501353与本正式501352条记录不直接当成精确前后同数据证明（诊断没有完成报告）；本轮严格前后新块等价证明来自40常规报告与独立真值回归。

当前正在验证D末端平衡求和：完整阶段诊断确认逐条Rational累加热点，已仅改为按最终target根收集既有Missing.volume稳定指针、二分精确求和；不改T/M/C/D几何、T累计、DSU、明细顺序、转换函数或schema，不添加业务阈值。临时计时已全部删除、调用顺序未改；即将重建正式app再跑复杂模型，测试补充由独立代理并行进行。以下旧通过不证明当前新实现已验收。

正式app已最小构建exit0（`build-nominal-balanced-app-20261019.log`），主CPP `fbe32181c6e8bd11d8226246fb653c4115042a72b8118117d915234a9d2c3164`，app `0645fe96ab130ea0a25696b3460bc07480c3d6d013eb2407a5f66c435a5c2476`。`candidate-final-balanced-accuracy`原112层完整输入正在900秒本机准确性边界下验证；40真实CLI/10保护并行，不将墙钟当独立吞吐对比。静态独审确认指针存续/最终根归属/显式Rational返回无表达式悬空，空组单项和递归取消均正确。Python合同170项本轮已过；正式原生全量及本轮新增多分母回归仍待测试冻结后构建。

本轮40CLI/10保护、同份报告消费者和20组A/B/CSV隔离已通过；40新块与上批串行Rational汇总完整JSON（含顺序）严格相同，`nominal-balanced-exact-equivalence.log`。**跨旧构建保护首次失败**：40打印指令/配置/G-code报告一致，但`new-island-arachne-support-off`旧二维面积两值差2.22e-16与−5.55e-17，旧几何/消费者39/40、旧10项A/B及CSV19/20严格相同，不能写全通过；失败保留`nominal-balanced-cross-build.log`，正用同app独立复跑5次定位，不修改容差或覆盖原证据。测试源已冻结b7f14c9c…，另加两目标各7个互素分母、非2幂项数，与独立串行Rational真值严格double==；最小测试构建exit0后全量正在`unit-nominal-balanced-final-20261019.log`运行。

正式原生全量exit0：231CASE/27325断言，test binary `27850140cd3c67474a43626d21a09f672f3defc142680cf57a7ca04aa057f9b1`；tests `b7f14c9cdec6e8aabc9c86ee91119c8703eccc8cb4357cb4532256c55a815a4a`。独立二审确认两新CASE数学及真实公开入口正确。模块正文/review指纹已更新，35模块检查0问题，diff检查通过。

旧字段尾差复核：同app、同输入参数在`nominal-balanced-island-repeat-ElKUee/`五次独立cwd/output/datadir均exit0，五份报告严格相同且旧字段与plate-bed基准一致；相对本轮40场景首份报告仍只差上述两处各1ULP，新精确连通块每次严格相同，实际指令/完整配置也均相同。因此已证实同一二进制不同运行存在这两处尾差，不是只能跨构建复现；尚未证实具体多边形顺序根因，不把推测写事实。原39/40及19/20严格失败仍保留，不用五次重跑覆盖或放宽断言。探针初次自身漏path的KeyError另存`probe-initial-error.txt`，修探针后复用保留run1，不改产品。输入/app/root result保护hash不变。

`profile-batched-tail-1`实测：112层建集92.166878秒，事件完成494.910941秒，501353条Missing、27861条关联、592目标。末端前115000项精确逐项加法累计96.183454秒，转换/JSON仅0.166841秒，已确认汇总热点而非猜测IO。获取证据后在墙钟644.619秒主动TERM已核对的本次进程组13820；wrapper因非0退出断言失败，**这是人工终止诊断，不是900秒超时或通过**，日志/measurement保留，无报告、峰RSS为null，进程组回收确认。未修改原root result或输入模型。

2026-10-19 续接进行中：已核对同一分支/HEAD、上述正式源码及root result SHA，无并发构建。此次先临时细分完整事件扫描、Missing精确累计及JSON转换计时，不改变调用顺序或生产超时；诊断产物不可交付，定位后移除探针并重建。三路并行只读审核helper等价简化、汇总归属及消费者边界，根代理独占构建目录。现有图谱不覆盖当前分析模块，以当前源码/模块文档为准。以下是上批冻结结果，不能证明本轮诊断源码已验收。

诊断构建exit0，app `028b1f4d70f3cdd85f8d5111dd9a94d7df065c57bc33e39283d89ded5fe5c3af`；`profile-batched-tail-1`完整112层、原调用顺序，按event标记T/M连接、T体积、C/D、C跨Z，末段分别计精确累计和转换/JSON，使用原有900秒本机诊断边界。命令日志`verification/{build-nominal-tail-profile,nominal-tail-profile}-20261019.log`；正在运行，尚无最终结论。测试代理只编辑原生测试、不构建。候选简化只有代数上严格等价的汇总/空交过滤，实际热点未定位前不改生产算法。

补充独立真值公开报告CASE：同事件每目标两个1/6有理缺失区域，下一层把两个目标合并，另一个目标保持独立；主T体积12e-12mm³、D=32e-12/3，五条缺失明细；远T=D=1e-12，一条明细。测试源SHA `fd9e316af97169674c6dca5683f63844727b5bd5381a300dda0706baa10bfb91`。根代理另构建测试目标exit0（不写应用），诊断版聚焦1CASE/156断言通过，`build-nominal-tail-test-20261019.log` / `unit-nominal-tail-fixture-20261019.log`；不是无探针最终回归。

本轮已完成阶段定位和保持精确语义的三项简化：批量owner建集、严格内部见证的相交阳性快路、删除无消费者的M/C体积累计。临时计时及计算顺序前移全部撤回，正式代码已重建；正常场景回归通过，但最终复杂dragon在900.005秒仍超时无分析报告，完整性验收失败，不能称整体可测试或只剩实印。未改变业务超时、打印路径、报告合同或GUI入口。

2026-10-19 本轮按“准确度优先，性能后续优化”执行。分支 `codex/model-feature-analysis-20261018`，HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`；无提交/推送，不连接业务服务或打印机，根result保持原SHA。

诊断历史：`profile-unified-stages-1` 在180秒超时，完整112层T/M建集108.959秒（T来源16.758/并集27.960，M来源19.961/并集43.241秒），C跨Z连接约1–2.5秒/层、C/D约1.5秒/层仅是首几层样本。首轮诊断原生在第4项空层fixture触发SIGSEGV，原因是临时探针取空slab的back；修正后228项/26662断言通过。第二次 `profile-unified-witness-1` 将新块前移但不跳层，180.005秒仍在事件扫描，未形成报告；不将诊断顺序或墙钟当最终性能。

最终批量版保留每个surface/path的原NonZero解释，只将中间逐owner的Region测量/重建移至平衡并集后一次执行；增补批量与逐owner精确等价、有理余片再组合、重数、孔/自交、取消及源数据不变回归。最终复杂验证 `candidate-final-batched-accuracy` 使用新的无探针app，本机900秒上限不改服务预算，实际900.004979959秒/ETIMEDOUT、无报告，测量中的峰RSS为null；期间ps采样不能冒称峰RSS。其间并行原生与小样例回归，仅用于完整性验收，不作隔离吞吐对比。测试进程组5558/5559已回收，原文件及程序SHA不变。

| 项目 | 当前状态 |
| --- | --- |
| 本轮结果 | 批量建集/严格内部见证快路及冗余累计清理已完成，原Z事件/DSU/schema和精确面积语义不变；正式最小构建、原生及正常CLI通过，复杂模型最终900秒仍无报告，未通过整体验收 |
| 输入口径 | 每个源region按NonZero外环减NonZero孔解释，再并集；每条原path的宽度多环作为独立NonZero组，各path再并集，避免不同路径负向小叶相互抵消。保留每path范围元数据，不重复offset；不修改源轮廓或新增容差 |
| 当前无诊断源码 | 主CPP `58b67516fb161d549b1eff5c737925490343691afd8034b99424156241ce9947`；helper CPP `2204213b01dd3740ae8b8e098d55b7c904e560256e4816b32d15b24f2b828c55`、HPP `5752c9f74ac22e102286532e7eb6994b04f125b7699aabe4819d22b515072750`；tests `391910f4dd85854a7251c91452d25b6d9ad389d12c2657ea36dfe0705d5c8e22`。无临时诊断，原调用顺序已恢复 |
| 构建与原生 | Mac arm64 Release应用/测试最小构建exit0；229项/26921断言通过（本文件227项+2既有回归）。app `3f2e1e90b5471c16b82574db4851299ae24baa5cb689becd49d1253096d0eb00`；test binary `e72e717014b780b76709b75b209530799b1d3152648bed5adefb332f7a95759a` |
| CLI与消费者 | 当前app重跑40真实切片/10保护全通过，40新块完整/52目标/24222条未覆盖位置；全部新块与优化前40份完整JSON严格相等（含顺序，不规范化/不加容差）。40份旧指令/配置/报告/消费者、20对旧A/B/CSV严格不变，Python合同170项通过。真实样例没有的全无覆盖/跨目标阳性另由原生覆盖 |
| 独立审核 | 三路复核helper、主端/消费者和测试，批量owner独立语义、同步源指针、严格内部见证充分性、未命中精确fallback及M/C累计无消费者均无确定阻断；不以静态审核替代运行 |
| 收尾检查 | 模块正文/review原因与当前指纹已更新，最新真实源fixture增加后35模块检查0问题，git diff --check通过，helper完整及新增测试范围格式检查通过。未暂存/提交/推送。缺层/高度/JSON表达不足为局部unknown，helper一致性异常则沿CLI报告失败路径保留G-code，二者不混称 |
| 剩余验收 | 优化前180/900秒及当前`candidate-final-batched-accuracy`900.005秒均超时，全部失败证据保留。最终已导出的112层G-code全指令/完整配置/用料事实与旧复杂基准严格相同（`nominal-batched-dragon-gcode.log`），但无完整新分析块，不能确认复杂模型准确性；服务180秒预算也未满足 |
| 下一步 | 已知逐层建集及C/D、跨Z精确叠加成本，但最终长运行没有末段定位证据。继续细分事件扫描与最终有理数汇总，必要时用限定层诊断隔离成本，再做精确等价调整；不可直接再次延长预算，不凭静态猜测认定大分母为根因。正常样例可按已验范围做局部功能核对，复杂报告仍为阻塞 |
| 后续边界 | 性能优化按用户要求后续安排；原生GUI无本报告入口，报告功能不适用，旧GUI兼容、业务整链、Windows/Linux原生与实印仍待集中验收，无自动择优/新质量阈值 |

本轮证据位于 `planning/bmr-commercial-design-20261018/verification/`：`build-nominal-batched-final-20261019.log`、`unit-nominal-batched-final-20261019.log`、`nominal-batched-{features,cli,consumer,cross-build,exact-equivalence}.log`、`python-nominal-batched-20261019.log`、`docs-nominal-batched-final-20261019.log`及复杂模型`nominal-batched-dragon-accuracy-20261019.log`，首轮/中间证据保留不累加。真实5/7/9点独立有理真值、微小区域/孔/大坐标和JSON塌缩回归继续通过，不恢复旧整数近似。原步骤范围/未实现通用算法见[总方案范围表](../../../doc/bmr-print-optimization.md#21-当前实现范围仍是-16-类不合并删减)，不将本次名义集合精算称为全部打印几何精确。

源入口核对已完成：`profile-exact-source`只探测前两层，原region目标分别73/1048个，其中2/110个未通过完整PWH校验；缓存的逐路径原始宽度环分别573/1039个，单环检查全部通过（不等于全部层、环间关系或完整M集合都已通过）。独立精确复核捕获的首两条5/7点目标，均有两对非邻边真穿越，无重复点/零边/点落边，原截面问题得到确认；没有把110条拒绝都称作真穿越。探针exit0，56.472秒/RSS1419526144B是带诊断的成本，不作为正式性能。移除探针后app重新链接回相同正式SHA，根result始终不变。

## 本轮精确计算过程（历史时点，以顶部当前状态为准）

首轮统一内核构建时点（历史未验证状态，不代表当前）：主端新三维块移除Clipper归并/交集，改用精确Region；路径缓存只新增每path在原by_height数组的范围，不再次offset。helper仅保留一个NonZero/EPECK内核，删去没有业务消费者的旧严格PWH入口，旧数值测试正迁移至同一API；不保留版本开关/兼容壳。精确外包AABB只筛选候选，不决定几何；最终JSON无法表达正宽度时仍unknown。首轮只构建OrcaSlicer以避开仍在编辑的测试文件，命令`cmake --build build/arm64 --config Release --target OrcaSlicer --parallel 6`，日志`verification/build-nominal-unified-first-20261019.log`。测试冻结后再构建测试目标，不能用旧217项证明当前实现。

`profile-exact-2` 在z=200000..400000捕获target11/48/82的222/152/127点环，均无重复点或零边，恰有一对非邻边严格穿越。平移后的连续4点分别为 `(0,0),(3223,-4659),(1,-1),(0,-1)`、`(0,0),(390,-402),(1,-1),(0,-1)`、`(0,0),(15224,-21288),(1,-1),(0,-1)`。交点是 `(3223/4659,-1)`、`(65/67,-1)`、`(1903/2661,-1)`，不是端点接触，不能继续以拆孔/插入既有顶点处理。第二例代数area2=11而NonZero集合area2=867/67，仅改D解释会与T体积/连通口径冲突。保留unknown；后续优先检验原输入，若要扩大精确范围，须统一该新块T/M/C/D与跨Z相交，不改实际切片。临时源探针只用于本机定位，产品最终移除。

最新定位：`profile-exact-1`捕获163点外环；精确整数/Fraction复核无真穿越，仅P重复形成CW内孔绕行。主环area2=58849824107448，内孔=-23274，原环净58849824084174，不能去重丢孔。helper已去过严is_simple前置、改精确area定向，保留完整PWH校验；新增最小弱简单触孔回归（T=15e-12mm³、切去角后D=13e-12mm³）正在构建。完整validator可能仍要求显式outer+hole，需实测，不能先写通过；如需拆环仅保留精确原顶点/有向面积再重验。helper/main临时捕获已移除，但应用仍是诊断旧产物，待最终重链。

复杂模型跟进：精确D `candidate-final-exact-1`实际54.667秒、峰RSS1423966208B、CLI exit0，但新块unknown、完整性断言失败（`nominal-exact-dragon-20261019.log`）。因此下列215项通过不等于本批交付通过。正在添加仅本机临时拒绝原因/非法多边形捕获并重建诊断程序；最终须删除诊断再构建、回归。根result未覆盖，未更改默认180秒预算。

最新验证：精确D二次 Mac arm64 Release 构建成功（`build-nominal-exact-final-20261019.log`）；215项/23800断言全部通过（`unit-nominal-exact-final-20261019.log`），包括两项首轮失败与JSON范围塌缩回归。CPP `068ecdc249021f9a7c53edc21607c60855e9e61351d8f06d2148f2c6766b1c1a`，helper CPP `b0193ab39ace345da18a3730e0d687eae1d7ab7b22c7178c3654b4568bbba608`，helper HPP `369812ba28064f6d858761e596da37af152db1a72481e942dacf8f66bb9be5a2`，tests `a5ec4c3c1e3497e8ab3258d15e29552f4c42d152e3c4b0abcc67582ec56e6fff`。app `c8f5433a85119ffa731ecd7003a57f7bf259e848254a5eebbe6060a70e3028a6`，test binary `9f7fa35a0314d03fbdf0d8ccdc8b4cf08d0cfddf1c217501f1840b6121ba6735`。独立静态审核无剩余确定阻断；复杂dragon正在独立180秒边界验证，之后是40CLI/10保护/消费者。此时尚未宣称复杂模型通过。

2026-10-19 用户明确选择“准确度优先，后续再优化”。本轮进入精确差集实施：未覆盖D对所有有材料候选的区域统一使用现有CGAL/EPECK精确布尔与有理数测量，而非仅越界时fallback；否则未越界的舍入偏差仍会漏检。无材料候选可直接测原T。窄职责helper编入既有libslic3r_cgal，输出面积及范围直到JSON前保留精确数，不回整点、不截断D到T、不新增外部依赖/业务阈值。旧切片及T/M/C名义连通暂不改，不把D精算宣称整个打印几何已精确；输入合法性、取消、确定性真值及复杂模型成本均需重新验证。下面旧完整性红例仍是修复前证据，尚未完成最终验收。

当前实施：`SliceAnalysisGeometry` 已接入现有 CGAL 静态目标；D 的区域面积、XY 范围与跨 Z 体积累加使用有理数，JSON 出口才转浮点。helper 验证完整带孔多边形，不只验证单环方向；取消异常继续传播。主端与测试正在统一构建，日志 `verification/build-nominal-exact-app-20261019.log`。下列旧 hash/结果均为修复前证据，不证明当前实现通过；下一步先跑精确真值与全部原生回归，再验证复杂模型与旧路径/消费者隔离。

精确实现首轮已完成 Mac 应用/测试构建。Connectivity 33项中31通过、2失败（`unit-nominal-exact-first-20261019.log`）：真实4/22有理真值与跨Z累加已通过；剩余为孔顶点落外边内部的表示，以及CGAL PWH扫描把仅点触的四角区域组合。保留失败期望，现分别用精确插入既有接触顶点后重验、读取arrangement内部面测量修正；不移动点、不放宽合法性。另加JSON double范围塌缩守卫，无法表示正宽度时新块未知，不扩范围/删区域。Python合同170项再次通过。最终二次构建与回归尚待执行。

- 范围：在原Blender/PAM/PDS/Compare链上完善特征测量、Orca只读切后观察、明细定位和条件一致的A/B比较；不新增自动择优、机料合格阈值或客户端调用。当前16类有不同深度的证据，具体未实现算法与本轮不做项见[范围表](../../../doc/bmr-print-optimization.md#21-当前实现范围仍是-16-类不合并删减)。
- 基准：`codex/model-feature-analysis-20261018`，Orca HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`。保留既有WIP，未提交/推送；根result SHA256仍为 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。
- 本批源码：SliceAnalysis.cpp `4430850ee436e275c83b1cc01e423e1966a67f31480bb8149ccca36b3a3e419e`；测试 `4d252d24442b307a3e4f180674ecfe9c41cf38de8cd8fb8d9466155186c3ae78`。采用普通Paths → Strict Paths → 普通NonZero树，修复严格树错误孔归属；精确整数面积/体积保留，诊断已删除。独立代码审核未发现确定阻断，运行验收未完成前不称可交付。
- 当前验证：Mac arm64 Release应用/测试最小构建exit0，202项/23318断言通过（`unit-nominal-reunion-final-20261019.log`），Python170项通过。但独立dragon `candidate-final-2`耗时62.704秒/RSS1573322752B，新块仍unknown，完整性断言失败；保留失败，不宣称当前可交付。profile7定位target75未覆盖体积超目标；profile8捕获308个局部差集超额。最新真实4点目标/22点材料回归已证明**原始diff_ex就超额**：area2目标41281，raw/union/strict/rebuilt均66754，不能归因额外Strict或用删除一次规范化冒充修复。测试日志 `unit-nominal-difference-repro-20261019.log`（预期完整而实际unknown的红例保留）；bbox相交但几何不交的保护测试190断言通过。产品临时诊断已删除，源码回到4430850e；当前应用仍是profile8诊断构建，不能交付，待方案定案后重建。已向用户询问“有效结果保留、受影响测量明确未知”与“新增精确几何计算”的取舍，优先遵循最简实现，不引入容差或截断值。
- 边界：未连接业务数据库/打印机，未部署；普通Orca GUI、业务整链、Windows/Linux原生和实印仍需集中验收。已有Web CUA是本机夹具验证，不代替后端E2E。

## 收尾与前序批次记录（历史状态，以顶部当前基准为准）

本次修正已冻结CPP `4430850ee436e275c83b1cc01e423e1966a67f31480bb8149ccca36b3a3e419e`：普通Paths union → Strict Paths → 普通NonZero union重新建树，保留精确面积与未知边界，临时诊断已移除。独立小例对照保留角点/顶点碰边分离、共边连接及孔/微小岛；这些是方案诊断，不替代最终程序测试。正在最小构建OrcaSlicer，日志 `verification/build-nominal-reunion-app-20261019.log`；测试源码正在移除诊断并固定真实9点回归，之后单独更新测试目标。Python合同170项通过，日志 `verification/python-nominal-final-20261019.log`。

当前续接（2026-10-19）：复杂模型耗时已降到约89秒，但新连通块仍为unknown，尚未交付通过。profile5/6定位为StrictlySimple产生错误包含关系：小外环area2=29808，所谓孔area2=-1345035087，孔的顶点实际穿出外环；不能只依方向或首个非边界点改归属。已提取9点真实轮廓，公开报告回归确实失败，证据 `verification/unit-nominal-weak-contour-repro-20261019.log`。当前仅比较最小轮廓规范化方案，不改共享Clipper、不删负面积区域、不添加面积容差；测试完成后移除临时诊断并重建应用。后续依次完成全量原生/真实CLI/消费者复核、模块文档与集中测试清单，不扩加自动择优规则。

最新阻断：三维新块当前已通过196cases/21512assertions、40CLI及10简单保护，但复杂dragon运行180.003秒超时，无分析JSON；旧同输入基线48.607秒。失败证据 `verification/nominal-connectivity-memory/candidate/measurement.json`，不将功能通过写成可测试交付。已停止超时自建进程；macOS sample没有获得有效栈，结束自建采样进程，不据此推断热点。下一步临时分段计时定位并修正新块性能；暂停新增负空间孔段算法。保留全部失败证据，最终须移除临时计时并重新构建/回归。

性能定位续接：profile1卡在第2层T的1051个region碎片/17485顶点严格PolyTree归并（109秒主动终止）。采用现有ClipperUtils fractal处理思路，先普通Paths union消除重叠，再strict tree，profile2/3均约89秒结束；31k次规范化合计约29秒。未改180秒默认后端预算：PDS整进程到时会杀进程并失败，Compare异步轮询1小时不能绕过该限制；线上覆盖值未读取。当前还有数值/方向阻断：新块仍unknown，profile3显示微小负面积；profile4用cpp_int精确面积确认部分确为StrictlySimple拆环后外环CW（如area2=-603），不是可用epsilon忽略的误差。正在独立审核按PolyTree角色规范环方向。暂不声称性能和新块通过；最后须无调试输出重建、复杂报告完整及全回归。两新增重叠/斜边测试17connectivitycases已通过，第三大坐标微区测试尚待构建。

当前三维增量实施中（新代码尚未验证，不能拿下方181项证明通过）：两方设计审核通过，按每个PrintObject单实例构造T=补偿后region截面×名义层厚、M=模型路径足迹×各path实际高度、C=T∩M。统一1e-6 Z事件扫描，T/M/C分别记录空间组件；C的数量区分整目标无覆盖与目标内多块，未裁剪M关联多个T可观察跨目标连接。D=T\M按分层正面积区域记录位置范围/实际体积并归属T，不把正常稀疏内部空白称缺陷。M必须保留目标外与目标高度外的材料，不能为性能裁掉后漏掉间隙桥/上层连接；单实例不跨对象/支撑。T不能复用象脚后被恢复的lslices，新取region->slices无safety union，旧二维口径不动。相邻高度正面积XY交集为连接，点/边接触与实际量化间隙不连；同高度共边的正面积竖直界面可连。仅新分析严格规范化弱轮廓，不能改变旧共享布尔默认行为；需实测复杂模型时间/内存/报告大小，不能以AABB筛选宣称没有额外成本。Native、Python合同及Web消费者并行实施，根代理负责页面和统一构建。没有提交/推送。

冻结合同为 `groups[].nominal_target_path_connectivity`，包含scope/coordinate_frame/z_resolution_mm/complete及target_components、cross_target_joins。每目标记录本次局部index、XYZ范围、实际目标/未覆盖体积、目标内部路径连通块数，以及逐事件高度未覆盖区域的位置范围/实际体积；跨目标连接由未裁剪M关联多个T记录。false时两数组null，合法空为[]。C多块可能经目标外M绕行连接，不能称整个模型断裂；T随补偿与层高变化，不用component index跨候选配对，不自动评分。

三维消费者阶段：Python170/170、Web374/374通过，严格1e-6/no-retained-layer未知/join只能引用C>0目标已交叉对齐，未加浮点容差。正式AutoReport完成14状态CUA：正常分块、全无路径、全覆盖、跨目标、空目标、unknown、非法index/join/bounds、三类45条分页、FAILED和历史；目标/未覆盖/连接均20/20/5且编号位置正确，截图 `verification/compare-nominal-connectivity-20261019.png`。临时页和标签/服务已清理。仅本机夹具，不是native数值验证或后端E2E。首次页面夹具误用runtime-only Vue，改夹具compiler import后加载；生产首次构建实际完成但shell退出码捕获误用zsh只读status，保留原log，改正常命令复跑exit0，不改产品迁就测试。

三维性能对照基线（旧bed源码的程序，未含新块）：复杂dragon独立测48.607s、峰RSS1424359424B、report1282485B，`verification/nominal-connectivity-memory/baseline/measurement.json`，app仍135fe707…。此前before目录为不同环境的探针，不并入正式前后对照。新程序尚待构建，不能声称增量性能达标。

三维native已冻结并开始最小构建：CPP `fd934313a6fb7eb6bea6f7e7fe34623ebf2923c78ef7847c66adec21c31bd12f`、tests `8971cfa46d32f9698c633d5d66d7db789129b535717658fbd4c6e32f328d8a8c`，新增15组CASE，尚未运行。命令 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，日志 `verification/build-nominal-connectivity-20261019.log`；架构/配置/仓库与前批相同，无并发写构建。Web本批最终AutoReport `fe126f1fea81028f9e1df60a1c87a89749968580d76d61eddbbe2e3724191f93`、helper `47dce695b4c5449b3a61f0e7a97a9ed8fe4824e6042f98374b667eab9f991c3e`，production build exit0 `compare-nominal-connectivity-build-frozen-20261019.log`。

三维首轮与取消修正版均构建/运行通过：196cases/21512assertions、40CLI和10简单保护。独审发现connect内层候选列表缺逐项取消检查，最小增加1行，不改几何；当前CPP `3159bb5a0ff486927af82f145d023926a83ded9dea648c943c61bd21b10b05e9`，tests保持8971cf…，app `ab90c0ecd9b9cd22fce0b3363c1d3a08166274db681d6deaeb168c69772e702c`，testbinary `9b5f2977664e08b1b4a9c8312502cf1fa70f5a18a9fc9097d63efcb9fae4e2df`。最终日志 `build-nominal-connectivity-cancel-20261019.log`、`unit-nominal-connectivity-final-20261019.log`、`nominal-connectivity-final-{features,cli}.log`。新取消点为静态复核，已有执行中路径取消/预先取消回归仍通过，不声称单独测到了此内层的中途取消。

三维首轮真实40报告均完整，52目标/24041分层未覆盖记录，1个目标内多块、无整目标全空或跨目标连接；后两类由原生真值和页面夹具覆盖，不当真实样例阳性。原值映射与分页重组、40旧消费者/20对AB及两CSV隔离通过；对前批bed的跨构建40打印指令/配置/旧报告/消费者均严格相同（无容差）。消费探针首轮错误比较了包含新增first/second快照的整个AB对象，修为仅剥对应新增数组后比较，产品未改；失败日志保留。取消修正版对应报告将再次核对。复杂模型独立性能测量当前运行中。

2026-10-19 当前续接：同一功能分支上继续收尾，未提交/推送、未连接业务数据库或打印机。Orca HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，Web HEAD `02e30f17db0da0c8b7b59a94a352f4a99440332d`，工作分支均为 `codex/model-feature-analysis-20261018`。用户根 `result.json` 保持 SHA256 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。历史批次数量不累加；下次源码改变后必须更新构建身份。

当前执行：第15类加减速阶段超cap路径距离、第8类贴床连通区域面积/XY范围，以及Compare独立全局指标对比/CSV已实现。Mac arm64 Release最小构建通过，181 cases / 10263 assertions、40真实CLI及简单/复杂各10保护通过。新增回抽/预计层时间/角色段长消费也已完成：Web最终361项/build、Python155项通过；40报告和20对2507行独立全局差值逐值核对通过。正式组件贴床区域8状态、全局对比9状态、路径记录10状态CUA及实际CSV下载通过。下一步为三维名义连接与目标局部缺失，正在独立审算法/合同，尚未修改native；不以二维参考量冒充完成。无新增业务阈值、自动择优或参数覆盖。

Web收尾源码：AutoReport `500cc1594998656eeecc6b8ee6ef0d16cebea75d4a1ef9f9efed7e3850cc74eb`；sliceAnalysis `a33fa878e3314bc1f810379bd69c3f8775b893db3b1afcb803d0404ad828304f`；experimentResults `fbfefb325486f392d8ec1cd2c6acba82d86cf9f261be2f88dc6ae74391b7a9ac`。日志 `/tmp/compare-web-records-final-{tests,build}-20261019.log`，361/361、build exit0（既有大包提示）；`/tmp/compare-global-details-known-zero-real-consumer-20261019.log` 为40报告/20对2507行原值与差值核对。已知空段总长raw0保留0，missing/矛盾不补0；时间范围和完整序列独立、速度/长度范围互不屏蔽。CUA曾发现空集合总长误显示未提供，修后复核0mm；45序列20/20/5分页、partial/非法范围/非法计数/独立速度/历史/FAILED都正确。截图 `verification/compare-gcode-records-20261019.png`。独立CSV的finite number直接序列化，文本仍防公式，旧resultCsv不改；真实下载 `/Users/liwei/Downloads/切片指标对比-record-check.csv` 核对-1/-1200/-20为数值。临时HTML/Vite/自建标签已全部清理。

贴床区域冻结身份：SliceAnalysis.cpp `04abaa8d09ba811cbf7f5bc4aa4b844b0cb8174fe9e1c9d4d3aa220415de3a1a`；tests `5233b7cb9eff4da49f4f095e6d2f31382489b8673e1374d9a9a00eb50fa21199`；app `135fe707a09eef584e938b78212709d5d24e58c6e67bbe2a556de2f58b787955`；test binary `637dbd77d4846704b9dc06f1f1b066c04d16e8c0e460f907159256cff4ba35b8`。证据 `verification/{build-plate-bed-regions-20261019,unit-plate-bed-regions-20261019,plate-bed-regions-features,plate-bed-regions-cli}.log`。独立只读审核无确定阻断：原4次union和面积表达式保持，ExPolygon面积扣洞、洞内材料岛单列，world XY含plate origin、Brim不重复变换；不以bbox短边冒充最小有效接地宽度。

贴床区域复杂龙模型10项保护通过，默认/关/开10.628/10.472/49.778s，`plate-bed-regions-dragon-cli/summary.json`；这仍是全部报告成本，不能把单次50.282→49.778当净性能改进。跨构建40打印指令/CONFIG_BLOCK/process/configured/gcode全部严格相同，20旧AB/CSV全同；旧geometry与其consumer39/40同，唯一new-island-tree-arachne第25层旧section_uncovered_area_mm2新0.3314142692179997、旧0.33141426921799966，差5.551115123125783e-17。`/tmp/compare-bed-regions-cross-build-20261019.log` strict exit1保留，不放宽容差。真实消费、同份报告只剥4arrays的隔离均通过，`/tmp/compare-bed-regions-real-consumer-20261019.log` exit0。模块文档35模块/12944归属/2496排除/0问题，`verification/module-docs-plate-bed-regions-20261019.log`。

新增全局对比沿用原输入/机料/修复门禁，材料与角色只按唯一身份配对；流量/冷却阈值不同只关闭相关阈值差值，负加速度合法、比例差用百分点；角色耗时/长度/质量各列独立，不由长度猜质量。旧10项/旧CSV不改，新增独立CSV保留条件说明。347项Web全量与build通过，独立静态审查无确定阻断。CUA覆盖正常、上限不同、冷却不同、材料重复、角色乱序、失败、输入不同、含修复、历史；真实按钮下载 `/Users/liwei/Downloads/切片指标对比-local-check.csv` 并核对内容。截图 `verification/compare-global-detail-comparison-20261019.png`，临时HTML/本机Vite/自建标签已清理，未触及业务API。

上一批流量距离最终验证为174 cases / 9923 assertions、40真实CLI、简单/复杂各10项保护通过。复杂模型1828500面/112层独立默认/关/开10.412/10.645/50.282s，证据 `estimated-flow-cap-distance-dragon-cli/summary.json`；仍是整份报告成本，不是该新块净开销。原测试夹具失败及跨进程旧面积一处ULP严格FAIL保留在下方历史，不据此抹掉失败或宣称全字段位级稳定。

联合路径最终构建成功（`verification/build-joint-lower-slabs-20261019.log`），native 164/9694、40场景和10简单保护见 `unit-joint-lower-slabs-20261019.log`、`joint-lower-slab-features.log`、`joint-lower-slab-cli.log`。CPP SHA `950addad0e83784a1f3bff99d3c4996015f445b7dba4b37bc994485c0c426a99`、test源码 `c9c275299e7d38c6b77cd8170fe8add4397d20fa3d1593b566cae82e5d3d675e`；app `7df120ed09cbab229fc2624cf86246494c78ab9ad576d47a1e1750667189aa74`、test binary `d683880420bef30c070bec3ddaa0226a6c8781cf83e256c337eebe035fd69714`。两方只读审查无确定阻断，缓存保留原路径顺序和旧统计语义。

联合路径真实消费40份/756层/758底面原值一致，其中9层无路径、302底面有正未覆盖面积、16底面有桥中心线空段；这些不是缺陷/失败计数。真实样例未出现unknown/多实例，以原生及页面夹具补验。仅删除新块后旧消费者40、A/B20、CSV20严格一致；跨构建对上一批 estimated-peak-flow-features，可执行G-code、完整CONFIG_BLOCK、process快照、configured_settings、完整gcode和所有旧geometry均严格一致，差异数组为空，不加容差。证据 `/tmp/compare-joint-lower-slab-{real-consumer,cross-build}-20261019.log`，均exit0。

1828500面复杂龙模型独立10项保护通过，默认/关/开 10.476/10.603/50.135 s（`joint-lower-slab-dragon-cli/summary.json`）。另用新目录和macOS `/usr/bin/time -l` 各测一次关/开，无并行构建/测试：10.087/49.078 s，进程峰值RSS 1341652992/1401389056 B，差约56.97 MiB；全部打印指令和配置仍相同。证据 `joint-lower-slab-memory/summary.json`。这是整份报告相对关闭的成本，不是新块净成本或普遍性能保证；未因本样例未超时就声称所有复杂模型性能合格。

执行中取消探针已确认并修复：一个region的4096条填充，测试派生现有路径offset虚函数，在第17次真实计算后调用 print.cancel()；旧实现direct/nested均继续至4096才抛出。原失败保留 `unit-cancel-probe-20261019.log`。最小产品修正为 PlannedFootprint 引用 Print，在递归与每路径入口检查取消，不改几何/报告。最小构建 `build-footprint-cancel-20261019.log` 成功，155 cases / 9434 assertions 全通过（`unit-footprint-cancel-20261019.log`）；两种取消均严格停在17次，无线程或时间等待。该批app SHA `a1ce3b7d514a107de71b1b1636525a4dbc4f5ef4071115515a8c6a0a29727b79`、tests binary `af75340a6ce33e9eb03381e43110021576d4a040801e4465578ffdc3259ea56f`，不证明随后新增的联合路径块。

密集桥探针未发现当前规模需优化：32/128条桥横跨同一梯架31/127个孔，段数992/16256严格正确，1 mm长度只用1e-12数值误差范围，重复报告/路径/配置严格一致。整报告首遍/重复约1.15/0.87 ms、8.92/8.86 ms，26断言通过，日志 `unit-dense-bridge-probe-20261019.log`。这是特定几何，不外推所有复杂桥吞吐，也不因此增加空间索引框架。

后续几何增量已完成两方只读设计审查：把当前路径按自身 height 分名义底面 Z，对较低源 print_z 的模型/支撑/筏路径名义块查询 `bottom < b <= top`，联合足迹后求未覆盖，桥沿同一区域测连续空段。相比旧最近支撑投影，新增实际路径高度、Z 空隙和联合材料关系；不同底面不能混算，原块/接口不改。它不证明同层先后、按对象打印的材料已存在、真实接触、锚定强度或不能桥接；不将第5/6/7标为全面完成。先完成取消修正，再实施该直接几何观察和确定性真值，不增加阈值/评分/自动择优。

2026-10-19 联合路径消费验证：原生实现与9组新测试已按上方身份构建通过，复用同一Layer的路径扩展缓存、按源打印Z和量化底面查询，并保持公开objects出口。Python 140项通过；Web交叉审发现正数但错误分辨率及重复/倒序底面可误报完整，已最小对齐固定1e-6和量化唯一升序；异常只使该层未知。修后197定向/308全量及build通过（`/tmp/compare-web-joint-lower-contract-{targeted,tests,build}-20261019.log`）。正式AutoReport/样式本地9状态CUA通过：正常多底面、单底面未知、整层未知、桥未知、无路径、历史、FAILED、非法面积、重复底面；首层完整但不含床，合法0与不适用分开，未知不抹其他有效层/底面。截图 `verification/compare-joint-lower-slabs-20261019.png`。临时HTML、服务和自建标签已清理，仅保留验证证据。这些页面夹具不替代业务E2E；真实CLI结果见上方独立证据。

第15类超cap距离在建：两方公式审核确认使用同一finalized block的真实加/巡/减距离和当时linear E体积，不能用未回存的exit速度；先以stored peak作为严格上界，cap>=q×peak记0，防止float平方根和double积分尾差制造假超限，不加epsilon。按材料独立累计参与/超限距离；配置cap缺失、非法或与计算时身份不一致，仅新块未知，旧峰值保留。Python147、Web206定向/317全量与build通过，正式组件9状态CUA通过（截图 `verification/compare-flow-cap-distance-20261019.png`）；缺上限夹具需同步把旧峰值cap ratio设null以表示一致的有效历史数据，未为夹具改变产品。临时HTML/服务/标签已清理。原生四文件已冻结，最小应用/测试构建进行中，尚不能声明新native通过。冻结processor `28a48a8ad2bdb907ec3d535d03ff065bda2ed1875a8a9f03ed9250a2fb73ea36`、hpp `b1d5836c08d29723fbdf32d864dd890bfac2244aaf7bcb8f442f733678ec76c0`、SliceAnalysis `082f2bfff2fa72c4a4f43b6f2e79cfc1c3f394d3f82e8912ca7d1038ed55f6a4`、tests `e1c6a45670cb072b1ccab6bf3f461ff34f54963d11a5a957810bf15daf030633`。

下一增量限定为第8类已约定的“每个接地区域面积/范围”：复用当前贴床模型/支撑/Brim及联合的四个union结果，各新增对应 `*_regions` 数组，每个正面积ExPolygon只含area_mm2和bounds_mm.min/max（XY毫米）。旧四面积与complete/scope不动，不再扫路径、不增加union次数；世界摆盘坐标含plate origin，不能称床局部坐标。洞扣面积、洞内独立岛另条；序号不是跨候选稳定身份。不用bbox短边冒充最小局部宽度，不加附着评分、语义脚或稳定性规则。代码先做消费者独立可选合同，原生等当前构建结束后实施，分别记录构建身份。

流量距离首轮构建：应用成功，tests因两处测试调用private动态配置入口失败。修为public process_file临时文件导入后，169/170已执行case通过、动态导入fixture发生SIGSEGV；原因定位为裸默认ConfigOptionEnumsGeneric缺少序列化keys_map，沿用test_print.cpp已有夹具的print_config_def映射准备，不开放private API/不改产品。两轮原失败保留，修正后10项流量距离case/229断言通过，全量重新运行中。当前test源码 `4d21d6d0e93f702729cb2c0072244f5426db0e53e558459d0df1a672db18a573`。LLDB初次argdumper失败、改launch后目标停于启动阶段，未获得有效栈；自己的debugger/目标/sample已停止，不以此声称有栈证据。

流量距离40真实CLI及10简单保护通过，新app SHA `4d9cb3e174d5720d6d4da3fbbe6480f9044d7f4cf8c5d00ff26b699df5107070`。40消费者原值一致，14份超cap长度为正、26份为零，不是失败计数；仅剥新块后40旧消费者/20旧AB/20旧CSV严格一致。跨构建40打印指令/CONFIG_BLOCK/有效配置/全部旧gcode均一致；旧geometry39/40严格相同，new-island-tree-arachne第25层旧section_uncovered_area_mm2为0.33141426921799966/0.3314142692179997，差5.551115123125783e-17。严格FAIL日志保留 `/tmp/compare-flow-cap-distance-cross-build-20261019.log`，不加容差转PASS。

接地区域消费者已完成Python155、Web216定向/327全量与build；root正式AutoReport 8状态CUA通过（正常、空、全部未知、单类未知、非法边界、历史、失败、45区域20/20/5分页），截图 `verification/compare-plate-bed-regions-20261019.png`。临时页和标签已删除，Vite暂复用于下一对比页面；未把夹具当原生区域功能已经通过。

候选对比审查确认新的全局量此前只在A/B两侧分别展示。现补独立detailRows和独立CSV：沿用原比较条件，按唯一材料索引/路径角色配对，整板按固定分类；比例差为百分点，负加速度保留。阈值不同只禁止相关超cap/冷却差值；局部层/底面/区域不按数组序号硬配。不改原10项/原实验CSV，不新设胜者或阈值。既有预计分角色时长与用料也纳入同口径对比；实现/独立审核/页面验证进行中。

第 15 类首次 native 153 cases / 9386 assertions 有 1 失败：queued M221 case 的期望硬写 100 mm/s，而现解析器 `6000.f * (1.f/60.f)` 为 100.00000762939453。只改测试为严格核对解析 feedrate、定型峰值及同块乘积，不改产品或放宽容差。最终 tests SHA `add2b8b77d815dbf1d1f75726fdea7bf7908907800ae8f080fd844eb33fe7547`，test binary `09ecf94beedecfe51477a37a38c8e95a1512cda42fb22a1839cd4bca09f74fe6`；原失败 `unit-estimated-peak-flow-20261019.log`、修正构建 `build-estimated-peak-flow-testfix-20261019.log`、最终全通过 `unit-estimated-peak-flow-final-20261019.log` 均保留。

第 15 类真实消费：40 份报告/40 个材料行的峰值和比例原值映射严格一致，40 均有效非零；未知/不适用/合法零仍主要由原生及前端夹具验证。删除仅新增 flow 后 40 份旧消费者、20 对 A/B、20 CSV 完全一致。跨构建对第 13 类 40 份报告，可执行 G-code、完整 CONFIG_BLOCK、输入 process 配置快照、configured_settings、去新 flow 的整个 gcode 块及全部旧 geometry 均严格相同，无尾数差异；无容差。两脚本 exit0，日志 `/tmp/compare-estimated-peak-flow-{real-consumer,cross-build}-20261019.log`。阶段先后结论分别与上述身份绑定，不用此结果覆盖历史 strict FAIL。

第 13 类定型运动块峰值速度/切向加减速已完成 Mac arm64 Release 最小构建（`build-estimated-motion-20261019.log`），40 次真实 CLI 与 10 项开关/文件保护均通过。原生首次 140 cases / 9100 assertions 有一项圆弧夹具失败：现解析器 `F600 × float(1/60)` 为 10.000000953674316，而测试写死上界 10.f。只改为严格核对正 E 圆弧的解析后 feedrate；无容差、产品不改，重建后全部通过，原失败日志保留。Python 124、Web 定向 175/全量 286 与 build 通过，正式组件 8 状态 CUA 完成，临时服务/页面/标签已清理。该批 app SHA `f845c061c1b5f338531742f0e5c363a4e86b1e17e35f05e689baa6205c7e102f`，不证明第 15 类新代码。

第 13 类重建测试目标后 140 cases / 9108 assertions 全通过（`unit-estimated-motion-final-20261019.log`），测试 SHA `8f7ce0923baaf2ecc400b3468b508a78cdb1996e64234fbc3144c9a19e1bf7ca`、测试程序 `a57e75ff5ccd9331825a2d22022cae1805529f91f3f61beef4f0b562e237ed4d`；SliceAnalysis `8e31e37e8945a68e29e0c993cf67535c45e565bd87980cd669e46200cd1ae669`，GCodeProcessor.cpp `4f01805b6a3a53e835784346d8737aeba850fabeac9e259e8ca1b0cec173d600`、hpp `f91a5961aa4a1d4dfc2d765dd9237065e69cf8c30548686ec1234f8f5f6aa9a0`。复杂龙模型独立 10 项保护通过，默认/关/开 10.723/10.555/46.747 s；无其他构建/测试并行计时，可执行指令和配置完全一致，见 `verification/estimated-motion-dragon-cli/summary.json`。仍不将单次计时差当性能改善。

第 15 类在建块时快照材料索引和线性 E 体积（含当时全局 M221 倍率），按同一 finalized block 的峰值速度计算每材料峰值范围，再对照既有 cap；不把不同块的最大值相乘。只增加新观察的支持边界，M200 D0 可用，体积 E 启用/分工具 M221/G20 后新流量未知，reset 恢复；不改旧解析/估时。S0 是合法零；未知材料上限或比值非有限只隐藏比例，不隐藏已知 raw。Python 131、Web 185 定向/296 全量和 build 已通过。首轮只读交叉审无正常流程阻断，补非法丝径基本校验只影响新观察。新增 13 组 native case 最终运行通过，原始夹具失败与修正证据见上方。

峰值流量正式 AutoReport 组件已完成 9 状态 CUA（正常/零/不适用/未知/非法/缺上限/比例溢出/历史/失败）；发现 Number.MIN_VALUE 上限被格式化为数百位小数，最小修正既有 formatter：非零绝对值小于 1e-9 时用三位有效数字科学计数法，原 8e-6/零/符号/单位保持，不改原始报告或 A/B/CSV。截图 `verification/compare-estimated-peak-flow-20261019.png`；临时 HTML、Vite 服务和自建标签已清理，未访问业务服务。Web 日志 `/tmp/compare-web-tiny-metric-{targeted,tests,build}-20261019.log`。

第 13 类真实 40 报告消费 316 个角色原值映射严格一致：276 适用、40 Custom 零块不适用，未知 0；4 行合法匀速零加速度。40 历史/失败/局部缺项状态正确，同份报告剥离新项后的旧消费者、20 A/B、20 CSV 严格不变。证据 `/tmp/compare-estimated-motion-real-consumer-20261019.log` 与 `verification/compare-estimated-motion-20261019.png`。跨构建 40/40 可执行 G-code、完整有效配置、旧 gcode 块（估时/用料/旧流量）严格相同；全几何 39/40，相同 new-island-arachne-support-on 两项旧面积末位差最大 2.22e-16，原严格 FAIL/exit1 日志 `/tmp/compare-estimated-motion-cross-build-20261019.log` 保留、不加容差。不自动改参或择优。

此前完成逐材料层时间对照与复杂模型空间裁剪优化：Mac arm64 Release 最小构建通过，121 项原生 / 5292 断言、40 次真实 CLI、简单模型 10 项 CLI 保护、复杂龙模型 10 项保护通过。Python 109 项、Web 269 项及生产 build 通过；40 份报告、旧消费者、20 对 A/B 和 CSV 保持一致。Computer Use 已检查层时间正常/未知/非法/空数据/历史缺块/失败/禁用/零阈值/合法零/多材料；此前深度、逐层与窄区页面验证保留。页面夹具不等于后端 E2E。

第 11 类首轮记录：`planned_material_depth` 完成首轮构建与功能验证，130 项原生 / 5605 断言、40 次真实 CLI、简单及复杂模型各 10 项保护、Python 117、Web 278 与 build 全通过。40 份真实消费、旧原始字段与 20 对 A/B/CSV 严格不变；正式组件 7 状态已 CUA 验证。但复杂模型开启报告升至 171.317 s（关 10.453 s，新增前开 43.828 s），没有把“未超时”当成本可接受。随后已完成下文目标驱动优化及重新构建/验证；旧填充目标算法不改，仍不改参数、A/B 选择或打印路径。

该首轮深度算法与目标深度独立：起点没有覆盖记为 0，无目标为不适用，非法路径高度为未知；不称实际密实厚度或强度。独立审核发现内部结果未搬运到对象报告出口，已补一行后重新构建；所有新测试均走公开报告入口。冻结 CPP `b4db876b21074536db44f3cbcd99b844e9881834e0d615ae275e17ba14e2af82`，tests `57ea8ec94925e4eba17afe2226bdeb46be91c3b1a02d408503948d2948164467`，app `ce8f153120af8c304544f5e0da96a1eb2a6ae70308cc43bdfefbad20f36dcaa5`。证据 `verification/{build-planned-material-depth-export-20261019,unit-planned-material-depth-20261019,planned-material-depth-features,planned-material-depth-cli,planned-material-depth-dragon-cli}.log`，消费者 `/tmp/compare-planned-material-depth-real-consumer-20261019.log`。40 组 80 顶/底项中 74 有目标且有起点留白（min=0/max>0），另 6 无目标；不是 74 个打印缺陷，全部无路径的 0..0 仅由原生/页面夹具覆盖。临时预览页/本机服务/标签已清理，截图 `verification/compare-planned-material-depth-20261019.png` 保留。

目标驱动优化已完成第二轮最小构建、131 项 / 7240 断言、40 CLI、简单和复杂模型各 10 项保护。加入 test-only 原全盘扫描，斜边/孔/重叠/固定随机条纹/镜像 Z 与真实稀疏顶面同一 Print 严格比较；原生 C++17 构建发现三处结构绑定隐式捕获扩展，已改为 init-capture，重新构建测试通过，不改数学计算。此冻结版 SliceAnalysis `2ad2b7757951adafe5ee93e625381d0f21e62e3c082c90017bdfe09642e0b0ab`，tests `37b33a7d4f87a703e44ba76cf3221a4014711437e6098661248ca1a38e0cd2c5`，app `4b193dad5d309bcb3452246afe45bc0f1f522fb453ac1dcee5e577814b2d7736`。复杂模型默认/关/开 10.396/10.530/46.602 s，较首轮 171.317 s 明显降低，仍高于关闭报告的成本。10 项保护通过、打印指令和有效配置完全一致；证据 `verification/material-depth-targeted-dragon-cli/summary.json`。

优化前后 40 报告跨进程严格比较有 12 处面积末位差（最大 2.220446049250313e-16 mm²）：新起点未覆盖面积 10 处，未改算法的旧面积 2 处；目标面积和深度 min/max 均一致。严格比较 FAIL 日志保留 `/tmp/compare-material-depth-targeted-real-consumer-20261019.log`，逐字段枚举见同前缀 `exact-differences` 日志，未增容差。该量级与 double 面积累加顺序舍入一致，但不能据此单独证明每个中间多边形相同。另用每份当前报告剥离新字段的隔离比较：40 份旧消费者、20 对 A/B/CSV 全部严格不变，80 项新字段逐值映射正确，见 `isolation` 日志；不得把同份报告隔离与跨进程位级稳定混为一谈。

第 13 类实现复用 normal 时间估算器已经定型的运动块，按角色记录每块峰值速度和切向加减速范围。不得用请求速度/配置加速度冒充实际估算曲线，不改变旧规划器、时间或路径；未定型 EOF 单块对应统计为未知，中途保留块不提前判失败。当前构建/测试进展见本节顶部，与上段材料深度的冻结身份分开。

复杂模型性能已完成一轮优化：本地 `dragon.stl` 为 1828500 三角面、112 层。原整进程默认/显式关/开为 10.557/10.544/81.582 s，优化后 10.631/10.524/43.828 s；仍有约 33 s 报告开销，不作吞吐承诺。仅用缓存精确包围盒排除必不相交的完整多边形，再执行原 Clipper 运算，不降精度、不截断、不改变相交/接触输入顺序。新增同一 Print 的未裁剪参考真值验证；40 CLI 与复杂模型 10 项保护均通过，配置和可执行打印指令严格一致。最终复杂模型报告恰与首轮逐字节一致；中间未优化诊断进程出现约 0.00005813 mm² 跨进程面积尾差，不能宣称所有重复报告位级稳定。临时计时代码、采样进程和预览页均已清理。

该完成批次应用 SHA256 `b2f68e4dd68a17d22cf60ab769a4226b0963fc695c1c8bd3549def186ff847d3`，SliceAnalysis.cpp `aeffbe87483285365a0080eb74970d30bc18dac81736b5c4eb97fbc114ed6f95`，原生测试 `228909473a603afbcfd52fa545ea8b1e88f9bbc2325e8cfb7be86d315e6f79d8`。证据 `verification/{build-bbox-pruning-20261019,unit-bbox-pruning-20261019,bbox-pruning-features,bbox-pruning-cli,bbox-pruning-dragon-cli}.log`。Web 复杂报告约 1.10 MiB、112 层、6730 接缝点，分页无截断；Node 消费计时不当浏览器渲染性能。复杂模型墙环统计为未知，静态审阅未确定具体断链/退化分支，不擅自放宽几何判断。

逐材料统计在 GCodeProcessor 现有 finalize 循环、临时 layer ID 被替换前保留实际用料集合，不按 Z 或相同 duration 猜测。无新增冷却阈值/选择规则，不改冷却算法、旧预览 duration 或估时；禁用/零阈值/无用料不适用，缺身份/非法参数未知。真实 40 个材料行中 15 个短层数量阳性、25 个合法零；多材料由单测/页面夹具覆盖。Web 另校验参与数不超过总层序列数；不改变旧 A/B/CSV。首次 120 项中 1 项夹具失败，已补最后停车运动及对应单块未知反例后通过，未改变原估时器。原失败日志保留。

完整 16 类仍有真正的软件算法缺口，见 [方案当前范围](../../../doc/bmr-print-optimization.md#21-当前实现范围仍是-16-类不合并删减)：通用笔画/三维分支对应、融合浮雕底界、最终外表面阶梯误差、三维接触与联合承托、任意曲槽/活动关系等尚未实现。重要区域、配合公差和未来自动选择条件另需业务输入；不能把算法缺口统称为“等待实印”或“等待目标”。用户已明确先分析对比，不自动择优。

### 2026-10-19 名义填充深度、定位与厚度复用证据

- 原生新块 `solid_fill_target_depth`：复用原 `fill_surfaces` 的 `is_solid()` 目标，按每个 Surface 的有效 thickness（合并层使用显式厚度）构造名义 Z slab；固定 XY 连续追踪，正反事件扫描分别查询顶/底目标。Z 按原生坐标单位量化，真实一个单位空隙中断，重叠取并集，不把侧向漂移的相邻交集串成深度。顶/底各高度先并集再累计面积；无目标 area=0/range=null，非法或无层整新块未知。它不是实物密实厚度、有效实体层数或封顶跨度。
- 原生真值含自适应层高、合并厚层+VOID、重叠/真实间隙/浮点假缝、XY 错位、孔、多高度目标、重复区域/实例、0/25/100 皮肤密度、取消与只读。100/400 层扩展斜坡整报告耗时约 3.79/14.26 ms，仅该本地简单几何，不外推复杂模型吞吐。
- 首轮新增测试 3 个 SECTION 失败于把 `ConfigOptionPercent` 当 float 设置；只修夹具类型后原断言全部通过，产品源码未改以迎合测试。前一批接缝处理器测试的角色标记改用 `reserved_tag`，避免 BBL/非 BBL 字符串差异；旧失败日志保留。
- 源码 SHA：SliceAnalysis `0016f9f69f7c50ee7ff88ee6fdd7b963bc88b5bb965ca0a9d473e1945ead015a`，native tests `d719bc0079d326f991a5088d252a8ec25c614009c54a4885c4fd4c1fed781bc1`。日志 `verification/{build-solid-depth-20261019,build-solid-depth-testfix-20261019,unit-solid-depth-final-20261019,solid-depth-features,solid-depth-cli}.log`；消费者 `/tmp/compare-layer-depth-real-consumer-20261019.log`。
- 隔离 OFF/ON 各 3 次：6 次 3800 条可执行指令及有效配置严格相同，保护源文件/输入/根 result 不变。整进程 real 中位 0.11/0.13 s、monotonic 0.14745/0.15022 s、RSS 差 1146880 B；这是整报告开销，不是本字段净成本。`verification/solid-depth-performance/measurement-series/summary.json` passed=true。
- Blender 同次模板分析复用独立 quality 的法向射线测量，不带默认 FDM 评分/最薄阈值；输出样本最小厚度、位置和覆盖。89 项 Python、20 项 Java 通过，21 份旧完整 quality 字典与旧 HEAD 严格相同。19200 三角面样例整进程中位 0.66→1.05 s、RSS 257.16→269.62 MB；抽样增加实际成本，不称免费或全表面保证。证据 `/tmp/model-feature-thickness-evidence-20261019.A8jrmM/`。
- Web 沿用正式 AutoReport，不增加接口：窄区明细保留首风险提前停止，层号/源采样 Z/打印 Z 分开，未知不变成 0，失败任务不显示旧特征摘要。闭合墙环是完整环不是短线段，接缝预览数组按 20 条分页不截断；新增明细不混入原 10 项 A/B 差值或 CSV。

### 前序批次记录（历史状态，不替代上方当前状态）

整板贴床批次验证已完成：98原生/4529断言、40真实CLI、10开关保护、91Python（含既有）、40真实Web消费全部通过，20对AB/CSV保持。应用SHA `a9871e7e9cf4f736657e1c19493fdbdaa5d92e576a5957fa55d3e83686fbe948`。40场景里10项支撑/筏面积阳性，Brim都是0，Brim正面积由native实际process样例覆盖；Cura两墙模型贴床0、筏层228.98728083681348mm²。日志 `verification/{unit-plate-bed-20261019,plate-bed-features,plate-bed-cli}.log` 与 `/tmp/compare-plate-bed-real-consumer-20261019.log`。新闭环/接缝源码不在此批身份内。

2026-10-19 继续执行（当前续接点）：整板贴床面积批次最小构建已退出0，命令仍为 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，日志 `verification/build-plate-bed-20261019.log`。其独立原生/40次CLI/10开关保护由当前二进制验证；之后新增闭合墙轮廓周长/转角统计，必须再重建，不能用这次程序证明后续源码。Blender同次模板分析复用已有法向射线厚度测量，纯测量不带默认FDM阈值；Web补独立逐模型明细。并行审核已修复Main多色耗材可能覆盖喷嘴后错误套用自动墙建议，保留模板/手动值/原多色路径；35入口测试、126相关回归通过。未提交/推送、未部署、未连接业务DB或打印。

2026-10-18 继续实施（进行中）：按用户要求推进到可集中测试，不再停在规划。Git 基准与下述源面批次相同，保留既有 WIP 和用户 result。并行补原始 Voronoi 平行边局部观察、最终 G-code 空移/接缝范围/分角色流量对照，复核五个后端及 Compare Web。先修发现的缺层报告被当完整问题；不新增自动择优/机料阈值，不改客户端和修复流程。新修改未构建时不得复用下方旧测试结论。16 类中的通用三维特征、真实接触与物理标定仍明确未完成，不以局部观察冒充全功能完成。

2026-10-19 平行边/空移/流量批次已通过当前最小构建和运行：91原生/4406断言、40次真实CLI、10项开关/输出保护、81Python、215Web及build。40份真实报告/756层/316角色消费一致；剥离新增字段后旧7块、旧消费者及20对10指标A/B严格不变。Computer Use已检查正常、未知、无适用、历史缺字段、FAILED隐藏旧产物、缺层未知；截图 `verification/compare-full-evidence-20261019.png`，真实消费日志 `/tmp/compare-completion-real-consumer-20261019.log`。这些数量含既有用例，不与下方历史累加。

该批最小构建命令 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6` 退出0；缓存本仓库/Ninja Multi-Config/arm64 Release/macOS14/MeshLib本机评估ON，无并发写构建。日志前缀 `planning/bmr-commercial-design-20261018/verification/{build-completion-20261019,unit-completion-20261019,completion-features,completion-cli}`。CPP SHA `614afd2b761840ee5661ba36e95bf0be7f413bec61a07dd86102d39a60f893e2`，native tests `85bf70828eff8775e514b545e21736f82b59416f381d378b5a8314ee97929c53`，Orca `00cd8e8190f800a84235f1558e419da62b32af42301f5551839a5ec8fed08e7b`，测试程序 `0f4d536047a93ec0f2d4ae7f7b8a9b75901432dd5a3d6c946239d0104f4ab0e4`。Voronoi只增加原图检查模板实例化，不运行修复；G1仅更新Travel记录距离，Extrude值不变，GUI读取仍仅Extrude。根result SHA仍 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。

继续实施（未验证新代码）：第8类整板名义贴床足迹，复用模型/支撑/筏/Brim计划路径；分类面积与整体并集分开，按每条path自身printZ−height判断，Brim已是整板坐标不能重复乘实例。排除输出条件不同的Skirt及自定义挤出，不作实物附着结论。另核实Blender独立quality已有3D法向射线厚度算法，模板入口将同次模型复用测量与抽样覆盖，不重造算法、不引入默认喷嘴下的细杆阈值或总评分。先完成各自真值/缩放/共享数据回归再统一构建；旧构建不证明下一批源码。

后端本批审核修正：PDS/Compare喷嘴按机器→工艺→耗材的现有覆盖顺序解算；Main在分析后收到含nozzle_diameter的耗材覆盖时跳过自动墙建议，手动参数/模板/APP行为不变。冻结时Main122/PDS85/Compare46/PAM79/BlenderJava20（0跳过）及其Python/Blender19+4+11通过，后续Blender新增另验。PDS上线前必须先执行已有slice_analysis加列迁移并更新Orca；未连接业务DB、未部署/打印。旧quality薄杆/弱接头启发式不等于模板已实现三维连接检查。

2026-10-18 本批“源面分层参考偏移”已完成并通过本机验证：81项原生/4299断言、40次真实CLI、10项开关保护、73项Python、198项Web及生产build；Computer Use六状态操作、旧38场景回归、报告开关只读验证均完成。Orca仍在 `codex/model-feature-analysis-20261018` / HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，Web HEAD `02e30f17db0da0c8b7b59a94a352f4a99440332d`；保留既有WIP/根result，未提交/推送。不改正常GUI/CLI、修复、默认配置/接口或自动择优。以下历史批次数量不累加为当前总数。

下一步：继续同岛开放槽与细颈的局部目标/宽度分析，先明确可算几何真值，避免把不同岛间距、细长评分或生成路径规则冒充测量；随后集中做复杂模型压力与跨服务E2E。曲面本批只完成源面分层参考量，最终布尔外表面/实际阶梯体误差及重要表面分布仍未完成；真实三维承托、Windows/Linux、原生GUI和实印仍待集中验收，不称16类或商用质量全部通过。

工具：MCP未暴露CodeGraph，CLI/索引可读，一次增量sync成功，修改前SliceAnalysis/SlicingAdaptive/PrintObjectSlice目标哈希与源码一致；最终新代码以源码审阅和当前构建测试为据，不用修改前索引证明最终内容。历史understand图不覆盖本模块，未初始化或生成过期差异图。差异/几何交叉审查否决细分相关指标并收紧float测试，具体如下。

### 本轮设计与实施：源面分层参考偏移

独立审查否决了“源三角面Z跨度×斜率”的水平变化量：同一平面细分后最大值会缩小，不能作为可靠层高对照。本批改为两个独立参考量，不合并评分：

- 非水平源面（包括竖面）：实际源层带为double `slice_z ± height/2`，与面Z范围交于 `[a,b]`；把其中源面点仅沿Z移动到切片器实际使用的 `z=double(float(slice_z))`，取位移在该面单位法向上的分量最大值，即 `max(|a-z|,|b-z|) * |nz|/||n||`。共面细分保持最大值（浮点尾差除外），竖面为0，45度面跨0.2mm完整层时约0.07071mm。float平面舍入不清零，不以h/2设硬界。这不是有限网格到打印阶梯表面的距离，也不能证明该处存在打印路径。
- 水平源面：到全部保留源层带边界的最近高度差，再按volume取最大；范围外的源水平面也统计，因此不能称“已被切片/打印的表面误差”。无适用面为count=0/max=null，而非伪造0。

新增独立 `group.source_surface_layer_alignment`，scope=`source_model_part_facets_and_retained_source_layer_bands`，按source MODEL_PART volume汇总complete、非水平面-层带交集数/最大法向偏移、水平面数/最大边界高度差。源面使用切片一致的float变换再以double计算法向；负体积/修饰器不计，重叠/内部源面未剔除，不冒充最终布尔外表面。无层或非法几何仅使该新统计未知，旧source sampling及其他块保持。层带使用源坐标，不能用受筏层/Cura偏移影响的printZ替代。

分工：原生共享报告核心/精确真值、CLI/Python合同、Compare独立折叠展示并行；原38个真实CLI配置保持，另加closed45度wedge的Classic/Arachne两例。根统一最小构建、新程序测试、40份消费/旧指标对照、Computer Use、只读成本与模块文档。当前正在实现，尚未以旧程序证明新代码通过；不提交/推送、无数据库/打印机操作。

消费者先行验证：Python72项、Web198项与生产build已通过，日志 `/tmp/model-feature-surface-alignment-cli-fixtures-20261018.log`、`/tmp/compare-web-surface-alignment-{tests,build}-20261018.log`。Computer Use已操作正式AutoReport/正式样式的六种本地合成状态：正常、无适用、未知、历史缺块、FAILED、多volume独立统计；截图 `verification/compare-surface-alignment-local.png`。这不是后端E2E或真实CLI结果。原生测试仍在补齐，最小构建及真实切片尚未执行，临时预览待实际报告复核后清理。

原生已冻结：CPP SHA256 `1434f8d081f237a3c618d6af89f8366345fda1b9c33e13fbbd761bca05fe6917`，测试SHA256 `88d3f553bc9fef78989316e37b5f3eab906d14422899ef5c5df05caa3ccb9392`。新增8组case并扩展shared owner，锁定float采样目的面；独立审查修正NaN顶点会按原float变换同时污染旧Z的测试期望，未修改旧算法以迎合测试。构建前确认无其他cmake/ninja/test写入；缓存仍本目录Ninja Multi-Config、arm64 Release、macOS14、MeshLib本机评估ON。启动 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，日志 `verification/build-surface-alignment.log`，状态运行中。真实Cura CLI补水平面真值断言，仍不改原38配置。

首轮构建退出1：测试新helper使用了仓库不存在的 `Vec3i` 别名（真实类型为 `Vec3i32`），产品CPP已编译且只有既有头文件警告。仅修正测试的两处类型，保留失败日志；重新构建同一最小目标，日志 `verification/build-surface-alignment-retry.log`。未修改产品算法或降低测试断言。

第二轮最小构建退出0。随后审查收紧float目的面断言：Catch的默认相对epsilon会盖过1e-12 margin，显式epsilon=0以真正区分float与double目的面；仅重建测试目标，日志 `verification/build-surface-alignment-tests-final.log`。产品CPP/app不变，开始40次真实CLI及10开关保护，输出 `verification/surface-alignment-features/`、`verification/surface-alignment-cli/`。Python补真实Cura源20层和水平端面对齐断言后73项通过，保留新日志 `/tmp/model-feature-surface-alignment-cli-fixtures-20261018-final.log`。

本轮运行已完成：最终测试目标构建退出0，81 cases/4299 assertions通过（79报告+2已有回归）；40真实CLI及10开关保护退出0。真实40报告/44源volume消费全部完整，wedge双墙法向参考值阳性，其余42源volume为0；水平面42个正数含浮点尾差，不当作42个缺陷。原38场景旧6块/旧消费指标及19AB严格不变，新块加入前后20对AB保持；历史删块/局部缺项/FAILED各40通过，日志 `/tmp/compare-surface-alignment-real-consumer-20261018.log`。无适用、非法几何等边界由native/页面夹具覆盖，不假称实际CLI已出现。

Computer Use临时页已删除，仅清理本轮自建 `__codex_surface_alignment_preview.html`、127.0.0.1:5188服务及本轮标签，截图和日志保留，不动用户文件。开始关/开报告交替各3次小型有支撑bridge成本与只读验证；测的是整份报告，不是新增字段净成本，也不作复杂模型吞吐承诺。

### 本批最终证据：源面分层参考偏移

| 检查 | 结果 | 证据 |
|---|---|---|
| 最小构建 | arm64 Release应用/最终测试目标成功；首次测试类型名编译失败已修，原失败日志保留 | `verification/build-surface-alignment*.log` |
| 原生定向 | 81 cases / 4299 assertions通过（79报告+2旧回归） | `verification/unit-surface-alignment.log` |
| 真实CLI / 开关保护 | 40 / 10全通过；原38配置不变，加45度楔体双墙 | `verification/surface-alignment-features/`、`verification/surface-alignment-cli/`及同名log |
| Python / Web | 73 / 198通过，Web生产build成功；既有大包提示保留 | `/tmp/model-feature-surface-alignment-cli-fixtures-20261018-final.log`、`/tmp/compare-web-surface-alignment-{tests,build}-20261018.log` |
| 真实报告消费 | 40报告/40组/44源volume与原值一致；删块、局部缺项、FAILED各40通过 | `/tmp/compare-surface-alignment-real-consumer-20261018.log` |
| 兼容 / 对比 | 原38场景旧6原始块/旧指标及19对AB严格相等，尾差0；新增块不改变全部20对AB | 同上consumer日志 |
| 页面操作 | 正式组件/样式、本地合成6状态通过，无布局重叠；临时页/服务/标签已清理 | `verification/compare-surface-alignment-local.png` |
| 模块文档 / 差异 | 模块35、归属12944、排除2496、问题0；两仓tracked diff及相关untracked文件空白检查无诊断 | `python3 scripts/check_ai_module_docs.py --all` |

真实样例：45度楔体两墙均为 `0.07071074555361281 mm`，水平底面对齐0；Cura筏模型的法向项为0，水平端面偏移 `8.88e-16 mm`（浮点尾差），源层.1..3.9与打印层1.2..4.9分离；多源薄片水平项分别约0.06/0.08mm。44volume中2个法向阳性、42个为0；水平42个正数含浮点尾差，不能当缺陷计数。真实CLI没有无适用/非法源面样例，这些由native/页面夹具覆盖。

成本/不变性：首次6进程测量全部3800可执行指令与有效配置严格相同，但同时运行了模块指纹检查，不采用其计时作隔离比较。保留该日志和 `measurement-series/summary.json`；随后不并行其他构建/测试/文档检查，重新在 `measurement-series-isolated/` 测量OFF/ON各3次，仍3800指令全部严格相同，保护输入/应用/根result不变。整进程real中位0.12/0.13s，monotonic中位0.144714/0.141513s，RSS中位差16384B，报告117076B。不同计时粒度与短进程噪声不解释为性能改善，不能据此外推吞吐或新增字段净开销。主证据 `verification/surface-alignment-performance/measurement-series-isolated/summary.json` passed=true；旧多岛非确定性/初始化超时历史继续保留，本轮未复现不等于根因解决。

最终SHA256：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `1434f8d081f237a3c618d6af89f8366345fda1b9c33e13fbbd761bca05fe6917` |
| `tests/fff_print/test_slice_analysis.cpp` | `a7324c0172aac1e5e9d19eed88778e9a2f85105c6981e01c76beb809c6058d57` |
| `scripts/test_cli_slice_features.py` | `86ccd7f7b5ee6147f36018572730b4f5f6f38c6bf87eb523c6861d550255a86a` |
| `scripts/tests/test_cli_slice_features.py` | `57617765e0ec2e1646e5d03c164daca4561215a528403c23d14cf1c730e9764f` |
| Orca arm64 Release应用 | `73e842f2ebfcc3e795faf82416eb1b61adc5a820f31438b7a34245040d515233` |
| fff_print_tests | `bdf7292bd17b25a33a03f23c005f76b543b8a48975d06c7faa7422f1325e26e8` |
| 用户根result.json（未改） | `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6` |

GUI/CLI交接：原生GUI没有报告入口，普通GUI预览/统计不进入新增计算，不把Web验证当原生GUI验收。本机应用为 `/Users/liwei/Desktop/wiseBeginner/projects/slicer/orca-slicer-soft/build/arm64/src/Release/OrcaSlicer.app`；后续集中手测用 `planning/bmr-commercial-design-20261018/verification/surface-alignment-features/slope-wedge.stl`，按 `slope-wedge-classic/command.json` 中机型/耗材/工艺文件加载相同配置，在准备页切片、预览20模型层，保存项目及导出G-code，与对应CLI output比较，不新增/调整层高以迎合指标。Compare环境联调后用同模型的新成功任务展开“源表面与分层的几何参考”，核对法向项约0.07071mm、底面对齐0；旧报告不显示新块，失败任务不显示该分析，未知与不适用不变成0。保存同一任务报告/页面截图用于复核；本轮本地夹具已覆盖页面状态，但未连接后端/业务数据库/打印机，服务E2E及实印仍待统一验收。

### 上一批完成：中间高度模型路径投影

2026-10-18 本批“中间高度模型路径投影”已完成并验证，本机原生73项/4179断言、38次真实CLI、10项开关保护、63项Python、189项Web及生产构建通过，临时页面/本机服务/标签已清理，截图及日志保留。分支仍 `codex/model-feature-analysis-20261018`，Orca HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`；Web HEAD `02e30f17db0da0c8b7b59a94a352f4a99440332d`。保留既有WIP和根result，未提交/推送。不改普通GUI/CLI、修复、服务接口、默认配置或自动选择；原始旧6数据块/旧指标/19AB保持。

下一步：按已有规划设计并补曲面/斜面的分层偏差，先确定源表面与实际层序的几何真值，再复用报告和Compare展示。不把现有自适应层高参数当实测误差，不添加自动择优阈值。同岛槽/颈宽、真实三维接触/打印时序、复杂模型压力、跨服务E2E、Windows/Linux和原生GUI/实印仍待集中验收；本批不是全16类完成。当前完整证据与边界在本节，下方历史数量不累加为当前总数。

工具：本目录CodeGraph一次增量sync成功，修改前SliceAnalysis索引SHA与源码一致；本轮新代码按源码与独立审查确认，不能用修改前图证明最终代码。历史understand图未含本模块，不生成旧节点差异叠加、不初始化或跨目录复制索引。

### 本轮过程与决定：中间高度模型路径投影

旧支撑分配保持独立，每个 `support_levels` 匹配区增加 `intervening_model_projection:{complete,area_mm2}`：统计该区与严格处于 `支撑printZ < 模型printZ < 目标printZ` 之间的整板模型计划路径足迹交集，重叠只计一次。使用现有perimeters/fills及所有实例shift，不用整个截面代替稀疏填充路径，不读支撑backup。模型在区间端点同Z时排除；支撑与目标本身同Z时，新项false/null、不适用，旧面积/间隔保留。

新项仅是两个打印平面之间的几何观察，不考虑路径厚度跨入、实际材料接触或逐对象打印先后，不能称“真实遮挡/有效支撑”。无支撑匹配时不生成虚假的0面积。非有限模型Z无法定位使新项未知；有限Z的非法层或路径仅使命中区间的新项未知，不能污染原支撑匹配。报告局部只保存层元数据，首次命中时计算并缓存局部路径足迹；共享层复用计算但仍按各实例位置查询。覆盖全部目标后仍需核查区间内其余层的完整性。

实施分工：共享报告核心及精确原生真值、现有38个CLI场景合同及Python测试、Web既有支撑行展示分别并行；不新增真实模型/配置或业务规则。根任务统一最小构建、真实CLI/旧数据对照、Computer Use、成本检查及文档收尾；仅本机测试，不连接业务数据库/打印机，不提交或推送。

实现冻结：新增7组原生case/SECTION，含真实共享筏首层新项为0、同对象前层足迹越出截面面积1、重复8/互补16等人工真值；独立源码审核未见阻断。CPP SHA `832a1ea5794ab7a40bf192c8debf3a07d0e96c5e6bb413db6588e6883045aacf`，测试SHA `be1f1658da9150575a45f2ae2357b70cdefe97907fdfcb97fd643b94ace51633`。Python63项已通过。构建前无其他cmake/ninja/test写入，缓存仍本目录/arm64/Release/macOS14/MeshLib本机评估ON；开始最小应用与测试构建，尚未使用旧程序验证新代码。

首轮运行：最小构建退出0；新二进制定向73 cases/4179 assertions全部通过（71报告+2既有回归），38真实CLI及10开关保护退出0，原38有效配置未改。Web189项及生产build通过。Computer Use已查看正式组件新面积列的正值/0，截图保留；其余状态、真实报告消费与只读成本还在收尾，尚不把本地页面当后端E2E。

末次独立审核无阻断，发现非阻塞文案歧义并收紧：旧支撑完整性只表述为“支撑高度匹配记录完整”，不能与新nested完整性混为一谈；“同高不适用”明确指支撑与本层模型打印Z相同。未改算法/合同，新代码冻结后没有再改CPP或测试。

文案修正后Web189项、生产build、真实38消费/19AB再次通过，日志 `/tmp/compare-web-intervening-model-scope-wording-{tests,build}-20261018.log`、`/tmp/compare-intervening-model-real-consumer-scope-wording-20261018.log`。Computer Use重载核对最终文案与新列，更新同一截图；只删除本轮自建 `__codex_intervening_model_preview.html`，关闭127.0.0.1:5188进程和本轮标签，未删用户文件或验证证据。

### 本批最终证据：中间高度模型路径投影

| 检查 | 本批结果 | 证据 |
|---|---|---|
| 最小构建 | arm64 Release应用/测试退出0；仅既有头文件警告 | `verification/build-intervening-model.log` |
| 原生定向 | 73 cases / 4179 assertions通过（71报告+2原回归），新增7组含SECTION | `verification/unit-intervening-model.log` |
| 真实CLI / 开关保护 | 38 / 10全通过，原场景/有效配置不改 | `verification/intervening-model-features/`、`verification/intervening-model-cli/`及同名log |
| Python / Web | 63 / 189全通过，Web生产build通过；既有大包提示保留 | `/tmp/model-feature-intervening-model-cli-fixtures-20261018.log`、`/tmp/compare-web-intervening-model-{tests,build}-20261018.log` |
| 真实原始报告消费 | 38组/38实例/716层，53个高度分区的新项完整且为0；706层没有匹配，不伪补0 | `/tmp/compare-intervening-model-real-consumer-20261018.log` |
| 兼容 / 旧指标 | 旧6个原始数据块、全部旧消费指标、19对AB严格相等，尾差0；历史删nested/FAILED各38，局部缺项10份有匹配报告通过 | 同上consumer日志 |
| 页面 | 正式AutoReport+正式样式，合成数据6状态Computer Use操作：正值/0、未知、同Z不适用、旧缺项、无匹配、FAILED | `verification/compare-intervening-model-local.png` |
| 文档 / 差异 | 模块35、归属12944、排除2496、问题0；两仓diff空白检查通过 | `python3 scripts/check_ai_module_docs.py --all` |

覆盖边界：真实38份报告中新项53个全部为0；正面积、未知和同Z来自native人工几何与Web本地夹具，不声称真实CLI已经覆盖这些阳性/异常。原生含同对象前层越出截面面积1、条带8/重复仍8/互补16/带孔12、无路径0、共享层各实例和ByObject只读反例。支撑匹配、模型投影、外桥路径三个观察仍独立，不合成有效承托率或打印成功判断。

只读与开销：小型有支撑bridge OFF/ON交替各3次，6次均3800条可执行指令且严格相同，有效配置/保护输入/根result不变。整进程real中位0.12/0.13s，monotonic中位0.14717575/0.18653350s，RSS差1064960B（1.015625MiB），报告115382B。此为整个报告而非新增字段净成本，小样例不能外推服务器吞吐/复杂模型内存。证据 `verification/intervening-model-performance/measurement-series/summary.json` passed=true。旧多岛非确定性及初始化超时历史保留，本轮通过不代表已解决其根因。

最终SHA256：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `832a1ea5794ab7a40bf192c8debf3a07d0e96c5e6bb413db6588e6883045aacf` |
| `tests/fff_print/test_slice_analysis.cpp` | `be1f1658da9150575a45f2ae2357b70cdefe97907fdfcb97fd643b94ace51633` |
| `scripts/test_cli_slice_features.py` | `b996d894d8f6f756784e4c3c37aea80a3964da5a25466c72899501063b3e03b6` |
| `scripts/tests/test_cli_slice_features.py` | `1af83d059e7e6fd856be1280a2bc56f455aa872e5d86f43f53e7e1028d31f871` |
| `OrcaSlicer` arm64 Release | `d109e7949ae78e83d14f264f8031b642279845689a2c66780125bffa190577ca` |
| `fff_print_tests` arm64 Release | `a3feb44a6f32b0b4a502963fa89d9faaedf4713c90e6533840ad33118448cb70` |
| 根 `result.json`（未改） | `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6` |

GUI/CLI交接：普通原生GUI没有新报告入口，未请求报告的CLI也不进入计算；本批未做原生GUI手工或实物打印。后续集中验收时，同模型/配置在原生GUI与默认CLI核对普通切片无变化；Compare新成功任务展开“模型新增区域与支撑计划路径投影→实例→第21模型层”，核对每一支撑高度行对应的新面积和打印Z，旧报告显示未提供、未知不当0、同支撑/目标Z显示不适用、无匹配不显示虚构行、失败不显示旧分析。页面本地夹具不代替跨服务E2E，未接业务数据库/打印机；本批Java/Blender服务和通知接口无改动。

### 上一批完成：外桥连续段

2026-10-18 本轮已完成“外桥中心线与前层计划材料投影”，仍在 `codex/model-feature-analysis-20261018` / `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，保留既有WIP，未提交/推送。最小新构建后66项原生/3953断言、38次真实CLI、10项开关保护、55项Python、180项Web及生产构建均通过；38份真实报告716层消费一致，原5个数据块与旧指标、19对A/B均保持。以下旧批次数量不累加为当前总数。

本批只增加分析报告、原Compare展示和测试；没有改路径生成、普通GUI/CLI切片、修复、服务接口、默认配置或自动选择。重要边界：测的是连续中心线空段和沿路径前后的投影覆盖，不是实物跨度/空间双端承托。下一步继续模型遮挡与支撑/桥接的联合关系，再推进曲面分层偏差；同岛槽/颈宽、跨服务E2E、Windows/Linux、复杂模型压力与原生GUI/实物仍待后续集中验收，不宣称16类全部完成。

### 本轮过程与决定：外桥连续段

本轮决定：仅对 `erBridgeInfill` 的开放保留路径，按原路径顺序测量前一模型层计划线宽足迹未覆盖的连续中心线段；弯折按弧长，跨顶点合并，不跨独立 ExtrusionPath 拼接。记录未覆盖段数/最长长度，以及两侧在同片段内都紧邻正长度覆盖区间的段数/其中最长长度。仅点接触、沿边界重叠不算内部覆盖；不偏移下层轮廓来制造覆盖。首层、非法前后层/路径或当前闭合外桥路径暂不支持时，新块四值 null、complete=false；有效且无外桥/无空段为 0/null。内部桥和悬垂墙仍保留原角色汇总，不混入新块。

此项使用前层计划材料足迹而非整个实体截面；正常稀疏填充空隙也可能被计入，不能据此称缺陷、实际桥跨度或双端锚定合格。支撑、同层先后、真实挤出接触和机料能力不在本轮内。Web 独立逐层展示四项，无比例、评分、A/B/CSV或自动选择增量。沿用38个真实CLI配置，精确区间真值放原生测试；只改报告、消费者和测试。

工具核对：本目录现有 CodeGraph 经一次增量 sync 成功（14文件/664节点），`SliceAnalysis.cpp` 和 `OrcaSlicer.cpp` 的索引 SHA256 与本轮修改前源文件一致，前者50节点且可定位 `make_slice_analysis_report`。后续新编辑回到源码确认，不能用修改前图证明新代码。另一份 `.understand-anything` 历史知识图仍过期，不生成旧节点差异叠加；不初始化或跨目录复制索引。

消费者验证先完成：Python55项、Web180项及生产构建通过，均未变原38个CLI有效配置。Web使用正式组件/样式、合成数据，Computer Use已检查正常/首层、没有空段、只有单侧证据、不完整、旧报告、失败，四值展示与未知/不适用区分正确，截图 `verification/compare-external-bridge-local.png`。本次临时HTML已删除、5188服务与标签已关闭，仅清理自建验证产物，截图/日志保留；不是后端E2E或原生GUI验收。

独立审查暂未见阻断，源码使用原线段投影排序及现有int128方向判定排除共线边界；只保留前层足迹，分段/边界循环检查取消。复杂层最坏仍可能随桥段数×前层边数增长；本地小样例性能不外推吞吐。原生真值补整体旋转/微小坐标间隙后统一最小构建，尚未用旧二进制验证新指标。

原生实现已冻结：新增9个TEST_CASE（含SECTION），源码 SHA `22a1d159e7c83806adabc9638b3f41726c28d12dc58a26fc4d3b7a24d9dd9abe`，测试 SHA `2ea6351ea6607212421675a46e26a214bc6776fef702cde01f73303e027c0083`。构建前确认无其他构建进程，缓存仍本目录/Ninja Multi-Config/arm64/Release/macOS14/MeshLib本机评估ON。开始最小 `OrcaSlicer fff_print_tests` 构建，日志 `verification/build-external-bridge.log`，待新二进制运行验证。

### 本轮最终证据：外桥连续段

| 检查 | 本轮结果 | 证据 |
|---|---|---|
| 最小应用/测试构建 | arm64 Release退出0；保留既有头文件警告 | `verification/build-external-bridge.log` |
| 原生定向回归 | 66 cases / 3953 assertions通过（64报告+2已有回归），新增9组case含SECTION | `verification/unit-external-bridge.log` |
| 真实CLI / 开关保护 | 38 / 10全部通过；不改变原38有效配置 | `verification/external-bridge-features/`、`verification/external-bridge-cli/`及同名log |
| Python / Web | 55 / 180通过；Web生产构建通过，保留既有大包提示 | `/tmp/model-feature-external-bridge-cli-fixtures-20261018.log`、`/tmp/compare-web-external-bridge-{tests,build}-20261018.log` |
| 原始报告消费 | 38组716层：38首层不适用、678完整，662层无空段、16层有空段、8层有前后覆盖；四值原样映射 | `/tmp/compare-external-bridge-real-consumer-20261018.log` |
| 兼容 / 旧指标 | 缺新块、局部缺项、FAILED各38；旧5数据块/旧消费严格相等，19对A/B与历史一致 | 同上消费日志，最终文案修正后再次执行通过 |
| 页面 | 合成数据6状态+首层，另用真实悬臂原始新块复核第21层；正式组件/样式 | `verification/compare-external-bridge-local.png`、`verification/compare-external-bridge-real-cantilever.png` |
| 模块/差异 | 模块35、归属12944、排除2496、问题0；两仓diff空白检查通过 | `python3 scripts/check_ai_module_docs.py --all` |

真实反例保留：Classic双墩桥第21层10个空段/10个前后覆盖段、最长7.999999 mm；单侧悬臂6个空段/4个前后覆盖段、最长14.506391 mm；独立新生岛1个空段、0个前后覆盖、长22.494233 mm。悬臂路径会从同一支承侧出去再折返，长度是折线弧长而不是8 mm外挑量，因此页面改为“前后有覆盖的段”，明确不能当双端承托或免支撑依据。字段名为稳定合同保留 `two_sided_run_count`，不新增第二套数据/计算规则。

只读/成本：同一有支撑小型bridge关/开报告交替各3次，6次均3800条可执行指令且严格相同，有效配置/保护输入/根result均不变。整进程real中位0.11/0.12秒，monotonic0.148995208/0.150109秒，峰值RSS差622592B（0.59375MiB），报告113827B；测的是整个报告，不是新字段净成本，不能外推复杂层吞吐。证据 `verification/external-bridge-performance/measurement-series/summary.json`，passed=true；此前多岛指令非确定性与初始化超时的旧证据仍保留，简单样例本轮成功不等于其根因已解决。

最终SHA256：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `22a1d159e7c83806adabc9638b3f41726c28d12dc58a26fc4d3b7a24d9dd9abe` |
| `tests/fff_print/test_slice_analysis.cpp` | `2ea6351ea6607212421675a46e26a214bc6776fef702cde01f73303e027c0083` |
| `scripts/test_cli_slice_features.py` | `5a4029394336982536606a488c6c6aa44bdc6f7105ea4f038721e6b3d39234c3` |
| `scripts/tests/test_cli_slice_features.py` | `a51d7edf5179d7debeca1b4722b2250243c53857c39028abf37b6a782f80bd64` |
| `OrcaSlicer` arm64 Release | `720034ece7b4687177330085c4cd9c5f10dddd43e743295cb78077e75d55309b` |
| `fff_print_tests` arm64 Release | `28f8e4dfaeb4348b3c1a24a5c909c19239a729d397d595b8ee34608c7a3eeeaf` |
| 用户根 `result.json`（未改） | `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6` |

GUI/CLI边界与集中验收：原生GUI没有报告入口，不执行这段只读分析；已有普通GUI/CLI同参清单继续适用，本轮未做原生GUI手工或实印。后续新切片任务在Compare查看bridge/cantilever/new-island的第21层，核对四项和前后层打印Z；首层显示不适用，旧报告隐藏新块，失败任务不展示旧分析。不要根据前后有覆盖自动选择免支撑或宣称打印成功。页面验证不替代后端E2E；本轮无Java/Blender/后端接口变更，无业务数据库或打印机操作。两次本地临时HTML、5188服务及本次标签已清理，日志/截图保留；只删除自建临时页，不动用户文件。

### 上一批完成：模型与支撑投影

2026-10-18 再续接完成：分支仍为 `codex/model-feature-analysis-20261018` / HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，保留既有 WIP，未提交/推送。本批“新增模型区域与支撑计划路径投影关系”已实现并通过本机验证：最终原生57项/3651断言、38次真实CLI、10项开关保护、47项Python、169项Web及生产构建。已有路径中轴带过滤/延伸，不冒充通用槽/颈宽，该部分仍待补。未改支撑生成、普通 GUI/CLI 切片、修复、接口、默认参数或自动选择。下方历史测试仅证明各自身份，不累加为当前总数。

下一步：继续桥接两端/实际无承托跨度与模型遮挡关系，再推进曲面分层偏差；同岛槽/连接宽度继续按可靠几何目标设计，不以现有打印路径生成规则冒充测量。跨服务环境E2E、Windows/Linux、复杂模型压力与原生GUI/实物集中验收仍待完成；本机结果不等于全16类完成或商用质量保证。

本批决定：组内新增 `planned_support_projection`，按实例/模型层输出。复用本层截面和路径，目标为 `(本层截面−前层截面)∩本层路径足迹`，首层为截面与路径交集；不称三维朝下面。整板当前 `support_fills` 按实例 shift 统一坐标、按打印 Z 合并，包括普通/树支撑与筏层，不读合并前 backup、不靠 enable_support 开关判断实际路径。按不高于模型本层 print_z 的支撑 Z 降序分配正交集，目标每部分只计最近一个高度；剩余为未匹配面积。每条匹配保留面积/支撑打印 Z/模型名义底面差值，允许零和负差值，不称真实接触间隙或承托合格。非法路径/层位保留未知，空路径是已知空；首层没有前层不等于无效。

实现边界：几何投影不判断中间模型遮挡、按对象打印先后、支撑刚度、桥接锚定或实物接触。不同实例周边支撑可能不同，因此不把单实例值乘数量冒充整板测量。Web 独立折叠展示成对数据，不拼接最小高度差/最大面积、不新增 A/B/CSV 或评分。验证计划：人工精确面积/多高度/多实例/共享筏/非法输入/只读测试，保留原 36 CLI 配置并补树支撑，开关保护回归、真实报告消费和页面；构建与结果及时回填。

实现已冻结进入验证：native 新增 9 cases，CLI 保留旧 36 加树支撑双墙共 38，Python 47 项及 Web 169 项/生产 build 已通过。两处 native 文件仅局部手工对齐（PATH 和常用 Homebrew 路径未找到 clang-format，未安装新工具）。构建前已确认没有其他 ninja/cmake/test 在写该目录，缓存仍为本仓库 arm64 / Release / macOS14 / MeshLib 本地评估 ON。开始 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，日志 `planning/bmr-commercial-design-20261018/verification/build-support-projection.log`，状态运行中；不得使用上一批二进制证明新指标。

首轮验证已完成：构建退出 0；原生 `[SliceAnalysis],[LayerCount],[PrintApplyCache]` 56 cases / 3618 assertions 通过；真实 CLI 全 38 场景退出 0（`verification/support-projection-features/`），本轮未遇到上轮初始化超时。仅说明本轮未复现，旧失败不删除。首轮 app SHA `d6ec7f3fe9ad4c309fc795f4d8c584f0742cbf887ded90da2d5f635103171d97`；cpp `cda1e94697ca87a978329316fe61bdb601c892c34c75485f6fa92849ca25a366`。独立源码审查未见阻断，但手工共享筏夹具不替代真实合并，因此再补实际 Print::process 共享筏集成测试，补完将重建测试目标、重跑原生。开关保护与38份真实Web消费继续运行。

本地页面操作已完成：正式 AutoReport+正式样式，合成数据，正常/负间隔、无匹配、无新增、不完整、旧报告、失败六种状态符合合同，无布局重叠。截图 `verification/compare-support-projection-local.png`；不是后端 E2E 或原生 GUI 验收。临时验证页/服务将在检查结束清理。

开关保护 10 项已退出 0；真实 38 份报告的旧 36 场景 Web 指标与上批严格相等，逐层新字段消费继续审核。小型有支撑 bridge 性能交替 OFF/ON 各3次全部退出0，6次3800条指令与有效参数严格相同，输入/二进制/根 result 均未改变；整进程 real 中位均0.12s，monotonic 0.127298→0.132084s，RSS +0.625MiB，报告102287字节。测的是整个报告开销，不是新字段净成本，也不代表复杂模型吞吐。证据 `verification/support-projection-performance/measurement-series/summary.json`，性能完成后才开始第二次测试构建，避免计时混入编译。

实际共享筏集成已补，测试源码 SHA `02e1bf1aa6b9c841dce487c8d198db4aac47500552aadfc7ce15daa63bbb84e5`；cpp/app未变。开始重建 `fff_print_tests`（`verification/build-support-projection-shared-raft.log`），随后全量定向过滤器重跑（`verification/unit-support-projection-final.log`）。

补充测试首轮失败保留：构建成功，57 cases中56通过，新增集成在 print.validate 即拒绝默认相对挤出机型缺少 `before_layer_change_gcode` 的 `G92 E0`，尚未进入支撑合并，不是算法验证通过。将按真实有效机型补齐夹具层前复位，不删除 validate 或放宽断言；产品 cpp/app 不变。测试修正后再次重建运行，旧日志不覆盖。

补充测试复测完成：仅夹具增加与已有 raft 测试相同的 `before_layer_change_gcode="G92 E0"`，第二次测试构建退出0，`unit-support-projection-final-retry.log` 最终57 cases / 3651 assertions全通过，含实际 Print::process 两对象/三筏层合并后两个对象首层均有正投影，重复报告不改模型/支撑/backup路径和全部配置。cpp/app未改，无需拿新测试二进制替换已验证CLI应用。

### 本批最终证据：支撑投影

| 检查 | 结果 | 证据 |
|---|---|---|
| 最小应用/测试构建 | arm64 Release退出0；补充测试两轮日志保留 | `verification/build-support-projection*.log` |
| 原生最终专项 | 57 cases / 3651 assertions，通过；含55项报告+2项已有回归 | `verification/unit-support-projection-final-retry.log` |
| 真实切片 | 38次通过；原36有效配置不改，加tree/slim双墙 | `verification/support-projection-features/` |
| 默认/开关/输出保护 | 10项通过 | `verification/support-projection-cli/` |
| Python / Web | 47 / 169项通过；Web生产构建通过，保留既有大包提示 | `/tmp/model-feature-support-projection-cli-fixtures-20261018.log`、`/tmp/compare-web-support-projection-{tests,build}-20261018.log` |
| 原始报告到页面消费 | 38组/38实例/716层、53个高度分区原值一致；10层匹配、662层无新增目标、44层未匹配，不作无承托判断 | `/tmp/compare-support-projection-real-consumer-20261018.log` |
| 历史兼容/旧指标 | 旧36场景coverage/source/cross/bridge及旧消费块严格相等；删新字段/局部缺项/FAILED各38验证通过；19对AB不受新字段影响，原18对输出与上批一致 | 同上消费日志 |
| 只读与成本 | 有支撑小样例6次3800条指令/有效配置严格一致；整报告RSS+0.625MiB，real中位0.12/0.12s；不能外推吞吐 | `verification/support-projection-performance/measurement-series/summary.json` |
| 本地网页 | 正式组件/样式的6状态操作及配对面积显示通过，截图已保留；临时HTML、5188服务及本次标签已清理 | `verification/compare-support-projection-local.png` |
| 静态/文档 | 两仓差异空白检查通过；模块35、归属12944、排除2496、问题0（最终测试修正后复查） | `python3 scripts/check_ai_module_docs.py --all` |

最终身份（SHA256）：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `cda1e94697ca87a978329316fe61bdb601c892c34c75485f6fa92849ca25a366` |
| `tests/fff_print/test_slice_analysis.cpp` | `ec754517649727e6c8319eac3c329aed59f8a5c2ac07f56c4a7a927f55ae62d3` |
| `scripts/test_cli_slice_features.py` | `df9bbc9d118c6df95484af8dfce70bcc7741f0877ee56db5bd60a3694713df41` |
| `scripts/tests/test_cli_slice_features.py` | `6747da80ca8df91aea686275d2001ee337df74f3b61d29155f17186c6af0651a` |
| `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `d6ec7f3fe9ad4c309fc795f4d8c584f0742cbf887ded90da2d5f635103171d97` |
| `build/arm64/tests/fff_print/Release/fff_print_tests.app/Contents/MacOS/fff_print_tests` | `b4e649065b1233a9a276d341f17bbb38a4e9f24e70470d41bbd3939b9dfd6039` |
| 用户根 `result.json`（未改） | `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6` |

GUI/后端边界：原生 GUI 没有新增自动分析/改参入口，普通切片不调用报告，原先集中 GUI/CLI 同参清单继续适用，不把网页局部组件当原生GUI回归。PDS现有Map读取和Compare原始JSON保留新增字段，无需新增接口/DTO/迁移；这次未运行数据库/服务E2E。后续集中页面验收用新任务切 bridge/cantilever/new-island 普通/树支撑及 Cura raft，查看按实例/层配对数据；关闭支撑仍保留原跨层风险数据，旧报告隐藏新块、失败不展示旧分析。远处下方支撑可有投影但不等于承托；零/负差值由人工确定性测试覆盖，当前38真实样例分区均为正差值。旧初始化超时及多岛指令非确定性尚未解释，本批简单样例成功不覆盖这些边界。

### 上一批完成：分离截面间距

2026-10-18 续接：已完成“分离截面区域最近间距”实现和本机验证，仍在 `codex/model-feature-analysis-20261018` / `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，保留全部既有 WIP，未提交/推送。实际改动仅 native 报告及测试、现有 CLI 两脚本、Compare 原报告展示/消费/测试与文档；普通 GUI/CLI 切片、服务接口、修复、参数和自动采用规则不改。最新是 47 项原生、36 次真实 CLI（复测）、10 项开关保护、39 项 Python、158 项 Web，不与下方历史结果叠加。

下一步：继续同岛槽/连接细部的可靠测量设计，再推进支撑接触与桥接两端、曲面偏差；没有通用窄颈宽度实现，不把现有中轴生成规则改名冒充测量。跨服务 E2E、目标 Windows/Linux、复杂模型性能和原生 GUI/实物仍列集中验收。一次初始化超时以及多岛指令非确定性仍有未解释边界，见下文。

决定：逐层新增 `section_min_island_gap_mm`，只量同一切片组、同层不同正面积截面岛之间的最近边界距离，包含孔边界；按岛建立 double 毫米线段树，双向顶点到边段查询，岛对包围盒下界剪枝。完整且少于两个岛时不适用（null），本层不完整时未知（null），消费者结合已有 `complete` 与岛数区分，合法 0 不认定为缺陷。Web 独立验证新字段，不破坏原六项/开放空白两项，也不新增 A/B/CSV。本批不把同岛 U/C 槽、连接颈或三维活动间隙冒充已测；现有 medial_axis 带路径生成过滤/延伸，不直接复用为无阈值局部宽度。

验证覆盖人工几何精确真值、单岛/孔内岛/接触/不完整/重复只读；原 34 CLI 加孔内岛双墙；原开关/保护回归、Web 正常/零/不适用/旧字段/不完整及实际报告消费。旧图谱未覆盖本文件，理解/差异审查按当前源码复核，不初始化图谱或用旧节点生成不可靠影响结论。

最小构建已退出 0：`cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，日志 `planning/bmr-commercial-design-20261018/verification/build-island-gap.log`。已核对缓存工作目录、arm64 / Release / macOS14 / MeshLib 本机评估 ON，无其他构建写同一目录；用户运行的 `/Applications/OrcaSlicer.app` 不动。构建仅既有编译警告，无新增构建失败。

源码交叉核对补充：`PrintObjectSlice.cpp:1362–1379` 将象脚层的 `lslices` 恢复成补偿前参考截面，所以该量明确不是象脚补偿效果或路径残留间隙。单岛快速跳过；多岛每层建树、岛对最坏二次遍历，用包围盒下界跳过无改善可能的配对，未加人为质量阈值。

构建退出 0，原生 47 项/3447 断言通过（45 SliceAnalysis + 2 已有回归）。首次真实 CLI `verification/island-gap-features/` 前 22 场景通过，第 23 个 Cura raft Classic 在 180 秒超时；stdout 仅 `Initializing StaticPrintConfigs`，无报告/业务错误栈，脚本已回收进程，不能归因于间距算法或算作通过。保留原日志，执行一次全套新目录复测 `verification/island-gap-features-retry/`，不提高超时或放宽断言；若再次停住，采集进程状态继续定位。

复测已结束：同一源码/二进制的全部 36 场景通过，新增孔内岛双墙 12 层间距 1 mm/面积 88 mm²，原分离块 0.5 mm；原默认/关闭/开启及保护 10 项通过（`verification/island-gap-cli.log`）。不抹去首次初始化超时或宣称根因已解决。Web 实际组件用正式样式和合成请求做正常 0.2 mm/零/不适用/缺字段/旧报告/失败重试六场景，布局无重叠，截图 `verification/compare-island-gap-local.png`；这是本地组件验证而非后端 E2E，临时 HTML、标签及 5188 服务已清理，用户原页面保留。

### 本批结果：分离截面间距

| 检查 | 结果 | 证据 |
|---|---|---|
| 原生测试 | 47 项/3447 断言通过（45 定向 + 2 原有） | `verification/unit-island-gap.log` |
| 真实 CLI | 复测 36 次通过，原 34 配置保留 | `verification/island-gap-features-retry/` 与同名 `.log` |
| 默认/开关/保护 | 10 项通过，简单样例指令与有效参数一致 | `verification/island-gap-cli/` 与同名 `.log` |
| Python | 39 项通过 | `/tmp/model-feature-island-gap-cli-fixtures-20261018.log` |
| Web | 158 项和生产构建通过，保留既有大包提示 | `/tmp/compare-web-island-gap-{tests,build}-20261018.log` |
| 原生报告消费 | 36 组间距一致：14 有值/22 不适用；40 源部件一致，旧 34 场景原八项/源部件/旧十指标保持，18 对 A/B 保持 | `/tmp/compare-island-gap-real-consumer-20261018.log` |
| 兼容与状态 | 旧删新字段/部分缺失/FAILED各 36 场景通过；原八项独立保留或按失败隐藏 | 同上消费日志 |
| Computer Use | 实际组件六状态与正式样式核对通过 | `verification/compare-island-gap-local.png` |
| 模块与差异 | 35 模块、12944 归属、2496 排除、问题 0；Orca/Web 空白检查通过 | `check_ai_module_docs.py --all` 与 `git diff --check` |

性能复用 64 方柱/20 层样例，关/开报告交替各三次，新独立进程且无并行构建/CLI；整进程耗时中位 0.28/0.63 s，单调时钟 0.316498/0.672146 s，峰值 RSS 112.3125/114.9375 MiB，报告 70,559–70,580 B。全六次有效参数一致，开启时各层 64 岛/576 mm²/间距 1 mm 均通过；源码/二进制/模型/预设/root result 哈希不变。整份报告增加约 0.35 s/2.625 MiB，不是新单字段的独立成本或大模型吞吐保证。严格打印指令仍不等：6 个哈希不同，关闭之间也不同（51,958–51,972 条），因此 `passed=false`，不归因报告开关、不标完全等同。证据 `verification/island-gap-performance/measurement-series/summary.json` 与 `run.log`，只测一轮、不修改路径排序。

当前 SHA256：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `96d4e3ff7315013da5d847b7f2715bfed58278cf9f5b8f8a2b376173969ca017` |
| `tests/fff_print/test_slice_analysis.cpp` | `fad2809827bfa21ed6d40aa41b7a71c2b012f37f3241a70cfc6030ac97cc8915` |
| `scripts/test_cli_slice_features.py` | `13cf10b2eef87e7c5fbce5a4a7cb5b108d93445081a3cd53f3bcceeabb37ad42` |
| OrcaSlicer | `a81b90003b56dd51f362a712c5dfacc4e5b4a4f07be5f0fcb0bac19917beac0f` |
| fff_print_tests | `27ba5f995ff5553f84ff2c5a5986649252f06a5421c7ed84138eddf8833a41cf` |

`verification/` 均位于 `planning/bmr-commercial-design-20261018/`，不入库。根 `result.json` 仍为 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。本批无 Java/Blender 服务逻辑变更、无业务数据库操作或自动选参；原生 GUI 不进入该只读报告路径，最终 GUI/CLI 同参集中验收见下面清单，不把网页验证替代桌面 GUI。

收尾交叉审核未发现本批确定性阻断：native 含孔/闭边、双向点线段与单位；CLI 真值/零/null/旧配置；Web 按组单实例最小值和新旧字段独立性均已复核。该审核不替代上述运行，也不消除初始化超时、复杂多岛非确定性或未执行平台/实物验收。所有本轮构建、CLI 与临时预览进程已结束，无新提交。

### 上一批完成：开放空白观察

2026-10-18 用户要求继续后续工作，已完成“原开放空白被计划路径围住”的增量和本机验证。分支/HEAD 未变，保留所有既有工作区改动，本批只改 native 两文件、CLI 两脚本及 Compare 原报告组件/消费/测试和文档。不改普通切片、GUI 参数、模型修复、服务接口或自动采用规则，未提交/推送。本批最新为 43 项原生、34 次真实 CLI、149 项 Web 测试；下方各历史批次不能叠加为本次测试数。

本批算法：从源截面 S 和计划足迹 P 的标准多边形孔分别得到有界空白 H(S)、H(P)，排除嵌套材料；对 H(P) 减去 S 与 H(S)，只留下原本与外界相通的空白 N。逐层新增 `planned_enclosed_exterior_region_count`（含正面积 N 的 H(P) 区域数，不是 N 碎片数）和 `planned_enclosed_exterior_area_mm2`（N 总面积）；本层不完整时两项 null。不引入围框、closing 半径或质量阈值，也不拿源实体替计划路径补围壁。完全填满槽后可能没有剩余空白，因此 0 不保证槽保留；不代表三维贯通或实印质量。

验证覆盖人工截面/路径正反例，原 28 次 CLI 加宽 U 槽/窄 U 槽/C 形槽各双墙到 34 次；Web 独立判断新两字段，不让旧六字段报告退化为未知。已重新最小构建并运行原开关/回归、几何脚本、Web/Python 测试及实际组件验证。CodeGraph MCP 未暴露，CLI 可读但提示 6 新增/2 修改待同步；旧知识图谱无 SliceAnalysis，以当前源码复核，没有初始化图谱。理解/差异审查技能用于确定受影响范围，但过期图谱未被当作当前实现依据。

最小构建命令：`cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，退出 0，日志 `planning/bmr-commercial-design-20261018/verification/build-open-space.log`。构建缓存仍为 arm64/Release/macOS14/MeshLib 本机评估 ON，未与其他构建并发写目录，用户运行的 `/Applications/OrcaSlicer.app` 不动。新槽口测试单独关闭尺寸补偿以锁定截面真值，不修改原 28 场景参数。

### 本批结果：开放空白观察

| 检查 | 结果 | 本机证据 |
|---|---|---|
| 原生测试 | 41 项 SliceAnalysis + 2 项已有回归，共 43 项、3387 断言通过 | `verification/unit-open-space.log` |
| CLI 默认/开关/输出保护 | 10 项通过，简单模型可执行指令/有效配置与旧结果保持 | `verification/open-space-cli/`、`open-space-cli.log` |
| 真实模型切片 | 34 次通过，新三槽双墙均保留 12 层和已知截面面积 | `verification/open-space-features/`、`open-space-features.log` |
| Python 夹具及合同 | 33 项通过 | `/tmp/model-feature-open-slots-cli-fixtures-20261018.log` |
| Web 测试/生产构建 | 149 项通过，生产构建成功；既有大包提示保留 | `/tmp/compare-web-open-space-tests-20261018.log`、`/tmp/compare-web-open-space-build-20261018.log` |
| 原生报告消费 | 34 报告新两项与 raw 一致，38 源部件字段一致；原 28 场景六项/源部件/旧十指标不变，17 对 A/B 不变 | `/tmp/compare-open-space-real-consumer-20261018.log` |
| 兼容和状态 | 旧删新两项保留六项、缺一个新字段仅新两项未知、失败隐藏，分别 34 场景通过 | 同上消费日志 |
| Computer Use | 实际 AutoReport 的正常/零值/不完整/旧报告/失败重试五场景通过，布局无重叠 | `verification/compare-open-space-local.png` |
| 维护 | 35 模块、问题 0；差异空白检查通过 | 模块检查与 Git 差异 |

原生人工 U 槽的计划围合空白是 16 mm²，缺少一面打印墙、真正开口或全部填满时为 0；嵌套实体扣除及一孔内多碎片只计一次均通过。三种真实槽夹具在 Classic/Arachne 下新两项都为 0，只表示本次没有观察到此类剩余围合空白，不等于槽口通畅或模型可打印。没有为了制造正值调整算法或把正常微小面积当缺陷。

稀疏填充性能另复用 12×8×4 mm、20 层、grid 15%、无顶底实体层的样例，关/开报告各三次，独立串行执行且未与构建/切片并行。`/usr/bin/time -l` 的整进程中位 0.10/0.11 s；单调时钟中位 0.1429235/0.1441842 s；峰值 RSS 中位 63.078/62.797 MiB，差异属于小样本波动，不宣称改善。报告 65,643 B；六次有效配置及 1162 条可执行指令完全相同，所有输入/源码/二进制/root result 哈希不变，报告仅开启时存在。证据 `verification/open-space-sparse-performance/measurement-series/summary.json`，`passed=true`。小样例和 0.01 s native 计时分辨率不证明大模型吞吐；既有 64 多岛样例关/关也不同的指令非确定性边界仍保留，未在本批修改路径排序。

当前 SHA256：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `7b7ff129b256cc0d16af4063091cec5d805e4cafd3092635cb3447a28ab39eaf` |
| `tests/fff_print/test_slice_analysis.cpp` | `567c98d513d5dd099ccfaa0d214060afb84bfbdf34f49ec0a40b2f46a8f323d2` |
| `scripts/test_cli_slice_features.py` | `f2ca6582720b684e0e616aaefd69d0889dd925c699a069e86117dbf2356c47e6` |
| OrcaSlicer | `9880b37ca4439480347b9a9fdff7aa2af4b86d574e485c54f0be59b0f793ffe7` |
| fff_print_tests | `26167d310fe4a64b1265e7a8b2aaffe8f873ce0c6faa97e92c15e21a2726f649` |

所有 `verification/` 均在 `planning/bmr-commercial-design-20261018/` 下，不入库。Computer Use 用真实组件和合成请求壳，实际 native 数据另经消费函数核验，不冒充后端 E2E。临时 `.codex-open-space-preview.html`、本轮标签和本地 5188 服务已清理；用户页面和原文件保留。根 `result.json` 仍为 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。

下一步继续局部间隙和连接细部，再补模型/支撑接触、桥接两端及曲面偏差；当前观察不替代语义槽识别、开口全填检测、最小间隙或三维连接。服务端/原生 GUI/目标 Windows/Linux 和大模型/实物仍按下面集中验收清单执行，不把当前单元/CLI/网页通过写成整套商用验收。

### 上一批完成基准

已完成源部件 Z 采样和逐层连接/闭孔空白两批只读统计，当前源码已通过 Mac 最小构建、原生/CLI 回归及 Compare 实际组件验证。分支为 `codex/model-feature-analysis-20261018`，HEAD 为 `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，未提交/推送；没有改首层参数、垫层或修复流程。仍处于 16 类方案的局部结构分析阶段，不是整体完成。

下一步按方案补明确局部目标、开放槽缝及连接细部的观测，再做模型/支撑承托与桥接两端关系、曲面局部偏差，最后统一跨服务/平台/性能验收。先审核现有数据可复用性再编码，不把二维区域计数冒充三维连接或质量结论；本阶段保持分析和对比，不新增自动选择与成本阈值。每批随实现完成本地测试，用户最后集中检测。

### 本批决定：逐层连接与闭孔空白

- 沿用 `planned_layer_coverage.layers`，增加六个非负计数：目标截面岛数、单个目标岛内有多块计划覆盖的岛数、同一计划覆盖块跨多个目标岛的块数、闭合孔空白区域数、完全被计划宽度占据的孔区域数、剩余空白分成多块的孔区域数。本层不完整时六项全部 null，不建另一套业务状态。
- 复用同一截面、路径足迹和孔负空间：岛内连接先裁到目标；跨岛覆盖不能先裁，否则跨缝部分被删；闭孔按剩余正面积区域判断。没有引入距离阈值、强度/质量评分、三维细颈算法或开放槽口识别。
- 正常稀疏填充也可能形成多块，窄缝处正常线宽也可能跨目标；真实通孔样例还有很小的非零侵入。只展示原始事实，不把非零计数直接认定为断裂、粘连或堵孔。Web 按组单实例取各自单层最大值，不跨层相加当部件/孔数量，不新增 A/B/CSV。
- 原生人工截面/路径构造可算真值；CLI 保留原 26 次，补窄颈哑铃双墙到 28 次，只锁定已确认的几何及计数合同，不先假设 Arachne 必然修好它。
- 审核一并发现逐层覆盖完整性原先只检查路径，已与相邻层/源体积统一为层几何有效且路径有效；非法高度、非有限层位、切片错误不能报完整。已加 7 类无效层反例，不修改任何实际切片。
- 当前代码和验证完成，结果与构建身份如下；后续源码变动后须重新验证受影响目标，不能沿用本表证明新代码。

### 当前批次验证结果

| 检查 | 结果 | 本机证据 |
|---|---|---|
| Mac arm64 Release 最小构建 | OrcaSlicer + fff_print_tests 成功；macOS 14、MeshLib 本机评估 ON | `verification/build-section-connectivity.log` |
| 原生与既有回归 | 37 项 SliceAnalysis + 2 项 LayerCount/PrintApplyCache，共 39 项、3301 断言通过 | `verification/unit-section-connectivity.log` |
| CLI 开关与保护 | 10 项通过，简单样例默认/关闭/开启报告的可执行指令与有效配置一致 | `verification/section-connectivity-cli/`、`section-connectivity-cli.log` |
| 真实几何切片 | 28 次通过，包含双墙、支撑开关、Cura V1 重叠、源部件薄片及窄连接 | `verification/section-connectivity-features/`、`section-connectivity-features.log` |
| Python 夹具与断言 | 28 项通过 | `verification/unit-section-connectivity-scripts.log` |
| Compare Web | 140 项及生产构建通过，既有大包警告保留 | `/tmp/compare-web-connectivity-tests-20261018.log`、`/tmp/compare-web-connectivity-build-20261018.log` |
| 真实报告消费 | 28 份报告六项逐层最大值一致；32 个源部件字段一致，原 26 场景源部件数据未变；14 对原有 A/B 指标不变 | `/tmp/compare-connectivity-real-consumer-20261018.log` |
| 历史/异常展示 | 28 份旧数据隐藏新块、28 份部分数据为未知、28 次最新失败不沿用旧指标 | 同上消费日志 |
| Computer Use | 实际组件正常/不完整/旧报告/失败重试 4 场景通过；0 与未知区分，截图无重叠 | `verification/compare-section-connectivity-local.png` |
| 最终维护检查 | 35 模块、12944 归属文件、2496 明确排除、问题 0；Orca/Web 差异空白检查通过 | `python3 scripts/check_ai_module_docs.py --all`、两仓库 `git diff --check` |

当前实测窄连接夹具中，Classic 有单个目标截面岛对应多块计划覆盖的层，Arachne 没有；只证明该夹具的二维路径区别，不自动判定实物断裂或 Arachne 对所有模型更优。开放槽口、最小间隙、三维连接和实印强度仍未完成。

性能复测仍用 64 个分离方柱、20 层，报告关/开交替各 3 次，未与其他构建/切片并行。整进程中位耗时 0.341 → 0.679 s（整份报告增加 0.338 s，约 +99%）；RSS 中位 112.44 → 116.41 MiB（+3.97 MiB），报告约 66.3 KB。六次运行、报告校验和有效配置检查通过；但关闭报告的三次打印指令自身也不同，开启三次也不同，严格指令等同性仍为 `passed=false`，不写成压力样例等同性通过。证据为 `verification/section-connectivity-performance/measurement-series/summary.json`。这里只是一个小样例的整份报告成本，不是本增量独立成本或大模型吞吐保证；复杂模型耗时和非确定性归因保留在整体验收范围。

当前 SHA256：

| 文件 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `d8fa468c6c7bc2f1b5f5d53276826551a9ec5a1132118a61fb9cb7bf592ec136` |
| `tests/fff_print/test_slice_analysis.cpp` | `1082a6877558de27d8fdd5e983ccf8868fe40d7960aabe83dd0e62455595b0ee` |
| `scripts/test_cli_slice_features.py` | `51a5866b668662978503022349b1f8bbcc705cbec5ff30d3eaf9729e24303e22` |
| OrcaSlicer | `4f93066456c26e595b369d12921d93552ab205044310f2631a63775272f43596` |
| fff_print_tests | `561019b212d37d8964af0ca30e44663ad315fe4b0009c0b65882551933421b92` |

`verification/` 位于 `planning/bmr-commercial-design-20261018/`，不入库。根 `result.json` 保持进入本批时的 SHA256 `aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`。临时预览 HTML、创建的浏览器标签及 5188 本地服务已清理；保留用户原页面和本机截图。没有修改后端接口/数据库或重复运行未改的 Java 服务测试；前端使用本地请求夹具，不冒充后端 E2E。

### 最后集中验收清单（尚未执行）

1. 同一最终模型、变换、资源和有效配置分别在原生 GUI 与 CLI 切片，核对预览、打印指令与报告；普通 GUI 不生成分析报告，本批无新增 GUI 控件。
2. 开启 Cura V1 垫层，确认模型首层的速度/线宽等参数仍作用于模型首层，第二层合法 Z 重叠保留；这轮真实 CLI 已验证重叠，原生 GUI 交互尚未重验。
3. 测试环境执行真实 Compare 任务，核对报告正常/缺失/不完整/失败重试以及修复参与的对照提示；所需 PDS 可空报告列迁移尚未执行，不连接业务数据库代替部署验收。
4. 在目标 Windows/Linux 构建并复跑 CLI，对实际业务的大模型和多部件工程测量报告耗时/内存；排查多岛样例指令非确定性，不能仅凭关/开哈希不同断言是报告改变了路径，也不能据此宣称完全等同。
5. 全部软件工作收尾后再交付用户集中检测；实物打印、公差、强度和机料标定仍另验，不把软件指标通过写成可打印保证。

### 上一批：Z 采样验证历史（当前回归已覆盖）

- Mac arm64 Release：`verification/build-source-volume.log` 退出 0；30 项 SliceAnalysis + 2 项已有回归，共 32 项、3071 断言通过（`unit-source-volume.log`）。
- CLI：原开关/保护 10 项通过；扩展几何 26 次通过；Python 23 项通过，分别见 `source-volume-cli/`、`source-volume-features/`、`unit-source-volume-scripts.log`。
- 两种墙算法下 Cura V1 第二模型层 gap=-0.1 mm、complete=true，之后 gap=0，实际垫层路径/耗材均存在；首层参数/排层实现没有修改。
- 标准 3MF 中主体与两片薄片为三个源体积；保留 20 层。薄片 [1.21,1.26] mm 未被任何采样面穿过，源截面/最终区域/有路径均 0；[1.28,1.32] mm 被一层采到，三项均 1。不是合并 STL 内自动特征识别。
- Web 133 项及生产构建通过；26 份真实报告、30 个源部件逐字段一致，13 对原有 A/B 指标保持，26 个历史缺字段/失败重试场景不沿用旧值。日志 `/tmp/compare-web-source-volume-tests-20261018.log`、`/tmp/compare-source-volume-real-consumer-20261018.log`；请求外壳是本地夹具，不是后端 E2E。
- Computer Use 验证正常/不完整/旧报告/失败重试四场景；实际组件显示 0 与未知有区别、薄片范围不被两位小数抹平，截图 `verification/compare-source-volume-local.png`。初次临时预览使用双 Vue 运行时导致测试壳不更新，已改为与组件同一 Vue 的 render 函数，重新验证；不属于产品组件问题。
- Z 批次 SHA256：源码 `80929af74af019a4e9c431797c3af6eefa7b8717415b9b1b56373ba503542484`；测试 `cc07aaa6bf50efc61ef9ca74f125f1217c3568539c1cefe9047511b3cbbc1de3`；Orca `02d24d981283fd1dc29fb88964026365e1ec9d51b60c7069fa7e35e759290a48`；fff_print_tests `4f1dd5ccb88f3b27c1e1e599d07ace7711e9266fb1d66b3bf4247010c6e2c6e4`。根 result.json hash 未变（见下方历史身份）。

新一轮进行中（用户“继续审核，然后继续规划，接着继续编码”）：分支仍为 `codex/model-feature-analysis-20261018`，HEAD `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，上一批源码/测试和原有用户改动保留。先独立复核 native/Web/调用边界，再确定明确目标的 Z 采样指标是否能复用现有数据；不把全模型高度、二维包围盒或旧墙评分当局部细节。新增代码后重新最小构建及回归，不提交/推送，不连接业务服务或发起打印。图谱仍未收录 SliceAnalysis，CodeGraph 提示 6 新增/2 修改，按源码定位。

### 进行中决定：合法层间重叠与源部件 Z 证据

- 审核确认 Cura V1 的 `raft_layer_0_z_overlap` 会令第二模型层底部低于前一层顶部，这是生产排层逻辑允许的重叠；不能仅因 signed gap 为负就拒绝平面投影统计。已修正报告判断并补原生用例，待当前源码构建和真实 Raft CLI 回归；不改变排层或打印路径。
- 下一批只按文件中已有的正向 `ModelVolume` 统计。`source_volume_sampling` 记录切片空间 Z 范围、穿过范围内部/恰在边界的实际采样面数量，以及源体积截面、与最终模型区域重叠、范围内有计划路径的层数。不是自动识别独立文字/浮雕；合并到一个 STL 中的局部细节仍未实现。
- 复用 `firstLayerObjSlice()` 的全层源体积切片缓存及已有路径足迹，既不重切，也不按包围盒猜路径。缓存是经过现有切片模式和闭合设置的源截面，不是未处理的三角形交线；负体积等仍可能从最终区域中删除它。路径相交只说明该位置有路径，不证明材料归属、完整覆盖或打印合格。
- 缓存可能比保留模型层多（末端空层被删），本批只统计保留层；共享对象通过既有同序源体积关系找到数据所有者。加载旧切片缓存缺少源截面时对应统计为 null，不补 0。Z 计算沿用切片器 float 精度，不引入任意业务容差。
- Native、Web 和 CLI 合成夹具并行实现；Web 仅单组/单实例展示，不增加 A/B、CSV、择优策略或后台字段。新增测试覆盖薄片命中/未命中、变换、缓存缺失/共享、合法负间隙及旧报告兼容，完成后统一最小构建和回归。
- 用户补充确认此前 Cura V1 改造目的为模型首层参数不被垫层占用。本批不修改该参数归属、排层高度或重叠量，仅修正只读统计的有效性判断；`PrintObjectSlice.cpp`、`Print.cpp`、`PrintObject.cpp` 本轮无改动。
- 当前原生 30 项 SliceAnalysis 测试待构建；Web 133 项及生产构建通过，Python 夹具 23 项通过。开始 `cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6`，日志 `verification/build-source-volume.log`；后续用新程序运行既有回归和扩展后的 26 次真实 CLI 切片，不以纯夹具测试替代。

2026-10-18 后续一轮已完成“相邻模型层投影与桥接路径量”增量及本地验证。用户授权按原方案继续；仍先分析和对比，不新增自动择优/阈值/客户端接口。分支、HEAD 与上轮相同，既有未提交改动保留，本轮未提交、推送、部署或打印。下一批先做有明确目标范围的 Z 细节采样/有效路径层数，再做局部连接与孔缝；联合支撑/桥接锚定和曲面偏差另补。每批本地自动化随实现完成，最后统一环境验收，不逐项等待用户手测。

### 本批决定：相邻模型层投影与计划桥接路径

- 导出后的 `Layer::lslices` 可用于相邻模型层投影；`SupportLayer` 不保留普通支撑生成过程的完整接触来源/间隙证明，`unsupported_bridge_edges` 对应填充代码已注释，不能借字段存在宣称检查完成。本批先提供原始投影及计划路径量，联合承托/锚定仍待做。
- 新 `cross_layer_geometry` 按组、单实例保留实际 `slice_z`、打印 Z、层高、前层 Z/间隙、重叠与超出面积、在前层无正面积重叠的完整截面岛数量和面积。首模型层比较项为 null（不适用）；错误/非法层序不冒充已完成。不是浮空缺陷数量，不包含支撑/筏层，也不把正 Z 间隙当接触。
- `planned_bridge_paths` 分外部桥接填充、内部桥接填充、悬垂墙，记录有效路径片段数、计划折线总长和逐层足迹面积之和；不把片段当桥数量、路径长度当跨度，面积不跨 Z 合并。沿用既有路径遍历，不重复切片、不改变路径。
- Web 只在原报告逐组展示新指标，不新增比较策略；旧报告缺值显示未知，首层不适用不当检查失败，其他层不完整则不展示成完整汇总。暂不算跨层 B−A，不用原覆盖层网格相同假设替代支撑条件一致。
- 验证安排已执行：原 CLI 默认/关闭/开启回归，追加双端桥、单侧外挑及上方独立岛，有/无支撑反例；当前源码最小构建后统一执行，结果见下表，不沿用上轮二进制证明新指标。
- 图工具状态：CodeGraph MCP 未暴露，CLI 存在但不在默认 PATH；既有索引 status 提示 6 个新增/2 个修改未同步，当前相关增量以源码核对。本轮没有初始化/复制图谱，历史 Understand 图只用作导航。

### 本批验证结果

| 检查 | 结果 | 本机证据 |
|---|---|---|
| Mac arm64 Release 最小构建 | OrcaSlicer + fff_print_tests 成功；缓存 macOS 14、MeshLib 本机评估 ON | `verification/build-cross-layer.log` |
| 原生指标与既有回归 | 23 项 SliceAnalysis + 2 项 LayerCount/PrintApplyCache，共 25 项、2958 断言通过 | `verification/unit-cross-layer.log` |
| CLI 开关与失败语义 | 10 项通过；简单样例开启/关闭/默认的可执行指令、有效配置和旧 JSON 一致 | `verification/cross-layer-cli/`、`cross-layer-cli.log` |
| 真实几何切片 | 原 10 次 + 桥/悬挑/新岛 × 两种墙 × 支撑开关 12 次，共 22 次通过 | `verification/cross-layer-features/`、`cross-layer-features.log` |
| 夹具与断言工具 | 15 项 Python 单测通过 | `verification/unit-cross-layer-scripts.log` |
| Compare Web | 124 项测试及生产构建通过；原有大包警告保留 | `/tmp/compare-web-cross-layer-tests-20261018.log`、`compare-web-cross-layer-build-20261018.log` |
| 原生报告消费 | 22 份真实报告逐组一致；11 组原有 A/B 指标不回归；8 份历史报告隐藏新块 | `/tmp/compare-cross-layer-real-consumer-20261018.log` |
| Computer Use | 正常/不完整/只有首层/旧报告 4 场景通过；截图无重叠，未知不补零 | `verification/compare-cross-layer-local.png` |
| 维护检查 | 模块 35 个、问题 0；差异空白检查通过 | `python3 scripts/check_ai_module_docs.py --all`、`git diff --check` |

以上 `verification/` 位于 `planning/bmr-commercial-design-20261018/`，为不入库的本机证据。Web 使用实际 Vue 组件和本地合成报告验证交互；真实 native payload 另经消费函数核对，请求 envelope 为测试夹具，不冒充真实后端 E2E。PDS/Compare 后端原 JSON/Map 原样传递，无需本批 Java/DB/API 增量；未再运行未修改的后端测试或连接业务数据库。

临时页面 `.codex-cross-layer-preview.html`、本轮浏览器页与本机 5188 服务均已清理，用户原页面保留；截图仍可复查。

真实截面在采样 Z=4.1 mm：桥梁的重叠/超出面积为 48/48 mm²，悬挑为 24/48 mm²，新生岛为 24/24 mm²，其中无前层重叠独立岛为 1 个、24 mm²。开启支撑不改变这些纯模型几何读数；脚本同时确认支撑层和最终 G-code 支撑路径/体积实际存在。无支撑桥样例的计划桥接与最终 G-code 桥角色均有正长度，不据此宣称桥端锚定或打印安全。

性能复测沿用 64 个分离方柱、20 层的小样例，交替关闭/开启报告各 3 次，未与构建或其他切片测试并行：整进程中位耗时 0.294 → 0.622 s（整份报告额外 0.327 s，约 +111%），RSS 中位 111.80 → 115.73 MiB（+3.94 MiB），报告约 57.1 KB。证据在 `verification/cross-layer-performance/measurement-series/summary.json`。六次成功、报告校验通过、有效配置一致；但指令仍有 6 个哈希，关闭报告自身也不稳定，故 summary 的严格指令等同性 `passed=false` 如实保留，不能把这个压力样例写成指令等同通过。该数据不代表大模型性能已验收，也不是本批相对上批单独新增成本。

本批当前身份（SHA256）：

- `SliceAnalysis.cpp`：`677b537ed8a44f60ea4655461267c886827559aa3e426c2427824cbc5c08a616`
- `test_slice_analysis.cpp`：`1e322a0e0100474c1e1d1eb4357644104c6d48e4776311dd0dc3fb8dec5cd764`
- Orca `.app/Contents/MacOS/OrcaSlicer`：`c007b92465cfade3378e9219534cd1ea26ad953ca25c835294a6d4675033dfa6`
- fff_print_tests `.app/Contents/MacOS/fff_print_tests`：`f32266a363d3169690fd26cee738aa4f570670532eed8490b4f08c73c8b50f14`
- 用户原 `result.json`：`aa3a68f10277867d33654ab94e7eb25a99e56899b481a35c6e7ea8e3d368acd6`，CLI/性能脚本前后保护校验均通过。

GUI/CLI 边界：本批仅显式 CLI 报告和 Compare 管理页面有新展示；桌面 GUI 普通切片不进入报告计算，也无参数/翻译变更，原生 GUI 交互未重复手验。最后统一验收时，用相同模型/摆放/有效配置核对 GUI 与 CLI 路径；在真实 Compare 实验检查新旧报告、不同层高与修复参与的提示；在目标 Windows/Linux 构建并复跑，当前仅 Mac 已验证。联合承托/锚定、大模型性能及实印质量仍未完成，不把 16 类指标整体标成已交付。

## 上一批记录（保留历史证据）

2026-10-18 本轮“分析与对比”软件增量及本地审核/回归已收尾：Orca 补可测量指标、Main 修复隐式模板姿态适用条件、Compare Web 接入原报告和 A/B/CSV，不新增自动择优。用户确认暂不设置时间/耗材上限；由用户最后统一测试，不逐批等待手验。七仓库均为 `codex/model-feature-analysis-20261018`，本轮未提交、推送、部署、操作业务数据库或打印机，原有 WIP 保留。

完整方案仍有 16 类，不以本轮的计划足迹指标宣称全部完成：局部三维颈宽/连通、曲面阶梯误差、实际桥接锚定/浮空与辅助附着联合检查仍未实现，机料标定/目标平台/真实服务联调也未完成。对应状态在[方案 2.1](../../../doc/bmr-print-optimization.md#21-当前实现范围仍是-16-类不合并删减)。本轮交付的是可以在原业务中观察和对比的证据链，不是自动质量验收或全部商用效果达标。

本轮决定与进行中修改：

- 原生共享只读报告补逐层计划路径足迹、整岛无路径、孔区侵入、配置后实体填充区域未覆盖面积；不是实物覆盖率，稀疏填充及低密度顶底面的有意空白不当缺陷。G-code 补导出后指令挤出段长度、逐层估时序列、接缝记录数。仅显式报告调用新增计算，GUI/默认 CLI 路径算法不变；当前源码已重新构建验证。
- Compare 后端已原样返回报告，不重复增加服务/字段；Web 在原报告/结果/CSV展示实际指标及 B−A 差异，保留 B 切片内修复时“非纯参数对照”的边界。Web 从已核对干净的 master 派生同名功能分支，测试不访问真实后端。
- 审核发现 Main 历史隐式模板缓存只含模型 URL/缩放、不含姿态。本轮限制隐式模板采用与现有墙建议使用同一模型/朝向适用条件，防止翻面后沿用原朝向推荐；客户端显式模板、六参数及原默认 raft/0.25 场景策略保留，不重跑分析或改接口。
- 原 PAM 仍只有既有支撑、附着、窄面墙建议三组规则；未新增桥速/风扇/冷却阈值。用户明确回复“暂不设上限，先完成分析和对比”：本轮不实现新增自动择优，不暗设质量评分或时间/耗材阈值，保留既有模板匹配/墙建议。

本轮首轮验证已完成：增量构建 OrcaSlicer/fff_print_tests 退出 0；`[SliceAnalysis]` 14 项/217 断言、`[LayerCount],[PrintApplyCache]` 2 项/2487 断言、原 CLI 10 项、新增四种合成几何 × 两种墙生成器共 8 次切片均通过。日志在 `planning/bmr-commercial-design-20261018/verification/` 的 `build-features.log`、`unit-features.log`、`regression-features.log`、`cli-features.log`、`shape-features.log`。已有夹具已补最终 G-code 凸起细笔画逐层核对，Python 5 项通过。

新增“模型首层计划底面占用面积”和“名义流量/当前材料配置上限”也已验证；无上限/未知返回 null，不猜材料能力。最终结果及证据如下；以上首轮数字仅为过程记录，不与最终结果叠加计数。

### 本轮最终验证（2026-10-18）

| 范围 | 最终结果 | 证据 |
|---|---|---|
| Orca Mac arm64 Release | 当前源码最小增量构建成功；17 项 `[SliceAnalysis]` + 2 项既有回归，共 2746 断言通过 | `verification/build-features-coverage-note.log`、`unit-features-verified.log` |
| 真实 CLI | 默认/关闭/开启及非法组合等 10 项通过；简单夹具 G-code 指令/有效配置/旧 JSON 保持一致 | `verification/cli-features-verified/`、`cli-features-verified.log` |
| 五组几何 × 两种墙 | 10 次切片通过：细笔画、通孔、分离部件、稀疏内部、低密度顶面 | `verification/shape-features-verified/`、`shape-features-verified.log` |
| 夹具/指令辅助脚本 | 5 项 Python 单测通过 | `python3 -B -m unittest discover -s scripts/tests -p test_cli_slice_features.py -v` |
| Main | 隐式模板/原请求兼容 117 项定向 Java 测试通过 | `/tmp/model-feature-main-pose-20261018.log`，命令见 Main README |
| Compare Web | 114 项测试与生产构建通过；既有大包提示仍在 | `/tmp/compare-web-feature-tests-final-20261018.log`、`/tmp/compare-web-feature-build-final-20261018.log` |
| 真实报告消费 | 10 份当前原生报告、5 对比较通过；新增底面/逐材料流量逐字段一致 | `/tmp/compare-native-consumer-final-20261018.log` |
| Computer Use | 本地实际 Vue 组件：正常/缺报告/修复参与/不同层高 4 场景通过；缺失不算差值、层高不一致不算覆盖差值，极小非零面积仍显示 | `verification/compare-ui-local.png`，是演示夹具而非后端 E2E |
| 文档/差异 | Orca 模块 35 个，问题 0；七仓库 `git diff --check` 通过 | 本轮命令记录 |

表中 `verification/` 均相对 `planning/bmr-commercial-design-20261018/`，仅本机证据不入库。Web 临时验收 HTML、浏览器页和本机 5188 端口服务已清理，保留截图。PDS/Compare backend/Blender/PAM 的生产与测试源码身份未变，沿用下方已核对的结果，不冒充本轮重跑。

细笔画对照再次核实最终 G-code：Classic 在上部六层没有挤出，Arachne 在 Z=1.4/1.6/1.8/2.0/2.2/2.4 mm 均生成路径。通孔和稀疏反例验证指标语义，不因此认定孔公差/实物表面合格。25% 顶面密度样例有未覆盖区域但不产生整岛无路径，不把有意空隙当缺陷。

异常测试曾暴露 cap=NaN/Inf 导致配置字符串序列化失败；已仅对该配置字段保留 null，正常字符串合同不变，异常用例保留并在最终 17 项中通过。首次 final CLI 命令误写非 `.app/Contents/MacOS/` 二进制路径，参数检查退出 2 且未运行切片；改正确路径后重跑成功，原失败日志保留，不计通过。

### 性能与非确定性边界

64 个 3×3×4 mm 方柱（8×8、间隔 1 mm、768 面、20 层）报告 off/on 各 3 次串行切片：全进程 wall 中位数 0.3105→0.6081 秒（+0.2975 秒），峰值 RSS 中位数 113.00→116.81 MiB（+3.81 MiB），报告约 38.9 KiB。有效配置一致，所有输入/二进制/仓库 result.json 哈希不变。它只是该机该小样本观察值，不是服务器性能上限；复杂层 Clipper 运算成本仍需业务模型/平台验收。

**该多岛样本不能宣称逐指令一致**：报告关闭自身的三次重复也产生不同路径顺序，六次共六种指令 hash；预先严格比较失败的产物保留。报告是在 G-code 导出后读取，不能仅凭两次差异归因于开关，也未据此改动路径算法或放宽正式 CLI 回归。详细记录：`verification/planned-layer-64-islands-perf-20261018/measurement-series/summary.json`。未找到现有单线程 CLI 参数，不新增产品开关。

### 当前源码与程序身份

Orca HEAD 仍为 `f653f0a47f81f6288c0bb97089f4118af7ed23e6`，必须结合未提交源码身份使用，不能只看 HEAD。Web 从干净 master `02e30f17db0da0c8b7b59a94a352f4a99440332d` 派生同名特征分支；其他六仓库基准见下方历史表。

| 当前文件/集合 | SHA256 |
|---|---|
| `src/libslic3r/SliceAnalysis.cpp` | `587c7b3fecdc42bb983edf31e3fed73eb1e56d54c5e5a80fd8bfc685e32d5f0d` |
| `tests/fff_print/test_slice_analysis.cpp` | `4708f3ac6dfadcd6bd5a547b7afad5270d20aaeec1182bd67387a33a9c7dc00f` |
| OrcaSlicer.app/Contents/MacOS/OrcaSlicer | `ba47a6aa99808a07dccd34cfa4422d85b8f5cff59b3827701a6870147168a3b5` |
| fff_print_tests.app/Contents/MacOS/fff_print_tests | `35cd345ee75ff48ef7135014e8198e722de5f10dd571681944332b60b7377424` |
| Main 本批 Java/Python 源码/测试聚合 | `baec3f38a9bf392b98c1de55177533781efe80108f439744f379c1b93bbe5114` |
| Web 本批 JavaScript/Vue 源码/测试聚合 | `3ac2cd61352418be259d972ca52df29f6cd4f95ef466fcc117ac8fcdc59a53e3` |

聚合命令沿用下文按 `git ls-files -m -o --exclude-standard -z` 后逐文件 SHA256 再聚合，不含文档/日志；Web 指定 `*.js`/`*.vue`。构建缓存为 Ninja Multi-Config/arm64/Release/macOS 14.0/MeshLib 本机评估 ON，测试前无同目录并行构建。

### 统一环境验收清单（后续一次执行）

1. 测试环境先执行 PDS 原任务表加列 SQL 一次，再部署同批 Orca 和服务/Web；不需要客户端新字段或新通知。未加列不可先发布 Java。无生产操作授权，本轮未执行。
2. 普通 APP：厚块/朝上细字走原高级参数及切片入口；原六字段、手动模板、任务通知、重打已有 G-code 和原 raft/0.25 策略保持。换模型、XY 翻面、拆件/多模型不沿用原分析的隐式模板；XY 平移/Z 旋转允许。详细正反例已在 Main 117 项测试覆盖。
3. Compare：同模型/变换/机料建 A/B，未手调的有效窄面建议可采用 Arachne，手调/旧副本保护；两组切后展示指标、B−A 和 CSV。缺失/本次重试失败不沿用旧报告，修复参与有提示；不同层高不计算覆盖差值。
4. 用五组本地夹具及业务代表模型确认报告单位和时长/耗材；低密度顶面不因有空白而被标记失败。再以目标 Windows/Linux 运行相同 CLI 工具，记录复杂真实模型耗时/内存；Mac 通过不等于目标平台通过。
5. Orca 原生 GUI 未新增自动分析/选参入口。手选 Classic/Arachne 预览细笔画，再查看通孔和低密度顶面；不把 CLI/网页夹具当原生 GUI 验证。实物效果、公差、强度、支撑/附着与冷却能力单独标定；尚未完成 2.1 列出的局部指标，不自动选优。

以下为上一批已完成结果，后续改动需重新验证，不沿用旧二进制证明新源码。

2026-10-18 优化完成（用户已批准按审核计划修改）：主服务默认模板路径和 Compare 默认墙值/人工覆盖判断已修正，完整入口回归通过；同次切片指标报告已接通原 PDS/Compare 链路，普通客户端请求和通知保持不变。不增加新旧流程版本、独立平台或数据库表；仅在既有任务表新增一个可空报告列，当前不扩展其他自动改参规则。原有未提交改动保留，本轮没有提交/推送或部署。

本轮结果：主服务 100、Compare 41、PDS 81，共 222 项定向测试通过，无失败、错误或跳过。主服务已安装当前 PDS interface 后重新回归；Compare 经交叉审查补齐无 `processSelection` 的历史副本保护，PDS 状态更新保留原更新时间填充。真实同几何细笔画对照中，Classic 没有生成凸起区域的挤出路径，Arachne 在该区域的 6 层均生成路径，预计增加约 3 秒。单例不代表孔缝合格或实物质量已验证，详见“按计划优化收尾”。

历史阶段说明：下方“首批后续审核”保留优化前发现及当时测试状态，两处接入缺口现已修正；不要把历史“未修复/仅审核”当作当前结论。商用及完整打印效果验收仍未完成。

- 基准：`feature/f_模型修复分阶段接入_20261017`，`f653f0a47f81f6288c0bb97089f4118af7ed23e6`。
- 最新用户授权：在指定基础分支派生模型特征分支并开始编码；复用正常业务流程，保留基础异常处理，不新增版本分流或无关兜底。完整范围仍是 16 类指标，按批接通，不以墙特征代替完整范围。
- 当前工作分支：六个相关仓库均为 `codex/model-feature-analysis-20261018`。Orca / 主服务 / PDS 从模型修复分阶段分支派生；Blender 从墙特征检测分支派生；PAM / Compare 从各自模型打印对比分支派生。既有工作区改动保留，未提交或推送。
- 前批只读 Orca 切片指标报告已通过 Mac arm64 当前源码构建、8 项定向单测、2 项现有回归和 10 项真实 CLI 检查。本轮未继续修改 C++，不重复构建；后端接入另见下方记录。
- 本轮已完成原 Blender 模板分析复用墙特征、PAM 建议、主服务采用及 Compare 候选接入；保持手动参数优先及现有客户端协议。只改变符合条件的原自动匹配参数，不改模型修复与原生 GUI。
- 下一步：按部署顺序在测试环境验证原客户端与 Compare 完整调用；补局部路径覆盖、孔缝/粘连的正反例及成本上限，再逐项扩展 16 类矩阵。当前不增加通用质量分数或未标定的自动采用阈值。
- 保留任务开始前已有的文档、下载工具及 `result.json` 改动，不将它们当成本批成果。

### 首批原始接入记录（2026-10-18，优化前）

- 已实现第一批接入：同一 Blender 模板任务复用墙特征；PAM 增加单项 `wall_generator=arachne` 建议；主服务复用原匹配记录与切片请求，Compare 只改匹配候选。独立墙特征接口保留供其他调用方使用，未替换/另建一套特征分析链。无新增服务/数据库列/业务版本开关，客户端六参数和通知保持。
- 基本适用条件：真实模板线宽可解析、分析和采用模板线宽相同；主服务仅同模型/缩放且 XY 未翻面的单模型。用户手动墙设置优先。旧已完成记录不自动重跑，仍提供原模板与六字段。
- PDS 当前批次新增派生 `defaultAdvancedParams.outerWallLineWidthMm`，仅解析原配置明确给出的绝对值、百分比与显式自动值；未知继承、多喷嘴不一致或非法值不猜测，不影响旧默认值。源码规则核对 Orca `PrintRegion::flow` 和 `Flow::auto_extrusion_width`。
- PDS 定向测试已通过 37 项：`SliceParamConfigWallWidthTest`、`SliceParamConfigLayerHeightRangeTest`、`OrcaWallParameterOverrideTest`、`OrcaSliceServiceImplTest`；离线 Maven/Java 17，日志 `/tmp/model-feature-pds-tests-20261018.log`。仅 mock 和临时目录，无数据库、云服务或打印调用。
- Blender：20 项 Java（含本地 HTTP 文件下载→真实 Blender→结果读取）+19 项联合 Blender+4 项独立 Blender+11 项纯几何通过，无跳过。原指标逐字段对照；独立入口保留。小 STL 同一进程内模板约 0.0011s、加墙约 0.2339s，含首次墙依赖加载，不含进程启动/网络/排队，不作为吞吐保证。
- PAM：79 项离线回归通过，报告在其 `params-auto-match/target/surefire-reports/`；主服务：91 项定向离线测试通过（含最终 OBJ 朝向边界），日志 `/tmp/model-feature-main-tests-20261018.log`；各仓库 README 记命令。三个 interface 仅安装到本机 Maven 供编译，未发布服务器。
- Compare 按其仓库约定本轮只添加 13 项测试和静态审核，不主动运行构建/测试。跨服务生产部署、GUI/客户端实机及实际打印未验。当前 Compare 是分析输入原模型、B 切片内按需修复，不能声称墙指标描述内部修复后几何；未扩改其修复编排。
- 16 类完整指标与现有/待做对照已补入方案 2.1 节；本轮墙建议接入不是全部类别已实现，更不代表候选质量或实物改善验收完成。

复核修正：Compare 的 `line_width=0%` 不是自动线宽，已修正为未知；超出 Blender 0.05–2 mm 范围不发送，避免阻断原模板分析。主服务无定位列表的 OBJ 会被 PDS 额外转 X 90°，因此不应用原朝向墙建议。主服务当前 createSlice 未传 PDS fineness，实际默认 standard，读取的是标准档镜像；本轮不修改原档位行为，未来接 fast/fine 时须同步取该档有效线宽。

最终静态检查：六仓库 `git diff --check` 均通过；Orca 模块检查 35 个模块、问题 0。PDS 在补充百分比零宽对照后重新执行 37 项测试，仍全部通过。没有暂存、提交、推送或远程部署。

当前未提交 Java/Python 源码与测试身份（各仓库执行 `git ls-files -m -o --exclude-standard -z -- '*.java' '*.py' | xargs -0 shasum -a 256 | shasum -a 256`；包含新增文件，不含文档，不是提交 SHA）：

| 仓库 | SHA256 |
|---|---|
| Blender | `de98fc02658a5de0caaa3a67e8242d19e382472606380034fbc511d550d41c61` |
| PAM | `6f60f705164bd4ac1c99e9fa909b6b75dbc148ddae677e9d55868607eda3daea` |
| 主服务 | `46a29f86c1bf3ee542d605b52687711c65554d922b99f43b4ceca10e6d699340` |
| PDS | `fefd40db34724561bcdbd35d7bf120088382c01f5e984a80c37971970239f4e2` |
| Compare（仅静态） | `9cc3145f864db108222218d74c1a473680bbbc98cca015f214cdf45046ae2348` |

### 本轮基准

六仓库均从下列已核对 HEAD 新建 `codex/model-feature-analysis-20261018`，无提交/推送；Orca 预存的未提交草稿和无关改动保留。

| 仓库 | 派生基础 | HEAD |
|---|---|---|
| Orca | 模型修复分阶段接入 | `f653f0a47f81f6288c0bb97089f4118af7ed23e6` |
| Print3DService | 模型修复分阶段接入 | `26dd94c516aedd2d69de735d752480af74b23e12` |
| Print3DSliceService | 模型修复分阶段接入 | `b4a038075d367f42eb6bf54b741718850351e9ac` |
| Print3dBlenderService | 墙特征检测 | `81ec630bc22efddd055364b1b41fe9b4ca2ca1b6` |
| Print3dParamAutoMatchService | 模型打印对比 | `da99c434e8e5525b4dc9acbb59d3d7c37966317e` |
| Print3DCompareService | 模型打印对比 | `d9349178f38276db03f7154f812cdea854843374` |

### 客户端 / GUI 待验证清单

以下只在测试环境操作，不发送实机打印：

1. 测试库先执行 PDS 加列 SQL，再部署支持报告的同批 Orca 和 PDS、Blender、PAM、主服务、Compare。准备标准模板外墙线宽明确的设备；使用尚无匹配记录的新模型，避免把旧缓存误当新算法结果。SQL 不重复执行，本轮未操作数据库。
2. 用原客户端“高级参数”入口分别提交正常厚块与朝上细字模型；确认原六字段、任务状态和支撑/附着建议正常。检查 Blender 原报告新增 `wallAnalysis`；无实际线宽时应 skipped，不能整任务失败。
3. 继续原切片操作，核对最终 `process.json`：阳性、同线宽/姿态才新增 Arachne，其他墙参数保留模板；未知、翻面、多模型、不同对象模板或无定位 OBJ 均不套用墙建议。
4. PDS/内部参数入口显式选择 Classic，确认最终值仍为 Classic；当前 APP 请求没有墙生成器字段，不将该项写成已有 APP 界面验证。旧已完成匹配记录不强制重新分析，不新增客户端操作。
5. Compare 创建 A/B 实验：A 不变；未手改的默认 Classic 副本可采用建议，真实手动墙设置、来源未知的历史副本及不同线宽的 B 不覆盖。核对跳过原因与实际 B 快照一致；完成后确认原结果 JSON 中存在 `sliceAnalysis`，旧任务/普通 APP 无报告时保持空缺。复制旧实验且没有 `processSelection` 时同样保护历史墙值。
6. Orca 原生 GUI 未改控件/自动选参；沿用原生墙生成器验证两种工艺预览，不把 CLI 单测当作 GUI 或实物质量验收。Windows/Linux 服务部署与实际模型性能另验。

### 按计划优化收尾（2026-10-18）

本轮只改既有业务接入，不重写接口或增加多版本流程：

- 主服务将“替换模板”和“应用建议”分开：PAM 无映射 ID 时，保留调用方原模板/场景；原模板可确定且有效线宽相同，仍可采用墙建议。原模板未知时不猜测，现有手动参数优先。补实际 `createSlice` 入口测试。
- Compare 在原工艺副本 JSON 用 `wallGeneratorOverridden` 记录真实墙类型修改；源模板默认值不算手调，删除覆盖/重载源模板按现有保存操作更新，实验快照冻结记录。历史缺元数据时保留墙值；没有 `processSelection` 的历史副本同样保护。未新增客户端输入或数据库列。
- PDS 仅已有 `source=compare` 添加 `--slice-analysis-report`。从同次输出目录读取并核对 `plate_1.analysis.json`，保存至原 `slice_job.slice_analysis`，原 `/query` 还原 `sliceAnalysis`，Compare 继续保存整个结果 JSON。未知空值保留，不转成零。请求报告却缺失/非法时本次转换失败；原固定测试 GCode 开关仍不伪造报告。
- 新任务进入处理中时，同一次数据库更新清空旧报告，并保留 MyBatis 原更新时间填充；未改变通知、排队或其他超时策略。普通 APP/Cura 不请求报告，无报告时 JSON 不新增空字段。

#### 测试与源码身份

全部是本地离线 Maven/Java 17 单测，使用 mock/临时目录，不连接业务数据库或发起打印；不是部署环境联调。PDS 的转换集成测试用本地成功进程替身与报告夹具验证读取/持久化，真实 Orca 另由下方 A/B 证明。

| 仓库 | 最终测试 | 日志 |
|---|---|---|
| 主服务 | 100 通过，含正常 `createSlice` 入口；对当前 PDS interface 重新执行 | `/tmp/model-feature-main-report-contract-20261018.log` |
| PDS | 81 通过，含报告读取、缺失/非法、普通客户端兼容、固定测试 GCode、异步保存/查询及重跑清理 | `/tmp/model-feature-pds-report-reviewed-20261018.log` |
| Compare | 41 通过，含默认/真实手调/历史副本的实际推荐入口及报告保存 | `/tmp/compare-feature-fix-jVfifz/tests-history-copy.log` |

测试命令在各仓库 README 或 `docs/process-schemes.md`。使用 `/Users/liwei/Library/Java/JavaVirtualMachines/ms-17.0.18/Contents/Home` 与 `/opt/homebrew/bin/mvn -o`。interface 只安装本机 Maven，未发布。最终源码/测试聚合 SHA256 使用前述同一命令：

| 仓库 | 本轮最终 SHA256 |
|---|---|
| 主服务 | `c5fb83226628aad29fcaaf4c250b8144db9a2cc6dfce8c93cf0442ab3f1e4de1` |
| PDS | `a180d80be1b38bfb88d9963fd0dc8324214e50212392ae3666e53cfcc0f20fe7` |
| Compare | `e0d0f05aab6dfe46bfd8e791424b47daac02c8ea115273b55e604bad0265ddb9` |

Blender/PAM 本轮生产源码不变，沿用前述身份及已通过结果；Orca 二进制和指标源码身份不变，未重复构建。上述哈希不包括文档或 SQL；不以未提交分支的 HEAD 代替源码身份。

#### 同模型真实 Classic / Arachne 对照

本机证据归档：`planning/bmr-commercial-design-20261018/verification/wall-ab-01/`（忽略目录，不提交）。包含生成/运行脚本、输入、命令、stdout/stderr、两份 G-code/指标报告、`summary.json` 与 `local-path-evidence.json`；原运行路径是 `/tmp/model-feature-classic-arachne-20261018-QzErLT`，归档未改证据内容。

- 模型为闭合相连的 12 × 8 × 1.2 mm 底座与 6 × 0.36 × 1.2 mm 凸起细笔画，共 68 个三角面。同一 STL、摆放、机型、材料；输入工艺和最终 G-code 配置都只差 `wall_generator`。
- 同一模型运行原联合 Blender 分析：`NARROW_STROKE`、风险阳性、中位宽约 0.357 mm。阳性提前结束的 `scanComplete=false` 不宣称全模型检查完成。

| 指标 | Classic | Arachne |
|---|---:|---:|
| 真实 CLI 退出码 | 0 | 0 |
| 模型几何层数 | 12 | 12 |
| 实际 G-code 层数 | 6 | 12 |
| 凸起区域存在挤出路径的层数 | 0 | 6 |
| 预计总时间（秒） | 391.0663 | 394.0967 |
| 预计耗材体积（mm³） | 163.3525 | 165.7038 |

Arachne 在 Z=1.4、1.6、1.8、2.0、2.2、2.4 mm 各生成一条长约 5.64 mm 的外墙路径，Classic 该区域没有挤出路径。代价约 +3.03 秒、+2.35 mm³；不以四舍五入后的 0.20/0.21 g 推导百分比改善。模型几何层数相同不代表细节路径相同。

这只是已知夹具的局部路径证据，不是通用覆盖率算法、孔缝保留判定、全链路数据库 E2E 或实印结果。Compare 既有 B 组可能切片内修复，仍需确保最终几何相同才能把差异归因于参数。

#### 部署与未验证项

先执行 PDS `print3d-slice/src/main/resources/db/migration/V20261018__add_slice_analysis_to_slice_job.sql` 一次，再更新支持报告参数的 Orca，最后发布 Java 服务。该脚本按仓库原 SQL 交付方式，不会因文件存在而自动迁移；未加列时 BaseMapper 查询也会失败，不能先上线 Java。只有现有表的一列变更，无新表或数据回填。

当前第一批接入缺口已关闭，并取得单样例实际收益证据；完整候选自动验收仍待局部覆盖、孔缝/粘连检查、成本阈值和机料标定。最终交叉审阅确认历史副本保护和状态更新时间问题已关闭；主服务/PDS/Compare 与 Orca 的 `git diff --check` 通过，Orca 模块检查 35 个模块、问题 0。测试环境迁移/服务联调、APP/Compare 页面、原生 GUI、Windows/Linux 和实物打印均未验证，按上方清单继续。没有提交、推送、部署或远端写操作。

### 首批后续审核（2026-10-18）

本次用户要求审查、达标判断和后续方案，不授权在本轮修复问题。检查覆盖六仓库当前工作区；采用代码理解及差异审查流程，旧 Understand 图谱不覆盖本批实现，直接以当前源码和差异为准。六仓库业务源码/测试聚合 SHA256 与上表一致；Orca 核心、CLI 测试与二进制 SHA256 与既有验证记录一致，未重复构建未变代码。

“第一批”应分两部分表述：Orca 的基础切片指标报告已实现并有本机验证；原特征分析链的墙建议接入已有实现，但还存在下列采用缺口。它不是全部 16 类特征完成，也不是实际打印效果已改善。

确认的接入问题：

1. **P2 / Compare 默认值误当人工选择。** `ProcessSchemeServiceImpl.debugSelection()` 令新实验统一为 `DEBUG`，工艺副本来自完整源模板；`ExperimentWorkerServiceImpl.java:381` 却以 `debug && localProcess.has("wall_generator")` 判定手动覆盖。源模板自带 `classic` 时，即使用户从未改墙类型，有效 Arachne 建议仍被跳过。旧的手动优先方向应保留，但字段存在本身不足以区分模板默认值与人工选择。最小修正应复用现有工艺配置语义区分两者，不新增版本分流或独立业务流程；补从正常实验输入走 `recommend()` 的测试，而不仅测试私有 helper。
2. **P2 / 主服务默认模板路径丢失建议。** PAM 允许无映射时返回空 `sliceConfId` 和有效墙建议；`AdvancedSliceParamMatchServiceImpl` 使用机型默认参数完成匹配。但 `PrintJobServiceImpl.java:1177` 在读取建议前就因空 ID 返回，导致默认模板线宽匹配时也无法采用建议。最小修正是分开“是否替换模板”与“是否采用建议”，保留原模板后仍按最终模板核对线宽；补该正常兜底路径测试。原切片继续执行，不是新增崩溃。

未发现其他有充分证据的本批阻断缺陷：Blender 复用同次导入/变换/网格；独立墙接口保留；PAM 限定单项建议且 Cura 不采用；主服务原 URL/缩放缓存、朝向限制、PDS 显式墙参数优先和实际线宽检查均保留。以上是本轮检查范围内结论，不代表所有部署环境通过。

效果验收仍未完成：

- 后端尚未请求/消费 `--slice-analysis-report`，Orca 的聚合报告也尚不包含细字覆盖率、丢笔画或孔缝粘连检查，不能据参数切换判定“效果更好”。
- Compare 原有 B 组切片内按需修复发生在特征分析之后；这是继承边界，不是新增回归。进行参数效果验收时应使用同一份最终模型，不能把几何修复的变化算作 Arachne 的收益。
- 现有墙指标仅涵盖朝上水平面窄笔画/短分支；任意三维薄壁、另外 15 类具体指标和实印标定仍按方案推进。旧已完成缓存不自动补分析，是既定覆盖限制。

后续顺序：修正两处采用缺口并验证完整入口 → 接通同次 Orca 切后报告 → 以同模型/姿态/有效工艺对照 Classic 与 Arachne 的细节保留、孔缝和时间耗材 → 优先复用已有悬垂/桥接、接地、层时间等证据逐项扩展规则。成本和机料阈值使用业务要求/实测值，不预设通用“最佳参数”。

本轮复跑：联合 Blender 19 项、独立 Blender 4 项、纯几何 11 项、PAM 推荐 14 项、Compare 30 项通过，共 78 项，无跳过。六仓库 `git diff --check` 通过；Orca 模块检查 35 个模块、问题 0。Main/PDS 本轮仅复核与源码身份一致的既有定向结果，未冒充重新运行。没有数据库联调、Linux 部署、GUI/APP 操作或实际打印；没有修改业务文件、暂存、提交、推送。

复跑证据：

- Blender 仓库运行 `/tmp/wall-feature-code-fix-20261017/blender-test --background --python-exit-code 1 --python print3d-blender/src/test/python/template_feature_support_test.py`，19 项通过；同程序加 `--factory-startup` 执行 `print3d-blender/src/test/python/test_wall_feature_blender.py`，4 项通过。stdout 在本轮工具执行记录中，未另写日志。
- 纯几何首次误用 Blender 参数执行 unittest 退出 2，不算测试通过；随后以 `PYTHONPATH=/tmp/wall-feature-timecheck-20261017/fresh-plain-packages /Applications/Blender.app/Contents/Resources/5.1/python/bin/python3.13 print3d-blender/src/test/python/test_wall_feature_geometry.py` 正确执行，11 项通过。
- PAM 仓库离线运行 `mvn -o -pl params-auto-match -am -Dtest=SliceTemplateRecommendServiceTest -Dsurefire.failIfNoSpecifiedTests=false test`，14 项通过；本机 Surefire 报告在 `params-auto-match/target/surefire-reports/`。
- Compare 仓库使用 Java 17 执行以下命令，当前生产/测试源码编译成功；工具 7 项、Worker 16 项、快照 4 项、工艺方案 3 项，共 30 项通过，包含本批新增 13 项。日志 `/tmp/compare-feature-review-CQIOon/tests.log`。未启动 Spring 业务上下文；测试未覆盖“新实验统一 DEBUG + 源模板默认 classic”的完整推荐入口，不能据通过否定前述发现。

```sh
JAVA_HOME=/Users/liwei/Library/Java/JavaVirtualMachines/ms-17.0.18/Contents/Home /opt/homebrew/bin/mvn -o -B -pl print3d-compare -am -Dtest=ExperimentWorkflowUtilTest,ExperimentWorkerServiceTest,CompareSnapshotServiceTest,ProcessSchemeServiceTest -Dsurefire.failIfNoSpecifiedTests=false -DskipTests=false -Dmaven.test.skip=false test
```

## 方案入口

[商业落地方案与分批验收](../../../doc/bmr-print-optimization.md) 规定总体指标、原生功能复用、自动采用条件与阶段边界。既有总体指标文档仍是指标定义来源，不用工程结构取代指标本身。

## 本批接口决定

- `--slice-analysis-report` 默认关闭，仅在 `--slice` 且指定 `--outputdir` 时启用。
- 每个实际完成切片的盘额外生成 `plate_N.analysis.json`；不改原 `result.json`、G-code 或 3MF。后续 PDS 接入仅在原返回 DTO 增加可空 `sliceAnalysis`，见本轮收尾。
- 读取同次切片的 `Print` 和 `GCodeProcessorResult`，不再切片、不另读模型、不运行修复。
- 已有同名报告拒绝覆盖，调用方使用独立输出目录；报告生成/发布失败返回明确失败，已生成的 G-code 保留。不能用旧报告补成成功。
- GUI 不增加界面：这是服务调用的可选数据导出；共用只读核心可以复用，普通 GUI 切片不调用它。没有 GUI/CLI 两套优化判断。

## 自动化与 CLI

仅构建 `OrcaSlicer`、`fff_print_tests`，未重新打包、升级依赖或发起打印。最终构建 `build-final.log` 退出 0。实际程序不是旧打包副本：

| 产物 | 仓库内路径 | SHA256 |
|---|---|---|
| CLI | `build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer` | `0ee496877d7d9100c36b196446a1da11f7b1eea74a6eaf1b38e02acbc574802e` |
| 测试 | `build/arm64/tests/fff_print/Release/fff_print_tests.app/Contents/MacOS/fff_print_tests` | `340d746d69cf27e2c126ff7f615b34d58c3a14bdfe95bdded926a7d4d39f6fa8` |

当前未提交源码身份：`SliceAnalysis.cpp` = `1100a9535d350364f203c398479baf48388fbd560b3398340ca126e5f95ae51e`；`test_slice_analysis.cpp` = `d41ea6f9fcfd4b39ddb533d44efb77776458084b7b8c7751f41b25ff64452636`；CLI 脚本 = `2cadf288c7a2fabe31ae88b813de6cb15d2eb6528a5e6ea5fdf6fc24125247a2`。模块 review 指纹覆盖其余当前归属源码，不能仅用 HEAD 代表未提交实现。

从仓库根目录执行：

```sh
cmake --build build/arm64 --config Release --target OrcaSlicer fff_print_tests --parallel 6
build/arm64/tests/fff_print/Release/fff_print_tests.app/Contents/MacOS/fff_print_tests '[SliceAnalysis]'
build/arm64/tests/fff_print/Release/fff_print_tests.app/Contents/MacOS/fff_print_tests '[LayerCount],[PrintApplyCache]'
python3 scripts/test_cli_slice_analysis.py --binary build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --output-dir planning/bmr-commercial-design-20261018/verification/cli-02
```

复测时 CLI 证据目录必须换成新的目录，脚本拒绝覆盖已有结果。本轮结果：

| 检查 | 结果 | 证据（均在 `planning/bmr-commercial-design-20261018/verification/` 下） |
|---|---|---|
| `[SliceAnalysis]` | 8 用例 / 149 断言通过，退出 0 | `unit-final.log` |
| `[LayerCount],[PrintApplyCache]` | 2 用例 / 2487 断言通过，退出 0 | `regression.log` |
| 真实 CLI | 10/10 通过，脚本退出 0 | `cli-02/summary.json` 及逐例命令、stdout/stderr、JSON/G-code |

CLI 验证包含帮助、默认关闭、显式 0、开启、四类不合法参数组合、旧报告拒绝覆盖、缺失输入。逐条比较所有可执行 G-code、完整配置块及旧结果字段，仅排除执行耗时字段；预计打印时间/用料仍参与比较。测试前后原输入、预设、二进制及仓库根 `result.json` 未变。

本机夹具为 `planning/cache/cli-assets/models/stl/cube.stl`，配合同目录资源中的 `ep7_0.4.json`、`ep7_standard_0.20.json`、`generic_pla_0.4.json`。资源不是仓库必备文件，脚本允许通过参数提供替代路径；本轮实际文件完整哈希见 `cli-02/summary.json`。开启/关闭均为 90 层，可执行指令 SHA256 同为 `05e22da2f14b38de2d8d2ec050c4801f868b10f2fb6bf99e1205fc2151f55d14`。

报告区分外墙约 0.2 mm 的路径高度与 custom 启动预挤出估算的 10.6 mm，后者不代表模型层高。多实例、不同缩放组、多耗材、支撑层、空值和非法值由构造数据单测覆盖；本轮真实 CLI 为单盘单模型，不把单测当作多盘/多材料工程实跑。未测后期磁盘写入失败、取消、压力性能、目标平台、原生 GUI 和实物质量。

构建缓存为本仓库 `build/arm64`，Ninja Multi-Config / Release / arm64 / macOS 14.0，MeshLib 本机评估 ON。开始前没有并行构建。CMake 重新生成触发 593 项依赖重编译；初始并行 2，在确认 64 GiB/16 核后中断自己的 Ninja 并以并行 6 续编，未更改源码或缓存选项。初始中断不是编译错误。日志：`planning/bmr-commercial-design-20261018/verification/build.log`、`build-resume.log`。不使用旧打包副本证明新实现通过。

首次续编在 CLI 新增预检处失败：现有 `PartPlate::empty/is_all_instances_unprintable` 未声明 const；修正局部指针类型，不修改两个既有方法或盘状态逻辑，再构建两个目标。修复轮 `build-fix.log` 退出 0。

初次单测在第 4 例“配置不被修改”执行期间发生 SIGSEGV（`unit.log`）；未取得调用栈，不将推测写成已确认根因。测试改为完整类型化配置副本比较，移除与只读验证无关的全 key 序列化；生产报告核心不因此改写。补充角色分组后重新构建，最终同例及全部 8 例通过。首轮 `cli-01` 也为 10/10，但以最新角色分组构建的 `cli-02` 为交付证据。

已执行静态审查、`git diff --check`、简中 `msgfmt --check-format`、Python 语法检查；均通过。模块文档在最终复核后更新正文/指纹并运行 `--all`，结果见本记录收尾。

## GUI 与平台验证范围

本批仅增加 CLI 服务数据导出，没有 GUI 对应控件、参数改动或优化选择逻辑，故无需新增 GUI 功能操作清单。原生 GUI 未执行手测，不将 CLI 默认对照通过写成 GUI 回归通过。Windows/Linux 原生构建和运行仍需对应平台验证；当前硬链接发布方式还需验证目标部署文件系统。

## 进展

### 2026-10-18：开始

已核对分支、工作区、构建配置及方案来源。旧知识图谱不覆盖当前功能，以当前模块文档和源码为准；不重建索引。三个并行只读审查分别覆盖构建证据、指标语义、业务边界，其中指标审查随后转为限定文件的核心实现。

### 第一批代码与审查

共享 `SliceAnalysis` 加入现有库和 `fff_print_tests`；CLI 仅增加显式导出调用，默认无额外遍历。核心报告包含对象/切片组/实例、模型层高范围、支撑层数、首个模型层截面积、G-code 层时长汇总、角色时间/耗材、各材料体积、挤出线宽/高度/指令速度/名义流量范围及分作用域的配置子集。

重要修正：聚合角色重量依赖耗材密度；任一使用材料的密度缺失、非正或非有限时该聚合重量为 null，不写 0。所有范围带有效/无效样本计数，缺样本上下限为 null。实例数不等于源对象数；首层面积扣除孔洞且按单实例报告，不冒充床面接触。

独立审核还发现内部 `PrintObject::size()` 可能来自源对象的首实例，不能直接当作不同缩放切片组的实际尺寸。首批删除这一字段，不改原有切片逻辑；保留不同缩放组的计数/字段不误报回归。正确的姿态后几何尺寸留待局部分析批次。此修正后核心和测试重新参与构建。

输出发布采用现有仓库同类报告使用的硬链接 no-replace 方式，需要文件系统支持硬链接（部署文件系统待验）。报告保护不代表允许两个任务共用 G-code 输出目录；独立任务目录仍是前提。没有新增版本分流、服务、数据库字段或业务阈值。

### 第一批收尾

最终独立只读复核未发现新增阻断项；`python3 scripts/check_ai_module_docs.py --all` 复核 35 个模块、问题 0，`git diff --check`、简中翻译格式和 Python 语法检查均通过。此次只完成第一批软件指标及本机验证，不包含局部结构检测、参数自动选择、后端业务接入或实物改善验收。没有暂存、提交或推送；任务开始前的无关改动保持原状。

### 用户纠正后的方案复核：先分析，不继续实施

以下为当前源码分析结论和待审核建议，不表示用户已批准新方案，也不触发前述第二批开发。

- Blender 当前为 `feature/f_墙特征检测_20261016` / `81ec630bc22efddd055364b1b41fe9b4ca2ca1b6`，工作区干净。旧 `analyzeTemplateFeatures` 已计算尺寸、悬垂、桥跨/置信度、底面、重心与细长指标；独立 `model-features` 已对朝上水平面检测窄笔画/短分支，命中可返回 `features.processOverrides.wall_generator=arachne`。这不是任意三维壁厚或实际路径保留检测。
- PAM 当前为 `feature/f_模型打印对比` / `da99c434e8e5525b4dc9acbb59d3d7c37966317e`，已有六类支撑/附着模板规则，尚不消费新的墙特征/覆盖结果。不能宣称墙字段只需透传就已生效。
- 主服务 / PDS 仍在 `feature/f_模型修复分阶段接入_20261017`，分别为 `26dd94c516aedd2d69de735d752480af74b23e12` / `b4a038075d367f42eb6bf54b741718850351e9ac`，工作区干净。修复选中的 URL 会写入模型 STL 字段，后续高级参数请求再取该文件分析；不是修复完成即自动分析/匹配。
- PDS 已有工艺 JSON 覆盖和墙生成器覆盖，可复用。当前 Arachne 显式设置会附带固定墙参数组，接入新建议前须明确优先级，避免覆盖掉模板/建议值。没有发现已接通的“局部特征坐标自动转 modifier”流程。
- 推荐待审核分工：Blender 继续做切片前几何特征；PAM 负责模板与有限改参决策；PDS 复用执行；Orca 提供实际层/路径结果与成本证据。不搬迁全部 Blender 分析，不在 Blender 重写墙、桥接、层高、冷却算法，不把本轮聚合报告当作细节检测器。
- 必须核对的语义：Blender 桥跨/悬垂为几何估算；整体宽高细杆评分不是局部细杆厚度；主服务现有分析入口旋转固定为 0，旧模板脚本还会整体落床。分析尺寸、实际摆放、有效工艺需要一致，不能直接拿预估替代最终打印路径。
- 后续分支：Orca 及上述主服务/PDS 从各自模型修复分阶段分支派生；Blender 从墙特征检测分支派生；PAM/Compare 另按其现有匹配/对比分支确定基准。本轮未创建或切换分支，未修改业务源码、构建或调用业务服务。

工具状态：Orca CodeGraph MCP 未暴露，CLI 不在 PATH 但可按实际路径调用；已有索引的六个本次核心目标文件与源码哈希一致，新增 SliceAnalysis 不在索引，直接读源码。Understand 历史图谱为 2026-08-28，不作当前功能事实。Blender/PAM 无本地索引，按源码核对，未初始化。
