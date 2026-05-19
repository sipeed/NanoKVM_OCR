#include "result_writer.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <regex>

// 转义 JSON 字符串中的特殊字符
static std::string escapeJsonString(const std::string& input) {
    std::ostringstream oss;
    for (char c : input) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

// 获取当前时间的 RFC3339 格式（自动获取系统时区）
static std::string getCurrentTimeRFC3339() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    
    // 获取本地时间
    localtime_r(&time_t_now, &tm_now);
    
    // 获取时区偏移量（秒）
    // tm_gmtoff 是 Linux 特有的扩展，表示本地时间与 UTC 的偏移（秒）
    long tz_offset = tm_now.tm_gmtoff;
    
    // 计算时区的小时和分钟
    int tz_hours = tz_offset / 3600;
    int tz_minutes = abs(tz_offset % 3600) / 60;
    
    // 确定时区符号
    char tz_sign = (tz_offset >= 0) ? '+' : '-';
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%S");
    oss << tz_sign << std::setw(2) << std::setfill('0') << abs(tz_hours) << ":" 
        << std::setw(2) << std::setfill('0') << tz_minutes;
    
    return oss.str();
}

// 从文件名中提取时间（格式：2026-05-19T14-09-28+08-00.jpg -> 2026-05-19T14:09:28+08:00）
static std::string extractTimeFromFilename(const std::string& filepath) {
    // 提取文件名（不含路径）
    size_t lastSlash = filepath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? 
                           filepath.substr(lastSlash + 1) : filepath;
    
    // 去掉扩展名
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        filename = filename.substr(0, lastDot);
    }
    
    // 尝试匹配格式：2026-05-19T14-09-28+08-00 或 2026-05-19T14-09-28
    std::regex timePattern(R"((\d{4})-(\d{2})-(\d{2})T(\d{2})-(\d{2})-(\d{2}))");
    std::smatch match;
    
    if (std::regex_search(filename, match, timePattern)) {
        // 提取时间组件
        std::string year = match[1].str();
        std::string month = match[2].str();
        std::string day = match[3].str();
        std::string hour = match[4].str();
        std::string minute = match[5].str();
        std::string second = match[6].str();
        
        // 格式化为 RFC3339: 2026-05-19T14:09:28+08:00
        return year + "-" + month + "-" + day + "T" + hour + ":" + minute + ":" + second + "+08:00";
    }
    
    // 如果文件名中没有时间信息，返回当前时间
    return getCurrentTimeRFC3339();
}

std::string formatResultsToJson(
    const std::string& inputImagePath,
    int imageWidth,
    int imageHeight,
    const std::vector<RecognitionResultWithIndex>& results,
    const std::vector<BoundingBox>& boxes)
{
    std::ostringstream jsonStream;
    
    // 计算整体置信度
    float totalConfidence = 0.0f;
    int validCount = 0;
    for (const auto& result : results) {
        if (result.result.success && !result.result.text.empty()) {
            totalConfidence += result.result.confidence;
            validCount++;
        }
    }
    float avgConfidence = (validCount > 0) ? (totalConfidence / validCount) : 0.0f;
    
    // 从文件名提取时间，如果文件名没有时间信息则使用当前时间
    std::string currentTime = extractTimeFromFilename(inputImagePath);
    
    // 写入 JSON 内容
    jsonStream << "{\n";
    jsonStream << "  \"schema_version\": 1,\n";
    jsonStream << "  \"type\": \"ocr_observation\",\n";
    jsonStream << "  \"captured_at\": \"" << currentTime << "\",\n";
    jsonStream << "  \"source\": {\n";
    jsonStream << "    \"kind\": \"screen_ocr\",\n";
    jsonStream << "    \"device\": \"nanoagent\",\n";
    jsonStream << "    \"engine\": \"ocr_process_npu\"\n";
    jsonStream << "  },\n";
    jsonStream << "  \"screen\": {\n";
    jsonStream << "    \"image_path\": \"" << escapeJsonString(inputImagePath) << "\",\n";
    jsonStream << "    \"width\": " << imageWidth << ",\n";
    jsonStream << "    \"height\": " << imageHeight << "\n";
    jsonStream << "  },\n";
    jsonStream << "  \"context\": {\n";
    jsonStream << "    \"app_name\": \"ScreenOCR\",\n";
    jsonStream << "    \"title\": \"OCR Process Result\"\n";
    jsonStream << "  },\n";
    jsonStream << "  \"event\": {\n";
    jsonStream << "    \"type\": \"ocr_capture\",\n";
    jsonStream << "    \"reason\": \"manual\"\n";
    jsonStream << "  },\n";
    jsonStream << "  \"ocr\": {\n";
    jsonStream << "    \"language\": \"zh\",\n";
    
    // 拼接所有文本
    std::string fullText;
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) fullText += " ";
        fullText += results[i].result.text;
    }
    jsonStream << "    \"text\": \"" << escapeJsonString(fullText) << "\",\n";
    jsonStream << "    \"confidence\": " << std::fixed << std::setprecision(4) << avgConfidence << ",\n";
    jsonStream << "    \"block_count\": " << results.size() << ",\n";
    jsonStream << "    \"blocks\": [\n";
    
    // 写入每个 block
    for (size_t i = 0; i < results.size(); i++) {
        const auto& result = results[i];
        jsonStream << "      {\n";
        jsonStream << "        \"id\": " << (i + 1) << ",\n";
        jsonStream << "        \"text\": \"" << escapeJsonString(result.result.text) << "\",\n";
        jsonStream << "        \"bbox\": [" << result.box.x1 << ", " << result.box.y1 << ", " 
                 << result.box.x2 << ", " << result.box.y2 << "],\n";
        jsonStream << "        \"confidence\": " << std::fixed << std::setprecision(4) 
                 << result.result.confidence << "\n";
        jsonStream << "      }";
        if (i < results.size() - 1) {
            jsonStream << ",";
        }
        jsonStream << "\n";
    }
    
    jsonStream << "    ]\n";
    jsonStream << "  },\n";
    jsonStream << "  \"tags\": [\"external\", \"ocr\"]\n";
    jsonStream << "}\n";
    
    return jsonStream.str();
}
