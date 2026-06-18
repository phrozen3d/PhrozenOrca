## 功能

匯出 PRZ 時，自動從當前 3D 場景渲染預覽縮圖（**含 SLA 支撐與 pad**），嵌入 PRZ header 的 116×116 與 290×290 預覽欄位（RGB565 big-endian 格式）。

## 行為規格

### 縮圖產生

- 匯出 PRZ 前，以 Orthographic camera 渲染當前板件（`plate_id = 當前板件 index`）
- 渲染參數：`printable_only=false`、`parts_only=false`、`show_bed=true`、`transparent_background=true`
  - `parts_only=false` 使 SLA 支撐／pad 等 aux volume（`composite_id.volume_id < 0`，如 `slaposSupportTree`、`slaposPad`）通過縮圖 volume 過濾而納入預覽
  - 此放寬僅作用於 `Plater::export_prz()` 本地的 `ThumbnailsParams`，**不修改**全域縮圖過濾邏輯（`GLCanvas3D.cpp`），故 3MF / FDM / 板件縮圖不受影響
- 渲染尺寸：290×290 pixels，RGBA 格式
- 若渲染結果有效（`ThumbnailData::is_valid()` 為 true），則用於產生兩個預覽欄位
- 若渲染失敗或無效，則預覽欄位填零（維持現有行為）

### 支撐結構同步載入

- 渲染縮圖前，`Plater::export_prz()` 須以**同步**方式確保 SLA 支撐／pad 網格已存在於 Prepare 視圖場景（`m_volumes`），即使匯出當下 SLA 支撐 Gizmo 處於啟用狀態
- 透過 `reload_scene(refresh_immediately, force_full_scene_refresh, force_load_sla_support=true)` 同步強制載入；`reload_scene` 為同步函式，回傳時 `m_volumes` 已含支撐，故與緊接的 `generate_thumbnail()` 之間無 race condition
- **不得**依賴「切換 UI 工具後等待非同步 reload」的方式載入支撐（該方式僅觸發 `set_as_dirty()` 與非同步 paint event）
- 渲染完成後，須以一般 `reload_scene()` 還原 Prepare 視圖場景，避免支撐 Gizmo 啟用時殘留支撐網格與點預覽的雙重渲染；gizmo 類型 / 選取 / 相機不被改動

### 預覽圖轉換

- RGBA 原始像素垂直翻轉（OpenGL 座標系修正）
- 290×290 預覽：直接從渲染結果轉 RGB565，無需 resize
- 116×116 預覽：從相同 290×290 渲染結果 resize 後轉 RGB565
- RGB565 格式：每像素 2 bytes，big-endian，R5G6B5

### Fallback 優先序

1. 若渲染縮圖有效 → 使用渲染縮圖
2. 若 `preview_image_path` 非空且含對應 PNG → 使用檔案（既有行為，保留）
3. 否則 → 填零

> 注意：目前實作中，傳入 `ThumbnailData*` 非 null 時，優先使用縮圖，不再讀取 `preview_image_path`。fallback 2 僅在未傳入縮圖時生效。

## 介面變更

### `PhrozenPRZ.hpp`

```cpp
// 新增可選參數
std::string generate_prz(const SLAPrint &print,
                          const ThumbnailData *thumb = nullptr);
```

### `PhrozenPRZ.cpp`（內部）

```cpp
// prz_header() 新增參數
static std::string prz_header(const SLAPrint &print,
                               const DynamicPrintConfig &cfg,
                               const ThumbnailData *thumb);
```

## 不在範疇

- 使用者自訂縮圖來源的 UI 選項
- CLI / headless 模式的縮圖支援
- 修改 `preview_image_path` config 的語意或生命週期