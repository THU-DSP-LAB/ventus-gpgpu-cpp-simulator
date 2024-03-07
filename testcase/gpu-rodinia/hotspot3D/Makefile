include ../../common/make.config

#Can be changed by `make TYPE=CPU`
TYPE = GPU

ifndef CC
	CC = clang 
endif

OUTPUT = *.txt
CCFLAGS=-O3 -lm 

LIBS = -lOpenCL 

hotspot3D.out: 3D.c CL_helper.c ../util/timing.c
	$(CC) $^ $(CCFLAGS) -I$(OPENCL_INC) -L$(OPENCL_LIB) $(LIBS) \
		-I../util -DTIMING \
		-o $@

clean:
	rm -f hotspot3D.out $(OUTPUT) 
