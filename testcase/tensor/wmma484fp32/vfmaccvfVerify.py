import struct
import numpy as np

# Convert hexadecimal strings to lists of floating-point numbers
def hex_to_floats(hex_str):
    return [struct.unpack("f", struct.pack('I', int(h, 16)))[0] for h in hex_str.split()]

def floats_to_hex(floats):
    return [format(struct.unpack('I', struct.pack("f", f))[0], 'x') for f in floats.flatten()]


# Given hexadecimal strings for matrices A, B, and C
vd_str = "3f15dc9f be806e35 3ee2217e 3f548eec bf1aa2ca 3e83fc6c be827c4b 3e6473bf"
v1_str = "3d521cdd 3f4287d5 3f360354 3f467678 bcbce554 bebebabc bd8d0308 bf2012ae"
r1_str = "3f467678"


vd = np.array(hex_to_floats(vd_str))
v1 = np.array(hex_to_floats(v1_str))
r1 = np.array(hex_to_floats(r1_str))


# Reshape r1 to match the shape of v1 for element-wise multiplication
r1_reshaped = r1 * np.ones_like(v1)

# Element-wise multiplication of v1 and r1, then adding the result to vd
vd_updated = vd + (v1 * r1_reshaped)

print(floats_to_hex(vd_updated))
