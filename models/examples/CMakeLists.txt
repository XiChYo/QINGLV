cmake_minimum_required(VERSION 3.10)
project(rknn_yolo_seg)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_AUTOMOC ON)

find_package(OpenCV REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Gui)

# Need to set RKNN_API_PATH manually or via env
if(NOT DEFINED RKNN_API_PATH)
    set(RKNN_API_PATH "/usr") # Default for some boards
endif()

include_directories(${RKNN_API_PATH}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Check for lib64 or lib
find_library(RKNNRT_LIB rknnrt PATHS ${RKNN_API_PATH}/lib ${RKNN_API_PATH}/lib64)

add_executable(rknn_yolo_seg main.cc postprocess.cc)
target_link_libraries(rknn_yolo_seg ${OpenCV_LIBS} ${RKNNRT_LIB})

add_executable(yolo_thread_test
    yolo_thread_test.cpp
    ../yolothread.cpp
    ../../logger.cpp
    postprocess.cc
)
target_link_libraries(yolo_thread_test Qt5::Core Qt5::Gui ${OpenCV_LIBS} ${RKNNRT_LIB})

install(TARGETS rknn_yolo_seg DESTINATION bin)
install(TARGETS yolo_thread_test DESTINATION bin)
