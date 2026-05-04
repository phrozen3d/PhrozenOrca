## 1. 介面修改

- [x] 1.1 在 `PhrozenPRZ.hpp` 的 `generate_prz()` 簽名新增 `const ThumbnailData *thumb = nullptr` 參數；新增 `#include "GCode/ThumbnailData.hpp"`

## 2. 核心轉換邏輯（PhrozenPRZ.cpp）

- [x] 2.1 在 `prz_header()` 簽名新增 `const ThumbnailData *thumb` 參數
- [x] 2.2 在 116×116 預覽區塊中，若 `thumb` 有效則：RGBA → cv::Mat BGR → `cv::flip(..., 0)` → `cv::resize()` 至 116×116，再套用現有 RGB565 迴圈；否則保留現有 `preview_image_path` fallback
- [x] 2.3 在 290×290 預覽區塊中，若 `thumb` 有效則：RGBA → cv::Mat BGR → `cv::flip(..., 0)`（不需 resize），套用現有 RGB565 迴圈；否則保留現有 `preview_image_path` fallback
- [x] 2.4 在 `generate_prz()` 中將 `thumb` 往下傳給 `prz_header()`

## 3. Plater 渲染呼叫（Plater.cpp）

- [x] 3.1 在 `export_prz()` 中，於呼叫 `generate_prz()` 前新增渲染邏輯：
  ```cpp
  ThumbnailData thumb;
  const ThumbnailsParams params = { {}, false, true, true, true,
      p->partplate_list.get_curr_plate_index() };
  p->generate_thumbnail(thumb, 290, 290, params, Camera::EType::Ortho);
  ```
- [x] 3.2 將 `generate_prz()` 呼叫改為傳入縮圖：`Slic3r::generate_prz(sla_print(), thumb.is_valid() ? &thumb : nullptr)`

## 4. 驗證

- [x] 4.1 編譯確認零錯誤（Windows Release）
- [x] 4.2 匯出 PRZ，用 hex editor 確認 header 偏移 `0x62`（116×116 起始）與 `0x432A`（290×290 起始）非全零
- [x] 4.3 在 Phrozen 印表機（或模擬器）載入 PRZ，確認縮圖正確顯示模型
- [x] 4.4 場景為空（無物件）時匯出 PRZ，確認不崩潰（N/A：無物件時切片與匯出按鈕為 disabled，此路徑不可達）