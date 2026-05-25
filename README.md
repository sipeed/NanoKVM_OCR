# 
 
# NanoKVM_OCR

fork 自上游项目[ax-npu-kit-620e](https://github.com/AXERA-TECH/ax-npu-kit-620e)

针对AX630C/AX620QX开发并优化的OCR应用

## 编译

+ 安装依赖
``` shell
# 下载 https://disk.corp.sipeed.com/f/1039905 到此文件夹，注意网盘服务由内网提供，需要登录
# 解压到 ax620q_bsp_sdk 目录下
tar -xzvf ax620q_bsp_sdk.tar.gz
# 安装依赖
sudo chmod +x ./download_third_party.sh
./download_third_party.sh
# 确保工具链存在, 并配置到环境变量中
# AX630C：gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu
# AX620QX：gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf
```

+ 编译
``` shell
cd ocr_pipline
chmod +x build.sh
# 对于AX630C
./build.sh AX630C
## 编译成功：
# ========================================
# Build completed successfully!
# ========================================

# Executables:
#   - build_ax630c/install/bin/ocr_pipline_app (OCR pipeline)
#   - build_ax630c/install/bin/test_rec_app (OCR recognition test)
#   - build_ax630c/install/bin/ocr_process_app (OCR process)
#   - build_ax630c/install/bin/image_getting (Image capture utility)
# 对于AX620Q
./build.sh AX620QE
# ## 编译成功：
# ========================================
# Build completed successfully!
# ========================================

# Executables:
#   - build_ax620qe/install/bin/ocr_pipline_app (OCR pipeline)
#   - build_ax620qe/install/bin/test_rec_app (OCR recognition test)
#   - build_ax620qe/install/bin/ocr_process_app (OCR process)
#   - build_ax620qe/install/bin/image_getting (Image capture utility)

# 编译产物
# ocr_pipline_app ： OCR 测试应用
# test_rec_app    ： OCR 识别测试应用（后半部份单独）
# ocr_process_app ： 实际用到的OCR处理应用
# image_getting   ： 图像捕获工具，需要搭配改过的kvm_vin实现
```

## 运行
``` shell
# 拷贝必要文件到目标平台
scp -r models/pp_ocr/ root@xxx.xxx.xxx.xxx:/root/models/
scp -r ocr_pipline/build_ax620qe/install/bin/ocr_process_app root@xxx.xxx.xxx.xxx:/ocr_pipline/
# 最好再来一张屏幕截图到目标平台，用于测试
# 运行ocr_process_app，测试OCR识别
./ocr_process_app   --image /root/screen.png                            # 输入单张图片，将自动输出结果至json
                    --folder /var/lib/openchronicle/screenshots         # (optional) 遍历里面的所有图片，将忽略单张图片，自动循环执行，将json自动上传至openchronicle应用
                    --mem_log                                           # (optional) 输出内存占用日志
                    --time_log                                          # (optional) 输出运行时间日志
                    --save ./output.png                                 # (optional) 输出OCR过程中的图片，如热力图等
                    --det_model /root/models/pp_ocr/ocr_model.bin       # (optional) 识别模型路径，不填默认为/root/models/pp_ocr/ocr_model.bin
                    --rec_model /root/models/pp_ocr/ocr_rec_model.bin   # (optional) 识别模型路径，不填默认为/root/models/pp_ocr_rec_model.bin
                    --dict /root/models/pp_ocr/dict.txt                 # (optional) 字典路径，不填默认为/root/models/pp_ocr/dict.txt
```

# OCR 流程简述 （实际代码经过优化和描述略有差别）

1. 读取图像，转换格式为BGR888
2. 带重叠的分割图像，将图片先缩小一倍再进行裁剪，尺寸统一为640*480
3. 加载det模型，输出Float格式的热力图
4. 阈值判断+热力图组合
5. 绘制方框：连通算法+最小面积阈值+边缘扩展
6. 文字框过长裁剪：重叠裁剪，使输入图像尽可能保留更多原始特征下维持NPU的输入尺寸：48*320
7. 加载rec模型，输出识别结果
8. 文字拼合+去重
9. 生成json文件
