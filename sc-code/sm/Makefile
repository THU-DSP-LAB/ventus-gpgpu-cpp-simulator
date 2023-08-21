LIB_DIR=-L$(SYSTEMC_HOME)/lib-linux64
INC_DIR=-I$(SYSTEMC_HOME)/include
LIB=-lsystemc -Wl,-rpath,$(SYSTEMC_HOME)/lib-linux64

# 检查默认 g++ 版本是否大于等于 11
GCC_VERSION := $(shell g++ -dumpversion)
GE_GCC_11 := $(shell expr $(GCC_VERSION) \>= 11)
# 设置 g++ 路径
ifeq ($(GE_GCC_11), 1)
    # 默认 g++ 版本大于等于 11，直接使用 g++
    GCC := g++
else
    # 默认 g++ 版本小于 11，使用指定路径下的 g++
    GCC_PATH := $(GCC11_PATH)
    GCC := $(GCC_PATH)/bin/g++ -L$(GCC_PATH)/lib64 -I$(GCC_PATH)/include -Wl,-rpath,$(GCC_PATH)/lib64
endif

App=ventus

.PHONY:GPGPU_test GPGPU_test_GDB

all: GPGPU_test

GPGPU_test_FILE=GPGPU_test.cpp BASE.cpp opc.cpp decode.cpp init_decodetable.cpp init_ins.cpp init_instable.cpp initialize.cpp\
 CTA_Scheduler.cpp init_extmem.cpp ../parameters.cpp \
 exec_salu.cpp exec_simtstk.cpp exec_lsu.cpp exec_vfpu.cpp exec_csr.cpp exec_mul.cpp \
 exec_sfu.cpp exec_valu.cpp

GPGPU_test:
	$(GCC) -o $(App).out $(GPGPU_test_FILE) $(LIB_DIR) $(INC_DIR) $(LIB) -std=c++20

GPGPU_test_GDB:
	$(GCC) -o $(App).out $(GPGPU_test_FILE) $(LIB_DIR) $(INC_DIR) $(LIB) -std=c++20 -g

clean:
	rm -rf $(App).out
