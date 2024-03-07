include ../../common/make.config

#Can be changed by `make TYPE=CPU`
TYPE = GPU

#C compiler
CC = g++

CC_FLAGS = -g -O3 -Wall

EXE = nw.out

$(EXE): nw.c
	$(CC) ${KERNEL_DIM} $(CC_FLAGS) -o $(EXE) nw.c ../util/timing.c \
		-I$(OPENCL_INC) -L$(OPENCL_LIB) -lOpenCL \
		-I../util -DTIMING \
		-Wno-unused-result

clean:
	rm -f $(EXE)
