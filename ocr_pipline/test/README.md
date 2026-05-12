# OCR Recognition Test

独立的 OCR 识别测试程序，用于测试单张图像的识别效果。

## 编译

在项目根目录的 build 文件夹中：

```bash
cd build
cmake .. -DCHIP_AX620Q=ON
make test_rec
```

## 使用方法

```bash
./test_rec <image_path> <model_path> <dict_path>
```

### 参数说明

- `image_path`: 输入图像路径（建议 320x48，如果不是会自动调整）
- `model_path`: OCR 识别模型路径（.axmodel 文件）
- `dict_path`: 字典文件路径（.txt 文件）

### 示例

```bash
# 识别一张 320x48 的图片
./test_rec test.jpg ../../models/ocr_rec.axmodel ../../models/dict.txt

# 识别其他尺寸的图片（会自动调整为 320x48）
./test_rec input.png ../../models/ocr_rec.axmodel ../../models/dict.txt
```

## 输出

程序会输出：
1. 控制台显示识别结果和置信度
2. 保存详细结果到 `image_path.txt` 文件

### 输出示例

```
========================================
OCR Recognition Result
========================================
Text: 以及 ATX 开关机、复位按键
Confidence: 0.9234
========================================

Result saved to: test.jpg.txt
```

## 注意事项

1. 图像会自动调整为 320x48 像素
2. 确保模型和字典文件路径正确
3. 识别结果会保存到输入图片同名的.txt 文件中
