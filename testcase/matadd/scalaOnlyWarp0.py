import os

def filter_lines(input_file, output_file):
    with open(input_file, 'r') as infile, open(output_file, 'w') as outfile:
        for line in infile:
            if line.strip().startswith("warp 0"):
                outfile.write(line)

if __name__ == "__main__":
    current_dir = os.path.dirname(__file__)
    input_file_name = os.path.join(current_dir, "matadd scala结果.txt")
    output_file_name = os.path.join(current_dir, "matadd scala结果(warp0 only).txt")

    filter_lines(input_file_name, output_file_name)
    print("处理完成！")
