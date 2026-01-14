# GLEW "Missing GL version" 修复总结

## 问题概述

在 Linux 无头服务器上使用 xvfb-run 生成缩略图时，GLEW 初始化失败：
```
[error] Unable to init glew library
[error] GLEW error code: 1
[error] GLEW error string: Missing GL version
```

## 根本原因（三个层面）

### 1. 代码 Bug（最关键）
**变量作用域错误**导致程序崩溃：
```cpp
// 错误的代码：
{
    GLFWwindow* window = glfwCreateWindow(...);  // 局部变量
}  // window 超出作用域

{
    glfwDestroyWindow(bg_window);  // ❌ bg_window 未定义！
}
```

这个 bug 导致：
- 段错误或未定义行为
- 降级逻辑无法执行
- 即使日志正确，实际逻辑出错

### 2. GLEW 重复初始化问题
GLEW 设计为每个进程只初始化一次：
- 第一次 `glewInit()` 失败后，无法重置状态
- 即使切换 OpenGL 版本，第二次 `glewInit()` 也可能失败
- GLEW 内部状态被"污染"

### 3. Mesa llvmpipe OpenGL 3.3 支持不完整
在无头环境下：
- Mesa llvmpipe 虽然声称支持 OpenGL 3.3
- 但实际上下文不完整（`glGetString(GL_VERSION)` 返回 NULL）
- GLEW 无法获取 OpenGL 版本信息
- OpenGL 2.1 支持更完整可靠

## 修复方案

### 修复 #1: 正确的变量作用域
```cpp
// 在函数/代码块最外层声明
GLFWwindow* bg_window = nullptr;

// 所有地方都使用同一个变量
bg_window = glfwCreateWindow(...);

if (need_fallback) {
    glfwDestroyWindow(bg_window);
    bg_window = glfwCreateWindow(...);  // 重新创建
}
```

### 修复 #2: 完整的降级策略
```cpp
// 1. 销毁旧窗口
glfwDestroyWindow(bg_window);
bg_window = nullptr;

// 2. 重置所有 hints
glfwDefaultWindowHints();

// 3. 设置 OpenGL 2.1
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

// 4. 重新应用所有必要的 hints
glfwWindowHint(GLFW_VISIBLE, false);
glfwWindowHint(GLFW_RED_BITS, 8);
// ... 等等

// 5. 创建新窗口
bg_window = glfwCreateWindow(...);
```

### 修复 #3: Pre-GLEW 诊断
在 glewInit 之前尝试获取 OpenGL 版本：
```cpp
PFNGLGETSTRINGPROC glGetStringPtr = 
    (PFNGLGETSTRINGPROC)glfwGetProcAddress("glGetString");
if (glGetStringPtr) {
    const char* version = (const char*)glGetStringPtr(GL_VERSION);
    if (version) {
        BOOST_LOG_TRIVIAL(info) << "Pre-GLEW GL_VERSION: " << version;
    } else {
        BOOST_LOG_TRIVIAL(warning) << "Pre-GLEW returned NULL - context may be incomplete";
    }
}
```

## 修改的文件

### 1. `src/OrcaSlicer.cpp`
- 修复变量作用域问题（`bg_window` 正确声明）
- 添加 Pre-GLEW 诊断
- 实现完整的 OpenGL 2.1 降级策略
- 改进错误处理和日志输出

### 2. `src/slic3r/GUI/OpenGLManager.cpp`
- 添加详细的 GLEW 初始化日志
- 说明常见错误原因

### 3. `prd/缩略图需求.md`
- 记录问题分析过程
- 详细说明复杂性来源
- 记录所有尝试和结果

## 如何测试

### 在服务器上编译
```bash
cd /path/to/orca-slicer-soft

# 如果已经有 build 目录，先清理
rm -rf build
mkdir build
cd build

# 配置
cmake -DCMAKE_BUILD_TYPE=Release ..

# 编译（使用多核）
cmake --build . --target OrcaSlicer --parallel $(nproc)
```

### 运行测试
```bash
cd /path/to/orca-slicer-soft

# 方法 1: 使用测试脚本
chmod +x test_thumbnail_fix.sh
./test_thumbnail_fix.sh your_model.3mf

# 方法 2: 手动运行
xvfb-run -a ./build/src/OrcaSlicer --export-gcode input.3mf --output output.gcode
```

### 成功的标志

#### 日志输出应该包含：
```
[info] === Step 1: GLFW Version Info ===
[info] GLFW compile-time version: 3.3.7
[info] === Step 2: Setting GLFW Error Callback ===
[info] === Step 3: Configuring Mesa Software Renderer ===
[info] ✓ Set LIBGL_ALWAYS_SOFTWARE=1 (force Mesa llvmpipe)
[info] Found DISPLAY=:99 (will use GLX with software rendering)
[info] === Step 4: Initializing GLFW ===
[info] ✓ glfwInit SUCCESS
[info] === Step 5: Setting GLFW Window Hints ===
[info]   OpenGL version: 3.3
[info]   Using GLX with Mesa llvmpipe software renderer
[info] === Step 6: Creating GLFW Window (640x480, offscreen) ===
[info] ✓ GLFW window created successfully
[info] === Step 7: Making OpenGL Context Current ===
[info] ✓ OpenGL context is now current
[info] === Step 8: Pre-GLEW Diagnostics ===
[info] ✓ Pre-GLEW GL_VERSION: 3.3 (Mesa 25.x.x)  <-- 理想情况
    或
[warning] ⚠ Pre-GLEW glGetString returned NULL  <-- 会触发降级

[info] === Step 9: Initializing GLEW ===
[info]   Setting glewExperimental = GL_TRUE
[info]   Calling glewInit()...
[info]   glewInit() returned GLEW_OK
[info] ✓ GLEW initialized successfully!

[info] === Step 10: Verifying OpenGL Context ===
[info] ✓ OpenGL Context Information:
[info]   Version:  3.3 (Mesa 25.x.x) 或 2.1 Mesa 25.x.x
[info]   Vendor:   Mesa
[info]   Renderer: llvmpipe (LLVM ...)
[info] ========================================
[info] ✓✓✓ OpenGL Setup Complete!
[info] ✓✓✓ Ready to Generate Thumbnails
[info] ========================================
```

#### 如果触发了降级：
```
[error] GLEW initialization FAILED with OpenGL 3.3!
[warning] Mesa llvmpipe may not fully support OpenGL 3.3
[info] >>> Attempting fallback: OpenGL 2.1 ...
[info] === Fallback Step 1: Reset GLFW window hints ===
[info] === Fallback Step 2: Create OpenGL 2.1 window ===
[info] ✓ OpenGL 2.1 window created
[info] === Fallback Step 3: Make context current ===
[info] ✓ OpenGL 2.1 context is now current
[info] === Fallback Step 4: Re-initialize GLEW ===
[info] ✓✓✓ OpenGL 2.1 fallback SUCCEEDED! ✓✓✓
```

#### G-code 文件检查：
```bash
# 检查是否包含缩略图
grep "thumbnail begin" output.gcode

# 应该看到类似：
; thumbnail begin 300x300 [数字]
; [base64 编码的图片数据]
; thumbnail end
```

## 如果仍然失败

### 诊断步骤

1. **检查 Mesa 安装**：
```bash
LIBGL_ALWAYS_SOFTWARE=1 glxinfo | grep "OpenGL version"
# 应该看到: OpenGL version string: 2.1 Mesa ... 或更高
```

2. **检查 DISPLAY 环境变量**：
```bash
echo $DISPLAY
# xvfb-run 应该会设置类似 :99 的值
```

3. **手动启动 Xvfb 测试**：
```bash
Xvfb :99 -screen 0 1024x768x24 &
export DISPLAY=:99
LIBGL_ALWAYS_SOFTWARE=1 glxinfo
```

4. **检查编译日志**：
确保没有编译警告或错误

### 可能需要的包（Ubuntu/Debian）
```bash
sudo apt-get update
sudo apt-get install -y \
    xvfb \
    mesa-utils \
    libgl1-mesa-glx \
    libgl1-mesa-dri \
    mesa-utils-extra
```

### 可能需要的包（CentOS/RHEL）
```bash
sudo yum install -y \
    xorg-x11-server-Xvfb \
    mesa-libGL \
    mesa-libGL-devel \
    mesa-dri-drivers \
    glx-utils
```

## 长期建议

### 考虑默认使用 OpenGL 2.1
如果无头渲染是主要使用场景，可以直接使用 OpenGL 2.1：

```cpp
// 在 OrcaSlicer.cpp 中
#ifdef __linux__
    // For headless rendering, OpenGL 2.1 is more reliable
    int gl_major = 2;
    int gl_minor = 1;
#else
    int gl_major = 3;
    int gl_minor = 3;
#endif
```

### 考虑使用 EGL 替代 GLFW
EGL 在无头环境下可能更稳定：
```cpp
#ifdef __linux__
    // Use EGL for headless rendering
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    // ...
#endif
```

### 考虑使用 OSMesa（纯软件渲染）
完全不依赖 X server：
```cpp
#include <GL/osmesa.h>
OSMesaContext ctx = OSMesaCreateContext(OSMESA_RGBA, NULL);
```

## 总结

这个问题之所以复杂，是因为：
1. **代码 Bug 导致基本功能失效**（作用域错误）
2. **GLEW 设计限制**（无法重复初始化）
3. **Mesa llvmpipe 支持不完整**（OpenGL 3.3 不完整）
4. **多层技术栈交互**（GLEW → GLFW → GLX → Mesa → Xvfb）
5. **远程调试困难**（只能通过日志）

修复的关键是：
1. ✅ 修复变量作用域错误
2. ✅ 实现正确的降级策略
3. ✅ 添加 Pre-GLEW 诊断
4. ✅ 完整的错误处理和日志

现在代码应该能够：
- 尝试 OpenGL 3.3（理想情况）
- 检测失败并自动降级到 OpenGL 2.1
- 提供详细的诊断信息
- 正确处理各种边缘情况
