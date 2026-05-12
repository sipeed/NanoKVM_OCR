#include <cstdio>
#include <cstdlib>
#include <string>
#include <opencv2/opencv.hpp>

#include "ocr_rec.h"

/**
 * @brief 加载图像并调整为 320x48
 * @param imagePath 图像路径
 * @return BGRImage 结构
 */
BGRImage loadImage(const std::string& imagePath)
{
    cv::Mat img = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (img.empty()) {
        printf("Error: Failed to load image: %s\n", imagePath.c_str());
        BGRImage empty;
        return empty;
    }
    
    // 调整大小为 320x48
    cv::Mat resized;
    cv::resize(img, resized, cv::Size(320, 48));
    
    // 转换为 BGR 格式（如果原来是 RGBA）
    if (resized.channels() == 4) {
        cv::cvtColor(resized, resized, cv::COLOR_BGRA2BGR);
    } else if (resized.channels() == 1) {
        cv::cvtColor(resized, resized, cv::COLOR_GRAY2BGR);
    }
    
    // 创建 BGRImage
    BGRImage bgrImage;
    bgrImage.data = resized;
    bgrImage.width = resized.cols;
    bgrImage.height = resized.rows;
    
    printf("Loaded image: %dx%d\n", bgrImage.width, bgrImage.height);
    
    return bgrImage;
}

/**
 * @brief 释放 BGRImage 资源
 */
void freeImage(BGRImage& img)
{
    img.data.release();
}

/**
 * @brief 输出每个字符的识别详情
 * @param result 识别结果
 * @param recognizer 识别器对象
 */
void printCharDetails(const OCRRecResult& result, const OcrRecNPU& recognizer)
{
    printf("\nCharacter Details:\n");
    printf("------------------\n");
    printf("Total characters: %zu\n\n", result.text.length());
    
    if (result.charConfidences.size() != result.text.length()) {
        printf("Warning: charConfidences size mismatch with text length\n");
        return;
    }
    
    printf("Idx  Char  Hex     Conf%%   Index\n");
    printf("---  ----  ------  -------  -----\n");
    
    for (size_t i = 0; i < result.text.length(); i++) {
        char c = result.text[i];
        float conf = result.charConfidences[i];
        int charIndex = (i < result.charIndices.size()) ? result.charIndices[i] : -1;
        
        // 打印字符（如果是不可打印字符，显示十六进制）
        if (c >= 32 && c <= 126) {
            printf("%3zu  '%c'   0x%02X    %6.2f%%  %5d\n", 
                   i, c, (unsigned char)c, conf * 100, charIndex);
        } else {
            printf("%3zu  [0x%02X]  0x%02X    %6.2f%%  %5d\n", 
                   i, (unsigned char)c, (unsigned char)c, conf * 100, charIndex);
        }
    }
    
    printf("------------------\n");
    
    // 统计信息
    if (!result.charConfidences.empty()) {
        float maxConf = result.charConfidences[0];
        float minConf = result.charConfidences[0];
        float sumConf = 0;
        
        for (float conf : result.charConfidences) {
            if (conf > maxConf) maxConf = conf;
            if (conf < minConf) minConf = conf;
            sumConf += conf;
        }
        
        printf("\nStatistics:\n");
        printf("  Max confidence: %.2f%%\n", maxConf * 100);
        printf("  Min confidence: %.2f%%\n", minConf * 100);
        printf("  Avg confidence: %.2f%%\n", (sumConf / result.charConfidences.size()) * 100);
    }
}

void printUsage(const char* programName)
{
    printf("Usage: %s <image_path> <model_path> <dict_path>\n", programName);
    printf("\nArguments:\n");
    printf("  image_path   Path to the input image (should be 320x48, or will be resized)\n");
    printf("  model_path   Path to the OCR recognition model (.axmodel)\n");
    printf("  dict_path    Path to the dictionary file (.txt)\n");
    printf("\nExample:\n");
    printf("  %s test.jpg ocr_rec_model.axmodel dict.txt\n", programName);
}

int main(int argc, char** argv)
{
    printf("========================================\n");
    printf("OCR Recognition Test\n");
    printf("========================================\n\n");
    
    // 检查参数
    if (argc != 4) {
        printf("Error: Wrong number of arguments!\n");
        printf("./test_rec image/66_1.jpg /root/models/pp_ocr/ch_PP_OCRv4_rec_npu.axmodel /root/models/pp_ocr/ppocr_keys_v1.txt\n");
        printUsage(argv[0]);
        return -1;
    }
    
    std::string imagePath = argv[1];
    std::string modelPath = argv[2];
    std::string dictPath = argv[3];
    
    printf("Input parameters:\n");
    printf("  Image: %s\n", imagePath.c_str());
    printf("  Model: %s\n", modelPath.c_str());
    printf("  Dict:  %s\n", dictPath.c_str());
    printf("\n");
    
    // 1. 加载图像
    printf("Step 1: Loading image...\n");
    BGRImage inputImage = loadImage(imagePath);
    if (inputImage.data.empty() || inputImage.width == 0 || inputImage.height == 0) {
        printf("Error: Failed to load image!\n");
        return -1;
    }
    printf("\n");
    
    // 2. 加载识别模型
    printf("Step 2: Loading OCR recognition model...\n");
    OcrRecNPU recognizer;
    
    if (!recognizer.loadModel(modelPath)) {
        printf("Error: Failed to load model: %s\n", modelPath.c_str());
        freeImage(inputImage);
        return -1;
    }
    printf("Model loaded successfully!\n\n");
    
    // 3. 加载字典
    printf("Step 3: Loading dictionary...\n");
    if (!recognizer.loadDictionary(dictPath)) {
        printf("Error: Failed to load dictionary: %s\n", dictPath.c_str());
        freeImage(inputImage);
        return -1;
    }
    printf("Dictionary loaded successfully! (dict size: %d)\n\n", recognizer.getDictSize());
    
    // 4. 运行识别
    printf("Step 4: Running OCR recognition...\n");
    OCRRecResult result = recognizer.recognize(inputImage);
    
    if (result.success) {
        printf("\n========================================\n");
        printf("Recognition Result:\n");
        printf("========================================\n");
        printf("Text: %s\n", result.text.c_str());
        printf("Confidence: %.4f\n", result.confidence);
        printf("========================================\n");
        
        // 输出每个字符的详细信息
        printCharDetails(result, recognizer);
    } else {
        printf("\n========================================\n");
        printf("Recognition Failed!\n");
        printf("========================================\n");
        if (!result.errorMessage.empty()) {
            printf("Error: %s\n", result.errorMessage.c_str());
        }
        printf("========================================\n");
        freeImage(inputImage);
        return -1;
    }
    
    // 5. 保存结果到文件
    std::string outputPath = imagePath + ".txt";
    FILE* fp = fopen(outputPath.c_str(), "w");
    if (fp) {
        fprintf(fp, "OCR Recognition Result\n");
        fprintf(fp, "======================\n\n");
        fprintf(fp, "Input Image: %s\n", imagePath.c_str());
        fprintf(fp, "Model: %s\n", modelPath.c_str());
        fprintf(fp, "Dictionary: %s\n", dictPath.c_str());
        fprintf(fp, "\nText: %s\n", result.text.c_str());
        fprintf(fp, "Confidence: %.4f\n\n", result.confidence);
        
        // 保存字符详细信息
        if (result.charConfidences.size() == result.text.length()) {
            fprintf(fp, "Character Details:\n");
            fprintf(fp, "------------------\n");
            fprintf(fp, "Idx  Char  Hex     Conf%%   Index\n");
            fprintf(fp, "---  ----  ------  -------  -----\n");
            
            for (size_t i = 0; i < result.text.length(); i++) {
                char c = result.text[i];
                float conf = result.charConfidences[i];
                int charIndex = (i < result.charIndices.size()) ? result.charIndices[i] : -1;
                
                if (c >= 32 && c <= 126) {
                    fprintf(fp, "%3zu  '%c'   0x%02X    %6.2f%%  %5d\n", 
                            i, c, (unsigned char)c, conf * 100, charIndex);
                } else {
                    fprintf(fp, "%3zu  [0x%02X]  0x%02X    %6.2f%%  %5d\n", 
                            i, (unsigned char)c, (unsigned char)c, conf * 100, charIndex);
                }
            }
            fprintf(fp, "------------------\n");
        }
        
        fclose(fp);
        printf("\nResult saved to: %s\n", outputPath.c_str());
    }
    
    // 清理资源
    freeImage(inputImage);
    
    printf("\n========================================\n");
    printf("Test completed successfully!\n");
    printf("========================================\n\n");
    
    return 0;
}
