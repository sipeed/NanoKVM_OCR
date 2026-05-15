cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
project(ocr_pipline)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "-g -O0")
    add_definitions(-D__AX_SKEL_DEBUG__)
elseif (CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_CXX_FLAGS "-O2 -fdata-sections -ffunction-sections")
endif()

set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR}/..)

if (CHIP_AX650)
    message(STATUS "Chip type: AX650")
    add_definitions(-DCHIP_AX650)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax650n_bsp_sdk/msp/out)
    endif()
elseif (CHIP_AX630C)
    message(STATUS "Chip type: AX630C")
    add_definitions(-DCHIP_AX630C)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax620q_bsp_sdk/msp/out/arm64_glibc)
    endif()
elseif (CHIP_AX620Q)
    message(STATUS "Chip type: AX620Q")
    add_definitions(-DCHIP_AX620Q)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax620q_bsp_sdk/msp/out/arm_uclibc)
    endif()
elseif (CHIP_AX620QE)
    message(STATUS "Chip type: AX620QE")
    add_definitions(-DCHIP_AX620QE)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax620q_bsp_sdk/msp/out/arm_glibc)
    endif()
else()
    message(FATAL_ERROR "FATAL: Chip type not set! Use -DCHIP_AX620Q=ON or -DCHIP_AX620QE=ON or -DCHIP_AX630C=ON or -DCHIP_AX650=ON")
endif()

if(NOT EXISTS ${BSP_MSP_DIR})
    message(FATAL_ERROR "FATAL: BSP_MSP_DIR ${BSP_MSP_DIR} does not exist")
endif()

message(STATUS "BSP_MSP_DIR = ${BSP_MSP_DIR}")

set(MSP_INC_DIR ${BSP_MSP_DIR}/include)
set(MSP_LIB_DIR ${BSP_MSP_DIR}/lib)

list(APPEND MSP_LIBS
        ax_sys
        ax_proton
        ax_ivps
        ax_venc
        ax_engine
        ax_interpreter)

include_directories(${MSP_INC_DIR})
link_directories(${MSP_LIB_DIR})

include_directories(${PROJECT_ROOT_DIR}/inc)
include_directories(${PROJECT_ROOT_DIR}/test_pipline)
include_directories(${PROJECT_ROOT_DIR}/test_pipline/3rdparty/eigen3)

if (CHIP_AX630C)
    set(OpenCV_DIR ${PROJECT_ROOT_DIR}/third-party/opencv/lib/arm64/glibc/cmake/opencv4)
elseif (CHIP_AX620Q)
    set(OpenCV_DIR ${PROJECT_ROOT_DIR}/third-party/opencv/lib/arm/glibc/cmake/opencv4)
elseif (CHIP_AX620QE)
    set(OpenCV_DIR ${PROJECT_ROOT_DIR}/third-party/opencv/lib/arm/glibc/cmake/opencv4)
elseif (CHIP_AX650)
    set(OpenCV_DIR ${PROJECT_ROOT_DIR}/third-party/opencv/lib/arm/glibc/cmake/opencv4)
endif()
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Add components directory
add_subdirectory(components)

# Main executable
add_executable(ocr_pipline test_pipline/ocr_pipline.cpp)
target_link_libraries(ocr_pipline
    ocr_image
    ocr_det
    ocr_rec
    ${MSP_LIBS}
    pthread
    ${OpenCV_LIBS}
)

# Test executable for standalone OCR recognition
add_executable(test_rec test_rec/test_rec.cpp)
target_link_libraries(test_rec
    ocr_image
    ocr_rec
    ${MSP_LIBS}
    pthread
    ${OpenCV_LIBS}
)

# Image getting application
add_subdirectory(image_getting)

set_target_properties(ocr_pipline PROPERTIES
    BUILD_RPATH "${MSP_LIB_DIR}"
    INSTALL_RPATH "${MSP_LIB_DIR}"
)

set_target_properties(test_rec PROPERTIES
    BUILD_RPATH "${MSP_LIB_DIR}"
    INSTALL_RPATH "${MSP_LIB_DIR}"
)

set_target_properties(image_getting PROPERTIES
    BUILD_RPATH "${MSP_LIB_DIR}"
    INSTALL_RPATH "${MSP_LIB_DIR}"
)

# Install target
install(TARGETS ocr_pipline DESTINATION bin)
install(TARGETS test_rec DESTINATION bin)
install(TARGETS image_getting DESTINATION bin)
