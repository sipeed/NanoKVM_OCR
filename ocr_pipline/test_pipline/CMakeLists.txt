add_executable(ocr_pipline
    ocr_pipline.cpp
)

# 链接 ocr_image 和 ocr_det 库
target_link_libraries(ocr_pipline
    ocr_image
    ocr_det
)
