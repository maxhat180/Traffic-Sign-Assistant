cmake_minimum_required(VERSION 3.20)
project(TrafficSignAssistant VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRAFFIC_SIGN_REQUIRE_OPENCV
       "Fail configuration when the OpenCV adapter cannot be built" OFF)

function(traffic_sign_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR
       CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    endif()
endfunction()

add_library(traffic_sign_core STATIC frame.c image.c detect.c)
target_include_directories(traffic_sign_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
traffic_sign_warnings(traffic_sign_core)

add_library(traffic_sign_cpp_bridge STATIC cpp/image_bridge.cpp)
target_include_directories(traffic_sign_cpp_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
traffic_sign_warnings(traffic_sign_cpp_bridge)

add_executable(traffic_sign_assistant main.c)
target_link_libraries(traffic_sign_assistant PRIVATE traffic_sign_core)
traffic_sign_warnings(traffic_sign_assistant)

find_package(OpenCV QUIET COMPONENTS core imgcodecs imgproc)
if(OpenCV_FOUND)
    add_library(traffic_sign_opencv STATIC cpp/opencv_adapter.cpp)
    target_include_directories(traffic_sign_opencv PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(traffic_sign_opencv PUBLIC
        traffic_sign_cpp_bridge ${OpenCV_LIBS})
    traffic_sign_warnings(traffic_sign_opencv)
    target_link_libraries(traffic_sign_assistant PRIVATE traffic_sign_opencv)
    target_compile_definitions(traffic_sign_assistant PRIVATE
        TRAFFIC_SIGN_WITH_OPENCV=1)
    message(STATUS "OpenCV adapter enabled (${OpenCV_VERSION})")
elseif(TRAFFIC_SIGN_REQUIRE_OPENCV)
    message(FATAL_ERROR
        "OpenCV was not found. Set OpenCV_DIR to its CMake package directory.")
else()
    message(STATUS "OpenCV not found: building the PPM-only executable")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_frame tests/test_frame.c)
    target_link_libraries(test_frame PRIVATE traffic_sign_core)
    traffic_sign_warnings(test_frame)
    add_test(NAME frame COMMAND test_frame)

    add_executable(test_detect tests/test_detect.c)
    target_link_libraries(test_detect PRIVATE traffic_sign_core)
    traffic_sign_warnings(test_detect)
    add_test(NAME detect COMMAND test_detect)

    add_executable(test_image_bridge tests/test_image_bridge.cpp)
    target_link_libraries(test_image_bridge PRIVATE
        traffic_sign_cpp_bridge traffic_sign_core)
    traffic_sign_warnings(test_image_bridge)
    add_test(NAME image_bridge COMMAND test_image_bridge)

    if(OpenCV_FOUND)
        add_executable(test_opencv_adapter tests/test_opencv_adapter.cpp)
        target_link_libraries(test_opencv_adapter PRIVATE
            traffic_sign_opencv traffic_sign_core)
        traffic_sign_warnings(test_opencv_adapter)
        add_test(NAME opencv_adapter COMMAND test_opencv_adapter)
    endif()

    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(NAME cli COMMAND ${POWERSHELL_EXECUTABLE} -NoProfile
            -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.ps1
            -Executable $<TARGET_FILE:traffic_sign_assistant>)
    endif()
endif()
