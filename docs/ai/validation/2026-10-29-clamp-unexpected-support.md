# 2026-10-29 机关夹夹乐无支撑配置与实印不符排查

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前状态

- 代码审核完成：基于add2c3e、目标分支67440ba。未发现本次工艺解析修复的阻塞缺陷；重跑原始分支30项解析测试、3项生成器测试均通过。建议小幅加强支撑关键字段约束并参数化补足关闭支撑的保存链路测试，保留现有两类分工和工艺字段清单；未改业务代码。

- 续接已完成：切片服务`feature/f_3MF参数解析修复_20261024` @ add2c3e直接覆盖本次导入根因。用该提交原始Java解析/合并代码验证旧、新两份真实3MF，均恢复344项工程工艺值并保持enable_support=0。当前集成分支未包含此提交，远端没有同名分支；补丁静态应用检查通过。仍未合并/部署/修改生产。

- 仅分析已完成；生产库只读查询和实际文件核对已取得证据。未修改数据库、业务代码、模型、方案，未触发重切或打印。GUI/CLI无本轮实现改动，无构建/实印验证。
- 已确认EPONE大扳机任务`1565059731934130176`于2026-10-28 17:48:37创建，22:37:35完成；其方案在当时数据库中的有效工艺为`enable_support=1`，实际G-code确有树支撑。不是“enable_support=0仍被引擎强行生成支撑”的证据。
- 根因定位：10-22 15:28上传3MF的project设置为`enable_support=0`，但后台保存了另一份差量工艺`基础-无支撑无垫层`并补成`enable_support=1`。当前Java导入逻辑“选最大编号process、用机型通用模板补缺值”与生产历史快照逐字段一致。没有生产发布commit/当时Nacos通用模板快照，因此该具体代码路径的部署身份仍未独立确认。
- 10-28 19:33方案已更新为关闭支撑；当前两个EPONE组件仍引用10-27生成的带支撑系统G-code。建议先按现有正确方案重新生成并替换这两份系统切片，再修后台3MF工艺选择/补值与缓存失效。无需以修改Orca C++支撑算法作为本例处理前提；本轮不执行修复。
- 证据：`planning/clamp-unexpected-support-20261029/`。下一步仅在用户要求实施时继续；先复核生产方案、精细度档位及缓存身份，再执行数据处理/后端修改。

## 基准与范围

- Orca仓库：codex/model-feature-analysis-20261018 @ aa39c82eb278014fa9046b44daf2fd932905db8f；既有文档/scripts/result.json改动保留，src/tests/resources未改。
- 主服务：`projects/threeDService/Print3DService`，feature/f_模型修复测试集成_20261029 @ 948ab05f330f1cdfbb53ee86457a675bc6bbe259；已有修复相关未提交改动，仅读取。
- 切片服务：`projects/threeDService/Print3DSliceService`，同名feature分支 @ b84c7e5fe9ad6e7e8e0d711ee6da764bcc3b5391；已有修复相关未提交改动，仅读取。当前源码不视为生产部署身份。
- 已读各自AGENTS；主/切片服务无docs/ai/README。Orca CodeGraph相关六文件哈希与源码匹配，但调用图有解析失真，已回到源码；会话无数据库MCP，使用现有MySQL CLI。凭据只在进程内从已有配置读取，临时600权限连接文件随调用删除，不记录明文。
- 浏览器只打开和查看生产后台菜单，未保存/更改配置/触发分析或打印。用户要求数据库后停止后台探索。

## 现有源码给出的核查点（均不是本例已证实原因）

1. Orca `--model-support`在OrcaSlicer.cpp:2805写对象enable_support；后续model-process在2825继续apply对象工艺。如两者冲突，后应用工艺可覆盖前值。全局无支撑方案也不能代替对象/工程覆盖核查。
2. Print.hpp:434的has_support是enable_support或enforce_support_layers>0；raft由单独模式/层数决定。仅看enable_support一个字段，或方案显示名称，不足以区分支撑、强制支撑和筏层。
3. PrintObject::generate_support_material在无支撑时可检查并告警；需要根据实际支撑/筏层条件进入生成，不能把动态需求检测本身解释为自动开启支撑。
4. 实际G-code需确认生成时间、文件hash、机型和角色。可能路径包括参数被覆盖、错误机型/组件映射、使用旧缓存文件；若没有真正Support角色，还需检查模型自带结构、桥接/填充和运动间隙，而不是先认定引擎自动加支撑。

## 数据查询计划

已由主服务实体发现print_job包含model_sn、model_component_sn、old_component_sn、gcode_url/gcode_3mf_url、slice_params、print_params、slice_conf_id、slice_type、support_filament_used、created_at/print_send_at；print_job_item包含print_sn与各模型/组件SN。截图中的SN可能对应旧组件标识，查询应分别匹配，不能只筛print_job.model_sn后宣布无记录。

只取与这两个模型关联的任务字段，不查询用户联系方式等无关数据。先确认索引和时间范围，使用有限返回行数与查询超时；禁止触发重切、删除缓存、更新方案或实际打印。

## 只读查询结果与时间链

连接为用户明确确认的`43.145.113.97:21993`。内部10.128.8.5:3306超时；主/切片服务prod凭据返回1045；ModelService既有prod凭据连接成功。连接文件600权限随调用删除，未保存或输出明文凭据。全部SQL为只读事务，15秒语句限制，按SN/组件索引或ID限定查询，不查询联系方式。模型SN`1525206682654109696`对应“机关夹夹乐”。

| 时间 | 证据 | 关键值 |
| --- | --- | --- |
| 10-22 14:59:36 | 方案变更日志717 | enable_support=0 |
| 10-22 15:28:35 | 方案变更日志725，方案1562156508109029376 | 名称仍为EPONE 无支撑-15%填充；实际default_process_conf.enable_support=1，print_settings_id=基础-无支撑无垫层 |
| 10-27 03:51:48～03:52:10 | model_slice_job 1564486744791547904/1564486790991806464 | 大、小EPONE组件系统切片；产物均enable_support=1 |
| 10-28 17:48:37～17:48:44 | print_job 321101，slice_job 320201，同SN1565059731934130176 | 100%比例，XYZ旋转0，EPONE大扳机；重新切片URL与10-27系统文件不同，不能称本次直接用了旧缓存 |
| 10-28 19:33:29 | 方案变更日志915 | 上传新3MF，enable_support=0；时间晚于此次切片和下发 |
| 10-28 22:37:35 | print_job状态completed | 支撑统计0.65g，模型31.34g，总31.99g |
| 查询时 | EPONE大、小组件160100/160101 | 系统G-code及last_slice_at仍为10-27；配置更新没有替换这两份已存产物 |

精确查EPONE/EPONEPlus两种组件SN，返回上述1条EPONE打印记录。10-29最新小扳机为EP7，任务1565442867174113280、数据库支撑统计0；它不是此次已证实的EPONE异常任务。未认定截图外其他机型同源。

## 实际打印文件与历史3MF

实际打印文件`epone-large-1565059731934130176.gcode`：10863270 bytes，SHA256 `02176f05ad4255e9a1a4badbeb6ade06d55996132b0822e623c533757e404538`。

- 文件头：OrcaSlicer 2.3.2.3-Release，2026-10-28 17:48:41。
- 507077行enable_support=1；507078行enforce_support_layers=0；507321行print_settings_id=基础-无支撑无垫层；507362/507363行raft_layers=0、raft_mode=legacy；507490行support_type=tree(auto)。筏层关闭。
- 有61次Support角色段与17次Support interface角色段；分别26793/1261条含XY与E的G1指令。按文件层高注释统计Support Z=0.2～10.4mm，interface Z=1.8～10.4mm，明确不只底部附着层。未独立实印验证机械卡住的具体位置。
- 旧系统文件`cached-large.gcode`、`cached-small.gcode`同为enable_support=1、同样的角色段数、无筏层，SHA/URL见download-evidence.json。

旧模板`preset-725.3mf`原URL来自操作日志，SHA256 `8ff46beeaf75e728159d132bfe3b96369e18a8905d57d2fc901571ff23c05fe2`：

- project_settings.config：当前print_settings_id=EPONE 无支撑-15%填充-1022，enable_support=0。
- process_settings_1.config：33个键，对应当前工艺，是差量预设，没有enable_support键。
- process_settings_2.config：28个键，名称基础-无支撑无垫层，也没有enable_support键。
- 数据库日志725保存的工艺与process_settings_2的28个键中27个完全相同，唯一差异from:project→system，恰好对应导入服务的强制转换；导入后enable_support=1。
- 历史数据库工艺的11个抽样有效参数（支撑开关/类型、工艺名、筏层间隙、速度、填充等）与实际打印文件完全一致。完整来源见imported-process-725.json和config-timeline.json。

新模板`preset-915.3mf`：仅一份process_settings_1.config，350个键，project和process都明确enable_support=0。数据库精细度表slice_param_conf_profile只查到standard档位（1562156509119856640），10-28 19:33:28更新，enable_support=0、enforce_support_layers=0，与方案主表一致。它解释了当前后台配置已正确，但不能改写更早的切片。

## 后端路径与根因边界

当前切片服务源码：

1. `ThreeMFConfigExtractor.java:137-145`选process_settings_N.config最大编号；不读取project当前所选工艺来消歧。编号不是“最新有效工艺”的可靠依据。
2. `SliceParamConfigServiceImpl.java:173`用orcaCommonJson的机型process与选中的差量工艺合并，184强制from=system。差量没有enable_support时，项目中明确关闭的值未被带入。历史DB结果补为1，与此路径吻合；当时Nacos模板原值尚未单独读取，不把推断冒充运行追踪。
3. `OrcaSliceServiceImpl.java:619-638`以保存的defaultProcessConf生成切片process；globalAdhesionEnable=false只关闭brim/raft，不会关闭enable_support。实际任务外层和对象sliceConfId相同，`needsModelProcessSetting`返回false；没有证据表明此例是对象工艺覆盖导致。
4. model_component.slice_param中遗留enableSupport=true是另一处数据不一致；但实际EPONE打印任务快照没有此字段，当前切片ParamDTO也没有该属性，不能将其直接判定为本次支撑来源。此前中间观察未作根因结论。
5. 当前主服务`PrintJobServiceImpl.java:3225-3237`及`ModelRuntimeFacadeImpl.java:882-886`会把systemGcode作为无人工默认文件时的回退数据；因此需要处理仍存的旧切片。是否某条线上入口会进一步阻断旧指纹复用取决于部署版本，不能仅凭此代码断言全部入口必用旧文件。

## 建议处理方式与工作量（未实施）

- 最小止损：核对现用方案/档位的enable_support=0、enforce_support_layers=0，并明确附着开关；用该有效配置重生成两个EPONE组件系统切片，替换默认/缓存引用。验收新文件支撑角色与支撑耗材为0，再验证活动结构。这是配置及产物处理，不需要改STL或Orca支撑算法。
- 后端修复：3MF导入依据project的当前工艺标识解析对应process；从工程有效配置按工艺字段分类补齐差量，保留显式false/0。不能把完整project（含机器/耗材字段）直接灌进process，也不能只将max编号改成1——两份旧差量均缺支撑开关。
- 增加一致性校验：导入后的支撑开关与工程有效值冲突时阻止静默保存；后台展示实际开关，不能凭名称判定“无支撑”。方案更新后按配置内容/档位指纹使关联切片失效或重生成。
- 工作量：此例恢复为配置/切片产物层的小范围操作；防复发涉及导入器、配置合并/校验和缓存关联，属于后端局部小到中等改动。若扩大为完整多工艺/多耗材/多盘3MF兼容，则需单独评估，不能承诺为一行修改。
- 回归最少覆盖：本次双工艺差量3MF、单工艺完整3MF、显式0、缺失字段、支撑开启方案、不同精细度档位、方案更新后的旧缓存；本轮仅分析，未增加或运行业务测试。

## 验证与交付

- 数据库SELECT、历史配置与真实G-code/3MF解析：已完成；证据文件保存在本地planning目录，不纳入提交。
- 当前源码静态链路核对：已完成；不代表生产部署commit已确认。
- 修复、重切、GUI预览、实印：未执行，用户要求仅分析。
- 本轮只更新本分析记录及文档入口，保留全部用户既有改动；收尾检查：python3 scripts/check_ai_module_docs.py --all通过（35模块、0问题）；本轮文档git diff --check通过。无提交。

## 2026-10-29 续查已有3MF修复分支

### 分支与集成状态

- 仓库：Print3DSliceService；本地分支`feature/f_3MF参数解析修复_20261024`，提交`add2c3ee9b1c22af87087164d872a9afb770ecaa`（2026-10-25 21:54，修复3MF当前工艺选择和工程参数还原），基于master的6b6ba2e，仅有一个新增提交。
- `git ls-remote --heads origin`针对该分支、master、develop和当前集成分支只读核对：未返回修复分支；master=6b6ba2e，develop=3cdf4d2，当前集成远端=b84c7e5。未fetch、切分支、merge、cherry-pick、推送或部署。
- 调查期间主工作目录由b84c7e5和6个既有修改变为`67440baaeecebc09990a2f6b99db9d3e11e45fb5`，新增“模型修复：默认关闭 Mesh 重建并上报阶段进度”；不是本轮产生。已重新核对，工作区干净、领先远端1提交，该新提交不涉及3MF解析。
- 对b84c7e5及最终67440ba分别执行`git apply --check`检查add2c3e独立提交补丁，均退出0（只检查，没有应用）。HEAD/master/develop及对应本地远端引用均不包含add2c3e。跨分支总体diff含基线差异，实施时应移植独立提交，不能直接覆盖整份服务类。
- 存在`Print3DSliceService-3mf-config`工作目录，但它是干净的detached 6b6ba2e旧基线，不能因目录名就当作已包含修复。本轮从Git对象读取add2c3e作为源码身份。
- CodeGraph MCP未暴露，CLI实际路径存在；已有索引属于当前工作目录，新增解析器在当前分支不存在，不把旧索引用于修复分支结论。未重建或同步跨分支索引，使用精确Git提交源码/差异。

### 该提交覆盖范围

- `ThreeMFConfigExtractor`收集工程和各工艺，交给新增`OrcaProcessConfigResolver`按project.print_settings_id精确匹配预设name，不再按工艺编号最大选择。
- 以344项工艺字段清单从project恢复有效值，保留0、false、百分比和空数组；机器/耗材元信息不混入工艺。工艺缺项继续使用既有通用模板，但显式关闭值可覆盖通用模板的开启值。
- 多工艺无当前选择、名称不匹配、重复匹配、损坏配置直接报错；仅旧单工艺允许无选择回退。仅下载失败重试，解析错误不重复下载。
- 服务调用变化是解析错误提示；标准/精细/快速档位共用buildProfiles→parseConfigFile路径。正常方案保存请求携带文件URL会重新解析；更新流程只生成方案测试G-code，不能代替模型组件系统切片重生成。
- 未实现历史数据自动回填、已生成G-code失效/替换、模型库批量重切；源码仍有通知模型库替换参数的TODO。机器/耗材继续最大编号选择，完整多耗材多盘兼容不在本提交范围。
- 核心为解析器2个类（含新增128行resolver），加服务错误提示、348行资源清单、测试/脚本/文档；10文件1192增215删不能误读为切片算法的大改。

### 本轮真实文件验证

没有沿用历史测试通过作为本轮结果。在独立证据目录从add2c3e导出ThreeMFConfigExtractor、OrcaProcessConfigResolver、JsonUtils及原始字段资源，Java17.0.18最小编译，运行本地核对程序；没有启动Spring服务、调用数据库写入或执行切片。

| 输入 | 选中工艺 | 与工程字段比对 | 支撑开关 |
| --- | --- | --- | --- |
| preset-725.3mf，10-22实际故障模板 | EPONE 无支撑-15%填充-1022，process_settings_1 | 344/344一致 | enable_support=0，enforce_support_layers=0 |
| preset-915.3mf，10-28更新模板 | EPONE 无支撑-15%填充，process_settings_1 | 344/344一致 | enable_support=0，enforce_support_layers=0 |

另用相同分支JsonUtils模拟既有合并调用，故意设置通用模板enable_support=1、enforce_support_layers=3，两份输入最终仍为0/0；缺项回退保留，机器/耗材字段未混入。输出原样落盘。该验证覆盖解析与JSON合并，不等同完整Spring保存、CLI切片、部署或实印验收。

证据：`planning/clamp-unexpected-support-20261029/branch-verification/`下identity.json、verification.log、两份resolved-process.json。源码SHA、输入SHA、编译与运行退出码均已记录。分支344字段与当前Orca aa39c82e的Preset.cpp提取结果逐项/顺序完全相同，无缺项，见schema-comparison.json。

分支文档历史记录为2026-10-25：Java306通过、1可选跳过、生成器3通过、EPONE/Plus/Pro3真实样本通过；本轮未重跑这套完整历史测试。

### 与客户端修复的区别及建议

Orca客户端另有master提交`20d621103a`（修复云端导入差量工艺时线宽和支撑参数丢失），存在于本地master及codeup/master引用；当前Orca特征分支不包含。它修复桌面GUI云工艺导入，不能替代Java后台解析分支。

建议复用add2c3e后端修复，经目标分支合并回归和部署后，重新解析保存仍错误的方案/实际档位，并重新生成受影响模型系统切片。当前本例EPONE标准档已为0，无需为本例强行重传旧模板；重点是旧系统切片替换。历史错误方案的处理不能只发布代码而不修数据。解析根因已有实现，剩余工作主要是集成验证与旧产物处理，不需要重新开发Orca支撑算法。

## 2026-10-29 代码审核与简化评估

审核范围：add2c3e相对其父提交的10文件，重点是ThreeMFConfigExtractor、OrcaProcessConfigResolver、服务保存接入、344项字段资源及生成脚本、增加的测试。源码从Git精确提交读取；没有把目标分支与修复分支之间的基线差异当作本提交修改。当前服务主目录67440ba，工作区干净，未切分支或写入源码。

### 审核结论

没有发现需要阻塞本次工艺修复的确定性逻辑缺陷。当前工艺唯一匹配、项目有效值优先、显式0/false保留、工艺字段隔离、单工艺旧包回退以及异常不保存均有对应代码。机器/耗材沿用最大编号属于已声明的旧行为，不能把本提交描述为完整修复所有3MF预设选择。历史配置和G-code不自动更新属于交付范围，仍需操作处理。

核心两个类分别负责归档读取和工艺语义，139/128行，分工合理。无需为了“简化”合并大类、引入通用策略框架、删除校验，或直接将整个project JSON合并到工艺；后者会混入机器/耗材配置。344字段是自动生成的运行时分类数据，当前与Orca一致，删除它或只覆盖差量已有键会重新丢失继承参数。新建工艺/覆盖已有值/读取旧包的兼容规则应保留。

### 建议的小范围改进（未实施）

1. **支撑关键字段校验**：scripts/sync_orca_process_keys.py当前只要求layer_height、line_width、support_type存在。建议在已有必需字段集合加入enable_support、enforce_support_layers（并按约定补raft_mode等关键开关），在现有生成器参数化用例验证缺失时报错。诊断仅对内存中的Preset.cpp移除enable_support后调用原分支extract_keys，当前代码接受343键且缺少该开关，证据required-key-probe.json。实际提交资源仍含正确的344键，这不是当前失效点，而是低成本防回归改进；无需重复造一套运行时schema框架。
2. **保存链路的关闭支撑用例**：SliceParamConfigProfileSaveTest:570-627新增用例覆盖standard/fine/fast，但enable_support固定为1；解析单元测试覆盖的是Boolean false、不是“数据库工艺→CLI文件”的关闭场景。建议给原用例增加开/关参数，使通用模板取相反值，断言存储值与CLI文件中enable_support/enforce_support_layers仍为0。复用现有用例，避免复制整段多档位fixture。上一轮真实样本已证明当前实现正确，这属于回归覆盖加强。
3. **可选精简错误分支**：OrcaProcessConfigResolver:53-60可先对缺失字段continue，再检查JSON null和set，减少嵌套；仅改善可读性，没有性能收益，不需要为此拆分更多类/公共工具。选择循环的result/selectedEntry/matches规模很小，不建议机械改为stream或增加包装对象。

没有建议在本补丁中扩展到机器/耗材全套选择、缓存调度框架或全量数据库迁移。这些各自需要明确需求与样本，避免扩大本次已验证修复。

### 本轮验证

- 使用已核验的add2c3e生产类、Java17.0.18、本机既有Jackson/JUnit依赖，隔离编译并通过JUnit Launcher执行提交内原始ThreeMFConfigExtractorTest：30项全部成功，0跳过、0失败，含本机HTTP失败/空下载重试、解析失败不重试。
- 原始生成器测试：3项全部通过。测试在独立临时Git夹具中验证生成/漂移检测，未修改业务仓库。
- 上轮真实故障与更新3MF各344项一致的证据继续有效，本轮核对源码哈希未变；没有重复计为新增测试。
- 本轮没有运行完整Spring服务保存测试、全量Maven构建、实际CLI切片、数据库写入或GUI联调。历史306项结果没有当作本轮全量通过。
- 证据目录branch-verification/review：java-focused-tests.log、python-tests.log、required-key-probe.json；分析文档之外未改业务文件。收尾文档检查通过（35模块、0问题），本轮文档git diff --check通过。
