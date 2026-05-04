# SLA 光柵化管線效能分析報告

**分析日期**: 2026-05-02  
**分析範圍**: `ac3149563c` → `7580ba2038`  
**問題描述**: 切片完成後至 Export GCode 按鈕啟用前等待時間增加，且記憶體用量大幅提升

---

## 目錄

1. [問題重現條件](#1-問題重現條件)
2. [切片管線架構說明](#2-切片管線架構說明)
3. [Commit 逐一影響分析](#3-commit-逐一影響分析)
4. [記憶體暴增根本原因](#4-記憶體暴增根本原因)
5. [背景等待時間增加根本原因](#5-背景等待時間增加根本原因)
6. [改善方案](#6-改善方案)
7. [風險評估](#7-風險評估)

---

## 1. 問題重現條件

- **切片後狀況**：`slapsRasterize` 步驟結束後，記憶體用量暴增
- **按鈕啟用延遲**：`rasterize()` 執行完畢後，進入 `merge_slices_and_eval_stats()` 之前，有一段異常長的等待
- **影響版本**：從 commit `2421a7ea54` 起引入，後續 commits 持續加重

---

## 2. 切片管線架構說明

這批 commits 引入後，`SLAPrint::Steps::rasterize()` 內部實際上有**兩條並列的光柵化管線**：

### Pipeline 1（原始 PrusaSlicer 管線）

```
m_printer_input[i].transformed_slices()
  → poly.translate(raster_shift)       ← 794bfb1ba8 加入
  → raster.draw(poly)
  → draw_layers()
  → SLAArchive（SL1 PNG / CTB 等格式輸出）
```

- 輸出目標：各格式的 archive 檔案（SL1、SL1S 等）
- 執行時機：每次切片
- 資料生命週期：寫入 archive 後不保留在記憶體

### Pipeline 2（新增 PRZ cv::Mat 管線）

```
m_printer_input[i].transformed_slices()
  → ep.translate(shift)
  → [全部層收集為 all_layers vector]
  → expolygons_layers_to_cvmat()       ← TBB 並行，但等同完整再光柵化一次
    → create_raster_grayscale_aa()
    → raster->draw(poly)              ← 每層逐 polygon
    → raster->encode()
    → mat.clone()
    → [gray_scale_level 量化 loop]    ← f843d7311d 加入
    → [Gaussian Blur]                 ← 34f26f2717 加入
  → m_layer_images[]                  ← 永久存放直到下次切片
  → [picture_grayscale LUT loop]      ← cf8951f080 加入
```

- 輸出目標：`SLAPrint::m_layer_images`（`std::vector<cv::Mat>`）
- 執行時機：**每次切片，無條件執行**
- 資料生命週期：**常駐記憶體，直到下一次切片或程式關閉**

---

## 3. Commit 逐一影響分析

### 3.1 切片主流程相關 Commits（依時間排序）

#### `2421a7ea54` feat(sla): port Phrozen PRZ format export from Mechado

**改動檔案**: `SLAPrintSteps.cpp`, `SLAPrint.hpp`, `RasterToCvMat.cpp/hpp`, `PhrozenPRZ.cpp/hpp`

**核心變動**：在 `rasterize()` 尾端加入 Pipeline 2，對全部 layer 執行第二次完整光柵化，結果存入 `m_layer_images`。

```cpp
// 新增於 rasterize() 尾端，無條件執行
m_print->m_layer_images =
    sla::expolygons_layers_to_cvmat(all_layers, res, pxdim, trafo, gamma);
```

- **記憶體衝擊**: ⚠️ **最大** — 每層一個全解析度 cv::Mat，永久存放
- **時間衝擊**: ⚠️ **最大** — 等同完整光柵化再做一次
- **影響 Pipeline 1**: 否（新增獨立區塊）

---

#### `4e8b9a8fe0` feat(sla/prz): 串接 export_prz 正式輸出流程，移除測試代碼

**改動**：移除 `#define PHROZEN_PRZ_TEST_EXPORT 0` 的 ifdef guard，正式串接 export 流程。

- **記憶體衝擊**: 無額外增加
- **時間衝擊**: 無額外增加（Pipeline 2 已在 2421a7ea54 加入）
- **影響 Pipeline 1**: 否

---

#### `4d2a7381a4` fix(sla): disable anti-aliasing in rasterizer when anti_aliasing=none

**改動**：Pipeline 2 中，當 `cfg.anti_aliasing == spNone` 時，強制 `gamma = 0.0`，使光柵器切換為 threshold 模式。

```cpp
if (cfg.anti_aliasing.value == spNone)
    gamma = 0.0;
```

- **記憶體衝擊**: 微小
- **時間衝擊**: 微小（影響 raster 內部運算精度）
- **影響 Pipeline 1**: 否（Pipeline 1 的 gamma 由 archive writer 決定）

---

#### `f843d7311d` feat(sla): port gray_scale_level pixel brightness adjustment into rasterizer

**改動**：Pipeline 2 的 `expolygons_to_cvmat()` 加入 gray_scale_level 量化後處理（Stage 1: AA 量化，Stage 2: 範圍映射）。

```cpp
// 在 RasterToCvMat.cpp 中，對每個非純黑/純白像素執行
if (aa_steps > 0) {
    // Stage 1: 量化至 aa_steps 個離散等級
    // Stage 2: 線性映射至 [gray_lo, gray_hi]
}
```

- **記憶體衝擊**: 微小
- **時間衝擊**: 小（per-pixel loop，但在 TBB 並行內）
- **影響 Pipeline 1**: 否

---

#### `794bfb1ba8` fix(sla): center SL1 layer PNGs on display using bed→display shift

**改動**：**Pipeline 1 的 `lvlfn` lambda 加入 `raster_shift`**，每層 ExPolygon translate 後再 draw。

```cpp
// Pipeline 1 lvlfn（原本）
for (const ExPolygon& poly : printlayer.transformed_slices())
    raster.draw(poly);

// Pipeline 1 lvlfn（修改後）
for (ExPolygon poly : printlayer.transformed_slices()) {  // 注意：改為 by value
    poly.translate(raster_shift);
    raster.draw(poly);
}
```

- **記憶體衝擊**: 微小（每層按值複製 ExPolygon，短暫使用）
- **時間衝擊**: 微小（translate 本身很快，但 by-value 複製 polygon 有額外分配）
- **影響 Pipeline 1**: ✅ **是** — SL1/SL1S 輸出的 PNG 位置已改變（修正了原本不置中的 bug）

---

#### `34f26f2717` feat: 新增 Gaussian Blur 後處理至 SLA 光柵化管線

**改動**：Pipeline 2 每個 cv::Mat 產生後，若啟用 `image_blur_enable`，執行 `cv::GaussianBlur()`。

```cpp
if (blur_pixel >= 2) {
    const int k = blur_pixel * 2 + 1;  // kernel size: 最大 (8*2+1) = 17×17
    cv::GaussianBlur(mat, mat, cv::Size(k, k), 0);
}
```

- **記憶體衝擊**: 微小（in-place 操作）
- **時間衝擊**: ⚠️ **中等** — 全解析度影像（例如 6480×3600）Gaussian Blur，每層單獨執行（TBB 並行），kernel 最大 17×17
- **影響 Pipeline 1**: 否

---

#### `2be8e66d3c` feat: SLA shrinkage compensation 實作（製程層縮放補正）

**改動**：`SLAPrint::sla_trafo()` 加入 shrinkage compensation 縮放，`SLAPrint::apply()` 快取參數並在變更時強制 invalidate。

```cpp
if (m_shrinkage_compensation) {
    corr.x() *= m_shrinkage_compensation_x / 100.0;
    corr.y() *= m_shrinkage_compensation_y / 100.0;
    corr.z() *= m_shrinkage_compensation_z / 100.0;
}
```

- **記憶體衝擊**: 微小（快取 4 個成員變數）
- **時間衝擊**: 微小（trafo 矩陣運算）
- **影響 Pipeline 1**: ✅ **是** — 物件的 transform 改變，所有下游切片結果（含 Pipeline 1 的 PNG 位置）都受影響

---

#### `3f8cb9a30f` feat: SLA 公差補償實作（tolerance_compensation 切層輪廓偏移）

**改動**：`apply_printer_corrections()` 新增 tolerance compensation，對每個 slice 執行 Clipper `offset_ex()` + `diff_ex()`。

```cpp
auto apply_tc_layer = [](ExPolygons &layer_slices, coord_t ca, coord_t cb) {
    for (const ExPolygon &ep : layer_slices) {
        Polygons new_contour = offset(ep.contour, float(cb));
        // ... 反轉 hole → solid，縮小後 diff
        append(result, diff_ex(new_contour, hole_solids));
    }
};
// 對所有 layers（分正常層/底層）迴圈執行
```

- **記憶體衝擊**: 小（Clipper 運算的暫時性中間資料）
- **時間衝擊**: ⚠️ **中等** — `offset_ex` + `diff_ex` 是 Clipper 重型運算，對全部 layer 執行（在 `apply_printer_corrections` 內，slice 步驟就執行，非 rasterize 步驟）
- **影響 Pipeline 1**: ✅ **是** — slice 輪廓在光柵化前已被修改，Pipeline 1 的 PNG 內容也受影響

---

#### `cf8951f080` feat: picture_grayscale 像素等比縮放實作（PRZ + SL1）

**改動**：`rasterize()` 裡，Pipeline 2 全部 `m_layer_images` 產生後，執行 LUT 縮放。

```cpp
if (pg < 255u) {
    uint8_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = static_cast<uint8_t>((i * pg + 127u) / 255u);
    for (cv::Mat &img : m_print->m_layer_images) {
        // 對每張 img 的所有像素套用 lut
    }
}
```

- **記憶體衝擊**: 微小
- **時間衝擊**: 小（LUT 索引，非常快）
- **影響 Pipeline 1**: 也對 SL1 輸出有影響（此 commit 同時修改了 `SL1.cpp`）

---

#### `7580ba2038` feat: prz_print_time_s — 切片後於 SLAPrintStatistics 提供完整物理列印時間

**改動**：`merge_slices_and_eval_stats()` 加入 `calculate_prz_print_time()` 呼叫，結果存入 statistics。

- **記憶體衝擊**: 微小
- **時間衝擊**: 微小（純數學計算）
- **影響 Pipeline 1**: 否

---

### 3.2 影響分類總表

| Commit | 影響 Pipeline 1 | 記憶體衝擊 | 時間衝擊 | 執行位置 |
|--------|:--------------:|:----------:|:--------:|---------|
| `2421a7ea54` | 否 | ⚠️⚠️⚠️ 最大 | ⚠️⚠️⚠️ 最大 | rasterize() 尾端 |
| `4e8b9a8fe0` | 否 | 無 | 無 | export 流程串接 |
| `4d2a7381a4` | 否 | 微小 | 微小 | Pipeline 2 內部 |
| `f843d7311d` | 否 | 微小 | 小 | RasterToCvMat per-pixel |
| `794bfb1ba8` | ✅ 是（bug fix） | 微小 | 微小 | lvlfn lambda |
| `34f26f2717` | 否 | 微小 | ⚠️⚠️ 中等 | RasterToCvMat GaussianBlur |
| `2be8e66d3c` | ✅ 是 | 微小 | 微小 | sla_trafo() |
| `3f8cb9a30f` | ✅ 是 | 小（暫時） | ⚠️⚠️ 中等 | apply_printer_corrections() |
| `cf8951f080` | ✅ 是（SL1 也改） | 微小 | 小 | rasterize() + SL1.cpp |
| `7580ba2038` | 否 | 微小 | 微小 | merge_slices_and_eval_stats() |

---

## 4. 記憶體暴增根本原因

### 4.1 `m_layer_images` 常駐記憶體

`SLAPrint::m_layer_images`（`std::vector<cv::Mat>`）在每次 `rasterize()` 完成後填入，**直到下一次切片或程式關閉才釋放**。

每個 `cv::Mat` 的記憶體佔用：

```
記憶體（bytes）= display_pixels_x × display_pixels_y × 1（CV_8UC1）

Phrozen Sonic Mega 8K S  (6480 × 3600):  23.3 MB / 層
Phrozen Sonic Mega 8K V2 (6480 × 3600):  23.3 MB / 層
Phrozen Sonic Mighty Revo 16K (12000 × 6750): 81.0 MB / 層
```

**累積用量估算（Mega 8K，23.3 MB/層）**：

| 層數 | 記憶體用量 |
|:---:|:---------:|
| 50  | 1.2 GB    |
| 100 | 2.3 GB    |
| 200 | 4.7 GB    |
| 400 | 9.3 GB    |

### 4.2 `all_layers` 中間資料結構

在 `expolygons_layers_to_cvmat()` 呼叫前，程式碼建立一個 `std::vector<ExPolygons>` 複本：

```cpp
std::vector<ExPolygons> all_layers;
all_layers.reserve(m_print->m_printer_input.size());
for (const PrintLayer &layer : m_print->m_printer_input) {
    ExPolygons polys = layer.transformed_slices();  // 複製 polygon 資料
    for (ExPolygon &ep : polys)
        ep.translate(shift);
    all_layers.push_back(std::move(polys));
}
```

這份資料在 `rasterize()` scope 結束前會釋放，但在執行期間與 `m_layer_images` 同時存在，峰值用量更高。

---

## 5. 背景等待時間增加根本原因

### 5.1 `rasterize()` 執行順序（現況）

```
[1] draw_layers()                     ← Pipeline 1（原 PrusaSlicer，已完成）
[2] 建立 all_layers 複本              ← 遍歷所有層，複製 + translate polygon
[3] expolygons_layers_to_cvmat()      ← TBB 並行，但等同完整光柵化再做一次
    ├─ create_raster_grayscale_aa()   ← 每層建立 AGG raster 物件
    ├─ raster->draw(poly)             ← 每層逐 polygon 畫入
    ├─ raster->encode()               ← 取出 raw pixel buffer
    ├─ mat.clone()                    ← 深拷貝像素資料
    ├─ [gray_scale_level 量化 loop]   ← 非純黑/白像素逐一處理
    └─ [cv::GaussianBlur()]           ← 全解析度 kernel 模糊（若啟用）
[4] picture_grayscale LUT loop        ← 遍歷所有 m_layer_images 的所有像素
```

步驟 `[1]` 完成後 export button 才會啟用，但 `[2]`–`[4]` 仍在同一個 step 函式（`rasterize()`）內執行，因此必須等到全部完成才會解除鎖定。

### 5.2 各步驟時間估算（Mega 8K，100 層，8 核心）

| 步驟 | 說明 | 估計時間 |
|------|------|:-------:|
| `draw_layers()` Pipeline 1 | 原始光柵化（TBB 並行） | T₀（基準） |
| `all_layers` 複本建立 | Polygon 複製 + translate | ~5% T₀ |
| `expolygons_layers_to_cvmat()` | 完整再光柵化（TBB 並行） | ~90–110% T₀ |
| `cv::GaussianBlur` per layer | 全解析度 kernel（若啟用） | ~20–40% T₀ |
| gray_scale_level + LUT loop | Per-pixel 運算 | ~5–10% T₀ |

**總計**：原始時間的 **2.1x–2.6x**（不含 GaussianBlur 時 ~2x，含時 ~2.3–2.6x）

### 5.3 Tolerance Compensation 的額外影響

`3f8cb9a30f` 加入的 tolerance compensation 在 **slice 步驟**（`apply_printer_corrections()`）執行，對全部 layer 各做一次 `offset_ex()` + `diff_ex()`。這是 Clipper 的重型 polygon 布林運算，在複雜模型時可能顯著增加 slice 步驟的時間。

---

## 6. 改善方案

### 方案 A：On-Demand 產生（建議）

將 `m_layer_images` 改為 **只在 export PRZ 時才產生**，export 完後立即釋放。

**優點**：完全消除記憶體問題與等待時間問題  
**缺點**：export PRZ 時需要額外時間產生影像（但這是使用者主動操作，可接受）

```cpp
// SLAPrintSteps.cpp rasterize() — 移除 Pipeline 2 的整段

// PhrozenPRZ.cpp generate_prz() — 在此自行執行 expolygons_layers_to_cvmat()
// export 完後不存入 m_layer_images（或存入後立即 clear）
```

### 方案 B：export 後立即釋放（快速修復）

在 `generate_prz()` 呼叫完成後加入：

```cpp
m_layer_images.clear();
m_layer_images.shrink_to_fit();
```

**優點**：最小改動，export 後立即釋放記憶體  
**缺點**：切片完到 export 前的等待時間問題仍然存在

### 方案 C：獨立背景執行（長期）

將 Pipeline 2 移出 `rasterize()` step，改為 export 流程觸發的獨立非同步工作。

**優點**：切片完畢後 export button 立即啟用；export 時再非同步產生 cv::Mat  
**缺點**：架構改動較大，需要與 GUI 的 progress 機制整合

---

## 7. 風險評估

### 已確認不影響原本 PrusaSlicer 切片管線的項目

- `m_layer_images` Pipeline 2：完全獨立的新增路徑
- gray_scale_level 量化：只在 Pipeline 2 內部執行
- Gaussian Blur：只在 Pipeline 2 內部執行
- picture_grayscale LUT（m_layer_images 部分）：只影響 cv::Mat

### 已確認影響原本 PrusaSlicer 切片管線的項目

- **`794bfb1ba8`（raster_shift in lvlfn）**：Pipeline 1 SL1 PNG 的像素位置改變（這是 bug fix，行為改變是預期的）
- **`2be8e66d3c`（shrinkage compensation）**：sla_trafo 改變，影響所有下游
- **`3f8cb9a30f`（tolerance compensation）**：slice 輪廓在光柵化前被 offset，Pipeline 1 的 PNG 內容也改變
- **`cf8951f080`（picture_grayscale 在 SL1）**：SL1.cpp 同時修改，SL1 輸出亮度改變

---

## 附錄：相關程式碼位置

| 元件 | 路徑 |
|------|------|
| 光柵化主流程 | `src/libslic3r/SLAPrintSteps.cpp` — `Steps::rasterize()` |
| cv::Mat 轉換 | `src/libslic3r/SLA/RasterToCvMat.cpp/hpp` |
| PRZ 格式輸出 | `src/libslic3r/Format/PhrozenPRZ.cpp/hpp` |
| m_layer_images 宣告 | `src/libslic3r/SLAPrint.hpp` — `SLAPrint` class |
| Slice 後處理（TC/EFC） | `src/libslic3r/SLAPrintSteps.cpp` — `apply_printer_corrections()` |
| Shrinkage trafo | `src/libslic3r/SLAPrint.cpp` — `SLAPrint::sla_trafo()` |
