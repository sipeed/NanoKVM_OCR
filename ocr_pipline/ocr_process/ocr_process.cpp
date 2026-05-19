#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <csignal>
#include <atomic>

#include "ocr_image.h"
#include "ocr_det.h"
#include "ocr_rec.h"
#include "result_writer.h"
#include "log.h"
#include <fstream>
#include <sstream>

// 全局标志变量，控制日志输出
static bool g_enableTimeLog = false;   // 默认关闭时间日志
static bool g_enableMemLog = false;    // 默认关闭内存日志
static LogLevel g_logLevel = LogLevel::INFO;  // 默认 INFO 级别

// 全局变量用于控制监控循环
static std::atomic<bool> g_running(true);

void signalHandler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    g_running = false;
}

// ============================================
// 包装函数：兼容旧代码，使用 log 库的功能
// ============================================

// 包装函数：记录时间戳
inline void recordTime(const char* stepName) {
    log_record_time(stepName);
}

// 包装函数：打印时间统计
inline void printTiming() {
    log_print_timing();
}

// 包装函数：打印内存使用
inline void printMemoryUsage(const std::string& step) {
    if (g_enableMemLog) {
        log_memory(step);
    }
}

// 包装函数：打印最终内存总结
inline void printFinalMemorySummary() {
    if (g_enableMemLog) {
        log_final_memory_summary();
    }
}

// 包装函数：获取内存信息
inline MemoryInfo getMemoryInfo() {
    return get_memory_info();
}

// 结构体用于存储文件夹信息
struct FolderInfo {
    std::string path;
    std::time_t modifyTime;
};

// 比较文件夹修改时间（降序）
bool compareFolders(const FolderInfo& a, const FolderInfo& b) {
    return a.modifyTime > b.modifyTime;
}

// 结构体用于存储图片信息
struct ImageInfo {
    std::string path;
    std::string name;
    std::time_t modifyTime;
};

// 比较图片修改时间（降序）
bool compareImages(const ImageInfo& a, const ImageInfo& b) {
    return a.modifyTime > b.modifyTime;
}

// 检查文件是否已处理（是否有 _ocr 后缀）
bool isFileProcessed(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) return false;
    
    std::string baseName = filePath.substr(0, dotPos);
    return baseName.size() > 4 && baseName.substr(baseName.size() - 4) == "_ocr";
}

// 获取文件夹下所有子文件夹（按修改时间排序）
std::vector<FolderInfo> getSubFolders(const std::string& parentPath) {
    std::vector<FolderInfo> folders;
    DIR* dir = opendir(parentPath.c_str());
    if (!dir) return folders;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        std::string fullPath = parentPath + "/" + name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            FolderInfo info;
            info.path = fullPath;
            info.modifyTime = st.st_mtime;
            folders.push_back(info);
        }
    }
    closedir(dir);
    
    // 按修改时间降序排序
    std::sort(folders.begin(), folders.end(), compareFolders);
    return folders;
}

// 获取文件夹下所有图片文件（按修改时间排序）
std::vector<ImageInfo> getImagesInFolder(const std::string& folderPath) {
    std::vector<ImageInfo> images;
    DIR* dir = opendir(folderPath.c_str());
    if (!dir) return images;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        // 检查是否为图片文件（.jpg 或 .png）
        size_t dotPos = name.find_last_of('.');
        if (dotPos == std::string::npos) continue;
        std::string ext = name.substr(dotPos);
        if (ext != ".jpg" && ext != ".png" && ext != ".JPG" && ext != ".PNG") continue;
        
        std::string fullPath = folderPath + "/" + name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            ImageInfo info;
            info.path = fullPath;
            info.name = name;
            info.modifyTime = st.st_mtime;
            images.push_back(info);
        }
    }
    closedir(dir);
    
    // 按修改时间降序排序
    std::sort(images.begin(), images.end(), compareImages);
    return images;
}

// 获取最新的未处理图片
std::string getLatestUnprocessedImage(const std::string& rootFolder) {
    // 获取所有子文件夹
    std::vector<FolderInfo> folders = getSubFolders(rootFolder);
    
    // 遍历每个文件夹（从最新到最旧）
    for (const auto& folder : folders) {
        std::vector<ImageInfo> images = getImagesInFolder(folder.path);
        
        // 找到第一个未处理的图片
        for (const auto& img : images) {
            if (!isFileProcessed(img.name)) {
                return img.path;
            }
        }
    }
    
    return "";  // 没有找到未处理的图片
}

// 重命名图片，添加 _ocr 后缀
bool markImageAsProcessed(const std::string& imagePath) {
    size_t dotPos = imagePath.find_last_of('.');
    if (dotPos == std::string::npos) return false;
    
    std::string baseName = imagePath.substr(0, dotPos);
    std::string ext = imagePath.substr(dotPos);
    std::string newPath = baseName + "_ocr" + ext;
    
    if (rename(imagePath.c_str(), newPath.c_str()) != 0) {
        printf("  Warning: Failed to rename %s to %s\n", imagePath.c_str(), newPath.c_str());
        return false;
    }
    
    printf("  Renamed: %s -> %s\n", imagePath.c_str(), newPath.c_str());
    return true;
}

// 上传 JSON 到 MCP 服务（使用 system 调用 curl）
bool uploadJsonToMcp(const std::string& jsonContent, const std::string& mcpUrl) {
    printf("  Uploading JSON to MCP service: %s\n", mcpUrl.c_str());
    
    // 将 JSON 内容写入固定临时文件
    std::string tempFile = "/tmp/result.json";
    std::ofstream tempStream(tempFile);
    if (!tempStream.is_open()) {
        printf("  Error: Cannot create temp file %s\n", tempFile.c_str());
        return false;
    }
    tempStream << jsonContent;
    tempStream.close();
    printf("  Saved JSON to temp file: %s\n", tempFile.c_str());
    
    // 构建 curl 命令（参考文档中的格式）
    std::string curlCmd = "curl -sS "
                         "-H 'Content-Type: application/json' "
                         "-X POST "
                         "--data-binary @" + tempFile + " " +
                         mcpUrl;
    
    printf("  Executing: %s\n", curlCmd.c_str());
    
    // 执行命令并获取结果
    FILE* pipe = popen(curlCmd.c_str(), "r");
    if (!pipe) {
        printf("  Error: Failed to execute curl command\n");
        return false;
    }
    
    char buffer[256];
    std::string response;
    long httpCode = 0;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        response += buffer;
    }
    
    int exitCode = pclose(pipe);
    
    // 从响应中尝试提取 HTTP 状态码（curl -w 方式）
    // 如果响应中包含 JSON 且有 "ok": true，则认为成功
    if (response.find("\"ok\":true") != std::string::npos || 
        response.find("\"ok\": true") != std::string::npos) {
        printf("  Upload successful!\n");
        printf("  Response: %s\n", response.c_str());
        return true;
    } else if (response.find("\"stored\"") != std::string::npos) {
        // 另一种成功标志：包含 stored 字段
        printf("  Upload successful!\n");
        printf("  Response: %s\n", response.c_str());
        return true;
    } else {
        printf("  Warning: Upload failed or unexpected response!\n");
        printf("  Response: %s\n", response.c_str());
        printf("  Exit code: %d\n", exitCode);
        return false;
    }
}

int ocr_pipline(
    std::string detModelPath, 
    std::string recModelPath, 
    std::string dictPath, 
    std::string inputImagePath, 
    bool saveImages,
    bool saveJson = true,
    bool uploadJson = false,
    std::string mcpUrl = "http://127.0.0.1:8743/v1/observations")
{
    // 如果启用了时间日志，初始化性能分析
    if (g_enableTimeLog) {
        log_perf_init();
        log_enable_perf_log(true);
        recordTime("Start");
    }
    
    // 如果启用了内存日志，也启用性能分析
    if (g_enableMemLog) {
        log_enable_perf_log(true);
    }
    
    // 从输入路径生成输出路径：去掉后缀名，添加 _output
    std::string inputBase = inputImagePath;
    size_t lastDot = inputBase.find_last_of('.');
    if (lastDot != std::string::npos) {
        inputBase = inputBase.substr(0, lastDot);
    }
    std::string outputPath = inputBase + "_output";
    
    log_info("ocr_pipeline", "========================================");
    log_info("ocr_pipeline", "OCR Pipeline Started");
    log_info("ocr_pipeline", "========================================");
    log_info("ocr_pipeline", "Detection Model: %s", detModelPath.c_str());
    log_info("ocr_pipeline", "Recognition Model: %s", recModelPath.c_str());
    log_info("ocr_pipeline", "Dictionary: %s", dictPath.c_str());
    log_info("ocr_pipeline", "Input: %s", inputImagePath.c_str());
    log_info("ocr_pipeline", "Output: %s", outputPath.c_str());
    log_info("ocr_pipeline", "Save Images: %s", saveImages ? "yes" : "no");
    log_info("ocr_pipeline", "Save JSON: %s", saveJson ? "yes" : "no");
    if (uploadJson) {
        log_info("ocr_pipeline", "Upload JSON: yes (%s)", mcpUrl.c_str());
    } else {
        log_info("ocr_pipeline", "Upload JSON: no");
    }
    log_info("ocr_pipeline", "Time Log: %s", g_enableTimeLog ? "enabled" : "disabled");
    log_info("ocr_pipeline", "Mem Log: %s", g_enableMemLog ? "enabled" : "disabled");
    log_info("ocr_pipeline", "========================================");
    log_info("ocr_pipeline", "");
    
    // 记录初始内存
    printMemoryUsage("Start");
    
    // 步骤 1: 加载 OcrDetNPU 模型
    log_info("ocr_det", "Step 1: Loading OCR detection model to NPU...");
    OcrDetNPU detector(detModelPath);
    
    if (!detector.isModelLoaded()) {
        log_error("ocr_det", "Failed to load model!");
        return -1;
    }
    log_info("ocr_det", "Model loaded successfully!");
    recordTime("Step 1 - Load Model");
    
    // 记录模型加载后的基础内存（包含 SDK 开销）
    MemoryInfo baseMemory = getMemoryInfo();
    log_info("memory", "");
    log_info("memory", "[Memory Analysis] NPU SDK base memory: %.2f MB", baseMemory.vmRSS / 1024.0);
    log_info("memory", "  Note: AXERA SDK pre-allocates memory pools for performance.");
    log_info("memory", "  This is normal even for small models.");
    log_info("memory", "");

    // 步骤 2: 加载图像并转换为 BGR
    log_info("ocr_image", "Step 2: Loading image and converting to BGR...");
    OCRImage image(inputImagePath);
    
    if (!image.isLoaded()) {
        log_error("ocr_image", "Failed to load image!");
        return -1;
    }
    
    log_info("ocr_image", "Image loaded successfully!");
    log_info("ocr_image", "  Size: %dx%d", image.getWidth(), image.getHeight());
    recordTime("Step 2 - Load Image");
    log_info("ocr_image", "");

    // 步骤 3: 获取 BGR 图像
    log_info("ocr_image", "Step 3: Getting BGR image structure...");
    BGRImage bgrImage = image.getImage();
    log_info("ocr_image", "BGR image: %dx%d, %d channels", 
           bgrImage.width, bgrImage.height, bgrImage.channels);
    recordTime("Step 3 - Get BGR Image");
    log_info("ocr_image", "");

    // 步骤 4: 计算分块裁剪参数
    log_info("ocr_crop", "Step 4: Calculating tile crop regions...");
    
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
        log_error("ocr_crop", "No valid crop regions generated!");
        return -1;
    }
    
    log_info("ocr_crop", "Total crop regions: %zu", cropRegions.size());
    recordTime("Step 4 - Calculate Crop Regions");
    log_info("ocr_crop", "");

    // 步骤 5: 对每个裁剪区域运行 NPU 推理
    log_info("ocr_det", "Step 5: Running NPU inference on each crop region...");
    std::vector<cv::Mat> heatmaps;
    
    // 记录推理前的内存状态
    MemoryInfo beforeInference = getMemoryInfo();
    log_info("memory", "");
    log_info("memory", "[Memory Before Inference] VmRSS: %.2f MB, VmPeak: %.2f MB",
           beforeInference.vmRSS / 1024.0, beforeInference.vmPeak / 1024.0);
    
    for (size_t i = 0; i < cropRegions.size(); i++) {
        const CropRegion& region = cropRegions[i];
        log_info("ocr_det", "");
        log_info("ocr_det", "Processing region %zu/%zu [%d,%d,%d,%d]...", 
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
            log_error("ocr_crop", "  Warning: Failed to crop region %zu, skipping...", i);
            continue;
        }
        
        // 记录裁剪完成时间
        recordTime(("Step 5." + std::to_string(i + 1) + " - Crop").c_str());
        
        log_info("ocr_crop", "  Cropped image: %dx%d", croppedImage.width, croppedImage.height);
        
        // 运行 NPU 推理
        cv::Mat heatmap = detector.detect(croppedImage, targetWidth, targetHeight);
        
        if (heatmap.empty()) {
            log_error("ocr_det", "  Warning: Failed to generate heatmap for region %zu, skipping...", i);
            continue;
        }
        
        // 记录 NPU 推理完成时间
        recordTime(("Step 5." + std::to_string(i + 1) + " - NPU Inference").c_str());
        
        // 每次推理后记录内存
        MemoryInfo currentMem = getMemoryInfo();
        log_info("memory", "  [Memory after region %zu] VmRSS: %.2f MB, VmPeak: %.2f MB",
               i + 1, currentMem.vmRSS / 1024.0, currentMem.vmPeak / 1024.0);
        
        heatmaps.push_back(heatmap);
        log_info("ocr_det", "  Heatmap generated: %dx%d, type=CV_32FC1", heatmap.cols, heatmap.rows);
    }
    
    log_info("ocr_det", "");
    log_info("ocr_det", "Total heatmaps generated: %zu", heatmaps.size());
    recordTime("Step 5 - NPU Inference");
    
    // 记录推理后的内存状态
    MemoryInfo afterInference = getMemoryInfo();
    log_info("memory", "");
    log_info("memory", "[Memory After Inference] VmRSS: %.2f MB, VmPeak: %.2f MB",
           afterInference.vmRSS / 1024.0, afterInference.vmPeak / 1024.0);
    log_info("memory", "[Memory Increase] VmRSS: +%.2f MB, VmPeak: +%.2f MB",
           (afterInference.vmRSS - beforeInference.vmRSS) / 1024.0,
           (afterInference.vmPeak - beforeInference.vmPeak) / 1024.0);
    printMemoryUsage("Step 5 - After All Inferences");
    log_info("memory", "");

    // 步骤 6: 将所有热力图拼接为原图尺寸（获取热力图数据）
    log_info("ocr_merge", "Step 6: Merging heatmaps to original size...");
    HeatmapData mergedHeatmap = mergeHeatmaps(bgrImage, heatmaps, cropRegions);
    
    if (!mergedHeatmap.checkValid()) {
        log_error("ocr_merge", "Failed to merge heatmaps!");
        return -1;
    }
    
    log_info("ocr_merge", "Merged heatmap: %dx%d, channels=%d", 
           mergedHeatmap.width, mergedHeatmap.height, mergedHeatmap.channels);
    recordTime("Step 6 - Merge Heatmaps");
    printMemoryUsage("Step 6 - After Merge");
    log_info("ocr_merge", "");
    
    // 步骤 7: 从热力图提取边界框
    log_info("ocr_postprocess", "Step 7: Extracting bounding boxes from heatmap...");
    std::vector<BoundingBox> boxes = extractBoxesFromHeatmap(mergedHeatmap, 0.1f, 500, 70);
    
    // 打印所有方框信息
    log_info("ocr_postprocess", "  Detected %zu bounding boxes:", boxes.size());
    for (size_t i = 0; i < boxes.size(); i++) {
        log_info("ocr_postprocess", "    Box %zu: [%d, %d, %d, %d], score=%.3f",
               i, boxes[i].x1, boxes[i].y1, boxes[i].x2, boxes[i].y2, boxes[i].score);
    }
    recordTime("Step 7 - Postprocess");
    printMemoryUsage("Step 7 - After Postprocess");
    log_info("ocr_postprocess", "");
    
    // 步骤 8: 加载 OCR 识别模型和字典
    log_info("ocr_rec", "Step 8: Loading OCR recognition model and dictionary...");
    OcrRecNPU recognizer;
    
    if (!recognizer.loadModel(recModelPath)) {
        log_error("ocr_rec", "Failed to load recognition model!");
        return -1;
    }
    
    if (!recognizer.loadDictionary(dictPath)) {
        log_error("ocr_rec", "Failed to load dictionary!");
        return -1;
    }
    
    log_info("ocr_rec", "Recognition model and dictionary loaded successfully!");
    recordTime("Step 8 - Load Recognition Model");
    printMemoryUsage("Step 8 - After Load Recognition");
    log_info("ocr_rec", "");
    
    // 步骤 9: 对每个方框进行裁剪、保存、识别
    log_info("ocr_rec", "Step 9: Cropping, saving and recognizing text in bounding boxes...");
    std::vector<RecognitionResultWithIndex> recognitionResults;
    
    // 创建 image 文件夹（仅在 --save 时）
    std::string imageFolder = "image";
    if (saveImages) {
        mkdir(imageFolder.c_str(), 0755);
        log_info("file_io", "  Created image folder: %s", imageFolder);
    }
    
    for (size_t i = 0; i < boxes.size(); i++) {
        const BoundingBox& box = boxes[i];
        
        // 1. 裁剪方框图像（包括完整大框和小框）
        std::vector<BGRImage> boxImages = cropTextLinesFromBox(
            bgrImage,
            box.x1, box.y1, box.x2, box.y2
        );
        
        if (boxImages.empty()) {
            log_error("ocr_crop", "  Warning: Failed to crop box %zu, skipping...", i);
            continue;
        }
        
        // 2. 保存图像到文件（仅在 --save 时）
        if (saveImages) {
            for (size_t j = 0; j < boxImages.size(); j++) {
                std::string fileName = std::to_string(i);
                if (j > 0) {
                    fileName += "_" + std::to_string(j);
                }
                std::string imagePath = imageFolder + "/" + fileName + ".jpg";
                
                if (! saveImage(boxImages[j], imagePath)) {
                    log_error("file_io", "  Warning: Failed to save image %s, skipping...", imagePath.c_str());
                    continue;
                }
            }
            log_info("file_io", "  Saved %zu cropped image(s) for box %zu", boxImages.size(), i);
        }
        
        // 3. 对裁剪的图像进行识别
        if (boxImages.size() == 1) {
            // 只有大框，直接识别
            const BGRImage& boxImage = boxImages[0];
            log_info("ocr_rec", "  Recognizing box %zu (full box, %dx%d)...", i, boxImage.width, boxImage.height);
            
            OCRRecResult recResult = recognizer.recognize(boxImage);
            
            if (recResult.success && !recResult.text.empty()) {
                log_info("ocr_rec", "    -> Text: \"%s\" (confidence: %.4f)", 
                       recResult.text.c_str(), recResult.confidence);
                
                RecognitionResultWithIndex result;
                result.boxIndex = i;
                result.subBoxIndex = 0;
                result.box = box;
                result.result = recResult;
                recognitionResults.push_back(result);
            }
        } else {
            // 存在小框，识别所有小框并合并
            log_info("ocr_rec", "  Recognizing %zu sub-boxes for box %zu...", boxImages.size() - 1, i);
            
            std::vector<RecognitionResultWithIndex> subBoxResults;
            for (size_t j = 1; j < boxImages.size(); j++) {
                const BGRImage& boxImage = boxImages[j];
                log_info("ocr_rec", "    Recognizing sub-box %zu_%zu (%dx%d)...", i, j, boxImage.width, boxImage.height);
                
                OCRRecResult recResult = recognizer.recognize(boxImage);
                
                if (recResult.success && !recResult.text.empty()) {
                    log_info("ocr_rec", "      -> Text: \"%s\" (confidence: %.4f)", 
                               recResult.text.c_str(), recResult.confidence);
                        
                    RecognitionResultWithIndex result;
                    result.boxIndex = i;
                    result.subBoxIndex = j;
                    result.box = box;
                    result.result = recResult;
                    subBoxResults.push_back(result);
                }
            }
            
            // 合并小框结果
            if (!subBoxResults.empty()) {
                log_info("ocr_rec", "");
                log_info("ocr_rec", "  Merging %zu sub-box results for box %zu...", subBoxResults.size(), i);
                std::string mergedText = OcrRecNPU::mergeAllTexts(subBoxResults);
                
                RecognitionResultWithIndex mergedResult;
                mergedResult.boxIndex = i;
                mergedResult.subBoxIndex = 0;
                mergedResult.box = box;
                mergedResult.result.text = mergedText;
                mergedResult.result.success = true;
                mergedResult.result.confidence = subBoxResults[0].result.confidence;
                
                recognitionResults.push_back(mergedResult);
                log_info("ocr_rec", "  Box %zu final merged text: \"%s\"", i, mergedText.c_str());
                log_info("ocr_rec", "");
            }
        }
    }
    
    log_info("ocr_rec", "Total recognition results: %zu", recognitionResults.size());
    recordTime("Step 9 - Crop, Save & Recognize");
    printMemoryUsage("Step 9 - After Recognition");
    log_info("ocr_rec", "");
    
    // 仅在 --save 时
    if (saveImages) {
        // 步骤 10: 将热力图可视化（带方框）
        log_info("ocr_vis", "Step 10: Visualizing merged heatmap with bounding boxes...");
        BGRImage visImage = visualizeMergedHeatmap(bgrImage, mergedHeatmap, 0.5f, boxes);
        recordTime("Step 10 - Visualize");
        printMemoryUsage("Step 10 - After Visualization");
        log_info("ocr_vis", "");
    
        // 步骤 11: 保存可视化结果
        std::string visImagePath = outputPath + "_vis.jpg";
        log_info("file_io", "Step 11: Saving visualization result...");
        if (saveImage(visImage, visImagePath)) {
            log_info("file_io", "  Saved: %s", visImagePath.c_str());
        } else {
            log_error("file_io", "  Warning: Failed to save %s", visImagePath.c_str());
        }
        recordTime("Step 11 - Save Image");
        printMemoryUsage("Step 11 - After Save");
        log_info("file_io", "");
    } else {
        log_info("ocr_vis", "Step 11: Skipping visualization save (use --save to enable)");
        log_info("ocr_vis", "");
    }
    
    // 步骤 12: 对象离开作用域，自动释放 NPU 资源和热力图数据
    log_info("resource", "Step 12: Destroying objects...");
    // detector, image, heatmaps, mergedHeatmap, boxes 等对象会自动调用析构函数
    log_info("resource", "All objects destroyed, resources released automatically");
    recordTime("Step 12 - Cleanup");
    printMemoryUsage("Step 12 - After Cleanup");
    log_info("resource", "");
    
    // 步骤 13: 生成 JSON 结果
    log_info("json", "Step 13: Generating JSON result...");
    std::string jsonContent = formatResultsToJson(inputImagePath, bgrImage.width, bgrImage.height, recognitionResults, boxes);
    
    // 根据参数决定是否保存或上传
    if (saveJson) {
        // 如果同时需要上传，保存到固定临时文件
        std::string jsonPath;
        if (uploadJson) {
            jsonPath = "/tmp/result.json";
            log_info("json", "  Saving JSON to temp file for upload: %s", jsonPath.c_str());
        } else {
            jsonPath = outputPath + "_result.json";
            log_info("json", "  Saving JSON to file: %s", jsonPath.c_str());
        }
        
        std::ofstream jsonFile(jsonPath);
        if (jsonFile.is_open()) {
            jsonFile << jsonContent;
            jsonFile.close();
            log_info("json", "  Saved JSON result: %s", jsonPath.c_str());
        } else {
            log_error("json", "  Error: Cannot create JSON file: %s", jsonPath.c_str());
        }
    }
    
    // 如果需要上传
    if (uploadJson) {
        // 检查是否有识别结果
        if (recognitionResults.empty()) {
            log_info("upload", "  Skipping upload: No text recognized (block_count = 0)");
        } else {
            log_info("upload", "  Uploading JSON to MCP service...");
            if (uploadJsonToMcp(jsonContent, mcpUrl)) {
                log_info("upload", "  Upload successful!");
            } else {
                log_error("upload", "  Warning: Upload failed!");
            }
        }
    }
    
    recordTime("Step 13 - Generate & Save/Upload JSON");
    printMemoryUsage("Step 13 - After Save JSON");
    log_info("json", "");

    log_info("ocr_pipeline", "========================================");
    log_info("ocr_pipeline", "Test completed successfully!");
    log_info("ocr_pipeline", "========================================");
    
    // 打印时间统计
    printTiming();
    
    // 打印内存分析总结
    printFinalMemorySummary();
    
    return 0;
}

int main(int argc, char** argv)
{
    bool saveImages = false;  // 默认不保存小图和可视化结果
    bool saveJson = true;     // 默认保存 JSON
    bool uploadJson = false;  // 默认不上传
    std::string folderPath;   // 文件夹路径
    std::string mcpUrl = "http://127.0.0.1:8743/v1/observations";  // MCP 服务地址
    
    // 默认路径
    std::string detModelPath = "/root/models/pp_ocr/ch_PP_OCRv3_det_npu.axmodel";
    std::string recModelPath = "/root/models/pp_ocr/ch_PP_OCRv4_rec_npu.axmodel";
    std::string dictPath = "/root/models/pp_ocr/ppocr_keys_v1.txt";
    std::string inputImagePath;
    
    // 解析参数：支持 --save, --folder, --det_model, --rec_model, --dict, --image, --upload, --mcp-url, --time_log, --mem_log, --log 选项
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--save") {
            saveImages = true;
        } else if (arg == "--no-save-json") {
            saveJson = false;
        } else if (arg == "--upload") {
            uploadJson = true;
        } else if (arg == "--time_log") {
            g_enableTimeLog = true;
        } else if (arg == "--mem_log") {
            g_enableMemLog = true;
        } else if (arg == "--log" && i + 1 < argc) {
            g_logLevel = string_to_log_level(argv[++i]);
        } else if (arg == "--folder" && i + 1 < argc) {
            folderPath = argv[++i];
        } else if (arg == "--mcp-url" && i + 1 < argc) {
            mcpUrl = argv[++i];
        } else if (arg == "--det_model" && i + 1 < argc) {
            detModelPath = argv[++i];
        } else if (arg == "--rec_model" && i + 1 < argc) {
            recModelPath = argv[++i];
        } else if (arg == "--dict" && i + 1 < argc) {
            dictPath = argv[++i];
        } else if (arg == "--image" && i + 1 < argc) {
            inputImagePath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --save          Save cropped images and visualization result\n");
            printf("  --no-save-json  Do not save JSON to file (default: save)\n");
            printf("  --upload        Upload JSON to MCP service\n");
            printf("  --time_log      Enable timing logs (default: disabled)\n");
            printf("  --mem_log       Enable memory monitoring logs (default: disabled)\n");
            printf("  --log LEVEL     Set log level (none/error/info, default: info)\n");
            printf("  --mcp-url URL   MCP service URL (default: http://127.0.0.1:8743/v1/observations)\n");
            printf("  --folder PATH   Monitor folder for new images (recursive)\n");
            printf("  --det_model PATH Detection model path (default: /root/models/pp_ocr/ch_PP_OCRv3_det_npu.axmodel)\n");
            printf("  --rec_model PATH Recognition model path (default: /root/models/pp_ocr/ch_PP_OCRv4_rec_npu.axmodel)\n");
            printf("  --dict PATH     Dictionary path (default: /root/models/pp_ocr/ppocr_keys_v1.txt)\n");
            printf("  --image PATH    Input image path (required if --folder not provided)\n");
            printf("  --help, -h      Show this help message\n");
            printf("\nExample:\n");
            printf("%s --image ./test.png\n", argv[0]);
            printf("%s --save --image ./test.png --det_model /path/to/model.axmodel\n", argv[0]);
            printf("%s --folder /var/lib/openchronicle/screenshots\n", argv[0]);
            printf("%s --folder /var/lib/screenshots --upload --mcp-url http://192.168.1.100:8743/v1/observations\n", argv[0]);
            printf("%s --image ./test.png --time_log --mem_log\n", argv[0]);
            return 0;
        }
    }
    
    // 检查参数：必须有 --image 或 --folder
    if (inputImagePath.empty() && folderPath.empty()) {
        printf("Error: Either --image or --folder is required!\n");
        printf("Usage: %s [--save] --image <input_image> [--det_model PATH] [--rec_model PATH] [--dict PATH]\n", argv[0]);
        printf("   or: %s [--save] --folder <folder_path> [--det_model PATH] [--rec_model PATH] [--dict PATH]\n", argv[0]);
        printf("Use --help for more information.\n");
        return -1;
    }
    
    // 如果同时提供了 --image 和 --folder，优先使用 --folder
    if (!folderPath.empty()) {
        printf("========================================\n");
        printf("OCR Folder Monitoring Mode\n");
        printf("========================================\n");
        printf("Monitoring folder: %s\n", folderPath.c_str());
        printf("Press Ctrl+C to stop monitoring\n");
        printf("========================================\n\n");
        
        // 注册信号处理
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // 监控循环
        int processedCount = 0;
        while (g_running) {
            // 获取最新的未处理图片
            std::string latestImage = getLatestUnprocessedImage(folderPath);
            
            if (latestImage.empty()) {
                printf("No unprocessed images found. Waiting 5 seconds...\n");
                // 等待 5 秒
                for (int i = 0; i < 5 && g_running; i++) {
                    usleep(1000000);  // 1 秒
                }
                continue;
            }
            
            printf("\n========================================\n");
            printf("Processing image: %s\n", latestImage.c_str());
            printf("========================================\n");
            
            // 调用 OCR 流程函数（文件夹模式：保存 JSON 到临时文件并上传）
            int result = ocr_pipline(detModelPath, recModelPath, dictPath, latestImage, saveImages, true, true, mcpUrl);
            
            if (result == 0) {
                // 处理成功，重命名图片
                markImageAsProcessed(latestImage);
                processedCount++;
                printf("Processed %d image(s) so far\n", processedCount);
            } else {
                printf("Error: Failed to process image %s\n", latestImage.c_str());
            }
            
            printf("\n");
        }
        
        printf("\nMonitoring stopped. Total processed: %d images\n", processedCount);
        return 0;
    }
    
    // 单张图片处理模式
    printf("========================================\n");
    printf("OCR Single Image Mode\n");
    printf("========================================\n");
    
    // 调用 OCR 流程函数（单图模式：保存 JSON，不上传）
    ocr_pipline(detModelPath, recModelPath, dictPath, inputImagePath, saveImages, saveJson, uploadJson, mcpUrl);
    
    return 0;
}
