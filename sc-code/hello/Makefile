LIB_DIR=-L /home/lan/utils/systemc-2.3.4/lib-linux64/
INC_DIR=-I /home/lan/utils/systemc-2.3.4/include/
LIB=-lsystemc -Wl,-rpath,/home/lan/utils/systemc-2.3.4/lib-linux64/
App=BASE
.PHONY:all
all:
	# g++ -o $(App) $(App).cpp worker1.cpp $(LIB_DIR) $(INC_DIR) $(LIB)   
	g++ -o $(App) *.cpp $(LIB_DIR) $(INC_DIR) $(LIB) -std=c++20
clean:                                                                                                           
	RM -RF $(App)

