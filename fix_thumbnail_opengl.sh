#!/bin/bash
set -e

echo "=========================================="
echo "修复 OrcaSlicer 缩略图 OpenGL 初始化问题"
echo "=========================================="
echo ""

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "✓ 当前目录: $SCRIPT_DIR"
echo ""

# 步骤 1: 清理旧的 GLFW 编译产物
echo "=========================================="
echo "步骤 1/4: 清理旧的 GLFW 编译产物"
echo "=========================================="
if [ -d "deps/build/dep_GLFW-prefix" ]; then
    echo "删除 deps/build/dep_GLFW-prefix"
    rm -rf deps/build/dep_GLFW-prefix
fi
if [ -d "deps/build/destdir/usr/local/lib/libglfw*" ]; then
    echo "删除 GLFW 库文件"
    rm -f deps/build/destdir/usr/local/lib/libglfw*
fi
if [ -d "deps/build/destdir/usr/local/include/GLFW" ]; then
    echo "删除 GLFW 头文件"
    rm -rf deps/build/destdir/usr/local/include/GLFW
fi
echo "✓ 清理完成"
echo ""

# 步骤 2: 重新编译 GLFW（启用 X11 支持）
echo "=========================================="
echo "步骤 2/4: 重新编译 GLFW 依赖"
echo "=========================================="
echo "GLFW 配置: 启用 X11 + OSMesa 双支持"
echo "修改文件: deps/GLFW/GLFW.cmake"
echo ""

# 验证 GLFW.cmake 的修改
if grep -q "GLFW_BUILD_X11=ON" deps/GLFW/GLFW.cmake; then
    echo "✓ GLFW.cmake 已正确配置（X11 已启用）"
else
    echo "⚠️  警告: GLFW.cmake 配置可能不正确"
    echo "请确认 deps/GLFW/GLFW.cmake 中包含："
    echo "  set(_glfw_build_x11 \"-DGLFW_BUILD_X11=ON\")"
fi
echo ""

echo "开始编译 GLFW..."
./build_linux.sh -d 2>&1 | tee /tmp/orca_glfw_build.log || {
    echo "❌ GLFW 编译失败，日志已保存到 /tmp/orca_glfw_build.log"
    exit 1
}
echo "✓ GLFW 编译完成"
echo ""

# 步骤 3: 清理并重新编译 OrcaSlicer
echo "=========================================="
echo "步骤 3/4: 重新编译 OrcaSlicer"
echo "=========================================="
if [ -d "build" ]; then
    echo "清理旧的编译产物..."
    rm -rf build/src/CMakeFiles/OrcaSlicer.dir
    rm -f build/src/orca-slicer
fi
echo ""

echo "开始编译 OrcaSlicer..."
if [ ! -d "build" ]; then
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cd ..
fi

cmake --build build --target OrcaSlicer --config Release -j$(nproc) 2>&1 | tee /tmp/orca_main_build.log || {
    echo "❌ OrcaSlicer 编译失败，日志已保存到 /tmp/orca_main_build.log"
    exit 1
}
echo "✓ OrcaSlicer 编译完成"
echo ""

# 步骤 4: 测试缩略图生成
echo "=========================================="
echo "步骤 4/4: 测试缩略图生成"
echo "=========================================="
echo ""
echo "测试命令示例:"
echo ""
echo "xvfb-run -a build/src/orca-slicer \\"
echo "  --slice 0 \\"
echo "  --export-3mf /tmp/test_output.3mf \\"
echo "  --load-settings /tmp/machine.json \\"
echo "  --load-settings /tmp/process.json \\"
echo "  --load-filaments /tmp/filament.json \\"
echo "  --outputdir /tmp/ \\"
echo "  /tmp/test.stl"
echo ""
echo "=========================================="
echo "修复完成!"
echo "=========================================="
echo ""
echo "关键日志应该显示:"
echo "  [info] Found DISPLAY=:99 (X11/Xvfb available)"
echo "  [info] DISPLAY=:99 - using Native GLX context API"
echo "  [info] ✓ OpenGL Context Information:"
echo "  [info]   Version: 3.3 Mesa ..."
echo ""
