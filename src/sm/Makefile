# Library and include path
LIB_DIR=-L$(SYSTEMC_HOME)/lib-linux64
INC_DIR=-I$(SYSTEMC_HOME)/include
LIB=-lsystemc -Wl,-rpath,$(SYSTEMC_HOME)/lib-linux64

# Build path and target
BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/objs
BINARY    = ventus.out

# Default build goal
default: $(BINARY)

# Build tools
# 检查默认 g++ 版本是否大于等于 11
GCC_VERSION := $(shell g++ -dumpversion)
GE_GCC_11 := $(shell expr $(GCC_VERSION) \>= 11)
# 设置 g++ 路径
ifeq ($(GE_GCC_11), 1)
    # 默认 g++ 版本大于等于 11，直接使用 g++
    CXX := g++
else
    # 默认 g++ 版本小于 11，使用指定路径下的 g++
    GCC_PATH := $(GCC11_PATH)
    CXX := $(GCC_PATH)/bin/g++ -L$(GCC_PATH)/lib64 -I$(GCC_PATH)/include -Wl,-rpath,$(GCC_PATH)/lib64
endif
LD  = $(CXX)

# Sources and objects
GPGPU_test_FILE=../GPGPU_test.cpp BASE.cpp opc.cpp decode.cpp init_decodetable.cpp init_instable.cpp \
 ../CTA_Scheduler.cpp ../parameters.cpp ../context_model.hpp ../context_model.cpp ../utils.hpp ../gpgpu_model.hpp \
 writeback.cpp regfile.cpp warp_scheduler.cpp \
 exec_salu.cpp exec_simtstk.cpp exec_lsu.cpp exec_vfpu.cpp exec_csr.cpp exec_mul.cpp \
 exec_sfu.cpp exec_valu.cpp exec_tensor.cpp

SRCS = $(GPGPU_test_FILE)
OBJS = $(SRCS:%.cpp=$(OBJ_DIR)/%.o)

# Build options
CFLAGS   += $(INC_DIR) -O0 -ggdb3
#CFLAGS   += -Wall -Wno-sign-compare
#CFLAGS   += -fsanitize=address -fsanitize=undefined
CXXFLAGS += $(CFLAGS) -std=c++20
LDFLAGS  += $(LIB_DIR) $(LIB) $(CFLAGS) -std=c++20

$(OBJ_DIR)/%.o: %.cpp
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BINARY): $(OBJS) 
	@echo + LD $@
	@$(LD) -o $@ $(OBJS) $(LDFLAGS)

RUNFLAGS = --inssrc imem --metafile matadd/matadd.metadata --datafile matadd/matadd.data --numcycle 20000

RUNFLAGS_NEW = --inssrc imem --numkernel 2 vecadd vecadd/vecadd4x4.metadata vecadd/vecadd4x4.data matadd matadd/matadd.metadata matadd/matadd.data --numcycle 30000
	
RUNFLAGS_vecadd = --inssrc imem --metafile vecadd/vecadd4x4.metadata --datafile vecadd/vecadd4x4.data --numcycle 20000

RUNFLAGS_nn = --inssrc imem --metafile adv_nn/NearestNeighbor_0.metadata --datafile adv_nn/NearestNeighbor_0.data --numcycle 20000

RUNFLAGS_tensor = --inssrc imem --numkernel 1 tensor tensor/tensor.metadata tensor/tensor.data --numcycle 1200

run: $(BINARY)
	./$(BINARY) $(RUNFLAGS_NEW)

run_vecadd: $(BINARY)
	./$(BINARY) $(RUNFLAGS_vecadd)

run_nn: $(BINARY)
	./$(BINARY) $(RUNFLAGS_nn)

run_tensor: $(BINARY)
	./$(BINARY) $(RUNFLAGS_tensor)

gdb: $(BINARY)
	gdb --tui -s $(BINARY) --args $(BINARY) $(RUNFLAGS_NEW)

clean:
	-rm -rf $(BUILD_DIR)
	-rm -rf $(BINARY) *.vcd

GPGPU_test_GDB:
	$(CXX) -o $(BINARY) $(GPGPU_test_FILE) $(LIB_DIR) $(INC_DIR) $(LIB) -std=c++20 -g

.PHONY: default clean run gdb run_vecadd run_nn
