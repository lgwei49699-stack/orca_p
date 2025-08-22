#!/usr/bin/env python3
import struct
import sys
import os

def analyze_binary_stl(filename):
    """分析二进制STL文件的基本信息"""
    print(f"分析STL文件: {filename}")
    
    if not os.path.exists(filename):
        print(f"错误: 文件 {filename} 不存在")
        return
    
    file_size = os.path.getsize(filename)
    print(f"文件大小: {file_size} 字节")
    
    with open(filename, 'rb') as f:
        # 读取头部（80字节）
        header = f.read(80)
        print(f"STL头部: {header.decode('ascii', errors='ignore').strip()}")
        
        # 读取三角形数量（4字节，小端序）
        triangle_count_bytes = f.read(4)
        if len(triangle_count_bytes) != 4:
            print("错误: 无法读取三角形数量")
            return
            
        triangle_count = struct.unpack('<I', triangle_count_bytes)[0]
        print(f"三角形数量: {triangle_count}")
        
        # 计算期望的文件大小
        expected_size = 80 + 4 + triangle_count * 50  # 每个三角形50字节
        print(f"期望文件大小: {expected_size} 字节")
        
        if file_size != expected_size:
            print(f"警告: 实际文件大小与期望不符！")
            return
        
        # 读取所有三角形并计算边界框
        min_x = min_y = min_z = float('inf')
        max_x = max_y = max_z = float('-inf')
        
        valid_triangles = 0
        
        for i in range(triangle_count):
            # 每个三角形: 12字节法向量 + 36字节顶点坐标 + 2字节属性
            triangle_data = f.read(50)
            if len(triangle_data) != 50:
                print(f"错误: 三角形 {i} 数据不完整")
                break
                
            # 解析法向量（跳过，不需要用于边界框计算）
            normal = struct.unpack('<3f', triangle_data[0:12])
            
            # 解析三个顶点
            vertices = []
            for j in range(3):
                offset = 12 + j * 12
                vertex = struct.unpack('<3f', triangle_data[offset:offset+12])
                vertices.append(vertex)
                
                x, y, z = vertex
                
                # 检查是否为有效数值
                if all(abs(coord) < 1e6 for coord in vertex):  # 排除异常大的值
                    min_x = min(min_x, x)
                    max_x = max(max_x, x)
                    min_y = min(min_y, y)
                    max_y = max(max_y, y)
                    min_z = min(min_z, z)
                    max_z = max(max_z, z)
            
            valid_triangles += 1
        
        print(f"有效三角形: {valid_triangles}")
        
        if min_x != float('inf'):
            print(f"\n边界框信息:")
            print(f"X范围: {min_x:.3f} 到 {max_x:.3f} (尺寸: {max_x - min_x:.3f})")
            print(f"Y范围: {min_y:.3f} 到 {max_y:.3f} (尺寸: {max_y - min_y:.3f}")
            print(f"Z范围: {min_z:.3f} 到 {max_z:.3f} (尺寸: {max_z - min_z:.3f})")
            
            center_x = (min_x + max_x) / 2
            center_y = (min_y + min_y) / 2
            center_z = (min_z + max_z) / 2
            print(f"模型中心: ({center_x:.3f}, {center_y:.3f}, {center_z:.3f})")
            
            print(f"\n模型尺寸分析:")
            size_x = max_x - min_x
            size_y = max_y - min_y
            size_z = max_z - min_z
            print(f"长度: {size_x:.3f} mm")
            print(f"宽度: {size_y:.3f} mm") 
            print(f"高度: {size_z:.3f} mm")
            
            # 检查是否可能超出常见打印床尺寸
            common_bed_sizes = [
                (200, 200, 200, "Ender 3"),
                (220, 220, 250, "Prusa i3"),
                (256, 256, 256, "Bambu X1"),
                (300, 300, 400, "大型打印机")
            ]
            
            print(f"\n打印床兼容性检查:")
            for bed_x, bed_y, bed_z, name in common_bed_sizes:
                fits = size_x <= bed_x and size_y <= bed_y and size_z <= bed_z
                status = "✓" if fits else "✗"
                print(f"{status} {name} ({bed_x}x{bed_y}x{bed_z}): {'合适' if fits else '太大'}")
                
        else:
            print("错误: 无法计算边界框，可能存在无效的几何数据")

def main():
    if len(sys.argv) != 2:
        print("用法: python3 stl_analyzer.py <stl文件路径>")
        return
    
    analyze_binary_stl(sys.argv[1])

if __name__ == "__main__":
    main() 