import os


def filter_lines(input_file, output_file):
    with open(input_file, "r") as infile, open(output_file, "w") as outfile:
        for line in infile:
            if line.strip().startswith("SM0 warp 0"):
                outfile.write(line)


chiselname = "matadd.chisel.log"
cppname = "仿真器结果.log"
name = cppname

if __name__ == "__main__":
    current_dir = os.path.dirname(__file__)
    input_file_name = os.path.join(current_dir, name)
    output_file_name = os.path.join(current_dir, name + "(warp0 only).log)")

    filter_lines(input_file_name, output_file_name)
    print("处理完成！")
