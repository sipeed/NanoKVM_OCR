cmake_minimum_required(VERSION 3.10)
project(ocr_process_app)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR}/..)

# 配置芯片类型和 BSP 路径
if (CHIP_AX650)
    add_definitions(-DCHIP_AX650)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax650n_bsp_sdk/msp/out)
    endif()
elseif (CHIP_AX630C)
    add_definitions(-DCHIP_AX630C)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax620q_bsp_sdk/msp/out/arm64_glibc)
    endif()
elseif (CHIP_AX620Q)
    add_definitions(-DCHIP_AX620Q)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax620q_bsp_sdk/msp/out/arm_uclibc)
    endif()
elseif (CHIP_AX620QE)
    add_definitions(-DCHIP_AX620QE)
    if(NOT BSP_MSP_DIR)
        set(BSP_MSP_DIR ${PROJECT_ROOT_DIR}/ax620q_bsp_sdk/msp/out/arm_glibc)
    endif()
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

# 查找 OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 添加可执行文件
add_executable(ocr_process_app ocr_process.cpp)

# 链接库
target_link_libraries(ocr_process_app 
    ocr_image
    ocr_det
    ocr_rec
    result_writer
    log
    ${MSP_LIBS}
    pthread
    ${OpenCV_LIBS}
)

# 设置运行时路径
set_target_properties(ocr_process_app PROPERTIES
    BUILD_RPATH "${MSP_LIB_DIR}"
    INSTALL_RPATH "${MSP_LIB_DIR}"
)

# 安装目标
install(TARGETS ocr_process_app DESTINATION bin)
