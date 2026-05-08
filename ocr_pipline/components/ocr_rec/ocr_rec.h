#ifndef OCR_REC_H
#define OCR_REC_H

#include <string>
#include <vector>
#include <memory>
#include "ocr_image.h"

// AXERA SDK headers
#include "ax_engine_api.h"
#include "ax_sys_api.h"

#define OCR_REC_CMM_ALIGN_SIZE 128
#define OCR_REC_CMM_SESSION_NAME "npu"

/**
 * @brief OCR 识别结果结构体
 * 
 * 存储识别的文字内容、置信度等信息
 */
struct OCRRecResult {
    std::string text;                    // 识别的文字
    float confidence;                    // 平均置信度
    std::vector<float> charConfidences;  // 每个字符的置信度
    std::vector<int> charIndices;        // 字符在字典中的索引
    bool success;                        // 是否成功
    std::string errorMessage;            // 错误信息
    
    /**
     * @brief 默认构造函数
     */
    OCRRecResult() : confidence(0.0f), success(false) {}
    
    /**
     * @brief 检查识别是否成功
     */
    bool isValid() const {
        return success && !text.empty() && confidence > 0.0f;
    }
};

/**
 * @brief OCR 识别类
 * 
 * 功能：
 * 1. 加载 OCR 识别模型和字典文件
 * 2. 输入 48x320 的 BGR 图像，输出识别的文字内容
 * 3. 析构函数自动释放模型资源
 */
class OcrRecNPU {
public:
    /**
     * @brief 默认构造函数
     */
    OcrRecNPU();
    
    /**
     * @brief 析构函数 - 释放模型资源
     */
    ~OcrRecNPU();
    
    /**
     * @brief 加载识别模型
     * @param modelPath 模型文件路径 (.axmodel)
     * @return true 加载成功，false 加载失败
     */
    bool loadModel(const std::string& modelPath);
    
    /**
     * @brief 加载字典文件
     * @param dictPath 字典文件路径 (.txt)
     * @return true 加载成功，false 加载失败
     */
    bool loadDictionary(const std::string& dictPath);
    
    /**
     * @brief 识别图像中的文字
     * @param inputImage 输入的 BGR 图像 (48x320)
     * @return OCRRecResult 识别结果
     */
    OCRRecResult recognize(const BGRImage& inputImage);
    
    /**
     * @brief 检查模型是否已加载
     * @return true 模型已加载，false 模型未加载
     */
    bool isModelLoaded() const { return modelLoaded; }
    
    /**
     * @brief 获取字典大小
     * @return int 字典字符数量
     */
    int getDictSize() const { return dictSize; }
    
    /**
     * @brief 根据索引获取字符
     * @param index 字符索引
     * @return std::string 字符字符串
     */
    std::string getChar(int index) const;
    
    /**
     * @brief 检查文件是否存在
     * @param filename 文件路径
     * @return true 文件存在，false 文件不存在
     */
    static bool fileExists(const std::string& filename);

private:
    AX_ENGINE_HANDLE handle;           // NPU 引擎句柄
    bool modelLoaded;                  // 模型是否已加载
    std::vector<char> modelBuffer;     // 模型文件缓冲区
    
    std::vector<std::string> dictionary; // 字典
    int dictSize;                        // 字典大小
    
    // 缓存的 IO 信息和数据（用于加速推理）
    AX_ENGINE_IO_INFO_T* cachedIOInfo;
    AX_ENGINE_IO_T cachedIOData;
    bool ioBuffersAllocated;
    
    /**
     * @brief 准备 IO 缓冲区
     */
    bool prepareIO(AX_ENGINE_IO_INFO_T* ioInfo, AX_ENGINE_IO_T* ioData);
    
    /**
     * @brief 释放 IO 缓冲区
     */
    void freeIO(AX_ENGINE_IO_T* io);
    
    /**
     * @brief CTC 解码 - 将模型输出转换为文字
     */
    std::string decodeCTC(const float* outputData, int width, int height,
                          std::vector<float>& charConfidences,
                          std::vector<int>& charIndices);
    
    /**
     * @brief 图像预处理 - 将输入图像转换为模型需要的格式
     */
    cv::Mat preprocessImage(const cv::Mat& inputImage, int targetWidth = 320, int targetHeight = 48);
};

#endif // OCR_REC_H
