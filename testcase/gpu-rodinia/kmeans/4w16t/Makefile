include ../../common/make.config

#Can be changed by `make TYPE=CPU`
TYPE = GPU

# C compiler
CC = clang++
CC_FLAGS = -g -O3 

kmeans: cluster.o getopt.o read_input.o kmeans_clustering.o rmse.o
	$(CC) $(KERNEL_DIM) $(CC_FLAGS) \
		kmeans.cpp ../util/timing.c cluster.o getopt.o read_input.o kmeans_clustering.o rmse.o -o kmeans.out \
        -I$(OPENCL_INC) -L$(OPENCL_LIB) -lOpenCL \
        -I../util -g

%.o: %.[ch]
	$(CC) $(CC_FLAGS) $< -c

cluster.o: cluster.c 
	$(CC) $(CC_FLAGS) cluster.c -c

getopt.o: getopt.c 
	$(CC) $(CC_FLAGS) getopt.c -c

kmeans.o: kmeans.c 
	$(CC) $(CC_FLAGS) read_input.c -c

rmse.o: rmse.c
	$(CC) $(CC_FLAGS) rmse.c -c

clean:
	rm -f *.o *~ kmeans.out *.linkinfo
