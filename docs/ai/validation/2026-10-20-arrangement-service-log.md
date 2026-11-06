# 2026-10-20 分盘服务日志与移动端截图排查

> 归档说明（2026-11-06）：本页保留调查当时的分支、构建、测试结果与待办；其中“当前／未提交／下一步”属于记录时点。此次只整理入库，不将历史结果视为当前发布验收。归档范围见[文档整理记录](2026-11-06-feature-documentation-consolidation.md)。

## 当前执行状态

- 目标：核对用户 11:18 截图的薄片、141 mm 尺寸和盘外位置；本轮先分析与复现，不改变业务规则、不操作部署。
- 基准：`codex/model-feature-analysis-20261018 @ bc6684f18026721b6d19348d509e1e8c780a11e8`；master 为 `3239eab859`。已有模块文档、result.json、下载工具等无关修改保留。
- 输入：用户提供 `Explore-logs-2026-10-20 11_18_15.txt`。导出仅覆盖所选一小时的34.90%，共1000条；内有5次分盘请求，无对应响应JSON。
- 已完成：Mac复现5次请求、master二进制复核2次，Linux x86_64容器复现多盘/单盘2次，并做3次真实3MF/禁止旋转对照。Linux返回与用户补充响应中的135.001717°以及后续Y=-70.199999完全对应。未修改业务源码。
- 结论：模型本来是薄片；101 mm床面的自动排布范围先内缩至99 mm，且默认对象安全外扩仍存在；放不下后遗留试探旋转，多盘JSON只将位置居中、Z归零。后续单盘操作按旧规则导出真实盘外位置。
- 待决定：是否仅在CLI多盘JSON中为未摆入实例恢复排布前姿态；是否为移动端单盘“一键适配”另行定义失败时的行为。保留用户此前不改变GUI/原规则的范围，本轮不自动实施。
- 边界：线上ELF哈希和客户端渲染坐标实现尚未读取；用户补充的11:25响应已可定位服务端旋转，不能再将141 mm只归因客户端。Mac未复现该旋转，Linux已复现，不把平台结果混用。

## 证据与边界

原始模型、日志提取配置和请求摘要位于 `planning/arrange-log-20261020/`（不入库）。

| 输入STL | SHA256 | 原始范围/尺寸 |
| --- | --- | --- |
| `97075318-bafd-4072-b13c-bb581fa510b1.stl` | `08ddfabaddcb5f5fb3604ff30efb0e9bcaaea37c5e69c240aeb6ae2a111c37c4` | X/Y=-50..50，Z=-0.12..0.12；100×100×0.24 mm |
| `5930fbe0-f556-4936-9e28-e815bcd7b169.stl` | `d3daec00a935dea769993a256e51b373f9db17b0375afbd405f85a819bf1e121` | X=1..31，Y=70..100，Z=0..0.25；30×30×0.25 mm |

两个模型均为12面二进制STL。配置床面为0..101×0..101，限高105 mm，无bed_exclude_area字段，no_brim、by layer。

CodeGraph MCP未暴露；CLI v0.9.6可用，索引存在。系统Python普通只读连接失败，immutable只读成功；目标CLI与PartPlate索引最初过期，执行一次增量sync，更新15文件。随后CLI、PartPlate、Arrange.cpp/.hpp、ModelArrange.cpp哈希均匹配；使用query定位相关函数并阅读源码。未重建索引。

补充的Flutter日志只摘录必要业务字段到 `mobile-response-sanitized.json`，不保存Authorization或账户字段：11:25:29请求splitPlate=true、orient=false，响应plateIndex=1、position=(50.5,50.5,0)、rotateZ=135.001717、modelScale=1；随后prepare请求/响应保留这些值。模型详情接口在分盘前已经返回 `100mm(X)*100mm(Y)*0mm(Z)` 文案。真实STL厚度0.24 mm，具体取整实现尚未读源码。

## 原因与代码路径

1. **模型并未压扁。** 原始STL及实际3MF均为0.24 mm厚。100 mm方片旋转135.001717°后，世界XY包围盒为141.4213562×141.4213562 mm，缩放仍为1。`orient=false`关闭的是三维自动定向，不关闭默认启用的排布Z旋转（`allow_rotations`）。
2. **101 mm床不等于101 mm可排布范围。** `ArrangeParams::bed_shrink_x/y=1`，`get_shrink_bedpts`将0..101缩成1..100，即99 mm。该例`arrangeSpacing=0`也不代表删除所有安全量：`estimate_arrange_support_margin`默认1 mm，`update_selected_items_inflation`在距离为0时用该margin。单凭99 mm已经放不下100 mm；没有改床面或间距来掩盖失败。
3. **失败旋转仍传出。** `Arrange.cpp::_arrange`在包围盒过大时调用`fit_into_box_rotation`，底层`fitIntoBoxRotation`搜索区间达到eps后即返回角度，不保证实际放得下。后续`bed_idx=-1`，`arrange`仍无条件写回rotation。Linux本例日志为`bed=-1 rotation_deg=-224.9982833862305`，JSON归一后135.001717°。试旋转调用和无条件写回的Git追溯均来自既有`1555904beff`；稳定种子另有2026-09-07定制，不据此声称整条当前实现等同上游版本。
4. **JSON投影没有保证适配成功。** CLI的`export_separately`只在多盘模式把越界/未归属实例的position改成床中心及Z=0，rotation/scale仍取实际结果。这符合此前“位置单独返回、旋转缩放保留”的实现，却会把失败试旋转显露给客户端；虚拟返回盘不代表实际可打印盘。该次未执行切片，exit0仅说明导出完成。
5. **后续负Y来自单盘旧行为。** 日志11:17:25开始`splitPlate=false/--auto-plate 0`，不会使用多盘居中规则。`PartPlateList::postprocess_arrange_polygon`将bed=-1移至盘外，CLI强制回盘号0但保留世界坐标。本例100 mm形状、深101 mm、stride=101×1.2=121.2，Y=101−50−121.2=−70.2；Linux实际JSON/3MF与随后客户端再次传入的Y吻合。
6. **Z=-49.88不是该次导出的底面。** 11:25响应positionZ=0，早先11:17:25请求的-49.88属于其他阶段回传，可能涉及客户端世界坐标换算，尚未定位具体实现。当前`--model-position`只使用输入XY，目标底面Z固定0，另有ensure-on-bed，因此该输入负Z不是本例压扁或移出XY的根因。
7. **日志ERROR不是本次退出失败。** `calc_exclude_triangles`是PartPlate禁放区显示几何的生成日志，正常小薄片也出现同一条，所有本轮导出exit0。无禁放区配置时仍调用生成入口；它不是本例135°或负Y的来源。

## 处理建议与既有边界

- 优先约束失败结果：如果遵循只改CLI返回的原范围，应保存排布前的有效姿态，仅对真正未摆入的多盘JSON实例使用该姿态及约定的中心/Z0；保留实际自动定向、用户缩放和镜像，不能简单将所有rotation清零。需增加Linux真实输入回归，覆盖成功实例不变和JSON开关不改3MF。
- 单盘“一键适配”另有调用语义：当前就是旧盘外位置输出。若继续维持此规则，客户端应识别仍越界并避免显示适配成功；如果要失败时保持原位置或居中，应单独确定接口行为。不能在本轮悄悄将单盘也改成多盘投影。
- `--allow-rotations 0`对照证明可消除135°，但会全局影响可摆入模型，且本例仍未排入。不是已验证的完整修复方案；不建议只靠扩大虚假床面解决。
- 薄片高度文案应保留小数，否则0.24/0.25 mm均显示成0；此项属于展示，独立于引擎变换。

## 运行验证

五次请求的machine/process/filament分别逐JSON核对完全一致。复现沿用原值，仅将debug=1改为4以记录排布过程，替换本地路径及输入basename；导出实际工程的3次对照另加`--export-3mf`。

| 场景 | 结果 |
| --- | --- |
| Mac五次请求 | 均exit0；多盘大薄片返回独立盘中心/Z0，单盘返回Y≈-70.2；大薄片初始rotation为0，未复现Linux135° |
| master Mac两次 | 多盘/单盘均exit0，结果与开发分支Mac一致 |
| Linux x86_64多盘 | exit0，plate_index=1、position=(50.5,50.5,0)、rotation=(0,0,135.001717)、scale=(1,1,1)，与用户11:25响应对应 |
| Linux x86_64单盘 | exit0，plate_index=0、position=(49.999999,-70.199999,0)、rotation=(0,0,0)、scale=(1,1,1) |
| Linux实际3MF多盘 | JSON同上；真实几何仍在盘外，位置(70.710678,-90.910678,0)、尺寸141.4213562×141.4213562×0.239999994；JSON居中确为只读投影 |
| Linux实际3MF单盘 | JSON与真实底面中心均(49.999999,-70.199999,0)，尺寸100×100×0.239999994 |
| Linux禁止旋转对照 | JSON中心/Z0、rotation全0；实际3MF仍在盘外(50,-70.2,0)，厚度不变，未排入不是修复成功 |

Mac当前应用SHA256为`88aa9af3e6b4ed0bf692a0b063e1244579c77ae394cf33f39d3b1f4daacfd46b`，与本轮合并构建证据匹配；master应用为`cf0678709d362c2991e9e2be5e2a2a9e8b61c78e9e5f756c299625416694475d`。Linux使用已构建的本机Docker容器`orca-meshlib-linux-40d8-probe`内`/work/orca/build/package/AppRun`，ELF SHA256为`914a928c6ad1b53d95c372ca34952e4f3f40a1ee5605bc6aca7bd94843b85732`；CLI/Arrange/ModelArrange源码哈希与当前工作区一致。没有重编译、替换部署包或调用线上接口，不能据此确认线上ELF身份。

CLI/图形变换证据、命令与日志均在上述planning目录。未操作原生GUI/移动端；用户两张图及Flutter日志作为用户实测证据。本轮只分析不改源码，无新增GUI验收任务或正式功能回归通过声明。
