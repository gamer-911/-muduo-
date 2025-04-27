cmake_minimum_required(VERSION 2.5)
project(mymuduo)

# mymuduo最终编译成so动态库，设置动态库路径,放在根目录lib文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置调试信息 以及启动c++11标准编译
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")

# 定义参与编译的源代码文件
aux_source_directory(. SRC_LIST)
# 编译生成动态库mymuduo
add_library(mymuduo SHARED ${SRC_LIST} "TcpServer.h" "Poller.h" "EPollPoller.h" "CurrentThread.h" "EventLoopThread.h" "Acceptor.h" "TcpConnection.h" "Buffer.h" "CMakeMuduo.cpp")

