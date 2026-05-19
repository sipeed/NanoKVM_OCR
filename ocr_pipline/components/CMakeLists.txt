# Components CMakeLists.txt
# This file manages all components in the components directory

# Add OCR image processing component
add_subdirectory(ocr_image)

# Add OCR detection component
add_subdirectory(ocr_det)

# Add OCR recognition component
add_subdirectory(ocr_rec)

# Add result writer component
add_subdirectory(result_writer)

# Add log component
add_subdirectory(log)
