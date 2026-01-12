# OrcaSlicer 缩略图生成修复指南

## 问题描述

Linux CLI 模式下 OpenGL 初始化失败，导致：
- ❌ 3MF 文件没有缩略图
- ❌ G-code 文件也没有缩略图
- ❌ 错误日志：`glGetString(GL_VERSION) returned NULL`

## 根本原因

**三层问题叠加：**

1. **编译层**：GLFW 编译时禁用了 X11 支持 (`GLFW_BUILD_X11=OFF`)
2. **初始化层**：代码无条件清空 `DISPLAY` 环境变量
3. **创建层**：代码强制使用 `GLFW_OSMESA_CONTEXT_API`

即使在 `xvfb-run` 环境下，GLFW 也无法使用 X11，只能用 OSMesa，而 OSMesa 上下文创建失败。

---

## 已完成的代码修复

以下文件已经修复（请确认）：

### 1. `deps/GLFW/GLFW.cmake`
```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # ✅ 启用 X11 支持（之前是 OFF）
    set(_glfw_build_x11 "-DGLFW_BUILD_X11=ON")
    set(_glfw_disable_wayland "-DGLFW_BUILD_WAYLAND=OFF")
    set(_glfw_use_osmesa "-DGLFW_USE_OSMESA=ON")
```

### 2. `src/OrcaSlicer.cpp` - 智能 DISPLAY 检测（~5933 行）
```cpp
#ifdef __linux__
    BOOST_LOG_TRIVIAL(info) << "=== Step 3: Detecting Display Environment ===";
    
    const char* display = getenv("DISPLAY");
    
    if (display && strlen(display) > 0) {
        // ✅ 有 DISPLAY 就保留，使用 X11
        BOOST_LOG_TRIVIAL(info) << "Found DISPLAY=" << display;
        setenv("WAYLAND_DISPLAY", "", 1);  // 只禁用 Wayland
    } else {
        // ✅ 真正 headless 才清空
        setenv("DISPLAY", "", 1);
        // ... 尝试 OSMesa
    }
#endif
```

### 3. `src/OrcaSlicer.cpp` - 智能 API 选择（~6015 行）
```cpp
#ifdef __linux__
    const char* display_check = getenv("DISPLAY");
    if (!display_check || strlen(display_check) == 0) {
        // ✅ 无 DISPLAY 才用 OSMesa
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_OSMESA_CONTEXT_API);
    } else {
        // ✅ 有 DISPLAY 用 Native GLX
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
    }
#endif
```

---

## 🔧 编译和测试步骤

### 方法 1：使用自动修复脚本（推荐）

```bash
cd /path/to/orca-slicer-soft
./fix_thumbnail_opengl.sh
```

脚本会自动完成：
1. 清理旧的 GLFW 编译产物
2. 重新编译 GLFW（启用 X11）
3. 重新编译 OrcaSlicer
4. 显示测试命令

---

### 方法 2：手动步骤

#### 步骤 1: 清理 GLFW 旧编译产物

```bash
cd /path/to/orca-slicer-soft
rm -rf deps/build/dep_GLFW-prefix
rm -f deps/build/destdir/usr/local/lib/libglfw*
rm -rf deps/build/destdir/usr/local/include/GLFW
```

#### 步骤 2: 验证 GLFW 配置

```bash
cat deps/GLFW/GLFW.cmake | grep -A 3 "CMAKE_SYSTEM_NAME.*Linux"
```

**应该看到：**
```
set(_glfw_build_x11 "-DGLFW_BUILD_X11=ON")  ← 必须是 ON
```

#### 步骤 3: 重新编译 GLFW

```bash
./build_linux.sh -d
```

**验证编译成功：**
```bash
ls -lh deps/build/destdir/usr/local/lib/libglfw* 
# 应该显示 libglfw3.a 或 libglfw.so
```

#### 步骤 4: 重新编译 OrcaSlicer

```bash
# 清理旧的编译产物
rm -rf build/src/CMakeFiles/OrcaSlicer.dir
rm -f build/src/orca-slicer

# 重新编译
cmake --build build --target OrcaSlicer -j$(nproc)
```

#### 步骤 5: 测试缩略图生成

```bash
# 准备测试文件
mkdir -p /tmp/test_slice

# 使用 xvfb-run 测试
xvfb-run -a build/src/orca-slicer \
  --slice 0 \
  --export-3mf /tmp/test_output.3mf \
  --load-settings /tmp/machine.json \
  --load-settings /tmp/process.json \
  --load-filaments /tmp/filament.json \
  --outputdir /tmp/ \
  /tmp/test.stl 2>&1 | grep -E "DISPLAY|GLX|OpenGL|thumbnail"
```

---

## ✅ 成功标志

编译和运行成功后，日志应该显示：

```
[info] === Step 3: Detecting Display Environment ===
[info] Found DISPLAY=:99 (X11/Xvfb available)
[info] Will use X11 for rendering (compatible with xvfb-run)

[info] === Step 4: Initializing GLFW ===
[info] ✓ glfwInit SUCCESS

[info] === Step 5: Setting GLFW Window Hints ===
[info] DISPLAY=:99 - using Native GLX context API  ← 关键！

[info] === Step 8: Verifying OpenGL Context ===
[info] ✓ OpenGL Context Information:
[info]   Version:  3.3 Mesa 25.0.7  ← 而不是 NULL！

[info] ✓✓✓ GLEW initialized successfully! ✓✓✓

[info] plate 1's thumbnail, need to regenerate
[info] framebuffer_type: ARB
[info] plate 1's thumbnail,finished rendering
```

---

## 🎯 G-code 缩略图支持

修复 OpenGL 后，要让 G-code 也包含缩略图：

### 快速方案：复用 3MF 缩略图

在 `src/OrcaSlicer.cpp` 第 5618 行附近修改：

```cpp
// ❌ 原代码
outfile = print_fff->export_gcode(outfile, gcode_result, nullptr);

// ✅ 修改为（需要额外实现）
outfile = print_fff->export_gcode(outfile, gcode_result, 
    [&](const ThumbnailsParams& params) -> ThumbnailsList {
        // 返回已生成的 3MF 缩略图数据
        return /* TODO: 从 plate_data 获取缩略图 */;
    }
);
```

**注意：** 这需要进一步的代码修改来传递缩略图数据，但 OpenGL 初始化修复是前提。

---

## 🐛 故障排查

### 问题 1: 仍然显示 "glGetString(GL_VERSION) returned NULL"

**原因：** GLFW 没有重新编译，或者使用了缓存的旧版本

**解决：**
```bash
# 完全清理缓存
rm -rf deps/build/dep_GLFW*
rm -rf build/CMakeCache.txt
rm -rf build/src/CMakeFiles

# 重新编译全部
./build_linux.sh -d
cmake --build build --target OrcaSlicer -j$(nproc)
```

### 问题 2: 编译时提示 "GLFW_BUILD_X11 not defined"

**原因：** GLFW.cmake 修改未生效

**解决：**
```bash
# 检查文件修改
git diff deps/GLFW/GLFW.cmake

# 确认包含
grep "GLFW_BUILD_X11=ON" deps/GLFW/GLFW.cmake
```

### 问题 3: xvfb-run 仍然失败

**解决：**
```bash
# 安装完整的 Mesa 和 X11 依赖
sudo apt-get install -y \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    mesa-common-dev \
    libegl1-mesa-dev \
    libgbm-dev \
    libdrm-dev \
    xvfb

# 重启 Xvfb
killall Xvfb 2>/dev/null || true
Xvfb :99 -screen 0 1920x1080x24 &
export DISPLAY=:99
```

---

## 📊 修复前后对比

| 场景 | 修复前 | 修复后 |
|------|--------|--------|
| **xvfb-run** | ❌ OSMesa 失败 | ✅ X11/GLX 成功 |
| **真正 headless** | ❌ OSMesa 失败 | ⚠️ OSMesa（需配置） |
| **正常桌面** | ❌ 强制 OSMesa | ✅ 使用 X11 |

---

## 📝 总结

1. **根本原因**：GLFW 编译时禁用了 X11，只能用不稳定的 OSMesa
2. **核心修复**：启用 GLFW 的 X11 支持 + 智能选择渲染 API
3. **关键步骤**：必须重新编译 GLFW 和 OrcaSlicer
4. **验证方法**：日志显示 "using Native GLX context API" 和有效的 OpenGL 版本

---

## 🔗 相关文件

- `deps/GLFW/GLFW.cmake` - GLFW 编译配置
- `src/OrcaSlicer.cpp` - 主程序（OpenGL 初始化逻辑）
- `fix_thumbnail_opengl.sh` - 自动修复脚本
- `.github/workflows/build-linux.yml` - CI 构建配置

---

**最后更新：** 2026-01-12
