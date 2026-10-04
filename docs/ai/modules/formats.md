# 模型／工程格式、颜色与缩略图

<!-- module-review
{
  "id": "formats",
  "source_fingerprint": "sha256:6b3ee13c8cce68ba6d5dc2a982f215f9ac516ea7698a0b8fe72f26ab0fbf3f9e",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
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

## 失败与取消

ZIP/XML/索引／图片解码及文件写入错误要保留上下文。缺 MTL 的几何可读与材质完整不同；导出修复后的当前模型不代表保存后总会重读完整检查。

## 修改与验证

改格式优先做读写往返、模型变换／盘名／颜色／配置保持、损坏输入拒绝；图片还检查 RGB/RGBA、方向和非法尺寸。外部产品互通需使用具体版本实际验证。

现有测试/工具范围：`tests/libslic3r/test_3mf.cpp`；`tests/libslic3r/test_obj.cpp`；`tests/libslic3r/test_stl.cpp`；`tests/libslic3r/test_thumbnail.cpp`；`tests/libslic3r/test_png_io.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 分支现状

本分支使用自己的 bbs_3mf 来源/版本逻辑；未整体同步用户版 9ce27fdbcc 的分离兼容标记。能打开某文件不等于完整保留另一分支的配置。普通模型导出不自动做保存后完整复检；只有明确检测工作流的产物验证使用对应复检路径。

## 关联模块

[模型数据与网格属性](model.md)、[模型检测、修复与保色](model-repair.md)、[配置定义、预设继承与资源更新](presets.md)、[G-code 解析、统计与预览](gcode-preview.md)。
