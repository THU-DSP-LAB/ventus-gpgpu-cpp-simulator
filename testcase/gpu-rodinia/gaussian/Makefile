include ../../common/make.config

#Can be changed by `make TYPE=CPU`
TYPE = GPU

#C compiler
CC = clang++

SRC = clutils.cpp gaussianElim.cpp utils.cpp

CC_FLAGS = -g -O3

EXE = gaussian.out

release:
	$(CC) $(KERNEL_DIM) $(CC_FLAGS) $(SRC) -o $(EXE) -I$(OPENCL_INC) -L$(OPENCL_LIB) -lOpenCL -I../util -DTIMING -Wno-unused-result

run:$(EXE)
	./gaussian.out -p 0 -d 0 -f ../../data/gaussian/matrix5.txt -v
	# ./$(EXE) -s 4 -p 0 -d 0

clean: 
	rm -f $(EXE) 
