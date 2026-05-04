## Why

目前 Island Detection 使用與正式列印相同的 layer height（SLA 典型值 0.05mm）進行偵測，無法調整偵測的粗細程度。使用者無法在「快速看大概」與「精確偵測所有細小孤島」之間取捨，也無法透過較粗的偵測來取得更大的 island 覆蓋範圍以產生更多支撐點。

## What Changes

- 新增 Island Detection 偵測等級（低/中/高），由 `GLGizmoLcdOverhangDetection` 的現有 `DetectionAccuracy` enum 驅動
- 各等級以**不同的 layer height** 對相同 CSG mesh 重新切片，產生專屬的偵測用切片資料
  - 高（High）：0.05mm — 精確，偵測所有細小孤島
  - 中（Medium）：0.1mm — 平衡，適合一般使用
  - 低（Low）：0.5mm — 粗略，偵測較大的孤島範圍，產生更多支撐點
- 偵測用切片資料為暫時性資料，用後即棄，**完全不寫入 `po.m_model_slices`**
- `prepare_for_generate_supports()` 與 auto generate support points 的流程不受影響

## Capabilities

### New Capabilities

- `sla-island-detection-accuracy`: Island Detection 支援低/中/高三種精度等級，各等級使用不同 layer height 獨立重新切片偵測孤島，不影響正式切片

### Modified Capabilities

- `sla-island-contour-overlay`: Island overlay 顯示的資料來源改為依精度等級產生的偵測結果（原有顯示規格不變，僅資料來源精度不同）

## Impact

| 檔案 | 修改內容 |
|------|---------|
| `SLAPrint.hpp` | 新增 `SLAPrint::redetect_islands(ObjectID, float)` public 宣告 |
| `SLAPrint.cpp` | 實作 `redetect_islands()`（建立 Steps 並委派給 `Steps::prepare_island_detection()`） |
| `SLAPrintSteps.hpp` | 新增 `Steps::prepare_island_detection(SLAPrintObject&, float)` 宣告 |
| `SLAPrintSteps.cpp` | 實作 `Steps::prepare_island_detection()` 及 `build_detection_height_levels()` helper |
| `GLGizmoLcdOverhangDetection.cpp` | `sync_island_data_for_object()` 改呼叫 `plater()->sla_print().redetect_islands()` |

**不修改**：`Plater.hpp/.cpp`、`SupportPointGenerator.hpp/.cpp`

**Auto Generate Support Points**：完全不受影響（仍使用 `po.m_support_point_generator_data`，即原始切片資料）
