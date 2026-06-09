## Context

`support_object_elevation` 已在 `SLAPrintObjectConfig` 中定義，預設 5 mm，並透過 `make_support_cfg()` 映射至 `SupportTreeConfig::object_elevation_mm`。Phrozen profile JSON 已包含此值，但 `PrintConfig.cpp` 的 label/tooltip 被註解，`TabSLAPrint` 的 Raft Setting 區塊也未顯示。

模型 Z 抬升預覽依賴 `GLVolume::sla_shift_z` 與 `SelectionInfo::get_sla_shift()`。目前 `SelectionInfo::on_update()` 從 `SLAPrintObject::get_current_elevation()` 取值，而該函式在支撐樹/pad 步驟未完成時回傳 0，導致手動支撐點編輯時模型貼地。

PrusaSlicer 原版透過 `SelectionInfo::set_use_shift()` 在 gizmo 開啟期間強制使用 config elevation；PhrozenOrca 移植時移除此 API（見 `GLGizmoSlaSupports.cpp` Step 4.2 註解），需以等價機制補回。

## Goals / Non-Goals

**Goals:**

- 在 Raft Setting 區塊、Raft Thickness 上方顯示 **Model Lift Height**（config: `support_object_elevation`）。
- 進入 SLA Support Point gizmo 時，以 config 值做視覺抬升；退出時恢復貼地。
- Gizmo 期間修改參數時即時刷新 3D 預覽。
- `pad_around_object` 啟用時正確隱藏/禁用欄位。
- 切片結果與既有 `object_elevation_mm` 管線一致。

**Non-Goals:**

- 新增獨立 config key（如 `model_lift_height_sla`）。
- 永久修改 `ModelInstance` 的 Z offset（僅視覺 shift，不動 instance 變換）。
- 變更支撐樹生成演算法或 pinhead 最小 elevation clamp 邏輯。
- 多 instance 同時抬升（維持現有單一 instance gizmo 限制）。

## Decisions

### 1. 重用 `support_object_elevation`，僅更新 UI 語意

- **Rationale**: 切片管線、profile、invalidation（`slaposObjectSlice`）均已就緒；避免雙參數不一致。
- **Alternative**: 新建 `model_lift_height_sla` 並在 `make_support_cfg()` 讀取。拒絕：增加遷移與 profile 維護成本，無功能增益。

### 2. 在 `SelectionInfo` 恢復 gizmo-aware Z shift

- **Rationale**: 最小侵入、與 PrusaSlicer 語意一致。新增 `m_use_config_elevation`（或等價 flag），gizmo 開啟時設為 true，`get_sla_shift()` 直接讀 `support_object_elevation`（經 `is_zero_elevation` 守衛後為 0）。
- **Alternative A**: 修改 `get_current_elevation()` 在 gizmo 開啟時回傳 config 值。拒絕：核心庫不應感知 GUI gizmo 狀態。
- **Alternative B**: 僅在 `GLGizmoSlaBase::update_volumes()` 設 `set_sla_shift_z()`。不足：raycast、unproject、clipper 等仍走 `SelectionInfo::get_sla_shift()`，需統一來源。

### 3. Gizmo 生命週期掛鉤

- `GLGizmoSlaSupports::on_set_state()` On → `selection_info()->set_use_config_elevation(true)` + `post_event(EVT_GLCANVAS_FORCE_UPDATE)`
- `on_set_state()` Off（實際關閉後）→ `set_use_config_elevation(false)` + force update
- `TabSLAPrint` 參數變更 `support_object_elevation` 時，若 gizmo 開啟則觸發 canvas refresh

### 4. `pad_around_object` 互斥

- 沿用 `is_zero_elevation()`：`pad_enable && pad_around_object` 時 elevation 強制 0。
- UI：`ConfigManipulation::toggle_print_sla_options()` 隱藏 Model Lift Height 欄位（可選顯示說明文字，參考 Tab 既有註解區塊）。

### 5. Tooltip 語意

- 使用使用者提供的英文 tooltip；「overwrite Z offset」解讀為：**gizmo 預覽與切片時以 Model Lift Height 決定模型最低點離平台距離**，不修改使用者手動放置的 instance Z（僅視覺覆蓋）。

## Risks / Trade-offs

- **[Risk] `set_use_config_elevation` 未在 gizmo 異常關閉時清除** → **Mitigation**: 在 `SelectionInfo::on_release()` 與 gizmo Off 路徑一律重置 flag。
- **[Risk] 最小 elevation clamp 與使用者輸入衝突** → **Mitigation**: 維持 `sync_support_object_elevation()`；UI 可顯示 sidetext 提示最小值由 pinhead 幾何決定。
- **[Risk] PartPlate 無 `SLAPrintObject` 時 fallback 路徑** → **Mitigation**: `m_use_config_elevation` 為 true 時直接讀 edited preset config，不依賴 `m_print_object`。
- **[Trade-off] 恢復 Prusa API 而非全域改 `get_current_elevation`** → **Benefit**: 核心庫與 GUI 邊界清晰，不影響已切片場景的 elevation 顯示邏輯。

## Migration Plan

- 無 schema 遷移；既有 3MF/profile 的 `support_object_elevation` 自動對應新 UI 標籤。
- 部署後手動驗證：進入/退出 gizmo、調整參數、pad_around_object 互斥、切片後支撐間隙。

## Open Questions

- 無（探索階段邊界已收斂；實作採方案 A：重用 key + 恢復 gizmo shift）。
