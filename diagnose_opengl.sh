#!/bin/bash
# OpenGL 环境诊断脚本
# 用于检查 Linux 运行环境的 OpenGL 配置

echo "=========================================="
echo "OpenGL 环境诊断工具"
echo "=========================================="
echo ""

echo "1. 检查系统 OpenGL 库"
echo "-------------------------------------------"
ldconfig -p 2>/dev/null | grep -E "libGL\.so|libGLU\.so|libEGL\.so" || echo "⚠️ 未找到 OpenGL 库"
echo ""

echo "2. 检查已安装的 Mesa 包"
echo "-------------------------------------------"
dpkg -l 2>/dev/null | grep -E "mesa|libgl" | grep "^ii" || echo "⚠️ 未找到 Mesa 包"
echo ""

echo "3. 检查 DISPLAY 环境变量"
echo "-------------------------------------------"
if [ -n "$DISPLAY" ]; then
    echo "✓ DISPLAY=$DISPLAY"
else
    echo "⚠️ DISPLAY 未设置"
fi
echo ""

echo "4. 检查 Xvfb 进程"
echo "-------------------------------------------"
ps aux | grep -v grep | grep Xvfb || echo "⚠️ Xvfb 未运行"
echo ""

echo "5. 测试 glxinfo（如果可用）"
echo "-------------------------------------------"
if command -v glxinfo &> /dev/null; then
    glxinfo | grep -E "OpenGL version|OpenGL vendor|OpenGL renderer" || echo "⚠️ glxinfo 执行失败"
else
    echo "⚠️ glxinfo 未安装（可选，用于测试）"
    echo "   安装命令: sudo apt-get install mesa-utils"
fi
echo ""

echo "6. 检查 AppImage 依赖的库"
echo "-------------------------------------------"
if [ -f "$1" ]; then
    echo "检查 AppImage: $1"
    ldd "$1" 2>/dev/null | grep -E "libGL|libGLU|libEGL" || echo "⚠️ AppImage 未链接 OpenGL 库"
else
    echo "⚠️ 请提供 AppImage 路径作为参数"
    echo "   使用方法: $0 /path/to/OrcaSlicer.AppImage"
fi
echo ""

echo "=========================================="
echo "诊断完成"
echo "=========================================="
echo ""
echo "📋 解决建议："
echo ""
echo "如果缺少 OpenGL 运行时库，请运行："
echo "sudo apt-get update"
echo "sudo apt-get install -y libgl1 libglu1 mesa-utils xvfb"
echo ""
echo "如果 Xvfb 未运行，请运行："
echo "Xvfb :99 -screen 0 1920x1080x24 &"
echo "export DISPLAY=:99"
echo ""
