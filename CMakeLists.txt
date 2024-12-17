
cmake_minimum_required(VERSION 3.5)
project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)

#设置生成路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
#搜索头文件
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

#搜索源文件
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SRC)

add_executable(threadpool ${SRC})

target_link_libraries(threadpool pthread)




