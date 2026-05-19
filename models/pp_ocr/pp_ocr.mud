[basic]
type = axmodel
model_npu = ch_PP_OCRv3_det_npu.axmodel
model_vnpu = ch_PP_OCRv3_det_vnpu.axmodel

[extra]
model_type = pp_ocr
input_type = bgr
det = true
rec_model = pp_ocr_v4.mud
labels = ppocr_keys_v1.txt
input_cache = true
output_cache = true

# just put here, not used actually
mean = 0,0,0
scale = 1,1,1
rec_mean = 127.5,127.5,127.5
rec_scale = 127.5,127.5,127.5