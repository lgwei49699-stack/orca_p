# 模型修复验收夹具

2026-09-08：代码及夹具已准备，**尚未编译、运行应用或测试**。本目录的 `manifest.json` 是验收标准，不是通过记录。
这些为本仓库原创的确定性合成几何，单位 mm，不是从 Thingi10K 下载的模型。常规组同时提供 STL / OBJ；第 11 组的 OBJ 引用同目录 `colors.mtl`。

## 2026-09-13：缺少 MTL 的导入规则（待重新编译验证）

- GUI 和 CLI 共用 OBJ 导入规则：引用的 MTL 缺失，或存在 `usemtl` 但没有声明 `mtllib` 时，忽略整个 OBJ 的源颜色和纹理，继续读取几何；不再要求手动添加 `--repair-ignore-materials`。多个 MTL 中有一个缺失也按整模型降级，避免只保留一部分面颜色。
- GUI 显示中文/英文导入提示；CLI 保留警告日志，修复报告的 `input_materials_ignored=true` 反映实际降级。无材质声明的纯几何 OBJ 正常导入，不提示材质缺失。
- 已存在但无法解析的 MTL、MTL 中缺少被使用的材质定义（严格保色任务）、非法顶点索引、非有限坐标等仍报错；缺少材质不会掩盖几何错误。显式 `--repair-ignore-materials` 仍可用于主动跳过损坏的材质。
- 用户显式指定的 `--model-filaments` 继续按面材质名绑定替代耗材，不因源 MTL 缺失而清空这些手动配色。该配色不等于恢复了缺失的原始颜色。
- 用例标签为 `[OBJ][MissingMTL]`，仍包含于完整 `[OBJ]` 专项；`scripts/test_cli_repair_materials.py` 增加缺失/部分缺失/无库声明、修复及重导出、错误几何保护用例。补丁和用例已添加不代表测试通过；本轮由用户重新构建后运行验证。

## 此轮范围

保持 CGAL 为统一高级修复入口，不回退 Windows 修复，不引入新重建引擎、不修改第三方库。

- 保留已有精确去重、退化清理、统一朝向、非流形处理、补洞、自交检测/受限修复。
- 在正常开放网格补洞前补基础边界清理：移除无边邻居的孤立单面；复用 Orca 的 ADMesh 近邻匹配算法修复同一连通部分的小缝隙。
- 近邻容差为 `min(包围盒对角线×1e-5, 最短边×1e-3, 0.005 mm)`；两次尝试（半容差、全容差）。总移动不得超过全容差；只接收面数不变、闭合、无退化、无非流形顶点、无自交的候选。失败则丢弃这次近邻候选，原边界交由既有 CGAL 补洞及检查。这不是 Windows 修复回退。
- 不把分离零件相互焊接；不因体积小、厚度小删除有效实体；不把复杂顶点拆分候选送入新的实验性修复。
- 开放网格补洞也检查原有表面、包围盒及连通部分保留。补洞能增加面，不能推断原始设计或证明补出的形状就是用户想要的形状。
- 修复失败：GUI 不替换原模型；CLI 以非零状态退出。修复成功后按实际打印参数切片，仍要检查警告和逐层预览。
- CLI 直接“修复 + 切片”也启用严格 OBJ 面颜色保留；保色失败时拒绝。仅需几何可显式使用 `--repair-ignore-materials`。STL 不含材质；不支持原 UV、贴图、完整渲染材质往返。

这补齐的是基本处理类别，不是照搬旧 ADMesh 所有容差和破坏性行为，也不保证任何损坏模型都能修好。NaN/Inf、越界索引、空几何继续拒绝，不猜测缺失坐标。

## 样本与预期

| 文件名前缀 | 问题 | 预期 |
|---|---|---|
| 00-healthy-cube | 健康基准，10×10×10 | 成功，不改变几何 |
| 01-missing-top | 删除两个顶面三角形 | 成功，补回顶面 |
| 02-small-crack | 单面一角偏移 0.00001 | 成功，合缝后仍 12 个面 |
| 03-wrong-winding | 3 个面朝向反转 | 成功，体积为正且方向一致 |
| 04-duplicate-face | 重复底面，边多重连接 | 成功，去重 |
| 05-degenerate-face | 共线面、重复顶点面 | 成功，删除无效面 |
| 06-isolated-face | 立方体内部的孤立单面 | 成功，移除单面，保留立方体 |
| 07-thin-solid | 正常实体 + 0.001 mm 薄实体 | 几何通过，两个实体都保留；普通层高未必可打印 |
| 08-intersecting-shells | 两个立方体大范围相交 | 拒绝；不改变原模型 |
| 09-nonmanifold-edge | 四面共边，拆分后仍接触 | 拒绝；不进入危险实验性修复 |
| 10-only-triangle | 只有一个平面三角形 | 拒绝；无可保留实体 |
| 11-colored-missing-top | 红绿面颜色与缺顶面 | 成功，原色保留；新面默认部件色 |
| 12-invalid-index | OBJ 面索引 99 越界 | 导入拒绝；不进入 CGAL |

除第 07 组外，成功组期望体积约 1000 mm³；第 07 组为 1000.1 mm³。新截面用例在 0.1、0.3……9.9 mm 取 50 层，关闭二维补缝，与健康立方体比较轮廓数、孔数量与截面积。它不是喷嘴/耗材/支撑/G-code 的完整测试。

## 1. 用户编译、运行单元测试与 CLI 几何验收

仓库根目录，现有 macOS arm64 Release 构建配置；只更新 GUI 不会更新测试程序。下面整段在编译失败时会停止，不会接着跑旧二进制。

```bash
bash <<'BASH'
set -e -o pipefail
mkdir -p planning/build planning/repair
repair_stamp=$(date +%Y%m%d-%H%M%S)
/Applications/CMake.app/Contents/bin/cmake --build build/arm64 --config Release \
  --target OrcaSlicer libslic3r_tests --parallel 2 \
  2>&1 | tee "planning/build/repair-acceptance-build-${repair_stamp}.log"
./build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests \
  '[ModelRepair],[MeshBoolean],[SavedPainting],[OBJ],[stl]' --reporter compact \
  2>&1 | tee "planning/repair/repair-acceptance-unit-${repair_stamp}.log"
python3 scripts/test_cli_repair_acceptance.py \
  ./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  2>&1 | tee "planning/repair/repair-acceptance-cli-${repair_stamp}.log"
python3 scripts/test_cli_repair_materials.py \
  ./build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  2>&1 | tee "planning/repair/repair-materials-${repair_stamp}.log"
BASH
```

现有目标需要已配置 `BUILD_TESTS=ON`；不更改构建页面菜单。聚焦新测试可用 `'[ModelRepair][Acceptance]'`，不能取代完整专项。Windows/Linux 换成相应平台的构建目录和新可执行文件，不能据 macOS 通过就声称跨平台通过。

CLI 脚本每次创建 `orca-repair-acceptance-*` 临时证据目录，保留各例的 `command.json`、`run.log`、`output/result.json`、修复产物以及 `summary.json`。失败必须有错误报告，闪退、超时不是“预期拒绝通过”。脚本检查输入哈希、输出体积、MTL 色值和导出后重导入无需再次修复。未提供切片配置时明确输出 `SLICING NOT VERIFIED`。

## 2. 实际服务器切片验收

### 验收工具与导出拓扑（2026-09-08 收尾补丁）

- 可先运行 `python3 scripts/test_repair_acceptance_helpers.py -v`，无需编译；仅验证脚本和夹具，不验证 C++ 修复代码。
- 新版验收直接检查 OBJ 索引连接，不把材质接缝的重复顶点先焊接后算通过；仍要求导出重导入无需再次修复。
- `10-only-triangle.stl` 的 solid 名称已补长至满足当前 STL 读取器探测长度。它必须成功导入后在修复阶段失败、不提交，不能只看最终进程非零返回码。
- 修复不能删空整个对象。若平面杂片与有效实体属于同一对象，仍允许清理杂片；其他对象或仅剩修改器不能掩盖被修复对象已经没有实体。
- `'[OBJ][Topology]'` 覆盖整面颜色接缝、单边细分 T 形接缝、镜像实例、色面积及独立壳体/部件/对象/实例不误合并。重新编译后运行，不能使用旧测试二进制的通过数字。

将以下占位路径替换为服务器实际使用的、继承已正确解析的完整配置。打印机、工艺、耗材配置必须相互匹配；彩色样本需要两个耗材槽。不使用自动跳过检查选项。

```bash
python3 scripts/test_cli_repair_acceptance.py /absolute/path/to/orca-slicer \
  --load-settings '/absolute/printer.json;/absolute/process.json' \
  --load-filaments '/absolute/filament1.json;/absolute/filament2.json'
```

该模式在几何验收之外，对成功的 STL、OBJ 原始样本运行 `--repair-model cgal --arrange 1 --slice 0`，确保修复发生在切片之前；不切第 07 组低于层高的薄实体。任何切片警告都会使验收脚本要求复核，不把所有警告都判断成 CGAL 错误。不会发送到打印机。

`result.json.repair_validation`：

- `mesh=passed`：当前输入的修复/完整网格检查通过，不代表物理可打印。
- `slicing=not_requested`：只修复/导出，没有切片。
- `slicing=completed`：本次存在完成的切片；须结合 `return_code=0`、`completed_plates`、预期盘数、非空 G-code、正耗材用量和警告判断。不是“无条件可打印”认证。
- `slicing=failed/not_completed`：本次任务失败或切片未完成；查看错误及分阶段报告。修复前报错时也可能为 `failed`，须结合 `mesh` 判断阶段。

修复 + 切片拒绝 `--no-check`、`--allow-newer-file`、`--load-slicedata`，避免跳过参数检查或拿旧模型缓存充当新修复模型的切片结果。普通不修复的旧命令不受影响。

警告检查覆盖 `Print::process()` 后与 `export_gcode()` 后两个时机，因为空挤出层警告可能直到 G-code 生成阶段才出现。CGAL 修复 + 切片遇到 CRITICAL 警告或冲突即失败；若刚生成了拒绝的 G-code，会尝试移除它。删除失败会记错误日志，调用方仍必须按非零返回码拒绝所有产物，不能直接遍历目录拿文件去打印。此后置门禁仍需真实运行验证；现有固定夹具不是所有 G-code 警告类型的穷举测试。

## 3. GUI 手动复核

1. 重新启动新编译的应用，新建空项目，选择实际打印机/工艺，先导入 `00-healthy-cube.stl` 切片，确认基准环境正常。
2. 每次清空当前测试模型，单独导入 01～06 组 OBJ。OBJ 可避免 STL 旧导入修复提前处理掉缺陷。右键“修复模型”，核对尺寸约 10×10×10、体积约 1000，以及导出后重导入的状态。
3. 看逐层预览：连续实体截面，无断层、异常填满/挖空、漂浮轮廓。01 顶面应闭合；02 不应多出薄壳。不要只看右下角非流形边为 0。
4. 08、09、10 应提示失败，模型仍在且几何不变；12 应导入失败。不得以这些失败否定验收，出现闪退则属于失败。
5. 11 的 OBJ 与 `colors.mtl` 保持同目录。显式“修复模型”保留支持的绘制信息，不依赖实验性的“网格变更后保留绘制特征”开关。修复前后检查红绿面和耗材列表；新增面采用默认部件色，不新增无来由颜色。导出 OBJ 后连同 MTL 重导入比较。另以缺失 MTL 的副本验证纯几何降级及提示，不删除原夹具的材质文件。
6. 对成功修复执行 Undo/Redo，再切片；失败和取消不应替换原网格。07 只验证薄实体不被删除，不要求当前工艺能打印这层厚度。

## Thingi10K：补充真实样本，不代替可控标准答案

[项目 README](https://github.com/Thingi10K/Thingi10K) 明确包含开放、自交、非流形、退化等真实缺陷，可作为第二阶段压力回归。应选 `raw` 原始网格，不使用已经 TetWild 重网格化的变体作为“损坏输入”。每个模型各有许可证，须记录原始 ID、来源、许可证、文件哈希；仓库代码 Apache-2.0 不代表模型全部同许可。

本轮只核查数据集，没有下载全量包或把外部模型放进本仓库。该数据集也没有为我们的保形/保色业务提供统一修复标准答案。建议先跑本目录，固定行为后再选少量真实异常模型补充回归，不宣称对全部 10,000 个模型可修复。README 的 49911（截断 STL）、74463/286163（空文件）、77942（损坏二进制 STL）、81313（NURBS OBJ）可用于导入拒绝测试，不能要求 CGAL 从无效文件重建原模型。

## 入库门禁

必须由新构建通过完整专项、CLI 几何与材质验收、实际配置切片及 GUI 复核后再判断。当前只完成源码修改和静态检查；没有此轮运行通过数据。现有闭合自交候选仍使用此前的受限 CGAL 实验性分支，本轮没有扩大它；复杂模型仍可能耗时或安全拒绝，不能承诺所有 CGAL 原生库风险归零。
