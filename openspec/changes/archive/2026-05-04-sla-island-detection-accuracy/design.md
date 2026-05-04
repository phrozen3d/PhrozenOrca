## Context

SLA island detection 目前在 `slice_model()` 末段呼叫 `prepare_for_generate_supports()`，它以 `po.m_model_slices`（正式切片，layer height = 列印設定值，SLA 典型 0.05mm）建立 `SupportPointGeneratorData`，再從中提取 `prev_parts.empty()` 的孤島輪廓存入 `po.m_island_contours`。

由於偵測解析度固定等於列印 layer height，使用者無法控制偵測粗細：
- 較大的 layer height（如 0.5mm）：跨越 0.5mm Z 間距的孤島更容易因截面不重疊而被判定為孤島，同一幾何特徵在較高 Z 截面的面積可能更大 → 較大 island 範圍 → 支撐點更多
- 較小的 layer height（如 0.05mm，現有值）：只有真正懸浮的細小區域才算孤島 → 精確但面積偏小

`prepare_generator_data()` 的簽章已接受任意 `slices` + `heights` 輸入，因此可以用相同的 CSG mesh（`po.m_mesh_to_slice`）以不同的 Z 網格重新切片，而不碰任何正式資料。

```
目前：
  po.m_mesh_to_slice
    → slice_csgmesh_ex(m_model_height_levels)  → po.m_model_slices      ← 正式切片
    → prepare_generator_data(slices_copy, m_model_height_levels)
        → po.m_support_point_generator_data                              ← auto support 用
        → extract islands → po.m_island_contours                        ← island detection 用

新架構：
  po.m_mesh_to_slice
    → slice_csgmesh_ex(m_model_height_levels)  → po.m_model_slices      ← 不動
    → prepare_generator_data(slices_copy, m_model_height_levels)
        → po.m_support_point_generator_data                              ← 不動

    ＋ [NEW] 依 DetectionAccuracy 選擇 detect_lh（0.05/0.1/0.5mm）
    → build_detect_heights(bb3d, detect_lh)    → detect_heights         ← 暫時
    → slice_csgmesh_ex(detect_heights)         → detect_slices          ← 暫時
    → prepare_generator_data(detect_slices, detect_heights)
        → temp_data（不寫入 po）
        → extract islands → po.m_island_contours                        ← 覆蓋
    → temp_data、detect_slices 生命週期結束
```

## Goals / Non-Goals

**Goals:**
- 低/中/高三種精度等級分別使用 0.5mm/0.1mm/0.05mm 作為 island detection 的 layer height
- 切換等級後重新觸發 island 偵測（使用者按下 Detect Selected）
- 偵測結果（`po.m_island_contours`）是唯一寫出的資料
- `po.m_model_slices`、`po.m_support_point_generator_data`、auto generate support points 完全不受影響

**Non-Goals:**
- 不在後台自動重偵測（使用者主動操作才觸發）
- 不持久化偵測 layer height 設定（session-only，gizmo 狀態）
- 不修改 `SupportPointGenerator` 的演算法
- 不影響切片輸出（SL1/ZIP）

## Decisions

### D1：偵測用切片在哪裡執行？

**決策**：邏輯放在 `SLAPrint::Steps::prepare_island_detection()`（Steps 已是 SLAPrintObject 的 friend，可直接存取 `m_mesh_to_slice`），並透過 `SLAPrint::redetect_islands(ObjectID, float)` 公開入口讓 Gizmo 呼叫。

**放棄的替代方案**：
- Pipeline step（新增 SLAPrint step）→ 需要 invalidation 邏輯與 step 依賴管理，island contour 是 UI-only 資料，不應進入 pipeline 狀態機
- `SLAPrintObject` public method → `SLAPrintObject` 本身無法呼叫 `throw_if_canceled()`，需額外傳入 cancel callback；且 slice 邏輯放在 object class 不符合現有職責劃分

**採用方案**：
```
Steps::prepare_island_detection(po, detect_lh)   ← 實作邏輯（Steps 有 friend 存取權）
SLAPrint::redetect_islands(ObjectID, float)       ← 公開入口（在 SLAPrint.cpp 實例化 Steps 並呼叫）
Gizmo: plater()->sla_print().redetect_islands(mo->id(), detect_lh)  ← 呼叫端
```

此方案與現有的「Gizmo 直接讀取 `sla_print()`，寫入透過公開方法」模式一致（全 SLA gizmo 均如此，無需 Plater wrapper）。

### D2：暫時切片資料的生命週期

**決策**：`detect_slices` 與 `SupportPointGeneratorData temp_data` 均為 `sync_island_data_for_object()` 的 local 變數，函式返回後即自動銷毀。不需要在 `SLAPrintObject` 新增任何成員。

### D3：等級對應的 layer height 值

| 等級 | detect_lh | 特性 |
|------|-----------|------|
| High（高）| 0.05mm | 與典型 SLA 列印 layer height 相同，精確偵測所有細小孤島 |
| Middle（中）| 0.1mm | 適合一般 SLA 解析度，平衡效能與精度 |
| Low（低） | 0.5mm | 孤島截面在較高 Z 可能更寬，覆蓋範圍更大，支撐點更多 |

**注意**：detect_lh 與模型的實際 layer height 設定無關；若模型使用 0.1mm 列印，High 仍強制以 0.05mm 偵測。

### D4：切換等級時是否自動重偵測？

**決策**：不自動。切換 `DetectionAccuracy` enum 只改變下次 Detect Selected 使用的 layer height。使用者需要重新按 Detect Selected 才能看到新等級的結果。

理由：重新切片有一定成本（需 `slice_csgmesh_ex()`），不應在使用者切換 UI 控制項時立即執行。

### D5：存取路徑：`SLAPrint::redetect_islands()` 包裝 Steps（方案 B）

**決策**：

```cpp
// SLAPrint.hpp — 新增 public 宣告
class SLAPrint {
public:
    void redetect_islands(ObjectID obj_id, float detect_lh);
};

// SLAPrint.cpp — 實作（透過 Steps 存取 m_mesh_to_slice）
void SLAPrint::redetect_islands(ObjectID obj_id, float detect_lh) {
    auto it = std::find_if(m_objects.begin(), m_objects.end(),
        [&](SLAPrintObject* po){ return po->model_object()->id() == obj_id; });
    if (it == m_objects.end()) return;
    Steps steps(this);
    steps.prepare_island_detection(**it, detect_lh);
}

// SLAPrintSteps.hpp — Steps private method 宣告
// SLAPrintSteps.cpp — Steps::prepare_island_detection() 實作

// GLGizmoLcdOverhangDetection.cpp — Gizmo 呼叫端
wxGetApp().plater()->sla_print().redetect_islands(mo->id(), detect_lh);
```

**修改的檔案（最終確定）**：

| 檔案 | 修改內容 |
|------|---------|
| `SLAPrint.hpp` | 宣告 `SLAPrint::redetect_islands(ObjectID, float)` |
| `SLAPrint.cpp` | 實作（~10 行，建立 Steps 並呼叫） |
| `SLAPrintSteps.hpp` | 宣告 `Steps::prepare_island_detection(SLAPrintObject&, float)` |
| `SLAPrintSteps.cpp` | 實作 `Steps::prepare_island_detection()` + helper |
| `GLGizmoLcdOverhangDetection.cpp` | 呼叫 `plater()->sla_print().redetect_islands()` |

`Plater.hpp`/`Plater.cpp` **不需要修改**。

### D6：高精度（0.05mm）等於現有行為嗎？

**是的**。當模型 layer height = 0.05mm 時，High 等級的行為與目前完全相同。當模型 layer height > 0.05mm（如 0.1mm），High 等級會比目前更精確。

## Risks / Trade-offs

- **重切片成本**：`slice_csgmesh_ex()` 是 O(面數 × 層數) 的操作。Low 等級（0.5mm）層數最少，成本最低；High 等級（0.05mm）層數最多，接近原始切片成本。→ 可在 UI 加進度提示（現有 `m_slice_pending_for_detect` 機制可延伸）
- **`po.m_mesh_to_slice` 可用性**：此方法在 `slaposObjectSlice` 完成後才有效（現有 `m_slice_pending_for_detect` 機制已確保此前提）→ 無額外風險
- **High 等級（0.05mm）成本高**：若模型有數百層，High 等級的重切片幾乎等同完整切片 → 可接受，因為使用者主動觸發，且有 pending 機制告知等待

## Open Questions

- 切換等級後，UI 是否要顯示目前使用的是哪個 layer height？（方便使用者理解結果差異）
- 是否需要讓使用者自訂 detect_lh 而非只有固定三個等級？（目前決策：固定三個等級，未來可擴充）
