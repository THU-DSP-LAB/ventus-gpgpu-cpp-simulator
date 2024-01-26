import os
import random


# Function to generate a .data file with random 32-bit integers
def generate_random_integers(hex_byte_size: str, file_name: str):
    # Convert the hex byte size to decimal
    byte_size = int(hex_byte_size, 16)
    # Calculate the number of integers (4 bytes per integer)
    num_integers = byte_size // 4

    with open(file_name, 'w') as file:
        for _ in range(num_integers):
            # Generate a random integer in the range of 14-bit signed integer
            rand_int = random.randint(-16383, 16383)
            # Convert to 32-bit signed integer format and write to file
            file.write(f'{rand_int & 0xFFFFFFFF:08x}\n')


try:
    # 适用于 .py 文件
    current_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    # 适用于 .ipynb 笔记本
    current_dir = os.getcwd()

# File name for the output
file_name = os.path.join(current_dir, "tensor.data")

# Generate the file
generate_random_integers("200", file_name)
