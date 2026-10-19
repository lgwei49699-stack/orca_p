# 二维／三维几何与空间算法

<!-- module-review
{
  "id": "geometry",
  "source_fingerprint": "sha256:1d2d06962a81abd7b0f4b9778a64b00b52dd45bf0c080bfb8fe8724bed83874a",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责二维／三维几何与空间算法。归属以[模块清单](../modules.json)的 `geometry` 为准。

## 入口与调用

- [Geometry.cpp](../../../src/libslic3r/Geometry.cpp)：实现几何变换、距离及方向等公共算法；`Transformation` 组合位移、旋转和缩放。
- [ClipperUtils.cpp](../../../src/libslic3r/ClipperUtils.cpp)：`offset`／`offset_ex` 及布尔运算封装 Clipper，在整数坐标空间处理多边形和路径。
- [Polygon.cpp](../../../src/libslic3r/Polygon.cpp)：`Polygon` 提供面积、方向、拆线与轮廓操作，供切片和路径生成复用。
- [AABBMesh.cpp](../../../src/libslic3r/AABBMesh.cpp)：`AABBMesh::init` 建立网格空间查询数据，供射线拾取和网格距离判断使用。

## 核心流程

Point/Polygon/ExPolygon 及 ClipperUtils 为偏移、布尔裁剪和路径几何提供公共基元；AABB／KDTree／Voronoi 等为空间检索和墙／填充提供输入。此层不决定打印参数，但数值误差会向切片、分盘和预览扩散。

## 配置与约束

整数缩放坐标与毫米浮点不可混用；环方向、孔洞、闭合边和容差是接口合同。几何帮助函数还承担切割、浮雕、测量等场景，不能仅为某一筏层样例更改公共算法。

## 失败与取消

退化点集、共线、空多边形、细薄区域需要明确处理；第三方断言失败应定位实际调用与前置条件，不能放宽业务断言隐藏越界。底层未提供取消的函数由外层阶段控制。

## 修改与验证

修改公共几何先运行对应 polygon/geometry/clipper/AABB 单元用例，再按调用方扩大；凸包测试必须覆盖同输入的实际凸包实现，二维算法差异不直接等同生产崩溃。

现有测试/工具范围：`tests/libslic3r/test_geometry.cpp`；`tests/libslic3r/test_polygon.cpp`；`tests/libslic3r/test_clipper_utils.cpp`；`tests/libslic3r/test_aabbindirect.cpp`。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[自动分盘与打印板](arrangement.md)、[周长墙与 Arachne 可变线宽](walls.md)、[填充、顶底面与窄区](fill.md)、[模型数据与网格属性](model.md)、[SLA 切片与支撑](sla.md)。
