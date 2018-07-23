# 目标声明
.PHONY: help all release install check clean

# 默认宏赋值
CXX := g++
CXXFLAGS += -O2 -std=c++0x
DEFS += -DNDEBUG
TARGET := sock_cli_once_loop

help:
	@echo ""
	@echo "passed, please make a choose target: make [target]"
	@echo "[target]::{all release clean install check}"
	@echo "demo:"
	@echo "make all"
	@echo ""

all_cpp := *.cpp



all:#CFLAGS += -g3 -Wall
	CXXFLAGS += -g3 -Wall
all:
	@echo "building all cc with < 隐晦规则 x3 > and gdb -g3"
	@echo "start link all .o"
	#$(CC) -c $(all_c) $(CFLAGS)
	$(CXX) -c $(all_cpp) $(CXXFLAGS)
	#$(CXX) -o $(TARGET) *.o $(LDFLAGS)
	$(CXX) -o $(TARGET) *.o
	@echo "make target finish"
	
release:
	@echo "building all cc with < 隐晦规则 x3 >"
	@echo "start link all .o"
	#$(CC) -c $(all_c) $(CFLAGS) $(DEFS)
	$(CXX) -c $(all_cpp) $(CXXFLAGS) $(DEFS)
	#$(CXX) -o $(TARGET) *.o $(LDFLAGS)
	$(CXX) -o $(TARGET) *.o
	@echo "make target finish"
	
install:
	@echo "install needs root authority"

check:
	@echo "make checking"
	g++ -MM $(all_cpp)
clean:
	rm $(TARGET) *.o
	@echo "clean up finish"
	
