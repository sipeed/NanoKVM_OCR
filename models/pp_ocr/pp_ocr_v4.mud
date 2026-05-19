[basic]
type = axmodel
model_npu = ch_PP_OCRv4_rec_npu.axmodel
model_vnpu = ch_PP_OCRv4_rec_vnpu.axmodel

[extra]
model_type = pp_ocr
input_type = bgr
det = false
labels = ppocr_keys_v1.txt
input_cache = true
output_cache = true

# just put here, not used actually
mean = 127.5,127.5,127.5
scale = 127.5,127.5127.5