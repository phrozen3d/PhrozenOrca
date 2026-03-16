# SLA PRZ 格式匯出實作記錄

**建立日期**: 2026-03-12
**最後更新**: 2026-03-16
**分支**: phrozen-resin-dev
**涉及檔案**:
- [src/libslic3r/Format/PhrozenPRZ.hpp](../src/libslic3r/Format/PhrozenPRZ.hpp)（新增）
- [src/libslic3r/Format/PhrozenPRZ.cpp](../src/libslic3r/Format/PhrozenPRZ.cpp)（新增）
- [src/libslic3r/SLA/RasterToCvMat.hpp](../src/libslic3r/SLA/RasterToCvMat.hpp)（新增）
- [src/libslic3r/SLA/RasterToCvMat.cpp](../src/libslic3r/SLA/RasterToCvMat.cpp)（新增）
- [src/libslic3r/CMakeLists.txt](../src/libslic3r/CMakeLists.txt)（修改）
- [src/libslic3r/SLAPrint.hpp](../src/libslic3r/SLAPrint.hpp)（修改）
- [src/libslic3r/SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp)（修改）
- [src/libslic3r/PrintConfig.cpp](../src/libslic3r/PrintConfig.cpp)（修改）

---

## 一、目的

將 Mechado（PhrozenOrca 前身）中 `Slicer::getPRZString2()` 的 PRZ 格式輸出邏輯移植進 PhrozenOrca，使切片完成後能夠產生 `.prz` 格式的二進位列印檔，供 Phrozen 印表機直接讀取。

**來源檔案**：`C:\repos\mechado\Mechado\Slicer.cpp`
- `Slicer::PrzHeader()` — PRZ 全域 header
- `Slicer::PrzLayerContent()` — 每層動作參數
- `Slicer::getPRZString2()` — 主體：呼叫上兩者並將各層像素資料 RLE 編碼

---

## 二、PRZ 格式結構

PRZ 是 Phrozen 專用的二進位格式，全部欄位使用 **big-endian** 排列。

```
PRZ File
├─ Header
│   ├─ Version "V3.0" (4 bytes)
│   ├─ DLP Tag (8 bytes: 07 00 00 00 44 4C 50 00)
│   ├─ Software (32 bytes, 空白)
│   ├─ Software Version (24 bytes, 空白)
│   ├─ File Time (24 bytes, "YYYY-MM-DD HH:MM:SS")
│   ├─ Printer Name (32 bytes) ← printer_settings_id
│   ├─ Printer Type (32 bytes) ← printer_model
│   ├─ Profile Name (32 bytes) ← sla_print_settings_id
│   ├─ Anti-aliasing level (2 bytes, big-endian short)
│   ├─ Grey level (2 bytes)
│   ├─ Blur level (2 bytes)
│   ├─ Preview 116×116 (116*116*2 bytes, RGB565) ← preview_image_path/PreviewImage_116_116.png（或全 0）
│   ├─ CR LF (2 bytes)
│   ├─ Preview 290×290 (290*290*2 bytes, RGB565) ← preview_image_path/PreviewImage_290_290.png（或全 0）
│   ├─ CR LF (2 bytes)
│   ├─ Total Layers (4 bytes, int)
│   ├─ XResolution (2 bytes, short) ← display_pixels_y（Portrait swap 後）
│   ├─ YResolution (2 bytes, short) ← display_pixels_x（Portrait swap 後）
│   ├─ Xmirror (1 byte: mirror_x=false→1, true→0)
│   ├─ Ymirror (1 byte: mirror_y=false→0, true→1)
│   ├─ PlatformXLength (4 bytes, float, mm) ← display_height
│   ├─ PlatformYLength (4 bytes, float, mm) ← display_width
│   ├─ PlatformZLength (4 bytes, float, mm) ← printable_height
│   ├─ LayerThickness (4 bytes, float, mm) ← layer_height
│   ├─ ExposureTime (4 bytes, float) ← exposure_time（一般層）
│   ├─ Exposure_delay_mode (1 byte, 固定 0x01)
│   ├─ TurnOffTime (4 bytes, float) ← light_off_day
│   ├─ Bottom_Before_lift_static_time (4 bytes, 0)
│   ├─ Bottom_After_lift_static_time (4 bytes, 0)
│   ├─ Bottom_After_retract_static_time (4 bytes) ← rest_time_after_retract
│   ├─ Before_lift_static_time (4 bytes, 0)
│   ├─ After_lift_static_time (4 bytes, 0)
│   ├─ After_retract_static_time (4 bytes) ← rest_time_after_retract
│   ├─ BottomExposureTime (4 bytes, float) ← bottom_exposure_time
│   ├─ BottomLayers (4 bytes, int) ← bottom_layer_count
│   ├─ BottomLiftDist (4 bytes, float) ← bottom_lift_distance[0]
│   ├─ BottomLiftSpeed (4 bytes, float) ← bottom_lift_speed[0]
│   ├─ LiftDist (4 bytes, float) ← lifting_distance[0]
│   ├─ LiftSpeed (4 bytes, float) ← lifting_speed[0]
│   ├─ BottomRetractDist (4 bytes, float) ← bottom_lift_dist + bottom_lift2_dist - bottom_drop2_dist
│   ├─ BottomRetractSpeed (4 bytes, float) ← bottom_retract_speed[0]
│   ├─ RetractDist (4 bytes, float) ← lift_dist + lift2_dist - drop2_dist
│   ├─ RetractSpeed (4 bytes, float) ← retract_speed[0]
│   ├─ BottomLift2Dist (4 bytes, float) ← bottom_lift_second_distance[0]
│   ├─ BottomLift2Speed (4 bytes, float) ← bottom_lift_second_speed[0]
│   ├─ Lift2Dist (4 bytes, float) ← lift_second_distance[0]
│   ├─ Lift2Speed (4 bytes, float) ← lift_second_speed[0]
│   ├─ BottomRetract2Dist (4 bytes, float) ← bottom_retract_second_distance[0]
│   ├─ BottomRetract2Speed (4 bytes, float) ← bottom_retract_second_speed[0]
│   ├─ Retract2Dist (4 bytes, float) ← retract_second_distance[0]
│   ├─ Retract2Speed (4 bytes, float) ← retract_second_speed[0]
│   ├─ BottomLightPwm (2 bytes, short) ← bottom_light_pwm
│   ├─ LightPwm (2 bytes, short) ← light_pwm
│   ├─ Advance_Mode (1 byte, 固定 0x00)
│   ├─ PrintTimes (4 bytes, int, 0)
│   ├─ TotalVolume (4 bytes, float) ← objects_used_material + support_used_material（mm³）
│   ├─ TotalWeight (4 bytes, float) ← total_weight（g）
│   ├─ TotalPrice (4 bytes, float) ← total_cost
│   ├─ PriceUnit (8 bytes, 0)
│   ├─ LayerContent_position_offset (4 bytes, 自參考偏移量)
│   ├─ Grayscale_level (1 byte, 固定 0x01 = 8-bit)
│   └─ Transition layers (2 bytes, short) ← transition_layer_count
│
└─ Per-Layer × N
    ├─ Layer Content
    │   ├─ PauseFlag (2 bytes, short, 0)
    │   ├─ PausePositionZ (4 bytes, float, mm)
    │   ├─ LayerPositionZ (4 bytes, float, mm) ← unscale(print_layers[id].level())
    │   ├─ LayerExposureTime (4 bytes, float, 插值計算)
    │   ├─ LayerOffTime (4 bytes, float) ← light_off_day
    │   ├─ Before_lift_static_time (4 bytes, 0)
    │   ├─ After_lift_static_time (4 bytes, 0)
    │   ├─ After_retract_static_time (4 bytes) ← rest_time_after_retract
    │   ├─ LiftDist (4 bytes, float) — 底/一般層各異
    │   ├─ LiftSpeed (4 bytes, float)
    │   ├─ Lift2Dist (4 bytes, float)
    │   ├─ Lift2Speed (4 bytes, float)
    │   ├─ RetractDist (4 bytes, float) ← lift + lift2 - drop2
    │   ├─ RetractSpeed (4 bytes, float)
    │   ├─ Retract2Dist (4 bytes, float)
    │   ├─ Retract2Speed (4 bytes, float)
    │   ├─ LightPwm (2 bytes, short) — 底/一般層各異
    │   └─ CR LF (2 bytes)
    ├─ Layer Data Size (4 bytes, big-endian int)
    ├─ Layer Pixel Data (RLE encoded)
    │   ├─ 0x55 (layer head byte)
    │   ├─ RLE runs...
    │   └─ Checksum byte (~sum & 0xff)
    └─ CR LF (2 bytes)
        └─ [最後一層後接 DLP End Tag 11 bytes: 00 00 00 07 00 00 00 44 4C 50 00]
```

### RLE 編碼規則

| 像素值 | Base byte | 說明 |
|---|---|---|
| `0x00`（黑） | `0x00` | |
| `0xff`（白） | `0xc0` | |
| 其他（灰階） | `0x40` + 額外 1 byte 色值 | |

run-length 依長度分 1～4 bytes 編碼（`BYTE_NUMBER[0..3]` = `{0x00, 0x10, 0x20, 0x30}`），
對應門檻 `CONTINUOUS_BOUND[0..3]` = `{16, 4096, 1048576, 268435456}`。
count 低 4 bits 編入 byte0，剩餘 bytes 再依序附加。

---

## 三、實作方式

### 3-1 新增檔案：PhrozenPRZ.hpp

```cpp
// src/libslic3r/Format/PhrozenPRZ.hpp
#pragma once
#include <string>

namespace Slic3r {

class SLAPrint;

// Generate a complete PRZ format binary string from a finished SLAPrint.
// m_layer_images must already be populated (call after slapsRasterize).
std::string generate_prz(const SLAPrint &print);

} // namespace Slic3r
```

使用 forward declaration 避免引入 SLAPrint 完整 header，保持編譯依賴最小化。

---

### 3-2 新增檔案：PhrozenPRZ.cpp — 輔助函式

**PhrozenPRZ.cpp** 包含以下 static helper：

| 函式 | 說明 |
|---|---|
| `cfg_f(cfg, key, def)` | 從 `DynamicPrintConfig` 以字串 key 取 float；key 不存在回傳 def |
| `cfg_i(cfg, key, def)` | 同上，取 int |
| `cfg_s(cfg, key)` | 同上，取 string（用於 Printer Name / Printer Type / Profile Name） |
| `cfg_floats0(cfg, key, def)` | 取 `ConfigOptionFloats::values[0]`，用於 lift/retract 等 `coFloats` 參數 |
| `write_be<T>(fh, val)` | 將 T（1/2/4 bytes）以 big-endian 寫入 `std::string` |
| `prz_header(fh, print, cfg)` | 輸出完整 header，對應 `Slicer::PrzHeader()` |
| `prz_layer_content(fh, print, cfg, layerId)` | 輸出每層動作參數，對應 `Slicer::PrzLayerContent()` |
| `generate_prz(print)` | 主函式，對應 `Slicer::getPRZString2()` |

**config 存取來源**：
- `print.full_print_config()` — 繼承自 `PrintBase`，回傳合併後的 `DynamicPrintConfig`，涵蓋所有 SLA 動作參數（曝光、抬升、下降等）
- `print.printer_config()` — 回傳 `SLAPrinterConfig`，取機器尺寸與顯示器參數

> **注意**：`SLAPrint::m_print_config` 的型別是 `SLAPrintConfig`（僅含 `filename_format`），**不是**含有 SLA 動作參數的 `PrintConfig`，必須使用 `full_print_config()`。

---

### 3-3 Mechado → PhrozenOrca config key 對應

| Mechado key | PhrozenOrca key | 型別 | 存取方式 |
|---|---|---|---|
| `normal_exposure_time` | `exposure_time` | `coFloat` | `cfg_f()` |
| `bottom_layer_exposure_time` | `bottom_exposure_time` | `coFloat` | `cfg_f()` |
| `light_off_time` | `light_off_day` | `coFloat` | `cfg_f()` |
| `rest_time` | `rest_time_after_retract` | `coFloat` | `cfg_f()` |
| `bottom_layer_count` | `bottom_layer_count` | `coInt` | `cfg_i()` |
| `transition_layer_count` | `transition_layer_count` | `coInt` | `cfg_i()` |
| `anti_aliasing_level` | `anti_aliasing_level` | `coInt` | `cfg_i()` |
| `gray_level` | `picture_grayscale` | `coInt` | `cfg_i()` |
| `blur` | `image_blur_pixel` | `coEnum` | `cfg_i()` |
| `Bottom Light PWM` | `bottom_light_pwm` | `coInt` | `cfg_i()` |
| `Light PWM` | `light_pwm` | `coInt` | `cfg_i()` |
| `layer_height` | `layer_height` | `coFloat` | `cfg_f()` |
| `bottom_layer_lift_height` | `bottom_lift_distance` | `coFloats[0]` | `cfg_floats0()` |
| `bottom_layer_lift_speed` | `bottom_lift_speed` | `coFloats[0]` | `cfg_floats0()` |
| `bottom_layer_lift_second_height` | `bottom_lift_second_distance` | `coFloats[0]` | `cfg_floats0()` |
| `bottom_layer_lift_second_speed` | `bottom_lift_second_speed` | `coFloats[0]` | `cfg_floats0()` |
| `bottom_layer_drop_second_height` | `bottom_retract_second_distance` | `coFloats[0]` | `cfg_floats0()` |
| `bottom_drop_speed` | `bottom_retract_speed` | `coFloats[0]` | `cfg_floats0()` |
| `bottom_drop_second_speed` | `bottom_retract_second_speed` | `coFloats[0]` | `cfg_floats0()` |
| `normal_layer_lift_height` | `lifting_distance` | `coFloats[0]` | `cfg_floats0()` |
| `normal_layer_lift_speed` | `lifting_speed` | `coFloats[0]` | `cfg_floats0()` |
| `normal_layer_lift_second_height` | `lift_second_distance` | `coFloats[0]` | `cfg_floats0()` |
| `normal_layer_lift_second_speed` | `lift_second_speed` | `coFloats[0]` | `cfg_floats0()` |
| `normal_layer_drop_second_height` | `retract_second_distance` | `coFloats[0]` | `cfg_floats0()` |
| `normal_drop_speed` | `retract_speed` | `coFloats[0]` | `cfg_floats0()` |
| `normal_drop_second_speed` | `retract_second_speed` | `coFloats[0]` | `cfg_floats0()` |

**新增 config 對應**（2026-03-16）：

| PRZ 欄位 | PhrozenOrca key | 型別 | 來源 |
|---|---|---|---|
| Printer Name | `printer_settings_id` | `coString` | `PrintConfig` |
| Printer Type | `printer_model` | `coString` | `PrintConfig` |
| Profile Name | `sla_print_settings_id` | `coString` | `SLAPrintConfig` |
| Preview image dir | `preview_image_path` | `coString` | `PrintConfig`（新增） |
| TotalVolume | — | — | `SLAPrintStatistics::objects_used_material + support_used_material` |
| TotalWeight | — | — | `SLAPrintStatistics::total_weight` |
| TotalPrice | — | — | `SLAPrintStatistics::total_cost` |

**機器尺寸**（來自 `print.printer_config()`，型別 `SLAPrinterConfig`）：

| Mechado | PhrozenOrca | 實際填入原因 |
|---|---|---|
| `image_size.x` | `display_pixels_y` | Portrait swap 後 cols = display_pixels_y |
| `image_size.y` | `display_pixels_x` | Portrait swap 後 rows = display_pixels_x |
| `bed_size` X span | `display_height` | 對應長邊 mm |
| `bed_size` Y span | `display_width` | 對應短邊 mm |
| `machine_z` | `printable_height` | Z 軸行程 mm |

---

### 3-4 Retract 距離計算

Mechado 的 RetractDist 並非直接存欄位，而是由 lift + lift2 – drop2 計算而來：

```cpp
// BottomRetractDist
float v = bottom_lift_distance[0] + bottom_lift_second_distance[0]
        - bottom_retract_second_distance[0];
if (v <= 0.f) v = bottom_lift_distance[0] + bottom_lift_second_distance[0];

// RetractDist（一般層）
float v = lifting_distance[0] + lift_second_distance[0]
        - retract_second_distance[0];
if (v <= 0.f) v = lifting_distance[0] + lift_second_distance[0];
```

同樣邏輯也用於 `prz_layer_content()` 的每層 RetractDist 欄位。

---

### 3-5 新增檔案：RasterToCvMat.hpp / RasterToCvMat.cpp

**路徑**：`src/libslic3r/SLA/RasterToCvMat.hpp` / `RasterToCvMat.cpp`

PRZ 像素資料的來源是 `SLAPrint::m_layer_images`（`vector<cv::Mat>`），由 `rasterize()` 呼叫 `sla::expolygons_layers_to_cvmat()` 產生。

#### 函式介面

```cpp
// 單層：將 ExPolygons 光柵化為 CV_8UC1 的 cv::Mat
cv::Mat expolygons_to_cvmat(
    const ExPolygons        &polys,
    const Resolution        &res,
    const PixelDim          &pxdim,
    const RasterBase::Trafo &trafo = {},
    double                   gamma = 1.0);

// 多層：TBB 並行對 vector<ExPolygons> 各層呼叫上函式
std::vector<cv::Mat> expolygons_layers_to_cvmat(
    const std::vector<ExPolygons> &layer_polys,
    const Resolution              &res,
    const PixelDim                &pxdim,
    const RasterBase::Trafo       &trafo = {},
    double                         gamma = 1.0);
```

#### expolygons_to_cvmat() 實作流程

1. 呼叫 `create_raster_grayscale_aa(res, pxdim, gamma, trafo)` 建立 `RasterBase`
2. `for each ExPolygon: raster->draw(poly)`
3. 以 passthrough encoder 取出原始像素 buffer（`EncodedRaster`，不做 PNG 壓縮）
4. 包成 `cv::Mat(height_px, width_px, CV_8UC1, data).clone()`

#### 輸出 cv::Mat 格式

```cpp
cv::Mat mat(int(res.height_px), int(res.width_px), CV_8UC1, data);
return mat.clone();
```

- **型別**：`CV_8UC1`（8-bit 單通道灰階）
- **rows = `res.height_px`**、**cols = `res.width_px`**
- 黑色背景（0）、白色前景（255）、左上角原點
- row-major 連續記憶體：`img.data[row * cols + col]`

---

### 3-6 SLAPrint.hpp — 新增 m_layer_images 成員與存取器

在 `SLAPrint` class 中新增：

```cpp
// public accessor
const std::vector<cv::Mat>& layer_images() const { return m_layer_images; }

// private member
// cv::Mat images generated per layer after slicing (CV_8UC1, grayscale).
// Populated at the end of slapsRasterize step.
std::vector<cv::Mat> m_layer_images;
```

`m_layer_images` 在 `SLAPrintSteps.cpp::rasterize()` 結尾填入，之後由 `generate_prz()` 透過 `print.layer_images()` 讀取。

---

### 3-7 SLAPrintSteps.cpp — rasterize() 修改

#### 新增 include

```cpp
#include <libslic3r/SLA/RasterToCvMat.hpp>
#define PHROZEN_PRZ_TEST_EXPORT 0
#ifdef PHROZEN_PRZ_TEST_EXPORT
#include <libslic3r/Format/PhrozenPRZ.hpp>
#include <fstream>
#endif
```

#### rasterize() 結尾填入 m_layer_images

```cpp
m_print->m_layer_images =
    sla::expolygons_layers_to_cvmat(all_layers, res, pxdim, trafo, gamma);
```

`all_layers` 是 `vector<ExPolygons>`，由 rasterize() 內的 for-loop 從 `m_printer_input` 各層的 `transformed_slices()` 合併而來。

#### Portrait 方向對調

`rasterize()` 在建立 `Resolution` 之前處理 Portrait 對調：

```cpp
double w  = cfg.display_width.getFloat();   // 物理螢幕 X（短邊，mm）
double h  = cfg.display_height.getFloat();  // 物理螢幕 Y（長邊，mm）
size_t pw = cfg.display_pixels_x.getInt();  // 像素 X（短邊）
size_t ph = cfg.display_pixels_y.getInt();  // 像素 Y（長邊）

if (orient == sla::RasterBase::roPortrait) { std::swap(w, h); std::swap(pw, ph); }

sla::Resolution res{pw, ph};
// Portrait 後：
//   res.width_px  = display_pixels_y（長邊）→ cv::Mat.cols
//   res.height_px = display_pixels_x（短邊）→ cv::Mat.rows
```

---

### 3-8 Portrait 模式造成的 X/Y 對調問題

PhrozenOrca 的 Phrozen 機器預設使用 Portrait 方向，`rasterize()` 執行 `std::swap` 後：

- **cv::Mat.cols = display_pixels_y**（物理螢幕的長邊像素數）
- **cv::Mat.rows = display_pixels_x**（物理螢幕的短邊像素數）

PRZ 的 XResolution 對應 cv::Mat 的水平方向（cols），因此必須填 `display_pixels_y`，而非 `display_pixels_x`。`PlatformX/YLength` 同理：

| PRZ 欄位 | 實際填入 | 原因 |
|---|---|---|
| XResolution | `display_pixels_y` | Portrait swap 後 cols = display_pixels_y |
| YResolution | `display_pixels_x` | Portrait swap 後 rows = display_pixels_x |
| PlatformXLength | `display_height` | 對應長邊 mm 尺寸 |
| PlatformYLength | `display_width` | 對應短邊 mm 尺寸 |

> **結論**：PRZ header 的解析度/尺寸欄位必須對應 cv::Mat 的實際儲存方向（Portrait swap 後的值），而非 config 欄位的原始命名。

---

### 3-9 encode_pixels 的處理

Mechado 預先計算 `vector<pair<int,uchar>>`（RLE 結果）再輸出；PhrozenOrca 改為在 `generate_prz()` 內**直接掃描 cv::Mat.data**（一維連續記憶體，row-major），省去中介 vector 的記憶體佔用與轉換時間：

```cpp
const uchar *data = img.data;
const int total   = img.rows * img.cols;
uchar cur = data[0]; int count = 1;
for (int i = 1; i < total; ++i) {
    uchar px = data[i];
    if (px == cur) { ++count; }
    else { flush_run(cur, count); cur = px; count = 1; }
}
flush_run(cur, count);
```

---

### 3-10 過渡層曝光時間插值

底層（`layerId < bottom_layer_count`）使用 `bottom_exposure_time`，一般層使用 `exposure_time`，兩者之間的過渡層線性內插：

```
expTime = bt + (nt - bt) / (1 + transition_layer_count) * (layerId - bottom + 1)
```

---

### 3-11 CMakeLists.txt 修改

在 `src/libslic3r/CMakeLists.txt` 的 `libslic3r_sources` 清單中加入：

```cmake
Format/PhrozenPRZ.hpp
Format/PhrozenPRZ.cpp
SLA/RasterToCvMat.hpp
SLA/RasterToCvMat.cpp
```

---

## 四、測試程式（暫時性）

在 `SLAPrintSteps.cpp::rasterize()` 結尾加入條件編譯的測試輸出，以驗證 `generate_prz()` 輸出正確性：

```cpp
// 頂端（#include 區域）
#define PHROZEN_PRZ_TEST_EXPORT 0
#ifdef PHROZEN_PRZ_TEST_EXPORT
#include <libslic3r/Format/PhrozenPRZ.hpp>
#include <fstream>
#endif
```

```cpp
// rasterize() 末尾
#ifdef PHROZEN_PRZ_TEST_EXPORT
// [TEST] 輸出 PRZ 檔案至桌面，驗證 generate_prz 正確性
{
    std::string prz_data = Slic3r::generate_prz(*m_print);
    std::string out_path = std::string(getenv("USERPROFILE") ? getenv("USERPROFILE") : ".")
                         + "/Desktop/test_output.prz";
    std::ofstream ofs(out_path, std::ios::binary);
    ofs.write(prz_data.data(), static_cast<std::streamsize>(prz_data.size()));
}
#endif
```

- 將 `#define PHROZEN_PRZ_TEST_EXPORT 0` 改為 `1` 可啟用，不影響正式編譯
- 測試成功後此段程式碼將被移除

---

## 五、驗證方式

1. Build：`build_release_vs2022.bat slicer`
2. 將 `PHROZEN_PRZ_TEST_EXPORT` 改為 `1`，以 SLA 模式切片任一模型，桌面應產生 `test_output.prz`
3. 以 Hex editor 確認：
   - offset 0: `56 33 2E 30`（"V3.0"）
   - offset 4: `07 00 00 00 44 4C 50 00`（DLP Tag）
   - XResolution / YResolution 與 printer profile 一致（注意 Portrait 對調）
4. 以 Phrozen 官方切片器（Mechado）開啟同一模型產生 PRZ，比對 header 欄位數值
5. 確認每層 RLE 資料：
   - 全黑層起頭應為 `55 0x（count）...`，base = `0x00`
   - 全白層 base = `0xc0`
   - 灰階像素 base = `0x40` + 額外色值 byte

---

## 六、後續待辦

- [ ] 移除測試用 `#define PHROZEN_PRZ_TEST_EXPORT` 及相關程式碼
- [ ] 整合進正式匯出流程（`BackgroundSlicingProcess::process_sla()` 或新增匯出選項）
- [x] Preview 116×116 / 290×290 填入實際縮圖（由 `preview_image_path` 指定目錄讀取 PNG，fallback 全 0）
- [x] Printer Name / Profile Name 欄位從 `printer_settings_id` / `sla_print_settings_id` 取值
- [x] Printer Type 欄位從 `printer_model` 取值
- [x] TotalVolume / TotalWeight / TotalPrice 從 `SLAPrintStatistics` 取值（不再輸出全 0）
- [ ] `preview_image_path` 加入 printer profile JSON 範本（Phrozen 機器）
- [ ] 驗證 `printer_settings_id` 在匯出時是否已正確填入 profile 名稱
