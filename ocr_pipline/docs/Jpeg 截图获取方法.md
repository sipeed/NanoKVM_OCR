## **1. 基本流程**



截图通过 Unix Domain Socket 控制接口完成，默认 socket 路径为：



```plain&#x20;text
/run/kvm/vin_ctrl.sock
```



客户端需要：



1. 创建 `AF_UNIX` + `SOCK_STREAM` socket。

2. 连接 `/run/kvm/vin_ctrl.sock`。

3) 发送一段 JSON 请求，命令为 `snapshot`。

4) 服务端先返回一行 JSON header，以换行符 `\n` 结束。

5. 如果 header 中 `ok=true` 且 `size>0`，继续读取 `size` 字节 JPEG 原始数据。

6. 将 JPEG 数据保存为 `.jpg` 文件。

## **2. 请求格式**



请求是一个 JSON 对象，直接通过 socket 发送 UTF-8 字符串即可。



```json
{
  "cmd": "snapshot",
  "quality": 85,
  "timeout_ms": 1000,
  "x": 0,
  "y": 0,
  "w": 0,
  "h": 0
}
```



字段说明：



| 字段           | 类型     | 默认值   | 说明                                |
| ------------ | ------ | ----- | --------------------------------- |
| `cmd`        | string | 必填    | 固定为 `snapshot`                    |
| `quality`    | int    | `85`  | JPEG 质量，范围 `1~100`，超出范围会被限制到边界值   |
| `timeout_ms` | int    | `500` | 获取 JPEG 帧的超时时间，单位毫秒；小于等于 0 时使用默认值 |
| `x`          | int    | `0`   | 裁剪区域左上角 X 坐标                      |
| `y`          | int    | `0`   | 裁剪区域左上角 Y 坐标                      |
| `w`          | int    | `0`   | 裁剪区域宽度                            |
| `h`          | int    | `0`   | 裁剪区域高度                            |



### **全屏截图**



当 `x=0, y=0, w=0, h=0` 时，表示截取完整画面。



```json
{
  "cmd": "snapshot",
  "quality": 85,
  "timeout_ms": 1000,
  "x": 0,
  "y": 0,
  "w": 0,
  "h": 0
}
```



### **裁剪截图**



指定 `x/y/w/h` 可以截取 ROI 区域。例如截取 `(100,100)` 开始的 `800x600` 区域：



```json
{
  "cmd": "snapshot",
  "quality": 90,
  "timeout_ms": 1000,
  "x": 100,
  "y": 100,
  "w": 800,
  "h": 600
}
```



如果裁剪区域右边或下边超出源图范围，服务端会将宽高裁剪到源图边界内；如果起点已经在画面外，则返回错误。



## **3. 响应格式**



服务端响应分为两部分：



```plain&#x20;text
JSON header + \n
JPEG binary data
```



第一部分是以换行符结束的一行 JSON：



```json
{
  "ok": true,
  "ret_code": 0,
  "message": "ok",
  "width": 1920,
  "height": 1080,
  "size": 123456
}
```



字段说明：



| 字段         | 类型     | 说明                   |
| ---------- | ------ | -------------------- |
| `ok`       | bool   | 是否截图成功               |
| `ret_code` | int    | 返回码，`0` 表示成功         |
| `message`  | string | 状态信息或错误原因            |
| `width`    | int    | 返回 JPEG 对应的宽度        |
| `height`   | int    | 返回 JPEG 对应的高度        |
| `size`     | int    | 后续 JPEG 二进制数据长度，单位字节 |



当 `ok=true` 且 `size>0` 时，header 后面紧跟 `size` 字节 JPEG 数据。



失败时通常没有 JPEG 数据，`size` 为 `0`。



## **4. 返回码**



| ret\_code | message              | 说明              |
| --------- | -------------------- | --------------- |
| `0`       | `ok`                 | 成功              |
| `-10`     | `no desktop`         | 当前没有桌面画面        |
| `-11`     | `invalid params`     | 参数非法，例如宽高小于等于 0 |
| `-12`     | `ROI out of bounds`  | 裁剪区域起点超出源图范围    |
| `-13`     | `snapshot timeout`   | 截图超时            |
| `-14`     | `JPEG encode failed` | JPEG 编码失败       |
| `-16`     | `snapshot busy`      | 当前已有截图请求正在执行    |



## **5. Python 最小示例**



```python
import json
import socket

sock_path = "/run/kvm/vin_ctrl.sock"

req = json.dumps({
    "cmd": "snapshot",
    "quality": 85,
    "timeout_ms": 1000,
    "x": 0,
    "y": 0,
    "w": 0,
    "h": 0,
})

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
    s.settimeout(5.0)
    s.connect(sock_path)
    s.sendall(req.encode("utf-8"))

    header = b""
    while not header.endswith(b"\n"):
        chunk = s.recv(1)
        if not chunk:
            raise RuntimeError("connection closed while reading header")
        header += chunk

    hdr = json.loads(header.decode("utf-8"))

    data = b""
    if hdr.get("ok") and hdr.get("size", 0) > 0:
        size = hdr["size"]
        while len(data) < size:
            chunk = s.recv(size - len(data))
            if not chunk:
                raise RuntimeError("connection closed while reading JPEG data")
            data += chunk

if hdr.get("ok"):
    with open("snapshot.jpg", "wb") as f:
        f.write(data)
else:
    raise RuntimeError(f"snapshot failed: ret={hdr.get('ret_code')} message={hdr.get('message')}")
```



## **6. 注意事项**



* 同一时间只允许一个截图请求执行；并发请求可能返回 `snapshot busy`。

* `quality` 会被限制在 `1~100`。

* `timeout_ms <= 0` 时会使用服务端默认超时。

* 裁剪输出宽高会按 2 对齐。

* header 是文本 JSON，以 `\n` 结束；JPEG 数据是二进制，必须按 `size` 精确读取。

* 保存文件时应使用二进制写入模式，例如 Python 的 `open(path, "wb")`。
