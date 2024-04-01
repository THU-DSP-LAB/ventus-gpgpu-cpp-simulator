import os
import re

current_dir = os.path.dirname(__file__)

# 定义要读取的文件名
input_filename = "gpu-rodinia/bfs/4x8/BFS_1_0.chisel.log"
# 定义要写入的文件名
output_filename = input_filename + ".filtered.log"

# 指定的s值，即要保留的"warp s"行
s = 0

# 打开输入文件以读取内容
with open(os.path.join(current_dir, input_filename), "r") as file:
    lines = file.readlines()

# 打开输出文件以写入内容
with open(os.path.join(current_dir, output_filename), "w") as file:
    # 正则表达式匹配"warp i"的行，其中i为任意整数
    pattern_general = re.compile(r"^\s*warp\s+(\d+)")  # 匹配所有"warp i"的行
    pattern_specific = "warp {}".format(s)  # 构造特定的字符串用于比较

    for line in lines:
        match = pattern_general.match(line)
        if not match or (match and line.strip().startswith(pattern_specific)):
            # 如果该行不匹配"warp i"模式，或者是"warp s"的特定行，就写入到输出文件中
            file.write(line)

print("完成。保留的行已经保存到 '{}'。".format(output_filename))
