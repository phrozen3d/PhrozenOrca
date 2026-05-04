## 背景

PRZ 格式的 header 包含兩個預覽圖欄位：116×116 與 290×290，格式為 RGB565 big-endian（每像素 2 bytes）。現有程式碼（`prz_header()`）從 `preview_image_path` 目錄讀取對應的 PNG 檔，以 OpenCV 解碼後轉 RGB565：

```cpp
// PhrozenPRZ.cpp ~250
std::string preview_path = cfg_s(cfg, "preview_image_path");
cv::Mat image;
if (!preview_path.empty())
    image = cv::imread(preview_path + "/PreviewImage_116_116.png");
if (!image.empty() && image.rows >= H && image.cols >= W) {
    // ... 逐像素轉 RGB565
} else {
    fh.append(sz, '\0');  // 空白 fallback
}
```

`preview_image_path` 從未被填入，因此永遠走 `fh.append(sz, '\0')` 路徑，產生空白預覽。

## 目標 / 非目標

**目標：**
- 匯出 PRZ 時自動產生來自當前場景的預覽圖
- 改動最小化，不動現有 RGB565 轉換邏輯
- 保留 `preview_image_path` fallback（向後相容）

**非目標：**
- 修改 `preview_image_path` 的語意
- 提供使用者自訂縮圖的 UI
- 修改 PRZ 格式本身

## 決策

### D1：縮圖資料介面

使用 `const ThumbnailData*`（nullable）作為貫穿 `generate_prz()` → `prz_header()` 的參數。

```
Plater::export_prz()
  → 渲染 ThumbnailData（RGBA, 290×290）
  → generate_prz(print, &thumb)
    → prz_header(... , thumb)
      → RGBA → cv::Mat BGR → cv::flip → cv::resize → RGB565 迴圈
```

**優點**：Plater.cpp 只需 `ThumbnailData.hpp`，不需引入 OpenCV header；轉換邏輯集中在已有 cv:: 的 `PhrozenPRZ.cpp`。

**考慮過的替代方案**：傳入 `cv::Mat*`。已拒絕——會污染 Plater.cpp（GUI 層）與 OpenCV 的耦合。

### D2：渲染次數

僅渲染一次 290×290。116×116 在 `prz_header()` 內 `cv::resize()` 降採樣。

**考慮過的替代方案**：分別渲染兩種尺寸。已拒絕——OpenGL 渲染有額外開銷，且 resize 品質在此尺寸差距下無明顯差異。

### D3：OpenGL 座標系修正

OpenGL framebuffer 原點在左下，cv::Mat 原點在左上，需垂直翻轉：

```cpp
cv::flip(bgr_mat, bgr_mat, 0);  // flipCode=0 → 垂直翻轉
```

此翻轉在 RGBA → BGR 轉換後、resize 之前執行。

### D4：RGBA → BGR 轉換

`ThumbnailData::pixels` 為 RGBA（4 bytes/pixel）。OpenCV 讀入的 PNG 為 BGR（3 channels）。轉換：

```cpp
cv::Mat rgba_mat(H, W, CV_8UC4, const_cast<uchar*>(thumb->pixels.data()));
cv::Mat bgr_mat;
cv::cvtColor(rgba_mat, bgr_mat, cv::COLOR_RGBA2BGR);
cv::flip(bgr_mat, bgr_mat, 0);
// 之後可直接餵入現有的 cv::split() + RGB565 迴圈
```

### D5：Plater 渲染呼叫模式

與 3MF 匯出保持一致（`Plater.cpp:13027`）：

```cpp
const ThumbnailsParams thumbnail_params = { {}, false, true, true, true, i };
p->generate_thumbnail(p->partplate_list.get_plate(i)->thumbnail_data,
    THUMBNAIL_SIZE_3MF.first, THUMBNAIL_SIZE_3MF.second,
    thumbnail_params, Camera::EType::Ortho);
```

PRZ 版本：`plate_id` 取當前板件 index，尺寸固定 290×290。

## 風險 / 取捨

- **OpenGL context 不可用**：`export_prz()` 若在無 GL context 的情況下被呼叫（例如未來的 CLI 模式），`generate_thumbnail()` 會失敗或回傳無效資料。→ `thumb.is_valid()` 檢查確保此時 fallback 為空白預覽，不崩潰。
- **渲染效能**：額外一次 OpenGL 渲染增加匯出時間。→ 290×290 渲染在現代 GPU 上幾乎可忽略（與 3MF 匯出行為一致）。
- **`const_cast` 的 RGBA mat**：`ThumbnailData::pixels` 為 `const std::vector<unsigned char>&`，建構 cv::Mat 時需 `const_cast`。→ 此 Mat 為 read-only 使用，立即 `cvtColor` 複製到新 Mat，不會有副作用。