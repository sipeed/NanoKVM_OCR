## 时间是否已经同步

chronyc tracking | grep -q "Leap status.\*Normal"

返回 0 成功

返回 1 失败



## 截图建议保存在：

```json
/var/lib/openchronicle/screenshots/2026-05-15/2026-05-15T10-30-00+08-00.jpg
```



## **1. 启动 OpenChronicle Go daemon**

```bash
OPENCHRONICLE_ROOT=/var/lib/openchronicle openchronicle start --foreground
```

默认会启动：

* ingest server: `http://127.0.0.1:8743`

* MCP server: `http://127.0.0.1:8742/mcp`

* daemon pipeline scheduler: timeline / session / reducer / classifier / compact



## **2. HTTP endpoint**

默认地址：`http://127.0.0.1:8743/v1/observations`

请求头：

```http
Content-Type: application/json
```

如果开启 token：

```http
Authorization: Bearer <token>
```



## **3. 最小 JSON 格式**

最小可用 observation：

```json
{
  "ocr": {
    "text": "OCR text here"
  }
}
```

OpenChronicle 会自动补齐：

* `schema_version = 1`

* `type = "ocr_observation"`

* `captured_at = 当前时间`

* `source.kind = "screen_ocr"`

* `event.type = "ocr_capture"`



## **4. 推荐 JSON 格式**

```json
{
  "schema_version": 1,
  "type": "ocr_observation",
  "captured_at": "2026-05-15T10:30:00+08:00",
  "source": {
    "kind": "screen_ocr",
    "device": "nanoagent",
    "engine": "your-ocr-engine"
  },
  "screen": {
    "image_path": "/path/to/screenshot.jpg",
    "width": 1920,
    "height": 1080,
    "image_sha256": "optional-sha256"
  },
  "context": {
    "app_name": "ScreenOCR",
    "title": "optional screen/window title",
    "url": "optional-url"
  },
  "event": {
    "type": "ocr_capture",
    "reason": "timer"
  },
  "ocr": {
    "language": "zh",
    "text": "这里是 OCR 识别出的完整文本",
    "confidence": 0.93,
    "block_count": 2,
    "blocks": [
      {
        "id": 1,
        "text": "第一段文字",
        "bbox": [10, 20, 300, 60],
        "confidence": 0.95
      },
      {
        "id": 2,
        "text": "第二段文字",
        "bbox": [10, 80, 500, 120],
        "confidence": 0.91
      }
    ]
  },
  "tags": ["external", "ocr"]
}
```



## **5. 字段说明**

| 字段                        | 类型     | 必填 | 说明                                                                  |
| ------------------------- | ------ | -- | ------------------------------------------------------------------- |
| `schema_version`          | number | 否  | 当前为 `1`，省略时自动补齐。                                                    |
| `type`                    | string | 否  | 固定为 `ocr_observation`，省略时自动补齐。                                      |
| `captured_at`             | string | 建议 | RFC3339 时间戳，例如 `2026-05-15T10:30:00+08:00`。省略时使用 ingest 时间。         |
| `source.kind`             | string | 否  | 默认 `screen_ocr`。                                                    |
| `source.device`           | string | 建议 | 设备名，例如 `nanoagent`、`nanokvm`、`linux-host`。                          |
| `source.engine`           | string | 建议 | OCR 引擎名，例如 `paddleocr`、`tesseract`、`easyocr`。                       |
| `screen.image_path`       | string | 建议 | 截图文件路径。OpenChronicle 只记录路径，不上传图片内容。                                 |
| `screen.width` / `height` | number | 否  | 截图尺寸。                                                               |
| `screen.image_sha256`     | string | 否  | 截图 SHA256，便于去重和溯源。                                                  |
| `context.app_name`        | string | 建议 | 由于外部程序通常不知道真实窗口，可填 `ScreenOCR` 或设备名。session soft cut 会使用 app\_name。 |
| `context.title`           | string | 建议 | 屏幕标题、页面标题、设备状态或 OCR 来源描述。                                           |
| `context.url`             | string | 否  | 如果能识别当前 URL 可填。                                                     |
| `event.type`              | string | 否  | 默认 `ocr_capture`。                                                   |
| `event.reason`            | string | 否  | 例如 `timer`、`manual`、`change_detected`。                              |
| `ocr.language`            | string | 否  | 例如 `zh`、`en`、`zh-en`。                                               |
| `ocr.text`                | string | 是  | 完整 OCR 文本。为空会被拒绝。                                                   |
| `ocr.confidence`          | number | 否  | 整体置信度。                                                              |
| `ocr.block_count`         | number | 否  | OCR block 数量。省略时可由 `blocks` 自动推导。                                   |
| `ocr.blocks`              | array  | 否  | 结构化 OCR blocks；如果 `ocr.text` 为空，会从 blocks 拼接。                       |
| `tags`                    | array  | 否  | 自定义标签。                                                              |



## **6. curl 示例**

单条 observation：

```bash
curl -sS \
  -H 'Content-Type: application/json' \
  -X POST \
  --data-binary @ocr.json \
  http://127.0.0.1:8743/v1/observations
```

成功响应：

```json
{
  "ok": true,
  "stored": [
    {
      "path": "/var/lib/openchronicle/observation-buffer/2026-05-15T10-30-00p08-00.json",
      "file_stem": "2026-05-15T10-30-00p08-00",
      "indexed": true
    }
  ]
}
```



## **7. 批量提交**

HTTP endpoint 支持一次提交数组，默认最大 batch size：`16`。

```json
[
  {
    "captured_at": "2026-05-15T10:30:00+08:00",
    "ocr": {
      "text": "first screen"
    }
  },
  {
    "captured_at": "2026-05-15T10:31:00+08:00",
    "ocr": {
      "text": "second screen"
    }
  }
]
```



## **8. Python 对接示例**

```python
from datetime import datetime
import requests

obs = {
    "schema_version": 1,
    "type": "ocr_observation",
    "captured_at": datetime.now().astimezone().isoformat(),
    "source": {
        "kind": "screen_ocr",
        "device": "nanoagent",
        "engine": "your-ocr-engine",
    },
    "screen": {
        "image_path": "/path/to/screenshot.jpg",
    },
    "context": {
        "app_name": "ScreenOCR",
        "title": "external timed capture",
    },
    "event": {
        "type": "ocr_capture",
        "reason": "timer",
    },
    "ocr": {
        "language": "zh",
        "text": "OCR text here",
    },
    "tags": ["external", "ocr"],
}

resp = requests.post(
    "http://127.0.0.1:8743/v1/observations",
    json=obs,
    timeout=5,
)
resp.raise_for_status()
print(resp.json())
```



## **9. Token 鉴权配置**

配置文件路径：

```plain&#x20;text
$OPENCHRONICLE_ROOT/config.toml
```

默认配置：

```toml
[ingest]
enabled = true
host = "127.0.0.1"
port = 8743
require_token = false
max_batch_size = 16
max_body_bytes = 1048576
```

如果外部程序不在同一台机器，可改为：

```toml
[ingest]
enabled = true
host = "0.0.0.0"
port = 8743
require_token = true
token = "your-token"
max_batch_size = 16
max_body_bytes = 1048576
```

POST 时加 header：

```bash
curl -sS \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer your-token' \
  -X POST \
  --data-binary @ocr.json \
  http://127.0.0.1:8743/v1/observations
```



## **10. 对接后的验证**

### **查看状态**

```bash
OPENCHRONICLE_ROOT=/var/lib/openchronicle openchronicle status
```

重点看：

```plain&#x20;text
Observations          N indexed
Timeline              N blocks
Sessions              N rows
Memory                N files, N entries
```



### **手动推进 pipeline**

如果 daemon 正在运行，通常不需要手动执行。调试时可以运行：

```bash
OPENCHRONICLE_ROOT=/var/lib/openchronicle openchronicle timeline tick
OPENCHRONICLE_ROOT=/var/lib/openchronicle openchronicle session tick
OPENCHRONICLE_ROOT=/var/lib/openchronicle openchronicle writer run
```



### **检查 MCP**

默认 MCP endpoint：

```plain&#x20;text
http://127.0.0.1:8742/mcp
```

常用 MCP tools：

* `current_context`

* `search_captures`

* `read_recent_capture`

* `list_memories`

* `search`

* `recent_activity`



## **11. 常见错误**

### **`ocr.text is required`**

`ocr.text` 为空，并且 `ocr.blocks` 也没有可拼接的文本。

解决：确保提交：

```json
{
  "ocr": {
    "text": "non-empty OCR text"
  }
}
```



### **`invalid captured_at`**

`captured_at` 不是 RFC3339/RFC3339Nano 格式。

正确示例：

```plain&#x20;text
2026-05-15T10:30:00+08:00
2026-05-15T02:30:00Z
```



**`batch size must be 1..16`**

批量提交数组为空或超过 `ingest.max_batch_size`。

解决：拆分 batch，或调大配置：

```toml
[ingest]
max_batch_size = 64
```



### **`unauthorized`**

开启了 `require_token=true`，但请求缺少或使用了错误 token。

解决：添加：

```http
Authorization: Bearer your-token
```

