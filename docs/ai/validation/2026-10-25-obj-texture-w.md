# OBJ 第三个纹理坐标分量兼容修复

## 2026-10-30 入库复审（审核及本地验证完成）

- 用户授权审核、测试通过后只提交 OBJ 修复到 master；未授权推送或打包。当前基准 `c0ccf31b22f1e4f8214699b401e896adc1475319`，目录 `orca-slicer-master`，暂存区为空。
- 已核对当前 parser/test_obj 哈希与下文历史记录一致；旧 `/tmp` 测试日志现已不存在，本轮必须重跑，不能用旧记录代替当前验证。
- 范围：可选第三纹理分量读取后仍只存二维 UV；现有正负索引、越界检查和 GUI/CLI 共享导入保留。不改网格、修复、切片参数或分盘。原有支撑分析及固定位置历史文档不提交。
- 核对现有构建为本目录 macOS arm64 Release、MeshLib OFF，无其他构建进程。下一步最小更新 OrcaSlicer/libslic3r_tests，运行 OBJ 专项和普通 CLI，补真实模型输入核查；证据保存在 `build/diagnostics/obj-texture-w-review-20261030/`。

### 本轮实际验证结果与提交范围

- 对当前 master 执行 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 2`，退出0。应用已含当前修改，无需重新编译；测试程序完成重新链接。源码/测试哈希与历史修改相同，程序身份另写 `build-identity.json`。
- `[OBJ]` 专项41用例、5900断言通过，覆盖两/三分量混用、非零w、正负索引、越界拒绝和原有材质/保色/几何导入。
- `test_cli_fixed_position_slice.py` 的58个普通CLI场景全部符合预期，包括有效出码及非法/盘外输入的预期拒绝，不将拒绝案例说成出码成功。
- 从原始URL重新取得EP7KE.obj，SHA256与历史记录完全一致。它包含512个顶点和一条三分量纹理记录；注释不是UTF-8，因此仅按原始字节分析ASCII记录，没有重编码原模型。
- 使用隔离CLI回归参数、220 mm床面、旋转X=90度、缩放1及位置110,110,0执行原文件和“仅删vt第三分量”的副本。两者正常校验、均退出0，生成339616字节G-code，12221条G0/G1/G2/G3/G92指令完全一致；耗时分别0.131/0.133秒。这不是历史EP7生产工艺的重放，也不是跨模型性能基准，更不代表实印质量验收。
- 当前应用SHA256：`aaf7539e8a55e62c3a9b2d0448abb5db261f4fe89151177bd7e9450ac72b18b8`；测试程序SHA256：`e1fcfa43c744610d8627d0bad2af30f44762c18d29ec6b2eb1b25e0ca49e858b`。证据目录保存命令、退出码、完整日志和输入/输出身份。
- 仅提交 `objparser.cpp`、`test_obj.cpp`、formats/test-tools模块正文和本记录。不提交原有support文档、固定位置历史复测记录、Cura首层间隔分析；不自动搬运到特征/发布分支，不推送或打包。
- GUI/CLI共用同一解析器，本轮不增加UI或翻译。GUI手测、Linux/Windows运行仍待验证；源码审阅未发现其他切片、分盘、修复路径改动。GUI手测应使用当前master应用，在空项目导入本轮证据目录的原始EP7KE.obj，确认没有纹理索引错误，切片预览及导出正常；含真实MTL/纹理贴图的原用户工程另需人工核对显示，不能用缺贴图的单OBJ回归代替。

### 剩余工作区核查

- master提交本项后仅剩support模块中的梵高首层分析链接、固定位置100次历史复测记录及Cura首层间隔分析文档，无其他待提交业务源码。
- 特征目录另有本地参数审计/下载脚本及历史文档、result.json；发布目录有一份未跟踪支撑分析文档。以上不属于此次OBJ修复。
- 2026-10-30核查threeDService下已存在的Git工作目录：Slicer、Print3DService、Compare、CompareWeb、参数匹配等未发现未提交业务改动；Blender仅有Python缓存。完整当时状态保存在 `other-workspaces.json`，不据此推断各远端合并或部署状态。

## 2026-10-25 历史执行状态

- 目标：按上游方式接受 `vt u v [w]`，仅保留 u/v；现有纹理索引越界校验不变，原模型不改写。
- 工作目录：`projects/slicer/orca-slicer-master`，分支 `master`，基准 HEAD `625b37a2669cf5d21a8df3779d68297a7843a6da`。
- 本次修改：`objparser.cpp`、`test_obj.cpp` 和格式模块／本验证文档。工作区原有的 `2026-10-19-cli-fixed-position-slicing.md` 修改不属于本次任务。
- 已完成：旧 master 程序验证原始 EP7KE.obj 导入失败，退出码 250；补充 optional w 解析及混合 UV、正负索引、越界保护回归。
- 当前状态：master 应用和测试目标增量构建、OBJ 专项及原文件完整切片均通过；未提交／推送。GUI 尚未手测，步骤见下。

## 决策与范围

参考 [OrcaSlicer 上游修复 8ffd3e5](https://github.com/OrcaSlicer/OrcaSlicer/commit/8ffd3e514ec9b69fcc1550bec577f5ffdd5aa9dc) 的 optional w 解析：读取第三分量但不写入二维 UV 数组。仅移植此修复；不采用非法 UV 回退为零坐标的行为。

GUI 模型导入和 CLI 经 `Model::read_from_file` / `load_obj` 共用此解析器；`OBJ.cpp` 的负数、越界及四边形第四顶点索引检查保留，显式 geometry-only 的既有行为不变。不改切片工艺、几何修复或模型内容。

## 输入与修复前证据

- 模型：纸箱猫狗冰箱贴／EP7KE.obj，任务 `1563605825516568576`，EP7，模板 `1527245869844996096`（树状支撑无brim）。
- [原模型下载](https://cdn-h.gongfudou.com/print3d/permanently/2026-10-24/b5718848-1b79-4f02-a16c-8927ffb9fd66.obj)。
- 本地原文件：`/tmp/slice-failure-analysis-20261025/models/40e726a0aa1e.obj`。
- SHA-256：`760afb0c98601b1f5aef5de3df886755c412a57184892d67735057e4f96d64cb`。
- 第 840 行为 `vt 0.0000 0.0000 0.0000`。旧解析器丢弃该记录，后续 UV 索引因记录缺失被拒绝。
- master 修复前二进制 SHA-256：`189ab1d3a56dbd26ee14805db8b850a31ec828a3df93c2e39bdb7c7638cda6ea`；退出 250，结果 JSON `return_code=-6`，0.124 秒。
- 命令、日志及结果：`/tmp/orca-obj-vt-fix-20261025/before/`。沿用原复现的机器／工艺／耗材、模型旋转、位置和缩放。

## 构建与验证

复用本工作目录的 `build/arm64`（Ninja Multi-Config，Release，arm64），已核对 `CMAKE_HOME_DIRECTORY`。`cmake --build build/arm64 --config Release --target libslic3r_tests OrcaSlicer --parallel 2` 已通过，退出 0；`build.log` 确认 objparser/test_obj 编译及应用／测试重新链接。

`build/arm64/tests/libslic3r/Release/libslic3r_tests.app/Contents/MacOS/libslic3r_tests '[OBJ]'` 退出 0：41 个测试、5900 条断言全部通过。覆盖 mixed UV 的 w=0、w=0.5、w=-0.5，绝对／相对索引、三分量纹理记录下的越界拒绝；现有缺省 UV、正负越界、第四顶点越界及保色导入／导出专项也通过。日志：`/tmp/orca-obj-vt-fix-20261025/obj-tests.log`。

### 原文件切片结果

相同模型 SHA-256、机器／工艺／耗材 JSON、旋转、位置和缩放，只替换新构建二进制与输出目录：

| 项目 | 修复前 | 修复后 |
| --- | --- | --- |
| 进程退出码 | 250 | 0 |
| 结果 JSON | `return_code=-6`，无法解析模型 | `return_code=0`，`Success.` |
| 完整进程耗时 | 0.124 秒（导入失败） | 1.194 秒（切片成功） |
| G-code | 无 | `plate_1.gcode`，564360 字节 |

修复后结果有 1020 个三角面、预计耗材 16.83 g、预计打印时间 3618 秒，无切片警告。G-code 包含带 XY 和正 E 参数的移动及完整配置块；并非只有结果 JSON。原文件校验值保持不变，不依赖前期删除 w 的临时模型。成功切片不证明实机打印质量。

命令、运行日志、结果、G-code 检查摘要在 `/tmp/orca-obj-vt-fix-20261025/after/`；G-code 位于 `after/output/plate_1.gcode`，SHA-256 为 `47dc728b38b22173d0e5b43a50fb40d862cd423c2f3ed4a40a2ff90493bf6c0e`。`result.json` 的 `output_files` 沿用既有空列表，实际 G-code 已单独核验。

### 构建身份

完整 SHA-256 列表保存在 `/tmp/orca-obj-vt-fix-20261025/build-identity.json`：

- `objparser.cpp`：`2895b7f4fda18c6a22b282b3074b14c0ee85eadb9df0b66883f7668071163a97`。
- `test_obj.cpp`：`0b2869f286b491562cbc2b3cc9a1334088ee066d4515b2b8cc78f8ce976e63a1`。
- 应用：`f9859ccb6985bb8600cdc62f0fcf4a93d320179c0172136a0f52b2ec76c4b058`。
- 测试程序：`d9efd4772987e9db8c2d7b7bc3263a78fe91e0d96362c05bc8fe610f8a4a8ae0`。

`OBJ.cpp` 未修改；核对差异确认索引校验完整保留。本次只验证 macOS arm64 Release，不代表 Linux/Windows 已编译运行。

## GUI 验证步骤（待执行）

1. 使用本工作目录新构建的 `build/arm64/src/Release/OrcaSlicer.app`；不要打开其他分支或旧打包目录的应用。
2. 在空项目导入上方原始 EP7KE.obj，不预先删除纹理第三分量。预期模型正常出现，没有纹理坐标索引错误。
3. 使用 EP7 和对应树状支撑无brim工艺，模型旋转 X=90°、缩放 1；对照 CLI 记录的有效参数及位置后切片、查看预览、导出 G-code。
4. 非法纹理索引仍应拒绝；自动化会覆盖此边界，不应通过关闭材质解析或重写模型绕过。

CLI／自动化通过不等于 GUI 操作、其他平台或实机打印已验收。
