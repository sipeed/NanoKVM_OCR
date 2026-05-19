#include "log.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <sstream>

// 全局日志等级
static LogLevel g_logLevel = LogLevel::INFO;

// 性能分析全局变量
std::chrono::high_resolution_clock::time_point g_timestamps[MAX_STEPS];
std::vector<std::string> g_stepNames;
int g_currentStep = 0;

// 全局标志：是否启用性能分析
static bool g_enablePerfLog = false;

// 初始化日志系统
void log_init(LogLevel level) {
    g_logLevel = level;
}

// 设置日志等级
void log_set_level(LogLevel level) {
    g_logLevel = level;
}

// 获取当前日志等级
LogLevel log_get_level() {
    return g_logLevel;
}

// 日志函数实现
void log(LogLevel level, const std::string& tag, const std::string& format, ...) {
    // 检查日志等级
    if (level > g_logLevel) {
        return;
    }
    
    // 构建格式化输出
    va_list args;
    va_start(args, format);
    
    // 打印前缀 [tag]
    printf("[%s] ", tag.c_str());
    
    // 打印内容
    vprintf(format.c_str(), args);
    
    // 换行
    printf("\n");
    
    va_end(args);
}

// 便捷函数：INFO 级别
void log_info(const std::string& tag, const std::string& format, ...) {
    if (g_logLevel < LogLevel::INFO) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    printf("[%s] ", tag.c_str());
    vprintf(format.c_str(), args);
    printf("\n");
    va_end(args);
}

// 便捷函数：ERROR 级别
void log_error(const std::string& tag, const std::string& format, ...) {
    if (g_logLevel < LogLevel::ERROR) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    printf("[%s] ", tag.c_str());
    vprintf(format.c_str(), args);
    printf("\n");
    va_end(args);
}

// 将字符串转换为日志等级
LogLevel string_to_log_level(const std::string& str) {
    if (str == "none" || str == "NONE" || str == "0") {
        return LogLevel::NONE;
    } else if (str == "error" || str == "ERROR" || str == "1") {
        return LogLevel::ERROR;
    } else if (str == "info" || str == "INFO" || str == "2") {
        return LogLevel::INFO;
    }
    // 默认返回 INFO
    return LogLevel::INFO;
}

// 将日志等级转换为字符串
std::string log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::NONE:
            return "NONE";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::INFO:
            return "INFO";
        default:
            return "UNKNOWN";
    }
}

// ============================================
// 性能分析工具实现
// ============================================

// 获取当前进程内存使用信息
MemoryInfo get_memory_info() {
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
void log_memory(const std::string& step) {
    if (!g_enablePerfLog) return;
    
    MemoryInfo info = get_memory_info();
    printf("[%s] Memory - VmRSS: %.2f MB, VmPeak: %.2f MB, VmSize: %.2f MB\n",
           step.c_str(),
           info.vmRSS / 1024.0,
           info.vmPeak / 1024.0,
           info.vmSize / 1024.0);
}

// 打印最终内存使用总结
void log_final_memory_summary() {
    if (!g_enablePerfLog) return;
    
    MemoryInfo finalMemory = get_memory_info();
    printf("\n========================================\n");
    printf("Memory Usage Summary\n");
    printf("========================================\n");
    printf("Final memory (VmRSS): %.2f MB\n", finalMemory.vmRSS / 1024.0);
}

// 记录时间戳
void log_record_time(const char* stepName) {
    if (!g_enablePerfLog) return;
    
    if (g_currentStep < MAX_STEPS) {
        g_timestamps[g_currentStep] = std::chrono::high_resolution_clock::now();
        g_stepNames.push_back(std::string(stepName));
        g_currentStep++;
    }
}

// 打印所有步骤的耗时
void log_print_timing() {
    if (!g_enablePerfLog) return;
    
    printf("\n");
    printf("==================================================\n");
    printf("Timing Report\n");
    printf("==================================================\n");
    
    for (int i = 1; i < g_currentStep; i++) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            g_timestamps[i] - g_timestamps[i-1]).count();
        printf("[%s] Time: %lld ms\n", g_stepNames[i].c_str(), duration);
    }
    
    // 总耗时
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        g_timestamps[g_currentStep-1] - g_timestamps[0]).count();
    printf("--------------------------------------------------\n");
    printf("[Total] Time: %lld ms\n", totalDuration);
    printf("==================================================\n");
}

// 初始化性能分析
void log_perf_init() {
    g_currentStep = 0;
    g_stepNames.clear();
}

// 启用/禁用性能日志
void log_enable_perf_log(bool enable) {
    g_enablePerfLog = enable;
}

bool log_is_perf_log_enabled() {
    return g_enablePerfLog;
}
