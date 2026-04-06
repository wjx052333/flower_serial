# WebSocket API 接口文档

## 概述

`serial_ws` 提供基于 WebSocket 的 JSON 接口，用于控制继电器和读取土壤传感器数据。

- **协议**：WebSocket（ws://）
- **默认端口**：`8765`
- **消息格式**：UTF-8 文本，JSON 对象
- **通信模式**：请求 / 响应，每条请求对应一条响应

---

## 连接

```
ws://<host>:8765
```

服务启动后即可接入，无需握手或鉴权。支持多客户端同时连接。

---

## 通用格式

### 请求

所有请求均为 JSON 对象，包含 `cmd` 字段：

```json
{ "cmd": "<命令名>" }
```

### 响应（成功）

```json
{ "ok": true, ... }
```

### 响应（失败）

```json
{ "ok": false, "error": "<错误描述>" }
```

| `error` 值                              | 含义                                      |
|----------------------------------------|------------------------------------------|
| `device unavailable`                   | 对应设备当前未插入或未被识别               |
| `relay open failed`                    | 继电器已连接但执行命令失败                 |
| `relay close failed`                   | 继电器已连接但执行命令失败                 |
| `relay query failed`                   | 继电器已连接但查询失败                     |
| `relay already open, has been closed`  | 定时开启时继电器已处于打开状态，已强制关闭  |
| `duration out of range (1-600 seconds)` | `duration` 参数不在有效范围内           |
| `soil read failed`                     | 传感器已连接但读取失败                     |
| `missing 'cmd' field`                  | 请求 JSON 中缺少 `cmd` 字段               |
| `unknown command`                      | `cmd` 值不在已知命令列表中                 |

---

## 命令列表

### relay_open — 打开继电器

**请求**

```json
{ "cmd": "relay_open" }
```

**响应（成功）**

```json
{ "ok": true }
```

**响应（失败）**

```json
{ "ok": false, "error": "device unavailable" }
```

---

### relay_close — 关闭继电器

**请求**

```json
{ "cmd": "relay_close" }
```

**响应（成功）**

```json
{ "ok": true }
```

**响应（失败）**

```json
{ "ok": false, "error": "device unavailable" }
```

---

### relay_query — 查询继电器状态

**请求**

```json
{ "cmd": "relay_query" }
```

**响应（成功）**

```json
{ "ok": true, "state": "open" }
```

| `state` 值  | 含义         |
|------------|-------------|
| `open`     | 继电器当前断开（线圈吸合） |
| `closed`   | 继电器当前闭合（线圈释放） |
| `unknown`  | 设备返回了响应但状态无法解析 |

**响应（失败）**

```json
{ "ok": false, "error": "device unavailable" }
```

---

### relay_open_timed — 定时打开继电器

打开继电器，经过指定秒数后自动关闭。

**若调用时继电器已处于打开状态**，服务器会先强制关闭继电器，然后返回错误，**不会**启动新的定时器。

调用 `relay_close` 或再次调用 `relay_open` 会取消正在运行的定时器。

**请求**

```json
{ "cmd": "relay_open_timed", "duration": 180 }
```

| 字段       | 类型 | 必填 | 说明                      |
|-----------|-----|------|--------------------------|
| `duration` | int | 是   | 持续时间，单位秒，范围 1–600 |

**响应（成功）**

```json
{ "ok": true, "duration": 180 }
```

响应立即返回，不等待定时器到期。`duration` 回显实际使用的秒数。

**响应（继电器已开启）**

```json
{ "ok": false, "error": "relay already open, has been closed" }
```

继电器在返回此错误前已被关闭。

**响应（其他失败）**

```json
{ "ok": false, "error": "device unavailable" }
{ "ok": false, "error": "duration out of range (1-86400 seconds)" }
```

---

### soil_query — 读取土壤传感器

**请求**

```json
{ "cmd": "soil_query" }
```

**响应（成功）**

```json
{
  "ok":           true,
  "moisture":     32.5,
  "temperature":  23.1,
  "conductivity": 418.0,
  "ph":           6.8,
  "nitrogen":     142,
  "phosphorus":   56,
  "potassium":    203,
  "salinity":     89
}
```

| 字段            | 类型    | 单位     | 范围           |
|----------------|--------|---------|----------------|
| `moisture`     | float  | %       | 0.0 – 100.0    |
| `temperature`  | float  | °C      | -20.0 – 80.0   |
| `conductivity` | float  | µS/cm   | 0 – 20000      |
| `ph`           | float  | —       | 3.0 – 10.0     |
| `nitrogen`     | int    | mg/kg   | 0 – 2000       |
| `phosphorus`   | int    | mg/kg   | 0 – 2000       |
| `potassium`    | int    | mg/kg   | 0 – 2000       |
| `salinity`     | int    | mg/kg   | 0 – 2000       |

float 字段保留 1 位小数，`conductivity` 保留 0 位小数。

**响应（失败）**

```json
{ "ok": false, "error": "device unavailable" }
```

---

## 热插拔行为

两个设备独立管理，互不影响：

- 设备拔出后，对应命令立即返回 `"error": "device unavailable"`，另一个设备的命令不受影响。
- 服务每 3 秒自动扫描一次，设备重新插入后无需重启服务，自动恢复可用。

---

## 示例

以下示例使用 Python `websockets` 库：

```python
import asyncio
import json
import websockets

async def main():
    async with websockets.connect("ws://192.168.1.100:8765") as ws:

        # 打开继电器
        await ws.send(json.dumps({"cmd": "relay_open"}))
        print(await ws.recv())
        # {"ok":true}

        # 查询继电器状态
        await ws.send(json.dumps({"cmd": "relay_query"}))
        print(await ws.recv())
        # {"ok":true,"state":"open"}

        # 定时打开继电器 3 分钟（180 秒后自动关闭）
        await ws.send(json.dumps({"cmd": "relay_open_timed", "duration": 180}))
        resp = json.loads(await ws.recv())
        if resp["ok"]:
            print(f"继电器已打开，将在 {resp['duration']} 秒后自动关闭")
        else:
            print(f"错误: {resp['error']}")
            # 若 error == "relay already open, has been closed"
            # 说明继电器之前已打开，服务器已将其关闭

        # 读取土壤传感器
        await ws.send(json.dumps({"cmd": "soil_query"}))
        resp = json.loads(await ws.recv())
        if resp["ok"]:
            print(f"湿度: {resp['moisture']}%  温度: {resp['temperature']}°C")
        else:
            print(f"错误: {resp['error']}")

asyncio.run(main())
```

以下示例使用 JavaScript（浏览器或 Node.js）：

```javascript
const ws = new WebSocket("ws://192.168.1.100:8765");

ws.onopen = () => {
    ws.send(JSON.stringify({ cmd: "soil_query" }));
};

ws.onmessage = (e) => {
    const resp = JSON.parse(e.data);
    if (resp.ok) {
        console.log("pH:", resp.ph, "湿度:", resp.moisture);
    } else {
        console.error("错误:", resp.error);
    }
};
```

---

## 注意事项

- 请求和响应严格一一对应。客户端应等收到响应后再发送下一条请求，避免乱序。
- 服务不主动推送消息，所有数据均由客户端查询触发。
- 继电器和传感器均通过 USB 串口连接，操作存在数十毫秒至数百毫秒的通信延迟，请勿设置过短的超时。
