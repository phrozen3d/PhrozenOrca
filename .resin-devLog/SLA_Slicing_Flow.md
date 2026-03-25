# PhrozenOrca SLA 切片完整流程

> 對應版本：PhrozenOrca（OrcaSlicer 分支）
> 分析日期：2026-03-05

---

## 一、整體架構

```
User（按下切片）
    │
    ▼
Plater::priv::on_action_slice_plate / on_action_slice_all
    │  schedule_background_process()
    ▼
BackgroundSlicingProcess        ← 背景執行緒
    │  call_process() → process_sla()
    ▼
SLAPrint::process()             ← 核心切片 FSM
    │
    ├─ Per-Object Steps (level1 → level2)
    │       └─ SLAPrint::Steps
    │
    └─ Print Steps
            └─ SLAPrint::Steps
```

---

## 二、UI 觸發層

| 事件 | 處理函數 | 位置 |
|---|---|---|
| 使用者按「切片目前板」 | `on_action_slice_plate()` | Plater.cpp |
| 使用者按「切片全部板」 | `on_action_slice_all()` | Plater.cpp |
| 計時器 / 組態變更 | `schedule_background_process()` | Plater.cpp:5459 |
| EVT_SCHEDULE_BACKGROUND_PROCESS | `schedule_background_process()` | Plater.cpp:3191 |

`schedule_background_process()` 透過 **0.5 秒防抖 timer** 最終呼叫
`BackgroundSlicingProcess::start()`，啟動背景執行緒。

---

## 三、BackgroundSlicingProcess 層

**檔案**：[BackgroundSlicingProcess.cpp](../src/slic3r/GUI/BackgroundSlicingProcess.cpp)

### 關鍵流程

```
thread_proc()
    └─ call_process()
            └─ ptSLA → process_sla()
                    ├─ m_print->process()         ← SLAPrint 核心切片
                    └─ [export_path 非空時]
                            m_sla_archive.export_print(zipper, *m_sla_print)
                            write_thumbnail(zipper, data)
                            zipper.finalize()
```

### 參數注入時機（apply）

```
BackgroundSlicingProcess::apply(model, config)
    ├─ new_config = config + m_current_plate->config()    ← 合併 PartPlate 組態
    └─ m_print->apply(model, new_config)                  ← 傳入 SLAPrint
```

> `apply()` 在 **每次組態改變或模型改變** 時由 UI 執行緒呼叫（非背景執行緒）。
> `process()` 才在背景執行緒執行實際切片。

---

## 四、SLAPrint 參數分層

**檔案**：[SLAPrint.hpp](../src/libslic3r/SLAPrint.hpp)、[SLAPrint.cpp](../src/libslic3r/SLAPrint.cpp)

`SLAPrint::apply()` 將一個扁平的 `DynamicPrintConfig` 拆解為 4 個子組態：

| 子組態類型 | 成員 | 說明 |
|---|---|---|
| `SLAPrintConfig` | `m_print_config` | Print-level 設定（目前 SLA 很少用） |
| `SLAPrinterConfig` | `m_printer_config` | 機器設定：display_width, display_height, area_fill, pixel_size 等 |
| `SLAMaterialConfig` | `m_material_config` | 材料設定：exposure_time, initial_exposure_time, initial_layer_height 等 |
| `SLAPrintObjectConfig` | `m_default_object_config` | 物件設定（支撐、中空等預設值） |

各物件的 `SLAPrintObject::m_config` 從 `m_default_object_config` + 物件本身的 override 合併而來。

---

## 五、SLAPrint::process() — 核心切片 FSM

**檔案**：[SLAPrint.cpp:745](../src/libslic3r/SLAPrint.cpp#L745)

執行順序：

```
SLAPrint::process()
    │
    ├─ [Level 1 — 每個物件依序執行]
    │   ├─ slaposHollowing         hollow_model()
    │   ├─ slaposDrillHoles        drill_holes()
    │   ├─ slaposObjectSlice       slice_model()       ← 產生 2D 輪廓
    │   ├─ slaposSupportPoints     support_points()    ← 計算支撐點
    │   ├─ slaposSupportTree       support_tree()      ← 建立支撐幾何
    │   └─ slaposPad               generate_pad()      ← 建立底座
    │
    ├─ [Level 2 — 每個物件依序執行]
    │   └─ slaposSliceSupports     slice_supports()    ← 切支撐幾何
    │
    └─ [Print Level Steps]
        ├─ slapsMergeSlicesAndEval merge_slices_and_eval_stats()  ← 合併 + 統計
        └─ slapsRasterize          rasterize()                    ← 產生 2D 影像
```

---

## 六、各步驟詳細說明

### Step 1：hollow_model（中空化）
- **類別**：`SLAPrint::Steps`
- **輸入**：原始 mesh
- **輸出**：`SLAPrintObject::m_hollowing_data`（包含 interior mesh）
- **參數來源**：`po.m_config`（SLAPrintObjectConfig）中的 `hollowing_*` 參數

### Step 2：drill_holes（鑽孔）
- 在中空化 mesh 上鑽排水孔
- **輸出**：`m_hollowing_data->hollow_mesh_with_holes`

### Step 3：slice_model（切模型）
**檔案**：[SLAPrintSteps.cpp:494](../src/libslic3r/SLAPrintSteps.cpp#L494)

- **輸入**：`po.get_mesh_to_slice()`（含中空/鑽孔後的 mesh）
- **核心作業**：
  1. 建立層高索引 `po.m_slice_index`（以 `initial_layer_height` + `layer_height` 為間隔）
  2. `slice_mesh_ex()` → 產生每層 `ExPolygons`，存入 `po.m_model_slices`
  3. 若有 interior mesh，以 `diff_ex()` 從每層切掉內部
  4. 呼叫 `apply_printer_corrections()`（absolute_correction, elefant_foot_compensation）
  5. 呼叫 `prepare_for_generate_supports()`（預計算支撐生成資料）
- **輸出**：`po.m_model_slices`（`vector<ExPolygons>`）、`po.m_slice_index`、`po.m_support_point_generator_data`
- **關鍵參數**：`layer_height`, `initial_layer_height`, `slice_closing_radius`

### Step 4：support_points（支撐點生成）
**檔案**：[SLAPrintSteps.cpp:610](../src/libslic3r/SLAPrintSteps.cpp#L610)

- **條件**：`supports_enable == true` 且 `sla_points_status != UserModified`
- **算法**（Phase 4 已更新為 PrusaSlicer 新算法）：
  1. **Phase 1** `sla::generate_support_points()` — Voronoi Medial Axis + NearPoints KD-tree，依懸空幾何取樣，無角度過濾
  2. **Phase 2** `sla::move_on_mesh_surface()` — 將 2D 層平面的點投影到 3D mesh 表面
  3. **Phase 3** Overhang angle filter（2026-03-25 新增）— 使用 `sla::normals()` 計算表面法線，以 `support_critical_angle` 為閾值過濾不符合懸空條件的點，確保 UI 顯示的點與 `SupportTreeBuildsteps::filterfn` 的條件一致（`polar >= π/2 + critical_angle`）。`critical_angle = 90°` 時 guard 短路，不執行過濾。
- **輸出**：`po.m_supportdata->pts`（`vector<sla::SupportPoint>`）
- **關鍵參數**：`support_points_density_relative`, `support_head_front_diameter`, `support_critical_angle`

若為使用者手動放置的點（`UserModified`），直接複製 `mo.sla_support_points` 到後端，不重新計算，Phase 3 不執行。

### Step 5：support_tree（支撐樹建立）
**檔案**：[SLAPrintSteps.cpp:694](../src/libslic3r/SLAPrintSteps.cpp#L694)

- 根據 `support_tree_type`（Default / Branching）選擇支撐策略
- `po.m_supportdata->create_support_tree(ctl)` → `sla::SupportTree::create()`
- **輸出**：`po.m_supportdata->support_tree_ptr`、`tree_mesh`（`TriangleMesh`）
- **關鍵參數**（Default 策略）：
  - `support_pillar_diameter`, `support_head_front_diameter`
  - `support_head_width`, `support_head_penetration`
  - `support_critical_angle`, `support_max_bridge_length`
  - `support_pillar_widening_factor`（PhrozenOrca 預設 0.0 → 等徑圓柱）

### Step 6：generate_pad（底座生成）
- 從 `pad_*` 參數建立 `sla::PadConfig`
- `po.m_supportdata->create_pad(bp, pcfg)` → 加入支撐樹底部
- **輸出**：`pad_mesh`

### Step 7：slice_supports（切支撐）
**檔案**：[SLAPrintSteps.cpp:790](../src/libslic3r/SLAPrintSteps.cpp#L790)

- `support_tree_ptr->slice(heights, ...)` → 依照與模型相同的層高網格切支撐 mesh
- **輸出**：`po.m_supportdata->support_slices`（`vector<ExPolygons>`）
- 完成後發送 `RELOAD_SLA_PREVIEW`，觸發 3D 預覽更新

### Step 8：merge_slices_and_eval_stats（合併 + 統計）
**檔案**：[SLAPrintSteps.cpp:930](../src/libslic3r/SLAPrintSteps.cpp#L930)

- `initialize_printer_input()` — 將所有物件所有 instance 的切片依 Z 高度合併到 `m_printer_input`（`vector<PrintLayer>`）
- 每個 PrintLayer：
  - 合併模型 + 支撐輪廓（`union_ex`、`diff_ex`）
  - 計算材料體積、列印時間估算
  - 存入 `layer.transformed_slices()`（供光柵化使用）
- **關鍵參數**：
  - `area_fill`（PhrozenOrca 預設 50%，PrusaSlicer 35%）→ 決定 fast/slow layer
  - `fast_tilt_time`, `slow_tilt_time`
  - `initial_exposure_time`, `exposure_time`
- **輸出**：`m_printer_input`（`vector<PrintLayer>`）、`m_print_statistics`

### Step 9：rasterize（光柵化 → 產生 2D 影像）
**檔案**：[SLAPrintSteps.cpp:1109](../src/libslic3r/SLAPrintSteps.cpp#L1109)

此步驟內部有**兩條平行 pipeline**：

**Pipeline 1 — SL1 PNG 匯出**（TBB 並行）：
```
for each PrintLayer:
    poly.translate(raster_shift)    ← 床座標 → 顯示器座標（2026-03-20 新增）
    raster->draw(poly)              ← AGGRaster 繪製
    raster->encode()                ← 編碼為 EncodedRaster（PNG）
```
- **輸出**：`m_sla_archive.m_layers`（`vector<sla::EncodedRaster>`）— SL1 ZIP 內的 PNG 資料
- **由 `SLAArchiveWriter::draw_layers()`** 負責，`SL1Archive::create_raster()` 建立每層的 `RasterBase`

**Pipeline 2 — cv::Mat 影像**（PRZ 與預覽用）：
```
polys = layer.transformed_slices()
for each poly: poly.translate(display_center - bed_center)
m_layer_images = expolygons_layers_to_cvmat(all_layers, ...)
```
- **輸出**：`SLAPrint::m_layer_images`（`vector<cv::Mat>`）— PRZ 格式及後處理使用

> **置中邏輯**：`raster_shift = display_center − bed_center`，確保模型在床上的位置正確對應到顯示器中心。兩條 pipeline 自 2026-03-20 起使用相同的 shift 計算，PRZ 因為只讀 Pipeline 2 的結果，不受 Pipeline 1 的修改影響。

- **關鍵參數**：`display_width`, `display_height`（決定解析度）、`printable_area`（決定 bed_center）

---

## 七、切片結果儲存位置

| 資料 | 儲存位置 | 型別 |
|---|---|---|
| 模型 2D 輪廓（每層） | `SLAPrintObject::m_model_slices` | `vector<ExPolygons>` |
| 支撐 2D 輪廓（每層） | `SLAPrintObject::SupportData::support_slices` | `vector<ExPolygons>` |
| 層索引（Z 高度對應） | `SLAPrintObject::m_slice_index` | `vector<SliceRecord>` |
| 合併後每層輪廓（含支撐） | `SLAPrint::m_printer_input` | `vector<PrintLayer>` |
| 支撐點 | `SLAPrintObject::SupportData::pts` | `vector<sla::SupportPoint>` |
| 支撐 3D mesh | `SLAPrintObject::SupportData::tree_mesh` | `TriangleMesh` |
| 底座 3D mesh | `SLAPrintObject::SupportData::pad_mesh` | `TriangleMesh` |
| 光柵化 2D 影像（記憶體） | `SLAArchiveWriter::m_layers` | `vector<sla::EncodedRaster>` |
| 列印統計 | `SLAPrint::m_print_statistics` | `SLAPrintStatistics` |

**注意**：`m_layers` 中的影像在切片完成後留在記憶體，直到 export 時才寫入磁碟。

---

## 八、2D 影像產生與輸出

### 是否有 2D image 產生？

**是**。在 `slapsRasterize` 步驟產生，但**僅在 export 時才寫入磁碟**。

### 輸出流程

```
process_sla()
    └─ [export_path 非空] → m_sla_archive.export_print(zipper, *m_sla_print)
            ├─ 寫入 config.ini（列印參數）
            ├─ 逐層寫入 PNG 影像
            │       {project}00000.png, {project}00001.png, ...
            └─ [可選] write_thumbnail() → thumbnail/thumbnail{W}x{H}.png
```

**最終格式**：ZIP 壓縮檔（.sl1），包含：
- `config.ini` — 列印參數
- `{project}00000.png` ~ `{project}NNNNN.png` — 每層 2D 曝光影像（PNG，灰階）
- `thumbnail/thumbnail*.png` — 預覽縮圖

### 關鍵類別

| 類別 | 角色 |
|---|---|
| `SLAArchiveWriter`（抽象） | 定義介面：`draw_layers()`, `export_print()` |
| `SL1Archive`（實作） | 具體 SL1/PW0 格式，`create_raster()` 建立 `RasterBase` |
| `sla::RasterBase` | 2D 畫布，支援 `draw(ExPolygon)` |
| `sla::EncodedRaster` | 編碼後的 2D 影像（含 PNG 資料） |
| `Zipper` | ZIP 寫入工具 |

---

## 九、參數引入的關鍵時機

| 時機 | 函數 | 參數來源 |
|---|---|---|
| 使用者切換 Profile / 修改設定 | `BackgroundSlicingProcess::apply()` → `SLAPrint::apply()` | `DynamicPrintConfig` (preset bundle) + PartPlate config |
| 機器設定注入光柵化器 | `SLAPrint::apply()` 中 `m_printer->apply(m_printer_config)` | `SLAPrinterConfig` |
| 層高計算 | `Steps::slice_model()` 中 `m_material_config.initial_layer_height` | `SLAMaterialConfig` |
| 支撐參數 | `make_support_cfg(po.m_config)` in `Steps::support_tree()` | `SLAPrintObjectConfig` |
| 底座參數 | `make_pad_cfg(po.m_config)` in `Steps::generate_pad()` | `SLAPrintObjectConfig` |
| 曝光時間計算 | `merge_slices_and_eval_stats()` | `SLAMaterialConfig` |
| 顯示尺寸（影像大小） | `rasterize()` 中 `create_raster()` | `SLAPrinterConfig` |

---

## 十、增量重算（Step Invalidation）

PhrozenOrca 採用 **Milestone / Step 狀態機**：

- 每個 Step 有 `set_started` / `set_done` / `invalidate` 狀態
- 當特定參數改變時，`SLAPrint::apply()` 的 `invalidate_state_by_config_options()` 只失效受影響的 Step，後續步驟自動重算
- 例如：只改曝光時間 → 只重算 `slapsMergeSlicesAndEval` 和 `slapsRasterize`，不重切 mesh

---

## 十一、相關檔案索引

| 檔案 | 主要職責 |
|---|---|
| [Plater.cpp](../src/slic3r/GUI/Plater.cpp) | UI 觸發、schedule_background_process |
| [BackgroundSlicingProcess.cpp](../src/slic3r/GUI/BackgroundSlicingProcess.cpp) | 背景執行緒、apply/process 橋接、export |
| [SLAPrint.hpp](../src/libslic3r/SLAPrint.hpp) | SLAPrintObject, SLAPrint, PrintLayer 宣告 |
| [SLAPrint.cpp](../src/libslic3r/SLAPrint.cpp) | apply(), process(), make_support_cfg() |
| [SLAPrintSteps.hpp](../src/libslic3r/SLAPrintSteps.hpp) | Steps class 宣告 |
| [SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp) | 所有切片步驟實作 |
| [Format/SLAArchiveWriter.hpp](../src/libslic3r/Format/SLAArchiveWriter.hpp) | Archive 抽象介面 |
| [Format/SL1.hpp](../src/libslic3r/Format/SL1.hpp) | SL1Archive 宣告 |
| [Format/SL1.cpp](../src/libslic3r/Format/SL1.cpp) | SL1 ZIP + PNG 輸出實作 |
| [SLA/RasterBase.hpp](../src/libslic3r/SLA/RasterBase.hpp) | 2D 畫布介面 |
