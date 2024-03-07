import struct

# Given hexadecimal strings for matrices A, B, and C
A_str = "bf0e2e19 bece188e bf2e0397 3f772f62 bf5d0ab0 bec2dcd7 3f709d27 3f6c116d"
B_str = "3e879bd6 3c9b85f3 bf044793 3d85f249 3d35723d bed06763 3c545a09 3f557e08"
C_str = "bf3541bb 3e2e54d3 bf3f3857 becb56ee"


# Convert hexadecimal strings to lists of floating-point numbers
def hex_to_floats(hex_str):
    return [struct.unpack("f", struct.pack('I', int(h, 16)))[0] for h in hex_str.split()]


# Convert lists of floats to matrices
import numpy as np

A = np.array(hex_to_floats(A_str)).reshape(2, 4)
B = np.array(hex_to_floats(B_str)).reshape(4, 2)
C = np.array(hex_to_floats(C_str)).reshape(2, 2)

# Compute D = A@B + C
D = np.dot(A, B) + C


# Convert matrix D back to hexadecimal representation
def floats_to_hex(floats):
    return [format(struct.unpack('I', struct.pack("f", f))[0], 'x') for f in floats.flatten()]


D_hex = floats_to_hex(D)
print(D_hex)
