import os
import random
import struct


# Function to generate a .data file with random 32-bit floating-point numbers
def generate_random_floats(hex_byte_size: str, file_name: str):
    # Convert the hex byte size to decimal
    byte_size = int(hex_byte_size, 16)
    # Calculate the number of floats (4 bytes per float)
    num_floats = byte_size // 4

    with open(file_name, "w") as file:
        for _ in range(num_floats):
            # Generate a random float in the range of -1.0 to 1.0
            rand_float = random.uniform(-1.0, 1.0)
            # Convert to 32-bit floating-point format
            float_bytes = struct.pack("f", rand_float)
            # Convert bytes to hexadecimal representation
            hex_str = format(struct.unpack('I', float_bytes)[0], 'x')
            # hex_str = float_bytes.hex()
            file.write(f"{hex_str}\n")


try:
    # 适用于 .py 文件
    current_dir = os.path.dirname(os.path.abspath(__file__))
except NameError:
    # 适用于 .ipynb 笔记本
    current_dir = os.getcwd()

# File name for the output
file_name = os.path.join(current_dir, "vectormma.data")

# Generate the file
generate_random_floats("400", file_name)
