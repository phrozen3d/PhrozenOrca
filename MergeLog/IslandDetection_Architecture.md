# Island Detection 架構說明

## 功能概述

Island Detection（孤島偵測）是針對 SLA 切片模型的品質分析功能。
它透過逐層比對切片影像，找出「懸空在空中、與下一層沒有接觸」的獨立區域（即 Island），
這些區域在列印時會因為缺乏支撐而失敗。

---

## 觸發入口

**位置**：`src/slic3r/GUI/Gizmos/GLGizmoLcdOverhangDetection.cpp`

使用者在 Overhang Detection Gizmo 的 UI 面板按下 **「Detect selected」** 按鈕後觸發：

```cpp
// on_render_input_window() 內
if (m_imgui->button(m_desc.at("detect_selected"))) {
    Island_Detection();   // ← 呼叫入口
}
```

---

## 執行流程

```
Detect selected 按鈕
        │
        ▼
GLGizmoLcdOverhangDetection::Island_Detection()
        │
        ├─ 1. 取得 SLAPrintObject（需已完成 slaposSliceModel 步驟）
        │
        ├─ 2. 透過 po->get_slice_index() 取得所有切層 SliceRecord
        │      └─ 對每層呼叫 sr.get_slice(soModel) → ExPolygons
        │
        ├─ 3. 從 printer preset 讀取顯示參數
        │      ├─ display_width / display_height（物理尺寸，mm）
        │      └─ display_pixels_x / display_pixels_y（解析度，px）
        │
        ├─ 4. 從 sla_print preset 讀取 layer_height（mm）
        │
        ├─ 5. 從 ModelObject::raw_bounding_box() 取得模型包圍盒
        │
        ├─ 6. 建立 island::DetectionConfig
        │
        ├─ 7. 光柵化（Rasterize）每一層 ExPolygons → cv::Mat（CV_8UC1）
        │      └─ 座標轉換（world mm → pixel）為 contour_to_world() 的反函式：
        │           col = (world_y - center_y + display_w/2) / display_w * img_w
        │           row = (display_h/2 - (world_x - center_x)) / display_h * img_h
        │
        ├─ 8. 呼叫 island::detect_islands(layer_images, config)
        │
        └─ 9. 結果寫入 m_detected_islands，更新 m_total_overhang_areas
```

---

## 新增 / 修改的檔案

### 新增：第三方函式庫 Clipper2

| 路徑 | 說明 |
|------|------|
| `deps_src/clipper2/CMakeLists.txt` | Clipper2 建置設定 |
| `deps_src/clipper2/include/clipper2/` | Clipper2 標頭檔（7 個 .h） |
| `deps_src/clipper2/src/` | Clipper2 實作（3 個 .cpp） |

Clipper2 用於輪廓 offset（外擴/內縮），在 `Island_Detector.cpp` 的 `offset_contour()` 中使用。
當 `DetectionConfig::offset_mm == 0.0f` 時不會觸發 Clipper2 程式碼路徑。

### 新增：Island Detector 核心演算法

| 路徑 | 說明 |
|------|------|
| `src/libslic3r/IslandDetection/Island_Detector.hpp` | API 宣告（namespace `Slic3r::island`） |
| `src/libslic3r/IslandDetection/Island_Detector.cpp` | 演算法實作 |
| `src/libslic3r/IslandDetection/main.cpp` | 獨立測試工具（CLI，不含於 slicer 建置） |

**核心資料結構**：
```cpp
namespace Slic3r::island {

struct Island {
    int label;                    // 全域 ID（0..N-1，依 Z 排序）
    std::vector<Point2f> contour; // 世界座標輪廓（mm）
    float z;                      // 世界座標 Z（mm）
};

struct DetectionConfig {
    float display_width;   // 顯示器物理寬度（mm）
    float display_height;  // 顯示器物理高度（mm）
    float layer_height;    // 層高（mm）
    BBox3D model_bbox;     // 模型包圍盒（世界座標）
    float offset_mm;       // 輪廓外擴量（0 = 不外擴）
};

std::vector<Island> detect_islands(
    const std::vector<cv::Mat>& layer_images,
    const DetectionConfig& config);

}  // namespace Slic3r::island
```

**演算法邏輯**（`detect_islands()`）：
1. 對每一層影像二值化（threshold = 127）
2. 對當前層做連通元件分析（OpenCV `connectedComponentsWithStats`）
3. 對每個元件，檢查它在下一層的 bounding box 範圍內是否有重疊像素
4. 若**沒有重疊** → 確認為 Island
5. 提取輪廓（`findContours`），轉換回世界座標（`contour_to_world()`）
6. 可選：用 Clipper2 對輪廓做外擴（`offset_contour()`）
7. 所有 Island 依 Z 排序後回傳

### 修改：CMakeLists.txt

| 路徑 | 修改內容 |
|------|---------|
| `deps_src/CMakeLists.txt` | 加入 `add_subdirectory(clipper2)` |
| `src/libslic3r/CMakeLists.txt` | 加入 Island_Detector 兩個檔到來源清單；加入 `Clipper2` 到 `target_link_libraries` |

### 修改：GLGizmoLcdOverhangDetection

| 路徑 | 修改內容 |
|------|---------|
| `GLGizmoLcdOverhangDetection.hpp` | include `Island_Detector.hpp`；宣告 `Island_Detection()` 和 `m_detected_islands` |
| `GLGizmoLcdOverhangDetection.cpp` | include `SLAPrint.hpp`, `PresetBundle.hpp`, `opencv2/imgproc.hpp`；實作 `Island_Detection()`；串接 Detect selected 按鈕 |

---

## 依賴關係

```
libslic3r_gui (GLGizmoLcdOverhangDetection)
    └── libslic3r (Island_Detector)
            ├── opencv_world  （連通元件分析、影像光柵化）
            └── Clipper2      （輪廓 offset，可選）
```

---

## 注意事項

1. **需要先切片**：`Island_Detection()` 呼叫時，模型必須已完成 `slaposSliceModel` 步驟，否則 `get_slice_index()` 回傳空陣列並提前返回。

2. **座標系**：SLA 顯示器的 UV 座標系與世界座標系有 90° 旋轉關係（texture plane rotation = π/2），光柵化時的 world→pixel 轉換必須與 `contour_to_world()` 的 pixel→world 轉換互為反函式。

3. **`main.cpp` 不納入 slicer 建置**：`IslandDetection/main.cpp` 是獨立 CLI 測試工具，目前 CMakeLists.txt 不包含它（僅有 hpp + cpp）。

4. **`m_total_overhang_areas`**：偵測完成後更新為 Island 總數，`m_current_overhang_area_index` 重設為 0，供 UI 導覽用。
