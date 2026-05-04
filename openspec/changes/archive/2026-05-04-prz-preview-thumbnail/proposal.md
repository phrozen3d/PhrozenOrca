## 為何

匯出 PRZ 時，預覽圖區塊一律為黑色空白。根本原因是 `generate_prz()` 只從 `preview_image_path` 讀取預先存放的 PNG 檔，但此路徑從未被填入，所以沒有任何圖可讀。使用者在 Phrozen 印表機上載入切片後，機台顯示的縮圖永遠是空白，無法辨識當前列印模型。

## 變更內容

- 在 `Plater::export_prz()` 中，用與 3MF 匯出相同的 OpenGL 渲染方式產生 290×290 的 `ThumbnailData`
- 將 `ThumbnailData*` 作為可選參數新增至 `generate_prz()` 與 `prz_header()`（預設 `nullptr`，維持向後相容）
- 在 `prz_header()` 內，若傳入有效縮圖，則將 RGBA 像素轉換為 cv::Mat BGR，垂直翻轉（OpenGL 座標系修正），並 resize 至目標尺寸（290×290 或 116×116），再套用現有的 RGB565 轉換迴圈；否則 fallback 至既有的 `preview_image_path` 路徑

## 功能範疇

### 新增功能
- `prz-preview-thumbnail`：匯出 PRZ 時自動從當前 3D 場景渲染預覽圖，嵌入 PRZ header 中的 116×116 與 290×290 預覽欄位

## 影響範圍

**修改的檔案：**
- `src/libslic3r/Format/PhrozenPRZ.hpp` — `generate_prz()` 新增 `const ThumbnailData *thumb = nullptr` 參數
- `src/libslic3r/Format/PhrozenPRZ.cpp` — `prz_header()` 與 `generate_prz()` 新增縮圖轉換邏輯
- `src/slic3r/GUI/Plater.cpp` — `export_prz()` 中新增 OpenGL 渲染呼叫

**不需修改：**
- 任何現有的 RGB565 轉換邏輯
- `preview_image_path` fallback 路徑（保留以維持向後相容）
- FDM 或其他 SLA 格式的匯出流程

## 實作備註

僅渲染一次 290×290 的縮圖，116×116 的預覽在 `prz_header()` 內使用 `cv::resize()` 縮小，不需兩次 OpenGL 渲染。

Plater 端的渲染呼叫：
```cpp
ThumbnailData thumb;
const ThumbnailsParams params = { {}, false, true, true, true,
    p->partplate_list.get_curr_plate_index() };
p->generate_thumbnail(thumb, 290, 290, params, Camera::EType::Ortho);
std::string prz_data = Slic3r::generate_prz(sla_print(),
    thumb.is_valid() ? &thumb : nullptr);
```