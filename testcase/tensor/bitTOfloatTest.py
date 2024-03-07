import struct
import random

rand_float = random.uniform(-1.0, 1.0)
print(rand_float)
float_bytes = struct.pack("f", rand_float)
print(float_bytes)
hex_str = format(struct.unpack('I', float_bytes)[0], 'x')
hex_str1 = float_bytes.hex()
print(hex_str)
print(hex_str1)

hex_str = "3f467678"

# 将16进制字符串转换为整数，然后打包为四字节的二进制数据
binary_data = struct.pack('I', int(hex_str, 16))
# 解包为浮点数
float_value = struct.unpack('f', binary_data)[0]

print(float_value)

