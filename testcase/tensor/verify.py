import numpy as np

# Define the strings
A_str = "fffff2ec 00003f29 00003ebc 00002eff 00000380 00003bb1 000006e3 00003499"
B_str = "00001e7c 00002c59 ffffdf21 00001c98 00002d27 0000035e 0000069b 0000091b"
C_str = "ffffddef 000008e6 ffffc189 ffffcad0"

# Convert the strings to numpy arrays
A = np.array([int(x, 16) for x in A_str.split()]).reshape(2, 4).astype(np.int64)
B = np.array([int(x, 16) for x in B_str.split()]).reshape(4, 2).astype(np.int64)
C = np.array([int(x, 16) for x in C_str.split()]).reshape(2, 2).astype(np.int64)

# Perform the matrix multiplication and addition
D = np.dot(A, B) + C

# Convert back to unsigned 32-bit integers and then to hexadecimal
D_hex_fixed = np.vectorize(lambda x: hex(x & 0xFFFFFFFF))(D)

print(D_hex_fixed)
