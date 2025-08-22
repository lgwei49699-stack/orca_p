#!/usr/bin/env python3
import re
import sys

def analyze_gcode_coordinates(gcode_file):
    """分析G代码文件中的坐标范围"""
    print(f"分析G代码文件: {gcode_file}")
    
    # 坐标范围记录
    x_coords = []
    y_coords = []
    z_coords = []
    
    # 读取G代码文件
    try:
        with open(gcode_file, 'r', encoding='utf-8') as f:
            line_count = 0
            move_count = 0
            
            for line in f:
                line_count += 1
                line = line.strip()
                
                # 匹配G0/G1移动命令
                if line.startswith('G0 ') or line.startswith('G1 '):
                    move_count += 1
                    
                    # 提取X坐标
                    x_match = re.search(r'X([-+]?\d*\.?\d+)', line)
                    if x_match:
                        x_coords.append(float(x_match.group(1)))
                    
                    # 提取Y坐标
                    y_match = re.search(r'Y([-+]?\d*\.?\d+)', line)
                    if y_match:
                        y_coords.append(float(y_match.group(1)))
                    
                    # 提取Z坐标
                    z_match = re.search(r'Z([-+]?\d*\.?\d+)', line)
                    if z_match:
                        z_coords.append(float(z_match.group(1)))
    
    except Exception as e:
        print(f"错误: 无法读取文件 {gcode_file}: {e}")
        return
    
    print(f"总行数: {line_count}")
    print(f"移动命令数: {move_count}")
    
    # 分析坐标范围
    if x_coords:
        min_x, max_x = min(x_coords), max(x_coords)
        print(f"\nX坐标范围:")
        print(f"  最小值: {min_x:.3f}")
        print(f"  最大值: {max_x:.3f}")
        print(f"  范围: {max_x - min_x:.3f}")
        print(f"  中心: {(min_x + max_x) / 2:.3f}")
    
    if y_coords:
        min_y, max_y = min(y_coords), max(y_coords)
        print(f"\nY坐标范围:")
        print(f"  最小值: {min_y:.3f}")
        print(f"  最大值: {max_y:.3f}")
        print(f"  范围: {max_y - min_y:.3f}")
        print(f"  中心: {(min_y + max_y) / 2:.3f}")
    
    if z_coords:
        min_z, max_z = min(z_coords), max(z_coords)
        print(f"\nZ坐标范围:")
        print(f"  最小值: {min_z:.3f}")
        print(f"  最大值: {max_z:.3f}")
        print(f"  范围: {max_z - min_z:.3f}")
    
    # 检查坐标系问题
    print(f"\n坐标系分析:")
    has_negative_x = any(x < 0 for x in x_coords) if x_coords else False
    has_negative_y = any(y < 0 for y in y_coords) if y_coords else False
    
    print(f"  包含负X坐标: {'是' if has_negative_x else '否'}")
    print(f"  包含负Y坐标: {'是' if has_negative_y else '否'}")
    
    if has_negative_x or has_negative_y:
        print(f"  ⚠️  检测到负坐标！这可能导致模型显示在打印床角落或超出边界")
        print(f"  💡 建议使用居中坐标系的配置文件或添加 --arrange 1 参数")
    
    # 常见打印床尺寸兼容性检查
    common_beds = [
        (200, 200, "Ender 3"),
        (220, 220, "Prusa i3"), 
        (256, 256, "Bambu X1"),
        (300, 300, "大型打印机")
    ]
    
    if x_coords and y_coords:
        model_width = max_x - min_x
        model_depth = max_y - min_y
        
        print(f"\n打印床兼容性检查:")
        print(f"模型实际尺寸: {model_width:.1f} x {model_depth:.1f} mm")
        
        for bed_x, bed_y, name in common_beds:
            # 检查是否需要坐标偏移
            fits_with_offset = (model_width <= bed_x and model_depth <= bed_y and 
                              min_x >= -bed_x/2 and max_x <= bed_x/2 and
                              min_y >= -bed_y/2 and max_y <= bed_y/2)
            
            fits_positive = (max_x <= bed_x and max_y <= bed_y and min_x >= 0 and min_y >= 0)
            
            if fits_positive:
                status = "✓ 完全兼容"
            elif fits_with_offset:
                status = "⚠️ 需要坐标偏移"
            else:
                status = "✗ 太大"
            
            print(f"  {name} ({bed_x}x{bed_y}): {status}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法: python3 gcode_analyzer.py <gcode文件路径>")
        sys.exit(1)
    
    analyze_gcode_coordinates(sys.argv[1]) 