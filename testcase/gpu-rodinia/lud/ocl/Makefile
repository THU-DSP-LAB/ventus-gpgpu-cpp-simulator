include ../../../common/make.config

#Can be changed by `make TYPE=CPU`
TYPE = GPU

INC = -I$(OPENCL_INC) -I../common
LIB = -L$(OPENCL_LIB)

#C compiler
CC = clang++
CC_FLAGS = -g -O3 -Wall

EXE = lud.out

$(EXE): lud.cpp
	$(CC) $(KERNEL_DIM) $(CC_FLAGS) \
		-o $(EXE) \
		lud.cpp ../common/common.c ../../util/timing.c \
		$(INC) $(LIB) -lOpenCL \
		-I../../util -DTIMING \
		-Wno-unused-result

clean:
	rm -f ../$(EXE)
