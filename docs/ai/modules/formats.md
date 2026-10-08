# 模型／工程格式、颜色与缩略图

<!-- module-review
{
  "id": "formats",
  "source_fingerprint": "sha256:2e8ff389a116b3d612ba19b0b8929a95313b4369bf6b3923af4edf0feb1c5edd",
  "reason": "保留原13类多盘回归和格式规则；增加12组合验证明确发送盘不被其他盘未切片/缺缓存拦截，全部盘仍拒绝不完整结果，成功包均读回。不为GUI当前盘修正放宽核心校验。"
}
-->

## 职责与边界

本模块负责模型／工程格式、颜色与缩略图。归属以[模块清单](../modules.json)的 `formats` 为准。

## 入口与调用

- [bbs_3mf.cpp](../../../src/libslic3r/Format/bbs_3mf.cpp)：工程、切片包、来源与嵌入配置的读写。
- [OBJ.cpp](../../../src/libslic3r/Format/OBJ.cpp)：`load_obj` 读取网格及材质信息，`store_obj` 导出模型／体积聚合后的网格。
- [STL.cpp](../../../src/libslic3r/Format/STL.cpp)：`load_stl` 通过网格读取接口导入；`store_stl` 接收网格／对象／模型并写出 STL。
- [Thumbnails.cpp](../../../src/libslic3r/GCode/Thumbnails.cpp)：图片加载、尺寸适配与编码。
- [PNGReadWrite.cpp](../../../src/libslic3r/PNGReadWrite.cpp)：`decode_png`／`decode_colored_png` 解码灰度和彩色图像；PNG 写入由同文件辅助接口承担。

## 核心流程

Model 导入分派 STL/OBJ/3MF/STEP 等格式；bbs_3mf 读写工程模型、配置、盘及切片数据，OBJ 保留材质或颜色映射，STL 主要保存网格。GCodeThumbnails 把渲染或读入的图片压缩写入 G-code；PNGReadWrite 是公共图片解码入口。

## 配置与约束

工程 3MF 与 gcode.3mf 不是同义：后者可能只有切片缓存，不能保证重新切片。来源版本、必需功能标记和配置兼容分别检查；不能修改品牌头绕过功能拒绝。

非增量 `WithGcode + SkipModel` 表示本软件输出的切片包，必须至少含一个目标盘有效且非空的 G-code。单盘只校验所选盘；全盘按 PartPlate 的可打印实例状态跳过不参与打印的盘，不能仅凭对象列表非空要求该盘切片。参与打印但切片失效的盘仍拒绝导出。没有可编辑实例但含切片/G-code 的导入盘仍需校验并保留，真正空盘可跳过；没有运行时快照的调用者对非空盘保守校验。GUI 和 CLI `--min-save` 共用此约束；普通带模型工程和增量备份仍可保存未切片状态。SkipModel 不含可编辑三角网格不是错误，工程及机型/工艺/耗材参数仍写入；保存模型应使用普通工程。

`PlateData::has_printable_instances` 是 PartPlateList 同时供 GUI/CLI 使用的临时导出快照，不写入工程协议。`is_selected_for_sliced_export` 统一预检、G-code、MD5、model_settings 内容及 slice_info 的盘选择，避免被跳过盘的旧缓存、缺失引用或本机绝对路径留在切片包。model_settings 对未选中盘仅保留原编号空槽，不带 G-code/实例/缩略图引用；读取器要求盘号连续，不能直接删掉前面盘的记录而让第二盘独存。普通工程不启用该筛选，不独立重排 G-code 文件名。

格式层没有“当前 GUI 盘”的上下文。发送端须先解析 GUI 哨兵再传非负盘号；不在核心把 `-1` 猜成首盘。两盘中只有目标盘有效时，明确选它可导出；全部盘必须继续拒绝其他参与盘的未切片/缺文件状态。

## 失败与取消

ZIP/XML/索引／图片解码及文件写入错误要保留上下文。缺 MTL 的几何可读与材质完整不同；导出修复后的当前模型不代表保存后总会重读完整检查。

切片包在预检、进度回调后及实际读取/压缩/复制入 ZIP 时检查错误，零个 G-code 或空文件不能报成功。并行压缩合并失败状态，只有成功写入才更新临时 PlateData 的包内路径；失败沿用临时文件回滚，保护已有目标文件，不保证读取过程中外部改写文件的原子快照。

## 修改与验证

改格式优先做读写往返、模型变换／盘名／颜色／配置保持、损坏输入拒绝；图片还检查 RGB/RGBA、方向和非法尺寸。外部产品互通需使用具体版本实际验证。

现有测试/工具范围：`tests/libslic3r/test_3mf.cpp`；`tests/libslic3r/test_obj.cpp`；`tests/libslic3r/test_stl.cpp`；`tests/libslic3r/test_thumbnail.cpp`；`tests/libslic3r/test_png_io.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

`[SlicedExport]` 覆盖有效/失效/缺失/空 G-code、无目标盘、选择盘不存在、单盘/全盘、导出中源文件消失及已有文件保护；另覆盖禁用盘有/无旧缓存、未知快照、纯 G-code 盘、全部禁用、普通工程不筛盘和首盘禁用后第二盘导出/读回。真实龙模型的包 CRC、G-code 内容和有效配置见[验证记录](../validation/2026-10-04-smart-print-engine.md#2026-10-07-悬空微区与切片包导出修正)。

## 分支现状

本分支使用自己的 bbs_3mf 来源/版本逻辑；未整体同步用户版 9ce27fdbcc 的分离兼容标记。能打开某文件不等于完整保留另一分支的配置。普通模型导出不自动做保存后完整复检；只有明确检测工作流的产物验证使用对应复检路径。

## 关联模块

[模型数据与网格属性](model.md)、[模型检测、修复与保色](model-repair.md)、[配置定义、预设继承与资源更新](presets.md)、[G-code 解析、统计与预览](gcode-preview.md)。
