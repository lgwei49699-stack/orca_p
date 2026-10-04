# 图标、着色器及静态辅助资源

<!-- module-review
{
  "id": "resource-ui",
  "source_fingerprint": "sha256:cdd02d28d2ff2e356526d1d3a2097f3894da61300361e9692652b41b73aeb11e",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责图标、着色器及静态辅助资源。归属以[模块清单](../modules.json)的 `resource-ui` 为准。

## 入口与调用

- [hints.ini](../../../resources/data/hints.ini)：产品提示内容与条件；文案提取通过 gettext 的 hints 工具衔接。
- [IconManager.cpp](../../../src/slic3r/GUI/IconManager.cpp)：`IconManager::init` 加载／栅格化图标并分配纹理，`release` 释放资源。

## 核心流程

IconManager／BitmapCache／GLTexture 从 resources 加载图标与贴图，shaders 供渲染，fonts／tooltip／dailytip／info 等提供界面字体和说明；handy_models 为辅助模型。资源文件不直接定义机型安全行程。

## 配置与约束

文件名、大小写、透明度、分辨率和引用路径是跨平台合同。打印机专属封面与床文件归 profiles 模块，网页的脚本和图片由 web-assets 管理，避免把资源变更当无影响。

## 失败与取消

资源缺失应定位打包路径或引用大小写；不要通过隐藏错误扩大默认回退。图片加载失败与底层切片失败需要区分。二进制资源不能只看文本 diff。

## 修改与验证

改资源检查真实显示及安装包包含情况；着色器还需相应 GL 驱动。文档指纹含二进制资源的 Git 身份，本轮没有重打包或展示验收。

现有测试/工具范围：暂无独立自动化目标，按本节检查场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[渲染、控件与平台 UI](render-ui.md)、[配置向导与预设创建](guide.md)、[中英文与翻译资源](localization.md)、[构建、打包与平台集成](build.md)。
