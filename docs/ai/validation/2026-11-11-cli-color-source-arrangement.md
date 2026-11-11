# CLI 按颜色与来源分盘

基准为 master `4d6a64716d`，包含工作区已有的超大件展示盘顺序改动。Orca CLI 实现及 macOS arm64 Release 验证完成。服务端接入和 Linux 环境验证属于后续工作。

## 实现

两种全局开关维持互斥。颜色模式收到来源号时使用独立的候选规则；沿用空间分盘已有的试摆、床面和逐盘擦料塔约束。正式采用前先试所有同色伙伴。无伙伴的单色小组可以重试已接纳同来源混色件的盘，也可以与列表后方的同来源混色件一起试摆。

在同一遍 OBJ 导入中按实际 `usemtl` 取色。没有可用面色、部分未绑定或缺少 MTL 时进入未知色组。部分有色的 OBJ 保留可确定面的涂色，未绑定面使用默认槽。如果调用方为组件只绑定一种耗材，该槽也适用于首次 `usemtl` 之前的面。只有顶点颜色或 UV 纹理的 OBJ 可通过显式材质绑定提供打印色。

第一次切片对照发现，OBJ/STL 混合输入在共享默认耗材时，后一个 OBJ 可能覆盖前一个 OBJ 的自动色表。联合模式复用多 OBJ 自动色表合并和补槽逻辑，已从实际工程读取颜色与槽位，对照两个使用相反材质顺序的 OBJ。

## 输入示例

以下文件均为已拆好的组件，左侧两个模型参数块对应同一个来源。机型、工艺与耗材使用已解析的 JSON。

```sh
mkdir -p out
./build/arm64/src/Release/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
  --load-settings 'machine.json;process.json' --load-filaments filament.json \
  --arrange 1 --auto-plate 1 --arrange-by-color 1 --ignore-wipe-tower \
  --model body.obj --model-source-group 1 \
  --model trim.stl --model-source-group 1 --model-color '#FF0000' \
  --model base.obj --model-source-group 2 \
  --export-model-transforms --outputdir out body.obj trim.stl base.obj
```

结果位于 `out/result.json` 的 `model_transforms`。加 `--export-3mf layout.3mf` 可核对真实盘，加 `--slice 0` 可切片。省略 `--ignore-wipe-tower` 时沿用实际工艺的擦料塔设置。两种来源分盘方式均接受最多 10 个不同来源号。

## 结果

| 输入 | 观察结果 |
| --- | --- |
| 来源 1 红黑、来源 2 黑红、来源 1 纯红与纯绿、来源 3 红黑绿、来源 4 纯红 | 四盘。红黑与黑红同盘，两个纯红同盘，纯绿一盘，红黑绿一盘；每盘的实际切片包有 G-code |
| 移除跨来源的纯红伙伴 | 来源 1 的纯红回到红黑盘，纯绿独盘。纯红先于红黑传入也可回盘 |
| 不可识别的颜色、部分面缺色与显式黑色 | 未知件同盘，纯黑独立匹配。部分面缺色且显式绑定统一耗材时可与同打印色的 STL 合盘并切片 |
| 同来源两个纯红附件，只有小件装得进当前盘 | 整批跳过，尝试后续来源。两个附件在下一盘一起排 |

构建命令为 `cmake --build build/arm64 --config Release --target OrcaSlicer libslic3r_tests --parallel 10`。实际 CLI SHA256 为 `ada1206f2ea7b55e13c1c61ddc20a3820392b93dcade27b0e5a0cca02a59a0dc`。以下均为该二进制或同次构建的测试目标结果。

| 验证 | 结果 |
| --- | --- |
| `test_cli_source_group_arrange.py` | 执行 35 条 CLI，包括预期的参数拒绝。核对来源组、旧主色、联合模式、擦料塔、真实盘与展示盘 |
| `test_cli_model_color.py`、`test_cli_multi_obj_palette.py` | 14 个旧 STL 颜色用例、25 个多 OBJ 色表与换料用例通过 |
| `test_cli_oversized_arrange.py`、`test_cli_fixed_position_slice.py` | 184 次超大件 CLI、64 个固定位置切片用例通过 |
| `libslic3r_tests '[OBJ],[Arrange]'` | 82 个原生用例、9214 项断言通过 |

多 OBJ 旧回归脚本会清空层切换 G-code 并启用擦料塔，使用 Marlin 相对挤出配置时缺少所需的 G92 E0。该套件最终使用 Klipper 相对挤出测试配置；其余测试按各自脚本的配置运行。新模式没有 GUI 入口，本轮运行了共享 OBJ 和排布的原生用例，未进行 GUI 手动操作。

## 本地耗时

100 × 100 mm 平台，每次启动 CLI 导出变换 JSON，忽略擦料塔。十个不同来源各传一个四面体 OBJ，十件均用红黑，交替使用相反的材质顺序。两个模式各预热一次，交替运行，各取随后五次的中位数。

| 组件 XY 跨度 | 旧主色分盘 | 联合分盘 |
| --- | --- | --- |
| 6 × 6 mm | 100.6 ms，2 盘 | 101.4 ms，1 盘 |
| 65 × 65 mm | 93.1 ms，10 盘 | 97.0 ms，10 盘 |

测量范围为本机短进程及简单几何。生产模型面数、组件数、占床比例和擦料塔重排会改变耗时。命令、测量及套件日志在本地 `build/diagnostics/source-palette-20261111/`。

## 16:28 发布包兼容性复核

本地发布任务实际日志为 `planning/build/macos-build-arm64-20261111-162800-742647.log`，退出码 0。运行的打包产物位于 `build/arm64/OrcaSlicer/OrcaSlicer.app`，主程序 arm64，最低 macOS 14.0，SHA256 为 `6bc1741a7abe2d73886e821639062edf20f6be04e9e299b9667d918be800b514`。

没有来源号的普通排布和主色排布继续走原排布器。指定 `--arrange-by-source-group 1` 时，仍要求每件有来源号。与 11 月 10 日打包产物的留档配对，184 次越界用例、64 次固定位置用例以及 4 次有 JSON 的旧颜色用例的返回码和已有模型变换相同，18 份含 G-code 的工程逐条非注释指令相同。本次发布包还通过 35 条来源组调用、14 个旧颜色用例和 25 个多 OBJ 色表用例。另用普通排布、无来源号复核三件超大的例子：仅导出预览时，前三件超大模型获得展示盘 0、1、2；同时导出 3MF 时它们使用实际盘之后的 2、3、4。该编号来自此前单独的展示盘顺序需求。

复核遇到两项联合模式的边界，留待修正：

1. 未绑定每件耗材，且没有传全局默认耗材，或传入多个不同的全局耗材配置时，多 OBJ 沿用既有逐文件覆盖自动色表的路径。依次导入红黑、黑红、纯红三个 OBJ，实际切片成功，但 3MF 的项目色表只剩白、红。单份共享默认耗材的对照正确保留白、红、黑。关闭来源号的对照也能复现色表覆盖。
2. 一个无 MTL 颜色的 OBJ 和一个小 STL 足以同盘，两件均通过 `--load-filament-ids 2,2` 绑定槽 2。联合模式将它们分在不同盘，3MF 中两件的对象耗材均为槽 2。颜色推断只识别了 STL 的旧式绑定。

本轮使用本机 arm64 发布包，未执行 GUI 手动、Linux 或实机打印。命令、日志、脱离根目录的输出及结构化对照在 `build/diagnostics/packaged-color-review-xNKozhgI/`。
