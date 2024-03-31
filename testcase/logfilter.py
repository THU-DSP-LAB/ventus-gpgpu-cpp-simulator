import os

current_dir = os.path.dirname(__file__)

# 定义要读取的文件名
input_filename = "gpu-rodinia/bfs/4x8/BFS_1_0.sim.log"
# 定义要写入的文件名
output_filename = input_filename + ".filtered"

# 打开输入文件以读取内容
with open(os.path.join(current_dir, input_filename), "r") as file:
    # 读取所有行
    lines = file.readlines()

# 打开输出文件以写入内容
with open(os.path.join(current_dir, output_filename), "w") as file:
    # 遍历每一行
    for line in lines:
        # 检查是否以"core 0"开头，注意是忽略了开头的空白
        if line.lstrip().startswith("SM1 warp 0"):
            # 如果是，就写入到输出文件中
            file.write(line)

print("完成。保留的行已经保存到 '{}'。".format(output_filename))
