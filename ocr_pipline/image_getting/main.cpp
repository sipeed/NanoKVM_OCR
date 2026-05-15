#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <set>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <dirent.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

static std::mutex g_imageMutex;
static cv::Mat g_lastImage;
static const int MAX_IMAGE_COUNT = 100;
static const std::string SOCK_PATH = "/run/kvm/vin_ctrl.sock";
static const std::string OUTPUT_BASE_DIR = "/var/lib/openchronicle/screenshots";

std::string getCurrentDateTime()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto tm_now = std::localtime(&time_t_now);
    
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm_now->tm_year + 1900) << "-"
        << std::setw(2) << (tm_now->tm_mon + 1) << "-"
        << std::setw(2) << tm_now->tm_mday;
    
    return oss.str();
}

std::string getCurrentTimeWithTz()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto tm_now = std::localtime(&time_t_now);
    
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm_now->tm_year + 1900) << "-"
        << std::setw(2) << (tm_now->tm_mon + 1) << "-"
        << std::setw(2) << tm_now->tm_mday << "T"
        << std::setw(2) << tm_now->tm_hour << "-"
        << std::setw(2) << tm_now->tm_min << "-"
        << std::setw(2) << tm_now->tm_sec << "+08-00";
    
    return oss.str();
}

bool isAllBlack(const cv::Mat& image)
{
    if (image.empty()) {
        return true;
    }
    
    cv::Mat small;
    cv::resize(image, small, cv::Size(640, 480));
    
    cv::Scalar meanValue = cv::mean(small);
    double threshold = 1.0;
    
    return (meanValue[0] < threshold && meanValue[1] < threshold && meanValue[2] < threshold);
}

bool areImagesSimilar(const cv::Mat& img1, const cv::Mat& img2, double threshold = 0.95)
{
    if (img1.empty() || img2.empty()) {
        return false;
    }
    
    cv::Mat resized1, resized2;
    cv::resize(img1, resized1, cv::Size(640, 480));
    cv::resize(img2, resized2, cv::Size(640, 480));
    
    resized1.convertTo(resized1, CV_32F);
    resized2.convertTo(resized2, CV_32F);
    
    resized1 = resized1.reshape(1, resized1.total());
    resized2 = resized2.reshape(1, resized2.total());
    
    double correlation = cv::compareHist(resized1, resized2, cv::HISTCMP_CORREL);
    
    return correlation >= threshold;
}

std::string generateFilename(int subIndex)
{
    (void)subIndex;
    return getCurrentTimeWithTz();
}

bool ensureDirectoryExists(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (mkdir(path.c_str(), 0755) != 0) {
            std::cerr << "Error: Cannot create directory: " << path << std::endl;
            return false;
        }
    }
    return true;
}

struct FileEntry {
    std::string path;
    std::string name;
    std::string dateDir;
    time_t createTime;
};

std::vector<FileEntry> getAllFiles(const std::string& baseDir)
{
    std::vector<FileEntry> allFiles;
    
    DIR* baseDirp = opendir(baseDir.c_str());
    if (!baseDirp) {
        return allFiles;
    }
    
    struct dirent* entry;
    while ((entry = readdir(baseDirp)) != nullptr) {
        std::string dateDirName = entry->d_name;
        
        if (dateDirName == "." || dateDirName == "..") {
            continue;
        }
        
        std::string dateDirPath = baseDir + "/" + dateDirName;
        
        struct stat st;
        if (stat(dateDirPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            DIR* dateDir = opendir(dateDirPath.c_str());
            if (!dateDir) {
                continue;
            }
            
            struct dirent* fileEntry;
            while ((fileEntry = readdir(dateDir)) != nullptr) {
                std::string fileName = fileEntry->d_name;
                
                if (fileName == "." || fileName == "..") {
                    continue;
                }
                
                if (fileName.size() > 4 && fileName.substr(fileName.size() - 4) == ".jpg") {
                    std::string fullPath = dateDirPath + "/" + fileName;
                    
                    struct stat fileStat;
                    if (stat(fullPath.c_str(), &fileStat) == 0) {
                        FileEntry file;
                        file.path = fullPath;
                        file.name = fileName;
                        file.dateDir = dateDirName;
                        file.createTime = fileStat.st_ctime;
                        allFiles.push_back(file);
                    }
                }
            }
            
            closedir(dateDir);
        }
    }
    
    closedir(baseDirp);
    return allFiles;
}

void cleanupOldFiles(const std::string& baseDir, int maxCount)
{
    std::vector<FileEntry> files = getAllFiles(baseDir);
    int fileCount = static_cast<int>(files.size());
    
    if (fileCount <= maxCount) {
        return;
    }
    
    int toDelete = fileCount - maxCount;
    
    std::sort(files.begin(), files.end(),
        [](const FileEntry& a, const FileEntry& b) {
            return a.createTime < b.createTime;
        });
    
    std::cout << "File count (" << fileCount << ") exceeds limit (" 
              << maxCount << "), deleting " << toDelete << " oldest file(s)..." << std::endl;
    
    std::set<std::string> emptyDirs;
    
    for (int i = 0; i < toDelete && i < static_cast<int>(files.size()); i++) {
        std::cout << "Deleting: " << files[i].path 
                  << " (created: " << ctime(&files[i].createTime) << ")" << std::endl;
        
        if (remove(files[i].path.c_str()) == 0) {
            std::cout << "Successfully deleted: " << files[i].name << std::endl;
            emptyDirs.insert(files[i].dateDir);
        } else {
            std::cerr << "Error: Failed to delete: " << files[i].path << std::endl;
        }
    }
    
    for (const auto& dateDir : emptyDirs) {
        std::string dateDirPath = baseDir + "/" + dateDir;
        
        DIR* dir = opendir(dateDirPath.c_str());
        if (!dir) {
            continue;
        }
        
        bool isEmpty = true;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                isEmpty = false;
                break;
            }
        }
        closedir(dir);
        
        if (isEmpty) {
            std::cout << "Removing empty directory: " << dateDir << std::endl;
            rmdir(dateDirPath.c_str());
        }
    }
    
    std::cout << "Cleanup completed. Current file count: " << (fileCount - toDelete) << std::endl;
}

std::string recvLine(int sock)
{
    std::string buf;
    char c;
    while (true) {
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            throw std::runtime_error("Connection closed while reading header");
        }
        if (c == '\n') {
            return buf;
        }
        buf += c;
    }
}

std::vector<uint8_t> recvExact(int sock, size_t n)
{
    std::vector<uint8_t> buf;
    buf.reserve(n);
    
    size_t received = 0;
    while (received < n) {
        uint8_t buffer[4096];
        size_t toRead = std::min(n - received, sizeof(buffer));
        ssize_t n = recv(sock, buffer, toRead, 0);
        if (n <= 0) {
            throw std::runtime_error("Connection closed: expected " + std::to_string(n) + " bytes");
        }
        buf.insert(buf.end(), buffer, buffer + n);
        received += n;
    }
    
    return buf;
}

bool doSnapshot(const std::string& sockPath, int quality, int timeoutMs,
                int x, int y, int w, int h,
                std::vector<uint8_t>& jpegData, int& width, int& height)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error: Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error: Failed to connect to " << sockPath << ": " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }
    
    std::ostringstream req;
    req << "{\"cmd\":\"snapshot\",\"quality\":" << quality
        << ",\"timeout_ms\":" << timeoutMs
        << ",\"x\":" << x << ",\"y\":" << y
        << ",\"w\":" << w << ",\"h\":" << h << "}";
    
    std::string reqStr = req.str();
    if (send(sock, reqStr.c_str(), reqStr.length(), 0) < 0) {
        std::cerr << "Error: Failed to send request: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }
    
    try {
        std::string hdrStr = recvLine(sock);
        
        size_t pos1 = hdrStr.find("\"ok\":");
        bool ok = (pos1 != std::string::npos && hdrStr.substr(pos1 + 5, 4) == "true");
        
        size_t pos2 = hdrStr.find("\"size\":");
        int size = 0;
        if (pos2 != std::string::npos) {
            size = std::stoi(hdrStr.substr(pos2 + 7));
        }
        
        size_t pos3 = hdrStr.find("\"width\":");
        width = 0;
        if (pos3 != std::string::npos) {
            width = std::stoi(hdrStr.substr(pos3 + 8));
        }
        
        size_t pos4 = hdrStr.find("\"height\":");
        height = 0;
        if (pos4 != std::string::npos) {
            height = std::stoi(hdrStr.substr(pos4 + 9));
        }
        
        if (ok && size > 0) {
            jpegData = recvExact(sock, size);
        }
        
        close(sock);
        return ok;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        close(sock);
        return false;
    }
}

cv::Mat captureImage()
{
    std::vector<uint8_t> jpegData;
    int width = 0, height = 0;
    
    bool success = doSnapshot(SOCK_PATH, 85, 1000, 0, 0, 0, 0, jpegData, width, height);
    
    if (!success || jpegData.empty()) {
        std::cerr << "Warning: Failed to capture image from JENC" << std::endl;
        return cv::Mat();
    }
    
    cv::Mat img = cv::imdecode(jpegData, cv::IMREAD_COLOR);
    
    if (img.empty()) {
        std::cerr << "Warning: Failed to decode JPEG image" << std::endl;
        return cv::Mat();
    }
    
    std::cout << "Captured image: " << width << "x" << height 
              << ", decoded: " << img.cols << "x" << img.rows 
              << ", size: " << jpegData.size() << " bytes" << std::endl;
    
    return img;
}

void imageCaptureThread()
{
    int subIndex = 1;
    const std::string outputDir = OUTPUT_BASE_DIR;
    
    std::string dateDir = outputDir + "/" + getCurrentDateTime();
    if (!ensureDirectoryExists(dateDir)) {
        return;
    }
    
    std::cout << "Image capture thread started. Saving images to: " << dateDir << std::endl;
    std::cout << "Using JENC snapshot via Unix socket: " << SOCK_PATH << std::endl;
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        cv::Mat frame = captureImage();
        
        if (frame.empty()) {
            std::cerr << "Warning: Captured empty frame, skipping..." << std::endl;
            continue;
        }
        
        if (isAllBlack(frame)) {
            std::cerr << "Warning: Captured all-black frame, skipping..." << std::endl;
            continue;
        }
        
        bool shouldSave = true;
        {
            std::lock_guard<std::mutex> lock(g_imageMutex);
            if (!g_lastImage.empty()) {
                if (areImagesSimilar(frame, g_lastImage)) {
                    std::cout << "Info: Image is similar to last one, skipping..." << std::endl;
                    shouldSave = false;
                }
            }
            
            if (shouldSave) {
                g_lastImage = frame.clone();
                frame.release();
            }
        }
        
        if (shouldSave) {
            std::string filename = generateFilename(subIndex);
            if (!filename.empty()) {
                cleanupOldFiles(outputDir, MAX_IMAGE_COUNT);
                
                std::string fullPath = dateDir + "/" + filename + ".jpg";
                if (cv::imwrite(fullPath, g_lastImage)) {
                    std::cout << "Saved: " << fullPath << std::endl;
                    subIndex++;
                } else {
                    std::cerr << "Error: Failed to save image: " << fullPath << std::endl;
                }
            }
        }
    }
}

void helloPrintThread()
{
    std::cout << "Hello print thread started." << std::endl;
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "hello" << std::endl;
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    std::cout << "Starting image getting application..." << std::endl;
    std::cout << "Maximum image count: " << MAX_IMAGE_COUNT << std::endl;
    std::cout << "Unix socket path: " << SOCK_PATH << std::endl;
    std::cout << "Output directory: " << OUTPUT_BASE_DIR << std::endl;
    
    std::thread captureThread(imageCaptureThread);
    std::thread helloThread(helloPrintThread);
    
    captureThread.join();
    helloThread.join();
    
    return 0;
}
