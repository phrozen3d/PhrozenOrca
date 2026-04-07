# SLA Anti-Aliasing 光柵化機制

**建立日期**：2026-03-19
**範圍**：`anti_aliasing` 設定如何影響 SLA 光柵化（rasterization）輸出的 cv::Mat 影像

---

## 一、參數定義

### Config Class 位置

`anti_aliasing` 同時存在於兩個 config class：

| Config Class | 欄位 | 用途 |
|---|---|---|
| `PrintConfig`（FFF 大 class） | `anti_aliasing` | FFF/舊版定義，`FullPrintConfig` 用 |
| `SLAPrinterConfig` | `anti_aliasing` | SLA 實際使用，`SLAPrint::rasterize()` 讀取 |

**檔案**：[PrintConfig.hpp](../src/libslic3r/PrintConfig.hpp)

```cpp
// SLAPrinterConfig（line ~1819）
((ConfigOptionFloat,              gamma_correction))
((ConfigOptionEnum<AntiAliasing>, anti_aliasing))     // ← 此行為 2026-03-19 新增
((ConfigOptionFloat,              fast_tilt_time))
```

> **注意**：`anti_aliasing` 在 `PrintConfig`（FFF）中也有定義（line ~1524）。`SLAPrinterConfig` 獨立加入此欄位是因為 `SLAPrint` 的 static config 系統只會從各自的 `diff()` / `apply_only()` 取值，不會存取 `PrintConfig`（FFF）的資料。

### Enum 定義

**檔案**：[PrintConfig.hpp](../src/libslic3r/PrintConfig.hpp)、[PrintConfig.cpp](../src/libslic3r/PrintConfig.cpp)

```cpp
enum AntiAliasing { spNone, spGrayScaleLevel, spAntiAliasingLevel };
```

| 值 | 字串 key | 說明 |
|---|---|---|
| `spNone` | `"none"` | 關閉 AA，純 threshold 二值輸出 |
| `spGrayScaleLevel` | `"gray_scale_level"` | 灰階層數模式 |
| `spAntiAliasingLevel` | `"anti_aliasing_level"` | AA level 模式 |

### 相關參數

| 參數 | Config Class | 說明 |
|---|---|---|
| `anti_aliasing` | `SLAPrinterConfig` | AA 模式開關（Enum） |
| `anti_aliasing_level` | `PrintConfig` | AA 層數（整數，min=1, default=4） |
| `picture_grayscale` | `SLAPrinterConfig`（+`PrintConfig`） | 像素等比縮放上限（0–255，default 255）；2026-04-07 加入 `SLAPrinterConfig`，供 rasterize 後置縮放及 SL1 encoder 使用 |
| `gray_scale_level` | `PrintConfig` | 灰階層數（ConfigOptionInts） |
| `image_blur_enable` | `PrintConfig` | 啟用影像模糊 |
| `image_blur_pixel` | `PrintConfig` | 模糊像素數 |
| `gamma_correction` | `SLAPrinterConfig` | Gamma 值，影響 AA 效果 |

---

## 二、Rasterization 技術架構

### 關鍵類別關係

```
expolygons_layers_to_cvmat()       ← SLAPrintSteps.cpp 呼叫入口
    └── expolygons_to_cvmat()      ← RasterToCvMat.cpp
            └── create_raster_grayscale_aa(res, pxdim, gamma, trafo)
                    └── RasterBase.cpp
                        ├── gamma > 0  → RasterGrayscaleAAGammaPower  (AA 開啟)
                        ├── gamma ≈ 1  → RasterGrayscaleAA + gamma_none()
                        └── gamma = 0  → RasterGrayscaleAA + gamma_threshold(.5)  (AA 關閉)
                                                      ↑
                                              純二值輸出（0 或 255）
```

### create_raster_grayscale_aa() — gamma 控制邏輯

**檔案**：[SLA/RasterBase.cpp](../src/libslic3r/SLA/RasterBase.cpp)

```cpp
std::unique_ptr<RasterBase> create_raster_grayscale_aa(
    const Resolution &res, const PixelDim &pxdim,
    double gamma, const RasterBase::Trafo &tr)
{
    if (gamma > 0)
        return make_unique<RasterGrayscaleAAGammaPower>(res, pxdim, tr, gamma);
    else if (std::abs(gamma - 1.) < 1e-6)
        return make_unique<RasterGrayscaleAA>(res, pxdim, tr, agg::gamma_none());
    else  // gamma == 0.0 → threshold → 無 AA
        return make_unique<RasterGrayscaleAA>(res, pxdim, tr, agg::gamma_threshold(.5));
}
```

**重點**：傳入 `gamma = 0.0` 時，走 `else` 分支，使用 `agg::gamma_threshold(.5)` — 每個像素只會是 0 或 255，無中間灰階值。

---

## 三、SLAPrintSteps::rasterize() — 實作修改

**檔案**：[SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp) line ~1158

### 修改前（只使用 gamma_correction）

```cpp
const SLAPrinterConfig &cfg = m_print->printer_config();
double gamma = cfg.gamma_correction.getFloat();

m_print->m_layer_images =
    sla::expolygons_layers_to_cvmat(all_layers, res, pxdim, trafo, gamma);
```

### 修改後（加入 anti_aliasing 判斷 + gray_scale_level 接入）

```cpp
const SLAPrinterConfig &cfg = m_print->printer_config();
double gamma = cfg.gamma_correction.getFloat();

// When anti-aliasing is disabled, force gamma=0 so the rasterizer
// uses threshold rendering (agg::gamma_threshold) instead of AA.
if (cfg.anti_aliasing.value == spNone)
    gamma = 0.0;

// Gray scale level 後處理參數（只在 spGrayScaleLevel 時填入）
int     aa_steps = 0;
uint8_t gray_lo  = 0;
uint8_t gray_hi  = 255;

if (cfg.anti_aliasing.value == spGrayScaleLevel) {
    const DynamicPrintConfig &full_cfg = m_print->full_print_config();
    if (auto *aa_lvl = full_cfg.option<ConfigOptionInt>("anti_aliasing_level"))
        aa_steps = std::max(1, aa_lvl->getInt());
    else
        aa_steps = 4; // default
    if (auto *gsl = full_cfg.option<ConfigOptionInts>("gray_scale_level");
            gsl && gsl->values.size() >= 2) {
        gray_lo = (uint8_t)std::clamp(gsl->values[0], 0, 255);
        gray_hi = (uint8_t)std::clamp(gsl->values[1], 0, 255);
    }
}

m_print->m_layer_images =
    sla::expolygons_layers_to_cvmat(all_layers, res, pxdim, trafo, gamma,
                                    aa_steps, gray_lo, gray_hi);
```

**邏輯鏈**：
- `anti_aliasing == spNone` → `gamma = 0.0` → `create_raster_grayscale_aa()` 走 threshold 分支 → 純二值 cv::Mat（0/255）
- `anti_aliasing == spGrayScaleLevel` → `aa_steps > 0` → `expolygons_to_cvmat()` 套用兩階段後處理

---

## 四、gray_scale_level 像素後處理（spGrayScaleLevel 模式）

移植自 `web_slicer_core/third_party/prusaslicer_fork/src/libslic3r/Format/SL1.cpp` 的 `get_encoder()` 邏輯。

### 演算法

對每個非純黑（0）、非純白（255）的 AA 中間像素執行兩階段處理：

**Stage 1 — AA 量化**：將 AGG 產生的連續灰階壓縮到 `aa_steps` 個離散層，消除不必要的中間值。

```cpp
double gray_interval = 255.0 / double(aa_steps);
c = (uint8_t)std::round(
    std::round(double(c) / gray_interval) / double(aa_steps) * 255.0);
```

**Stage 2 — 範圍映射**：線性映射到 `[gray_lo, gray_hi]`（`{0,255}` 時為 identity，不改變）。

```cpp
c = (uint8_t)std::round(double(gray_lo) + range * (double(c) / 255.0));
```

### 語義對照（web_slicer_core vs PhrozenOrca）

| web_slicer_core | PhrozenOrca | 說明 |
|---|---|---|
| `gray_level`（int 0-8）| `gray_scale_level = {lo, hi}` | 暗部抬升 → 輸出值域下限/上限 |
| `init_val = 32 * gray_level` | `gray_lo`（uint8_t） | 最暗 AA 像素的輸出值 |
| Stage 2 線性平移 | 線性映射 `[0,255]→[lo,hi]` | 等效語義，`{0,255}` = 關閉 |

`gray_scale_level` 在 `PrintConfig`（FFF 大 class），必須透過 `m_print->full_print_config()` 存取（非 `SLAPrinterConfig`）。

---

## 五、修改檔案總覽

| 檔案 | 修改內容 |
|---|---|
| [PrintConfig.hpp](../src/libslic3r/PrintConfig.hpp) | `SLAPrinterConfig` 新增 `anti_aliasing`（2026-03-19）、`picture_grayscale`（2026-04-07） |
| [SLA/RasterToCvMat.hpp](../src/libslic3r/SLA/RasterToCvMat.hpp) | 新增 `aa_steps`、`gray_lo`、`gray_hi`、`blur_pixel` 參數（預設值，向後相容） |
| [SLA/RasterToCvMat.cpp](../src/libslic3r/SLA/RasterToCvMat.cpp) | 實作兩階段後處理（Stage 1 量化 + Stage 2 範圍映射）；AA 後處理完成後執行 Gaussian Blur |
| [SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp) | `rasterize()` 讀取 `anti_aliasing`；`spNone` 強制 `gamma=0`；`spGrayScaleLevel` 讀取 `gray_scale_level`/`anti_aliasing_level` 並傳入；讀取 `image_blur_enable`/`image_blur_pixel` 並傳入 `blur_pixel`；`m_layer_images` 產生後套用 `picture_grayscale` 等比縮放 pass（2026-04-07） |
| [Format/SL1.cpp](../src/libslic3r/Format/SL1.cpp) | `get_encoder()` 加入 `picture_grayscale` LUT encoder（2026-04-07） |
| [SLAPrint.cpp](../src/libslic3r/SLAPrint.cpp) | `steps_rasterize` 新增 `"picture_grayscale"`，確保 UI 變更觸發重新 rasterize（2026-04-07） |

**未修改**（不需要改動）：

- [SLA/RasterBase.cpp](../src/libslic3r/SLA/RasterBase.cpp) — `gamma=0` 的 threshold 邏輯已存在
- `PrintConfig.cpp` — `add("picture_grayscale", coInt)` 已存在（全域定義不重複）
- `ConfigManipulation.cpp` — UI 可見性已正確控制
- `PhrozenPRZ.cpp` — 直接讀 `m_layer_images`，縮放已在上游套用，不需改

---

## 六、Config 存取機制補充

`SLAPrint` 持有四個獨立的 static config 實例：

```cpp
// SLAPrint.hpp
SLAPrintConfig          m_print_config;        // filename_format only
SLAPrinterConfig        m_printer_config;      // 機台硬體參數 ← anti_aliasing 在這裡
SLAMaterialConfig       m_material_config;     // 材料曝光參數
SLAPrintObjectConfig    m_default_object_config;
```

`SLAPrint::apply(DynamicPrintConfig config)` 呼叫各 config 的 `diff()` / `apply_only()` 自動分發。
`SLAPrinterConfig` 新增 `anti_aliasing` 欄位後，`m_printer_config.diff(config)` 會自動偵測並 apply 此 key。

`gray_scale_level`、`anti_aliasing_level` 屬於 `PrintConfig`（FFF 大 class），不在 `SLAPrinterConfig`，需透過 `m_print->full_print_config()` 存取。

---

## 七、UI 連動（已存在，不需修改）

`ConfigManipulation::toggle_print_sla_options()` 已有對 `anti_aliasing` 的 UI 可見性控制：

- `anti_aliasing == spNone` → 隱藏 `gray_scale_level`、`anti_aliasing_level`
- `image_blur_enable == false` → 隱藏 `image_blur_pixel`

**檔案**：[ConfigManipulation.cpp](../src/slic3r/GUI/ConfigManipulation.cpp) line ~922

---

## 八、Gaussian Blur 後處理（2026-03-23 新增）

### 設計概念

模糊化在所有 AA 後處理（Stage 1 量化、Stage 2 範圍映射）**完成後**執行，作為獨立的第三階段。參考邏輯：

```cpp
// 參考來源（web_slicer_core）
const int blur = g_config->Options_i["blur"] ? g_config->Options_i["blur"] + 1 : 0;
cv::GaussianBlur(_image, _image, cv::Size((blur << 1) + 1, (blur << 1) + 1), 0);
```

### Config 對應

| 參數 | Config Class | 說明 |
|---|---|---|
| `image_blur_enable` | `PrintConfig`（FFF） | 啟用模糊化（Bool） |
| `image_blur_pixel` | `PrintConfig`（FFF） | 模糊半徑（`ConfigOptionEnum<ImageBlurPixel>`） |

`ImageBlurPixel` enum（PrintConfig.hpp line 412）：

```cpp
enum ImageBlurPixel { sp2, sp3, sp4, sp5, sp6, sp7, sp8 };
// 字串 key: "2"~"8"，enum int 值 0~6 → 實際 pixel = enum_int + 2
```

兩者都在 `PrintConfig`（FFF 大 class），需透過 `m_print->full_print_config()` 存取。

### Kernel Size 計算

| enum 值 | 實際 pixel | kernel size |
|---|---|---|
| `sp2`（0） | 2 | 5×5 |
| `sp3`（1） | 3 | 7×7 |
| `sp4`（2） | 4 | 9×9 |
| `sp5`（3） | 5 | 11×11 |
| `sp6`（4） | 6 | 13×13 |
| `sp7`（5） | 7 | 15×15 |
| `sp8`（6） | 8 | 17×17 |

公式：`kernel = blur_pixel * 2 + 1`，其中 `blur_pixel = enum_int + 2`。

### 實作位置

**`SLAPrintSteps.cpp` — `rasterize()` 讀取 blur 設定**

```cpp
// Gaussian blur post-processing (applied after all AA stages)
int blur_pixel = 0;
{
    const DynamicPrintConfig &full_cfg = m_print->full_print_config();
    if (auto *blur_en = full_cfg.option<ConfigOptionBool>("image_blur_enable");
            blur_en && blur_en->getBool()) {
        if (auto *blur_px = full_cfg.option<ConfigOptionEnum<ImageBlurPixel>>("image_blur_pixel"))
            blur_pixel = blur_px->getInt() + 2;  // enum 0–6 → pixel 2–8
    }
}
```

**`RasterToCvMat.cpp` — `expolygons_to_cvmat()` 執行 blur**

```cpp
// Gaussian blur: applied after AA/gray-scale post-processing.
if (blur_pixel >= 2) {
    const int k = blur_pixel * 2 + 1;
    cv::GaussianBlur(mat, mat, cv::Size(k, k), 0);
}
```

`cv::GaussianBlur` 需要 `opencv2/imgproc.hpp`（已在 `RasterToCvMat.cpp` 新增 include）。

### 執行順序總覽

```
expolygons_to_cvmat()
    1. create_raster_grayscale_aa()  — AGG 光柵化（gamma 控制）
    2. aa_steps > 0 時
       ├── Stage 1：量化到 aa_steps 離散層
       └── Stage 2：線性映射到 [gray_lo, gray_hi]
    3. blur_pixel >= 2 時
       └── cv::GaussianBlur(kernel = blur_pixel*2+1)

SLAPrintSteps::rasterize()（expolygons_layers_to_cvmat 之後）
    4. picture_grayscale < 255 時
       └── 256-entry LUT 等比縮放：pixel = (pixel * pg + 127) / 255
```

---

## 九、picture_grayscale 等比例縮放（2026-04-07 新增）

### 設計概念

`picture_grayscale`（整數 0–255，default 255）是所有像素的**等比例縮放上限**。縮放在所有 AA/blur 後處理**完成後**，作為獨立第四階段套用於整個 `m_layer_images`（PRZ 路徑）及 SL1 archive PNG encoder（SL1 路徑）。

兩條路徑互相獨立，但使用相同公式：

```
pixel_out = (pixel_in * picture_grayscale + 127) / 255   // 整數運算，round-to-nearest
```

- `picture_grayscale = 255`（default）→ identity，輸出與修改前完全相同
- `picture_grayscale = 200` → 所有像素乘以 200/255 ≈ 0.784（255→200，128→100）
- `picture_grayscale = 0` → 所有像素歸零（完全黑）

### 與 gray_scale_level 的關係

兩者**獨立堆疊**（不互相排斥）：

```
AGG 光柵化 → [gray_scale_level 量化+映射] → [Blur] → [picture_grayscale 縮放]
```

例如：`gray_hi = 200`、`picture_grayscale = 200` → 最大輸出 ≈ 200 × 200/255 ≈ 157（非 200）。

### Config 存取

| 路徑 | 存取方式 |
|------|---------|
| PRZ（`SLAPrintSteps.cpp`） | `cfg.picture_grayscale.getInt()`（`cfg` 是 `SLAPrinterConfig`） |
| SL1（`SL1.cpp get_encoder()`） | `m_cfg.picture_grayscale.getInt()`（`m_cfg` 是 `SLAPrinterConfig`） |

`picture_grayscale` 加入 `SLAPrinterConfig`（`PrintConfig.hpp` line ~1828）後，`SLAPrint::apply()` 自動 diff/apply，UI 變更直接觸發重新 rasterize（`steps_rasterize` 已含此 key）。

### PRZ 路徑實作（SLAPrintSteps.cpp）

```cpp
// 在 expolygons_layers_to_cvmat() 之後，SLAPrintSteps::rasterize() 末端
{
    const unsigned pg = static_cast<unsigned>(
        std::clamp(cfg.picture_grayscale.getInt(), 0, 255));
    if (pg < 255u) {
        uint8_t lut[256];
        for (int i = 0; i < 256; i++)
            lut[i] = static_cast<uint8_t>((i * pg + 127u) / 255u);
        for (cv::Mat &img : m_print->m_layer_images) {
            uint8_t  *data  = img.data;
            const int total = img.rows * img.cols;
            for (int j = 0; j < total; ++j)
                data[j] = lut[data[j]];
        }
    }
}
```

**注意**：`cv::LUT(src, lut, dst)` 不支援 `src == dst`（OpenCV 4.6.0 內部會先 `_dst.create()` 再讀 src，造成資料破壞），因此使用手寫 `img.data` 指標迴圈（in-place 安全）。

### SL1 路徑實作（Format/SL1.cpp）

```cpp
sla::RasterEncoder SL1Archive::get_encoder() const
{
    const unsigned pg = static_cast<unsigned>(
        std::clamp(m_cfg.picture_grayscale.getInt(), 0, 255));

    if (pg >= 255u)
        return sla::PNGRasterEncoder{};

    std::array<uint8_t, 256> lut;
    for (int i = 0; i < 256; i++)
        lut[i] = static_cast<uint8_t>((i * pg + 127u) / 255u);

    return [lut](const void *ptr, size_t w, size_t h, size_t) -> sla::EncodedRaster {
        const size_t npx = w * h;
        std::vector<uint8_t> buf(
            static_cast<const uint8_t *>(ptr),
            static_cast<const uint8_t *>(ptr) + npx);
        for (uint8_t &v : buf)
            v = lut[v];
        return sla::PNGRasterEncoder{}(buf.data(), w, h, 1);
    };
}
```

LUT 在 `get_encoder()` 建立時預算一次，由 lambda `[lut]` by-value 捕捉，每層呼叫時直接使用，不重複計算。
