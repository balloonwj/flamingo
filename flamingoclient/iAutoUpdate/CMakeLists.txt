cmake_minimum_required(VERSION 3.0)
project(iAutoUpdate)
SET(AUTO_UPDATE_SOURCE_FILE ./iUpdateAuto.cpp 
./resource.h 
./unzip.h 
./unzip.cpp
./wlconfig.h
./zwldef.h)

add_link_options("/SUBSYSTEM:WINDOWS")
SET(AUTO_UPDATE_RESOURCE_FILE ./iUpdateAuto.rc)
if(CMAKE_CL_64)
    #设置64位的程序目标(可执行程序)输出路径
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/../bin/Debug64/)     
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/../bin/Release64/) 
else()
    #设置32位的程序目标(可执行程序)输出路径
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/../Bin/)     
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/../Bin/) 
endif()
 
#设置可执行程序的文件名，注意此处有WIN32表示是一个Win32的GUI程序
add_executable(iAutoUpdate WIN32 ${AUTO_UPDATE_SOURCE_FILE} ${AUTO_UPDATE_RESOURCE_FILE})