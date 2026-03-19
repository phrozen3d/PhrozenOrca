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
| `picture_grayscale` | `PrintConfig` | 灰階數值 |
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

### 修改後（加入 anti_aliasing 判斷）

```cpp
const SLAPrinterConfig &cfg = m_print->printer_config();
double gamma = cfg.gamma_correction.getFloat();

// When anti-aliasing is disabled, force gamma=0 so the rasterizer
// uses threshold rendering (agg::gamma_threshold) instead of AA.
if (cfg.anti_aliasing.value == spNone)
    gamma = 0.0;

m_print->m_layer_images =
    sla::expolygons_layers_to_cvmat(all_layers, res, pxdim, trafo, gamma);
```

**邏輯鏈**：`anti_aliasing == spNone` → `gamma = 0.0` → `create_raster_grayscale_aa()` 走 threshold 分支 → 輸出純二值 cv::Mat（0/255）

---

## 四、修改檔案總覽

| 檔案 | 修改內容 |
|---|---|
| [PrintConfig.hpp](../src/libslic3r/PrintConfig.hpp) | `SLAPrinterConfig` 新增 `((ConfigOptionEnum<AntiAliasing>, anti_aliasing))` |
| [SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp) | `rasterize()` 讀取 `cfg.anti_aliasing`，`spNone` 時強制 `gamma = 0.0` |

**未修改**（不需要改動）：

- [SLA/RasterBase.cpp](../src/libslic3r/SLA/RasterBase.cpp) — `gamma=0` 的 threshold 邏輯已存在
- [SLA/RasterToCvMat.hpp/.cpp](../src/libslic3r/SLA/RasterToCvMat.cpp) — 函式簽名不變

---

## 五、Config 存取機制補充

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

---

## 六、UI 連動（已存在，不需修改）

`ConfigManipulation::toggle_print_sla_options()` 已有對 `anti_aliasing` 的 UI 可見性控制：

- `anti_aliasing == spNone` → 隱藏 `gray_scale_level`、`anti_aliasing_level`
- `image_blur_enable == false` → 隱藏 `image_blur_pixel`

**檔案**：[ConfigManipulation.cpp](../src/slic3r/GUI/ConfigManipulation.cpp) line ~922
