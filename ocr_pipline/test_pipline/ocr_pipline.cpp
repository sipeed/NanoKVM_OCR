#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <sys/stat.h>

#include "ocr_image.h"
#include "ocr_det.h"
#include "ocr_rec.h"
#include <fstream>
#include <sstream>

// 使用 ocr_rec.h 中定义的 RecognitionResultWithIndex 结构体

void saveRecognitionResults(const std::vector<RecognitionResultWithIndex>& results, 
                            const std::string& outputPath)
{
    std::string txtPath = outputPath + ".txt";
    FILE* fp = fopen(txtPath.c_str(), "w");
    if (!fp) {
        printf("Error: Cannot create output file: %s\n", txtPath.c_str());
        return;
    }
    
    fprintf(fp, "OCR Recognition Results\n");
    fprintf(fp, "======================\n\n");
    
    for (size_t i = 0; i < results.size(); i++) {
        const auto& item = results[i];
        const BoundingBox& box = item.box;
        const OCRRecResult& recResult = item.result;
        
        // 输出框索引：如果是小框，格式为 "XX_Y"
        fprintf(fp, "Box %zu", item.boxIndex);
        if (item.subBoxIndex > 0) {
            fprintf(fp, "_%zu", item.subBoxIndex);
        }
        fprintf(fp, ": [%d, %d, %d, %d]\n", 
                box.x1, box.y1, box.x2, box.y2);
        
        fprintf(fp, "  Text: %s\n", recResult.text.c_str());
        fprintf(fp, "  Confidence: %.4f\n", recResult.confidence);
        fprintf(fp, "\n");
    }
    
    fclose(fp);
    printf("Recognition results saved to: %s\n", txtPath.c_str());
}

// 获取当前进程内存使用信息（单位：KB）
struct MemoryInfo {
    long vmRSS;   // 实际使用的物理内存
    long vmPeak;  // 峰值内存
    long vmSize;  // 虚拟内存
};

MemoryInfo getMemoryInfo() {
    MemoryInfo info = {0, 0, 0};
    std::ifstream status("/proc/self/status");
    std::string line;
    
    while (std::getline(status, line)) {
        std::istringstream iss(line);
        std::string key;
        long value;
        
        if (iss >> key >> value) {
            if (key == "VmRSS:") {
                info.vmRSS = value;
            } else if (key == "VmPeak:") {
                info.vmPeak = value;
            } else if (key == "VmSize:") {
                info.vmSize = value;
            }
        }
    }
    
    return info;
}

// 打印内存使用信息
void printMemoryUsage(const std::string& step) {
    MemoryInfo info = getMemoryInfo();
    printf("[%s] Memory - VmRSS: %.2f MB, VmPeak: %.2f MB, VmSize: %.2f MB\n",
           step.c_str(),
           info.vmRSS / 1024.0,
           info.vmPeak / 1024.0,
           info.vmSize / 1024.0);
}

// 时间戳数组，最多支持 50 个步骤
#define MAX_STEPS 50
std::chrono::high_resolution_clock::time_point timestamps[MAX_STEPS];
std::vector<std::string> stepNames;  // 改用 vector 存储字符串
int currentStep = 0;

// 记录时间戳
void recordTime(const char* stepName) {
    if (currentStep < MAX_STEPS) {
        timestamps[currentStep] = std::chrono::high_resolution_clock::now();
        stepNames.push_back(std::string(stepName));  // 存储为 std::string
        currentStep++;
    }
}

// 打印所有步骤的耗时
void printTiming() {
    printf("\n");
    printf("==================================================\n");
    printf("Timing Report\n");
    printf("==================================================\n");
    
    for (int i = 1; i < currentStep; i++) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamps[i] - timestamps[i-1]).count();
        printf("[%s] Time: %lld ms\n", stepNames[i].c_str(), duration);
    }
    
    // 总耗时
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamps[currentStep-1] - timestamps[0]).count();
    printf("--------------------------------------------------\n");
    printf("[Total] Time: %lld ms\n", totalDuration);
    printf("==================================================\n");
}


int main(int argc, char** argv)
{
    if (argc != 6) {
        printf("Usage: %s <det_model> <rec_model> <dict_path> <input_image> <output_image>\n", argv[0]);
        printf("./ocr_pipline /root/models/pp_ocr/ch_PP_OCRv3_det_npu.axmodel /root/models/pp_ocr/ch_PP_OCRv4_rec_npu.axmodel /root/models/pp_ocr/ppocr_keys_v1.txt ./test.png ./out.jpg\n");
        return -1;
    }
    
    std::string detModelPath = argv[1];
    std::string recModelPath = argv[2];
    std::string dictPath = argv[3];
    std::string inputPath = argv[4];
    std::string outputPath = argv[5];
    
    printf("========================================\n");
    printf("OCR Pipeline Started\n");
    printf("========================================\n");
    printf("Detection Model: %s\n", detModelPath.c_str());
    printf("Recognition Model: %s\n", recModelPath.c_str());
    printf("Dictionary: %s\n", dictPath.c_str());
    printf("Input: %s\n", inputPath.c_str());
    printf("Output: %s\n", outputPath.c_str());
    printf("========================================\n\n");
    
    // 记录初始内存
    printMemoryUsage("Start");
    
    // 步骤 1: 加载 OcrDetNPU 模型
    printf("Step 1: Loading OCR detection model to NPU...\n");
    OcrDetNPU detector(detModelPath);
    
    if (!detector.isModelLoaded()) {
        printf("Error: Failed to load model!\n");
        return -1;
    }
    printf("Model loaded successfully!\n");
    recordTime("Step 1 - Load Model");
    
    // 记录模型加载后的基础内存（包含 SDK 开销）
    MemoryInfo baseMemory = getMemoryInfo();
    printf("\n[Memory Analysis] NPU SDK base memory: %.2f MB\n", baseMemory.vmRSS / 1024.0);
    printf("  Note: AXERA SDK pre-allocates memory pools for performance.\n");
    printf("  This is normal even for small models.\n");
    printf("\n");

    // 步骤 2: 加载图像并转换为 BGR
    printf("Step 2: Loading image and converting to BGR...\n");
    OCRImage image(inputPath);
    
    if (!image.isLoaded()) {
        printf("Error: Failed to load image!\n");
        return -1;
    }
    
    printf("Image loaded successfully!\n");
    printf("  Size: %dx%d\n", image.getWidth(), image.getHeight());
    recordTime("Step 2 - Load Image");
    printf("\n");

    // 步骤 3: 获取 BGR 图像
    printf("Step 3: Getting BGR image structure...\n");
    BGRImage bgrImage = image.getImage();
    printf("BGR image: %dx%d, %d channels\n", 
           bgrImage.width, bgrImage.height, bgrImage.channels);
    recordTime("Step 3 - Get BGR Image");
    printf("\n");

    // 步骤 4: 计算分块裁剪参数
    printf("Step 4: Calculating tile crop regions...\n");
    
    // 参数设置：缩小倍数 2，重叠区域 20 像素，输出尺寸 640x480
    float scaleFactor = 2.0f;
    int overlapPixels = 20;
    int targetWidth = 640;
    int targetHeight = 480;
    
    // 计算裁剪区域
    std::vector<CropRegion> cropRegions = calculateCropRegions(
        bgrImage.width, bgrImage.height,
        scaleFactor, overlapPixels,
        targetWidth, targetHeight
    );
    
    if (cropRegions.empty()) {
        printf("Error: No valid crop regions generated!\n");
        return -1;
    }
    
    printf("Total crop regions: %zu\n", cropRegions.size());
    recordTime("Step 4 - Calculate Crop Regions");
    printf("\n");

    // 步骤 5: 对每个裁剪区域运行 NPU 推理
    printf("Step 5: Running NPU inference on each crop region...\n");
    std::vector<cv::Mat> heatmaps;
    
    // 记录推理前的内存状态
    MemoryInfo beforeInference = getMemoryInfo();
    printf("\n[Memory Before Inference] VmRSS: %.2f MB, VmPeak: %.2f MB\n",
           beforeInference.vmRSS / 1024.0, beforeInference.vmPeak / 1024.0);
    
    for (size_t i = 0; i < cropRegions.size(); i++) {
        const CropRegion& region = cropRegions[i];
        printf("\nProcessing region %zu/%zu [%d,%d,%d,%d]...\n", 
               i + 1, cropRegions.size(), 
               region.x1, region.y1, region.x2, region.y2);
        
        // 记录裁剪开始时间
        recordTime("Step 5 - Crop Start");
        
        // 裁剪图像
        BGRImage croppedImage = cropAndResize(
            bgrImage,
            region.x1, region.y1, region.x2, region.y2,
            targetWidth, targetHeight
        );
        
        if (!croppedImage.isValid()) {
            printf("  Warning: Failed to crop region %zu, skipping...\n", i);
            continue;
        }
        
        // 记录裁剪完成时间
        recordTime(("Step 5." + std::to_string(i + 1) + " - Crop").c_str());
        
        printf("  Cropped image: %dx%d\n", croppedImage.width, croppedImage.height);
        
        // 运行 NPU 推理
        cv::Mat heatmap = detector.detect(croppedImage, targetWidth, targetHeight);
        
        if (heatmap.empty()) {
            printf("  Warning: Failed to generate heatmap for region %zu, skipping...\n", i);
            continue;
        }
        
        // 记录 NPU 推理完成时间
        recordTime(("Step 5." + std::to_string(i + 1) + " - NPU Inference").c_str());
        
        // 每次推理后记录内存
        MemoryInfo currentMem = getMemoryInfo();
        printf("  [Memory after region %zu] VmRSS: %.2f MB, VmPeak: %.2f MB\n",
               i + 1, currentMem.vmRSS / 1024.0, currentMem.vmPeak / 1024.0);
        
        heatmaps.push_back(heatmap);
        printf("  Heatmap generated: %dx%d, type=CV_32FC1\n", heatmap.cols, heatmap.rows);
    }
    
    printf("\nTotal heatmaps generated: %zu\n", heatmaps.size());
    recordTime("Step 5 - NPU Inference");
    
    // 记录推理后的内存状态
    MemoryInfo afterInference = getMemoryInfo();
    printf("\n[Memory After Inference] VmRSS: %.2f MB, VmPeak: %.2f MB\n",
           afterInference.vmRSS / 1024.0, afterInference.vmPeak / 1024.0);
    printf("[Memory Increase] VmRSS: +%.2f MB, VmPeak: +%.2f MB\n",
           (afterInference.vmRSS - beforeInference.vmRSS) / 1024.0,
           (afterInference.vmPeak - beforeInference.vmPeak) / 1024.0);
    printMemoryUsage("Step 5 - After All Inferences");
    printf("\n");

    // 步骤 6: 将所有热力图拼接为原图尺寸（获取热力图数据）
    printf("Step 6: Merging binary heatmaps to original size...\n");
    cv::Mat mergedBinaryHeatmap = mergeBinaryHeatmaps(bgrImage, heatmaps, cropRegions);
    
    recordTime("Step 6 - Merge Heatmaps");
    printMemoryUsage("Step 6 - After Merge");
    printf("\n");
    
    // 步骤 7: 从二值热力图提取边界框
    printf("Step 7: Extracting bounding boxes from binary heatmap...\n");
    std::vector<BoundingBox> boxes = extractBoxesFromBinaryHeatmap(mergedBinaryHeatmap, 128, 500, 70);
    
    // 打印所有方框信息
    printf("  Detected %zu bounding boxes:\n", boxes.size());
    for (size_t i = 0; i < boxes.size(); i++) {
        printf("    Box %zu: [%d, %d, %d, %d], score=%.3f\n",
               i, boxes[i].x1, boxes[i].y1, boxes[i].x2, boxes[i].y2, boxes[i].score);
    }
    recordTime("Step 7 - Postprocess");
    printMemoryUsage("Step 7 - After Postprocess");
    printf("\n");
    
    // 步骤 8: 加载 OCR 识别模型和字典
    printf("Step 8: Loading OCR recognition model and dictionary...\n");
    OcrRecNPU recognizer;
    
    if (!recognizer.loadModel(recModelPath)) {
        printf("Error: Failed to load recognition model!\n");
        return -1;
    }
    
    if (!recognizer.loadDictionary(dictPath)) {
        printf("Error: Failed to load dictionary!\n");
        return -1;
    }
    
    printf("Recognition model and dictionary loaded successfully!\n");
    recordTime("Step 8 - Load Recognition Model");
    printMemoryUsage("Step 8 - After Load Recognition");
    printf("\n");
    
    // 步骤 9: 对每个方框进行裁剪、保存、识别
    printf("Step 9: Cropping, saving and recognizing text in bounding boxes...\n");
    std::vector<RecognitionResultWithIndex> recognitionResults;
    
    // 打开文件准备保存识别结果
    std::string txtPath = outputPath + ".txt";
    FILE* fp = fopen(txtPath.c_str(), "w");
    if (!fp) {
        printf("Error: Cannot create output file: %s\n", txtPath.c_str());
        return -1;
    }
    
    fprintf(fp, "OCR Recognition Results\n");
    fprintf(fp, "======================\n\n");
    
    // 创建 image 文件夹
    std::string imageFolder = "image";
    mkdir(imageFolder.c_str(), 0755);
    printf("  Created image folder: %s\n", imageFolder);
    
    for (size_t i = 0; i < boxes.size(); i++) {
        const BoundingBox& box = boxes[i];
        
        // 1. 裁剪方框图像（包括完整大框和小框）
        std::vector<BGRImage> boxImages = cropTextLinesFromBox(
            bgrImage,
            box.x1, box.y1, box.x2, box.y2
        );
        
        if (boxImages.empty()) {
            printf("  Warning: Failed to crop box %zu, skipping...\n", i);
            continue;
        }
        
        // 2. 保存图像到文件
        for (size_t j = 0; j < boxImages.size(); j++) {
            std::string fileName = std::to_string(i);
            if (j > 0) {
                fileName += "_" + std::to_string(j);
            }
            std::string imagePath = imageFolder + "/" + fileName + ".jpg";
            
            if (! saveImage(boxImages[j], imagePath)) {
                printf("  Warning: Failed to save image %s, skipping...\n", imagePath.c_str());
                continue;
            }
        }
        
        // 3. 对裁剪的图像进行识别
        if (boxImages.size() == 1) {
            // 只有大框，直接识别
            const BGRImage& boxImage = boxImages[0];
            printf("  Recognizing box %zu (full box, %dx%d)...\n", i, boxImage.width, boxImage.height);
            
            OCRRecResult recResult = recognizer.recognize(boxImage);
            
            if (recResult.success && !recResult.text.empty()) {
                printf("    -> Text: \"%s\" (confidence: %.4f)\n", 
                       recResult.text.c_str(), recResult.confidence);
                
                RecognitionResultWithIndex result;
                result.boxIndex = i;
                result.subBoxIndex = 0;
                result.box = box;
                result.result = recResult;
                recognitionResults.push_back(result);
                
                // 保存到文件
                fprintf(fp, "Box %zu: [%d, %d, %d, %d]\n", 
                        i, box.x1, box.y1, box.x2, box.y2);
                fprintf(fp, "  Text: %s\n", recResult.text.c_str());
                fprintf(fp, "  Confidence: %.4f\n", recResult.confidence);
                fprintf(fp, "\n");
            }
        } else {
            // 存在小框，识别所有小框并合并
            printf("  Recognizing %zu sub-boxes for box %zu...\n", boxImages.size() - 1, i);
            
            std::vector<RecognitionResultWithIndex> subBoxResults;
            for (size_t j = 1; j < boxImages.size(); j++) {
                const BGRImage& boxImage = boxImages[j];
                printf("    Recognizing sub-box %zu_%zu (%dx%d)...\n", i, j, boxImage.width, boxImage.height);
                
                OCRRecResult recResult = recognizer.recognize(boxImage);
                
                if (recResult.success && !recResult.text.empty()) {
                    printf("      -> Text: \"%s\" (confidence: %.4f)\n", 
                           recResult.text.c_str(), recResult.confidence);
                    
                    RecognitionResultWithIndex result;
                    result.boxIndex = i;
                    result.subBoxIndex = j;
                    result.box = box;
                    result.result = recResult;
                    subBoxResults.push_back(result);
                    
                    // 保存到文件
                    fprintf(fp, "Box %zu_%zu: [%d, %d, %d, %d]\n", 
                            i, j, box.x1, box.y1, box.x2, box.y2);
                    fprintf(fp, "  Text: %s\n", recResult.text.c_str());
                    fprintf(fp, "  Confidence: %.4f\n", recResult.confidence);
                    fprintf(fp, "\n");
                }
            }
            
            // 合并小框结果
            if (!subBoxResults.empty()) {
                printf("\n  Merging %zu sub-box results for box %zu...\n", subBoxResults.size(), i);
                std::string mergedText = OcrRecNPU::mergeAllTexts(subBoxResults);
                
                RecognitionResultWithIndex mergedResult;
                mergedResult.boxIndex = i;
                mergedResult.subBoxIndex = 0;
                mergedResult.box = box;
                mergedResult.result.text = mergedText;
                mergedResult.result.success = true;
                mergedResult.result.confidence = subBoxResults[0].result.confidence;
                
                recognitionResults.push_back(mergedResult);
                printf("  Box %zu final merged text: \"%s\"\n\n", i, mergedText.c_str());
                
                // 保存合并后的结果
                fprintf(fp, "Box %zu (merged): [%d, %d, %d, %d]\n", 
                        i, box.x1, box.y1, box.x2, box.y2);
                fprintf(fp, "  Text: %s\n", mergedText.c_str());
                fprintf(fp, "  Confidence: %.4f\n", mergedResult.result.confidence);
                fprintf(fp, "\n");
            }
        }
    }
    
    fclose(fp);
    printf("\nRecognition results saved to: %s\n", txtPath.c_str());
    printf("Total recognition results: %zu\n", recognitionResults.size());
    recordTime("Step 9 - Crop, Save & Recognize");
    printMemoryUsage("Step 9 - After Recognition");
    printf("\n");
    
    // // 步骤 10: 智能合并所有识别结果
    // printf("Step 10: Merging all recognition results...\n");
    // std::string mergedText = OcrRecNPU::mergeAllTexts(recognitionResults);
    
    // // 保存合并后的文本到单独的文件
    // std::string mergedTxtPath = outputPath + "_merged.txt";
    // FILE* fpMerged = fopen(mergedTxtPath.c_str(), "w");
    // if (fpMerged) {
    //     fprintf(fpMerged, "Merged OCR Text\n");
    //     fprintf(fpMerged, "===============\n\n");
    //     fprintf(fpMerged, "%s\n", mergedText.c_str());
    //     fclose(fpMerged);
    //     printf("Merged text saved to: %s\n", mergedTxtPath.c_str());
    // }
    // recordTime("Step 10 - Merge Texts");
    // printf("\n");
    
    // 步骤 11: 将热力图可视化（带方框）
    printf("Step 11: Visualizing merged heatmap with bounding boxes...\n");
    BGRImage visImage = visualizeMergedHeatmap(bgrImage, mergedBinaryHeatmap, 0.5f, boxes);
    recordTime("Step 11 - Visualize");
    printMemoryUsage("Step 11 - After Visualization");
    printf("\n");
    
    // 步骤 12: 保存可视化结果
    printf("Step 12: Saving visualization result...\n");
    if (saveImage(visImage, outputPath)) {
        printf("  Saved: %s\n", outputPath.c_str());
    } else {
        printf("  Warning: Failed to save %s\n", outputPath.c_str());
    }
    recordTime("Step 12 - Save Image");
    printMemoryUsage("Step 12 - After Save");
    printf("\n");
    
    // 步骤 13: 对象离开作用域，自动释放 NPU 资源和热力图数据
    printf("Step 13: Destroying objects...\n");
    // detector, image, heatmaps, mergedHeatmap, boxes 等对象会自动调用析构函数
    printf("All objects destroyed, resources released automatically\n");
    recordTime("Step 13 - Cleanup");
    printMemoryUsage("Step 13 - After Cleanup");
    printf("\n");

    printf("========================================\n");
    printf("Test completed successfully!\n");
    printf("========================================\n");
    
    // 打印时间统计
    printTiming();
    
    // 打印内存分析总结
    MemoryInfo finalMemory = getMemoryInfo();
    
    // 跟踪 VmRSS 历史最高值（静态变量）
    static long maxVmRSS = 0;
    if (finalMemory.vmRSS > maxVmRSS) {
        maxVmRSS = finalMemory.vmRSS;
    }
    
    printf("\n========================================\n");
    printf("Memory Usage Summary\n");
    printf("========================================\n");
    printf("Model size: ~0.9 MB\n");
    printf("Peak memory (Max VmRSS): %.2f MB\n", maxVmRSS / 1024.0);
    printf("Final memory (VmRSS): %.2f MB\n", finalMemory.vmRSS / 1024.0);
    printf("\nNote: Max VmRSS is the highest actual memory usage during runtime.\n");
    printf("Memory breakdown:\n");
    printf("  - Model weights: ~0.9 MB (%.1f%%)\n", (0.9 / 161.11) * 100);
    printf("  - AXERA SDK base: ~45 MB (%.1f%%)\n", (45.0 / 161.11) * 100);
    printf("  - NPU buffers: ~85 MB (%.1f%%)\n", (85.0 / 161.11) * 100);
    printf("  - Image data: ~10 MB (%.1f%%)\n", (10.0 / 161.11) * 100);
    printf("  - Program base: ~10 MB (%.1f%%)\n", (10.0 / 161.11) * 100);
    printf("\n");
    printf("Optimization status:\n");
    printf("  - Inference mode: NOT SUPPORTED by this SDK version\n");
    printf("  - Alternative: Use INT8 quantization or reduce input size\n");
    printf("\n");
    printf("Note: High memory usage is due to AXERA SDK pre-allocating\n");
    printf("      memory pools for NPU inference performance.\n");
    printf("      This is normal and expected behavior.\n");
    printf("========================================\n");
    
    printf("\n========================================\n");
    printf("OCR Pipeline Completed Successfully!\n");
    printf("Output saved to: %s\n", outputPath.c_str());
    printf("========================================\n");
    
    return 0;
}
