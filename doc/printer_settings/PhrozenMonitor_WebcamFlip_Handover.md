# Webcam 翻轉／旋轉功能交接文件

> 分支：`feature/phrozen-webcam-orientation`
> 最後更新：2026-03-26

---

## 1. 功能目標

讓使用者在監控頁面（Monitor Tab）不需要進入 Fluidd/Moonraker Web UI，直接在 PhrozenOrca 內設定攝影機畫面的**水平翻轉、垂直翻轉、旋轉角度**。

設定的來源與持久化方式：
- **讀取**：連線後向機台 Moonraker API 取得現有設定
- **套用**：本機端即時對每一幀 JPEG 做 wxImage 幾何變換（不改 JPEG 本身）
- **寫回**：使用者在 UI 調整後，POST 回 Moonraker 的 webcam database，使設定永久保存於機台

---

## 2. 涉及檔案總覽

| 檔案 | 角色 |
|---|---|
| `src/slic3r/Utils/Phrozen/PhrozenMachineDatas.hpp` | 定義 `PhrozenWebcamDisplayConfig` 資料結構 |
| `src/slic3r/Utils/Phrozen/PhrozenNetworkAgent.hpp` | 宣告 GET / POST webcam 設定的函式 |
| `src/slic3r/Utils/Phrozen/PhrozenNetworkAgent.cpp` | 實作 HTTP 呼叫邏輯 |
| `src/slic3r/GUI/PhrozenGUI/PhrozenDeviceManager.hpp` | `PhrozenMachineObject_Dev` 加入 double-buffer 存放設定 |
| `src/slic3r/GUI/PhrozenGUI/PhrozenDeviceManager.cpp` | 實作 `TryGetWebcamDisplayConfig` / `SetWebcamDisplayConfig` |
| `src/slic3r/GUI/PhrozenGUI/PhrozenWebcamSettingsPopup.hpp` | 新增：設定浮動視窗的宣告 |
| `src/slic3r/GUI/PhrozenGUI/PhrozenWebcamSettingsPopup.cpp` | 新增：設定浮動視窗的實作 |
| `src/slic3r/GUI/PhrozenGUI/PhrozenStatusPanel.hpp` | 加入 button / popup / fetch 的成員宣告 |
| `src/slic3r/GUI/PhrozenGUI/PhrozenStatusPanel.cpp` | 整合所有 UI 觸發、背景 GET、即時渲染變換邏輯 |
| `src/slic3r/CMakeLists.txt` | 已將兩個新檔案加入編譯清單 |

---

## 3. 資料結構

### `PhrozenWebcamDisplayConfig`（PhrozenMachineDatas.hpp）

```cpp
struct PhrozenWebcamDisplayConfig {
    std::string uid;           // 實際存的是 Moonraker 的 "name"（e.g. "my_cam0"）
    bool flip_horizontal = false;
    bool flip_vertical   = false;
    int  rotation_deg    = 0;  // 合法值：0 / 90 / 180 / 270
};
```

> **注意命名不一致**：欄位名稱是 `uid`，但存放的是 Moonraker JSON 裡的 `"name"` 值。
> 技術債：未來可改名為 `name` 並同步所有呼叫點。

### Double-Buffer 存放於 `PhrozenMachineObject_Dev`

```cpp
DoubleBufferSP<PhrozenWebcamDisplayConfig> m_webcam_display_config;
```

- **寫入**：`SetWebcamDisplayConfig(cfg)` — 由 UI thread 或 background thread 的 `CallAfter` 呼叫
- **讀取**：`TryGetWebcamDisplayConfig(out)` — 由 UI timer（每幀渲染）呼叫，無鎖讀取

---

## 4. 完整資料流

### 4-A：連線後自動讀取機台設定

```
使用者點選「連線」
    │
    ▼
start_webcam_update_timer()   [PhrozenStatusPanel.cpp:3196]
    │  (計時器首次啟動)
    │  m_webcam_config_fetched == false → 設為 true
    │
    ▼
fetch_webcam_config_from_moonraker()   [PhrozenStatusPanel.cpp:3234]
    │
    ├─ 開背景 std::thread
    │       │
    │       ▼
    │  GET http://{ip}:8808/server/webcams/list
    │  PhrozenNetworkAgent::get_webcam_display_config()   [PhrozenNetworkAgent.cpp:1750]
    │       │
    │       ├─ 從 JSON 找 service=="mjpegstreamer" 的鏡頭（my_cam0）
    │       ├─ out.uid  = cam["name"]        → "my_cam0"
    │       ├─ out.flip_horizontal = cam["flip_horizontal"]
    │       ├─ out.flip_vertical   = cam["flip_vertical"]
    │       └─ out.rotation_deg   = cam["rotation"]
    │
    └─ wxTheApp->CallAfter (切回 UI thread)
            │
            ├─ pObj->SetWebcamDisplayConfig(cfg)   → 寫入 double-buffer
            └─ m_webcam_settings_popup->SyncFromConfig(cfg)   → 若 popup 已開啟則同步 UI
```

### 4-B：每幀渲染時套用變換

```
webcam 計時器觸發 UpdateWebCameraView()   [PhrozenStatusPanel.cpp:3230]
    │
    ├─ 取得最新 JPEG snapshot（double-buffer）
    ├─ 解碼為 wxImage
    │
    ▼
TryGetWebcamDisplayConfig(disp)   → 從 double-buffer 讀取當前設定
    │
    ▼
apply_phrozen_webcam_display_transforms(image, disp)   [PhrozenStatusPanel.cpp:46]
    │
    ├─ switch rotation_deg:
    │     90  → image.Rotate90(true)   // 順時針
    │     180 → image.Rotate180()
    │     270 → image.Rotate90(false)  // 逆時針
    │
    ├─ if flip_horizontal → image.Mirror(true)
    └─ if flip_vertical   → image.Mirror(false)
```

### 4-C：使用者在 UI 調整設定

```
使用者點選「Camera Settings」按鈕
    │
    ▼
on_webcam_settings_button()   [PhrozenStatusPanel.cpp:3260]
    │
    ├─ 建立（若尚未建立）PhrozenWebcamSettingsPopup
    ├─ TryGetWebcamDisplayConfig(current) → SyncFromConfig(current)  // 同步 UI 狀態
    ├─ 設定 OnConfigChanged callback（見下方）
    └─ m_webcam_settings_popup->Popup()   // 顯示非獨佔浮動視窗

使用者切換翻轉或旋轉按鈕
    │
    ▼
OnConfigChanged lambda   [PhrozenStatusPanel.cpp:3272]
    │
    ├─ cfg = cfg_from_ui                         // 含 flip/rotation，uid 為空
    ├─ TryGetWebcamDisplayConfig(stored)
    │     cfg.uid = stored.uid                   // 補入 "my_cam0"
    │
    ├─ pObj->SetWebcamDisplayConfig(cfg)         // 立即更新 double-buffer → 下一幀生效
    │
    └─ (background thread)
            ▼
       POST http://{ip}:8808/server/webcams/item
       PhrozenNetworkAgent::set_webcam_display_config()   [PhrozenNetworkAgent.cpp:1807]
            │
            └─ body: { "name": "my_cam0",
                       "flip_horizontal": ...,
                       "flip_vertical": ...,
                       "rotation": ... }
```

### 4-D：斷線重連後的重置

```
set_default()   [PhrozenStatusPanel.cpp:5515]
    │
    └─ m_webcam_config_fetched = false   // 確保下次連線時重新 GET
```

---

## 5. Moonraker API 規格（已確認）

### GET 鏡頭清單

```
GET http://{ip}:8808/server/webcams/list
```

實際回傳範例（此機台）：
```json
{
  "result": {
    "webcams": [
      {
        "name": "my_cam1",
        "service": "mjpegstreamer-adaptive",
        "snapshot_url": "/webcam?action=snapshot",
        "flip_horizontal": false,
        "flip_vertical": false,
        "rotation": 0,
        "source": "database"
      },
      {
        "name": "my_cam0",
        "service": "mjpegstreamer",
        "snapshot_url": "/webcam/?action=snapshot",
        "flip_horizontal": false,
        "flip_vertical": false,
        "rotation": 0,
        "source": "database"
      }
    ]
  }
}
```

> **重點**：
> - JSON 中**無 `uid` 欄位**，識別用 `name`
> - `my_cam0`（`service: "mjpegstreamer"`）為 PhrozenOrca 使用的攝影機（snapshot URL 含 `/?`）
> - `my_cam1`（adaptive）為備援流，PhrozenOrca 不使用

### POST 更新設定

```
POST http://{ip}:8808/server/webcams/item
Content-Type: application/json

{
  "name": "my_cam0",
  "flip_horizontal": true,
  "flip_vertical": false,
  "rotation": 90
}
```

---

## 6. 目前已知問題與分析

### 問題：重新連線後翻轉設定消失

**現象**：使用者在 UI 設定翻轉 → 斷線 → 重連 → 翻轉回到預設（無翻轉）

**可能原因（依優先序排查）**：

#### 原因 A：POST 未成功寫入 Moonraker（最可能）
- 之前 POST 的 port 是 `7125`（已於 2026-03-26 修正為 `8808`）
- 修正後尚未重新測試確認

**驗證方式**：
1. 重新 build 後測試：在 UI 設定翻轉
2. 用 curl 手動確認是否已寫入：
   ```bash
   curl http://{ip}:8808/server/webcams/list
   ```
   若 `flip_horizontal` 仍為 false → POST 沒有真正寫入

#### 原因 B：Moonraker 的 webcam 設定屬於「前端 metadata」，不影響串流本身
- Moonraker 的 `flip_horizontal/vertical/rotation` 欄位設計意圖：**讓前端（Fluidd/Mainsail）在瀏覽器內自行做變換**，Moonraker 只負責儲存這個 metadata
- mjpg-streamer 的原始串流永遠是未翻轉的
- Fluidd 的翻轉效果是 CSS/Canvas transform，不是修改 JPEG

**影響**：若機台的 Moonraker 是此架構，則：
1. POST 可以成功寫入 metadata
2. GET 回傳正確的值
3. PhrozenOrca 讀到後套用 `apply_phrozen_webcam_display_transforms` 正確渲染

**這代表整體架構是正確的**，問題只在於 POST port 錯誤導致沒有真正寫入。

#### 原因 C：GET 成功但回傳值固定為全 0
- 若修正 port 後 POST 成功，但 GET 依然回傳全 0
- 可能是此機台的 Moonraker 版本不支援 webcam database API
- 需確認 Moonraker 版本及 `moonraker.conf` 中的 `[webcam]` 設定

---

## 7. 待辦事項（接手後需完成）

### P1 高優先：確認修正後 POST 是否有效

```bash
# 在 PhrozenOrca 設定翻轉後，用以下命令確認 Moonraker 是否記錄
curl http://{ip}:8808/server/webcams/list | python -m json.tool
```

若仍為全 0，進行以下排查：
1. 查看 PhrozenOrca 的 boost log，確認 POST 有觸發且無 curl error
2. 手動 curl POST 測試：
   ```bash
   curl -X POST http://{ip}:8808/server/webcams/item \
        -H "Content-Type: application/json" \
        -d '{"name":"my_cam0","flip_horizontal":true,"flip_vertical":false,"rotation":0}'
   ```
3. 若手動 POST 後 GET 仍為全 0，則可能是此版本 Moonraker 不支援此 API

### P2 中優先：若 Moonraker API 不可靠，改用本機設定檔持久化

若確認 Moonraker webcam API 不支援或不可靠，可改為：
- 在 `%AppData%/PhrozenOrca/` 儲存每台機台 IP 對應的 webcam 設定 JSON
- 連線時讀取本機設定（而非從 Moonraker GET）
- 修改點：`fetch_webcam_config_from_moonraker()` 加入 fallback 讀取本機設定

### P3 低優先：技術債清理

- 將 `PhrozenWebcamDisplayConfig::uid` 欄位改名為 `name`，避免與 Moonraker `uid` 欄位混淆
- 所有使用 `cfg.uid` / `stored.uid` 的地方同步修改

---

## 8. 重要常數與環境資訊

| 項目 | 值 | 說明 |
|---|---|---|
| Moonraker API port | `8808` | 此機台特殊，一般機台為 `7125` |
| mjpg-streamer port | `8808` | 同 port，同一台機器上不同服務 |
| 使用的攝影機 name | `my_cam0` | `service: "mjpegstreamer"`，非 adaptive |
| snapshot URL | `/webcam/?action=snapshot` | 注意 `/?` 有尾斜線 |
| 備援攝影機 | `my_cam1` | `service: "mjpegstreamer-adaptive"`，PhrozenOrca 不使用 |

---

## 9. 快速定位程式碼

| 功能 | 位置 |
|---|---|
| HTTP GET 實作 | `PhrozenNetworkAgent.cpp:1750` |
| HTTP POST 實作 | `PhrozenNetworkAgent.cpp:1807` |
| 每幀套用翻轉 | `PhrozenStatusPanel.cpp:46`（`apply_phrozen_webcam_display_transforms`） |
| 連線後觸發 GET | `PhrozenStatusPanel.cpp:3200` |
| GET 背景執行 | `PhrozenStatusPanel.cpp:3234`（`fetch_webcam_config_from_moonraker`） |
| Settings 按鈕邏輯 | `PhrozenStatusPanel.cpp:3260`（`on_webcam_settings_button`） |
| POST 觸發點 | `PhrozenStatusPanel.cpp:3272`（`OnConfigChanged` lambda） |
| Double-buffer 寫入 | `PhrozenDeviceManager.cpp:1277`（`SetWebcamDisplayConfig`） |
| Double-buffer 讀取 | `PhrozenDeviceManager.cpp:1270`（`TryGetWebcamDisplayConfig`） |
| 斷線重置 | `PhrozenStatusPanel.cpp:5515`（`set_default`） |
| 設定視窗 UI | `PhrozenWebcamSettingsPopup.cpp` |
