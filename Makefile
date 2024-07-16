# Library and include path
SYSTEMC_HOME ?= $(HOME)/.local/systemc/current
LIB_DIR=-L$(SYSTEMC_HOME)/lib-linux64
INC_DIR=-I$(SYSTEMC_HOME)/include
LIB=-lsystemc -Wl,-rpath,$(SYSTEMC_HOME)/lib-linux64

# Build path and target
BUILD_DIR = ./build
OUTPUT_DIR = ./output
OBJ_DIR   = $(BUILD_DIR)/objs
BINARY    = $(OUTPUT_DIR)/ventus-sim.out

# Default build goal
default: $(BINARY)

# Build tools and flags
# check gcc version not less than 11
GCC_VERSION := $(shell g++ -dumpversion)
GE_GCC_11 := $(shell expr $(GCC_VERSION) \>= 11)
ifeq ($(GE_GCC_11), 1)
    CXX := g++
else
    GCC_PATH := $(GCC11_PATH)
    CXX := $(GCC_PATH)/bin/g++ -L$(GCC_PATH)/lib64 -I$(GCC_PATH)/include -Wl,-rpath,$(GCC_PATH)/lib64
endif
CC = gcc
LD = $(CXX)

# Sources and objects
SRCS_CPP := $(shell find ./src -name '*.cpp')
SRCS_C   := $(shell find ./src -name '*.c')
DEPS  = $(SRCS_CPP:%.cpp=$(OBJ_DIR)/%.d)
DEPS += $(SRCS_C:%.c=$(OBJ_DIR)/%.d)
OBJS  = $(SRCS_CPP:%.cpp=$(OBJ_DIR)/%.o)
OBJS += $(SRCS_C:%.c=$(OBJ_DIR)/%.o)

# Build options
CFLAGS   += $(INC_DIR) -O0 -ggdb3 -MMD
#CFLAGS   += -Wall -Wno-sign-compare
#CFLAGS   += -fsanitize=address -fsanitize=undefined
CXXFLAGS += $(CFLAGS) -std=c++20
LDFLAGS  += $(LIB_DIR) $(LIB) $(CFLAGS) -std=c++20

# Rule to compile cpp files
$(OBJ_DIR)/%.o: %.cpp
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# Rule to compile c files
$(OBJ_DIR)/%.o: %.c
	@echo + CC  $<
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c -o $@ $<

# Dependences
-include $(DEPS)

# Linking
$(BINARY): $(OBJS) 
	@echo + LD $@
	@mkdir -p $(dir $@)
	@$(LD) -o $@ $(OBJS) $(LDFLAGS)

RUNFLAGS = --numkernel 2 vecadd adv_vecadd/vecadd4x4.metadata adv_vecadd/vecadd4x4.data matadd adv_matadd/matadd.metadata adv_matadd/matadd.data --numcycle 30000
RUNFLAGS_tensor484 = --numkernel 1 tensor tensor/wmma484fp32/wmma484fp32.metadata tensor/wmma484fp32/wmma484fp32.data --numcycle 2000
RUNFLAGS_tensor242 = --numkernel 1 tensor tensor/wmma424fp32/wmma424fp32.metadata tensor/wmma424fp32/wmma424fp32.data --numcycle 2000
RUNFLAGS_vectormma = --numkernel 1 tensor tensor/wmma484fp32/vectormma.metadata tensor/wmma484fp32/vectormma.data --numcycle 6000

RUNFLAGS_rodinia_bfs = --numkernel 1 bfs gpu-rodinia/bfs/4x8/BFS_1_0.metadata gpu-rodinia/bfs/4x8/BFS_1_0.data --numcycle 30000

run: $(BINARY)
	./$(BINARY) $(RUNFLAGS)

gdb: $(BINARY)
	gdb --tui -s $(BINARY) --args $(BINARY) $(RUNFLAGS)

clean:
	-rm -rf $(BUILD_DIR)
	-rm -rf output/*.vcd

.PHONY: init default clean run gdb

init:
	git submodule init && git submodule update --recursive
