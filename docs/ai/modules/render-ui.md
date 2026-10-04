# 渲染、控件与平台 UI

<!-- module-review
{
  "id": "render-ui",
  "source_fingerprint": "sha256:7dcea8649517cc619f2021169c3c20b32f97b1f80bc89f2e1a18985fe1411b9b",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责渲染、控件与平台 UI。归属以[模块清单](../modules.json)的 `render-ui` 为准。

## 入口与调用

- [GLCanvas3D.cpp](../../../src/slic3r/GUI/GLCanvas3D.cpp)：`reload_scene` 刷新模型／体积，`render` 检查可见画布与 GL context 后绘制场景和预览。
- [GLTexture.cpp](../../../src/slic3r/GUI/GLTexture.cpp)：`load_from_file`／`load_from_svg_file` 解码并准备纹理；`reset` 释放纹理和压缩状态。
- [ImGuiWrapper.cpp](../../../src/slic3r/GUI/ImGuiWrapper.cpp)：`new_frame` 准备字体与 ImGui 帧，`render` 提交绘制数据并结束当前帧。
- [Button.cpp](../../../src/slic3r/GUI/Widgets/Button.cpp)：`Button::render` 按选中／悬停状态选择图标，结合文本尺寸与 DPI 进行布局绘制。

## 核心流程

`GLCanvas3D::reload_scene` 更新场景体积；`load_gcode_preview` 把 GCodeProcessorResult 交给 GCodeViewer。`render` 先检查可见画布、当前 GL context 和 OpenGL 初始化，再检查平台、设置相机并绘制。预览输入与模型场景输入需要分别排查。

GLTexture 从 PNG／SVG／原始像素准备纹理，reset 释放 GPU 纹理和压缩状态。ImGuiWrapper 的 new_frame／render 配对管理一帧；wxWidgets 的 Button 则使用 wxDC 与状态图标布局，不走同一绘制入口。

## 配置与约束

DPI、主题、平台媒体后端、纹理尺寸和当前 GL context 都是调用前提。床贴图只表达视觉信息，实际打印区域来自机器配置，不能通过图片判断可打印尺寸。

## 失败与取消

GLCanvas3D 在画布不可用、未显示、上下文设置失败或平台无效时提前返回；平台无效会请求刷新床形状。部分纹理接口直接操作 OpenGL，不能假设所有调用都支持无 GUI 环境。

改名称纹理、图片加载或异步压缩时核对调用方的 Plater／context 和窗口生命周期；无界面的 3MF 导出必须单独覆盖。图片解码失败和后台取消不能被当作切片失败或成功，需按实际返回值处理。

## 修改与验证

模型显示改动从 reload_scene 追踪，G-code 显示从 load_gcode_preview 追踪；图标／字体问题检查 GLTexture、ImGuiWrapper 和具体控件。覆盖高 DPI、亮暗主题、中英文、损坏图片及无 GUI 导出。

现有 `tests/slic3rutils/test_imslider.cpp` 只覆盖滑块相关逻辑，不验证 GL context、纹理释放或设备界面。OpenGL 显示需 GUI 与对应平台验收；C++ 几何测试不能替代。执行要求见[验证规范](../validation.md)。

## 关联模块

[场景、对象与交互编辑](scene-editing.md)、[G-code 解析、统计与预览](gcode-preview.md)、[图标、着色器及静态辅助资源](resource-ui.md)、[公共运行时与平台辅助](platform.md)。
