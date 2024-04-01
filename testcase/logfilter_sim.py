import os
import re

current_dir = os.path.dirname(__file__)

input_filename = "gpu-rodinia/bfs/4x8/BFS_1_0.sim.log"
output_filename = input_filename + ".filtered.log"

# 保留以SMk warp s开头的行
k = 1
s = 0

with open(os.path.join(current_dir, input_filename), "r") as file:
    lines = file.readlines()

with open(os.path.join(current_dir, output_filename), "w") as file:
    # 正则表达式匹配"SMi warp j"的行，其中i和j为任意整数
    pattern_general = re.compile(r"^\s*SM(\d+)\s+warp\s+(\d+)")
    pattern_specific = "SM{} warp {}".format(k, s)

    for line in lines:
        match = pattern_general.match(line)
        if not match or (match and line.strip().startswith(pattern_specific)):
            # 如果该行不匹配"SMi warp j"模式，或者是"SMk warp s"的特定行，就写入到输出文件中
            file.write(line)

print("完成。保留的行已经保存到 '{}'。".format(output_filename))
