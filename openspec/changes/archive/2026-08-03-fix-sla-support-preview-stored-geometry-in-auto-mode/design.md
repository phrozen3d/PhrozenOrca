## Context

支撐點的幾何參數有兩個來源：點自身儲存的 per-point 值（`sp.head_front_radius`、`sp.head_width_mm`、`sp.head_penetration_mm`、`sp.head_back_radius_mm`、`sp.contact_sphere_radius`、`sp.pillar_radius`），以及 SLA print preset 的即時值。

`SupportPoint.hpp:126-164` 定義了唯一的仲裁規則，切片端與 preview 端共用：

```cpp
inline float point_head_width_mm(const SupportPoint &sp, double preset_mm)
{ return sp.head_width_mm >= 0.f ? sp.head_width_mm : float(preset_mm); }
```

即「per-point 值 ≥ 0 就用 per-point，否則退回 preset」。`SUPPORT_POINT_USE_PRESET = -1.f` 是預設值，auto 生成路徑不覆寫它，所以 auto 點自然全部退回 preset；只有手動放置時 `freeze_process_top_into_point()` 才會寫入實值。

切片端（`SupportTreeBuildsteps.cpp:693`）無條件套用這套規則。preview 端（`preview_sla_head_for_point()`）則多了一層 `use_stored_point` 開關，而該開關在 `render_points()` 被 `m_editing_mode &&` 關掉：

```
編輯模式  → use_stored_geometry = preview_use_stored_top(...)  → 與切片一致 ✅
非編輯模式 → use_stored_geometry = false（恆定）               → 全部走 preset ❌
切片      → 無此開關，永遠 per-point                            ✅
```

三者中只有非編輯模式的 preview 不一致。

## Goals / Non-Goals

**Goals:**

- 非編輯模式的 Points preview 與切片端使用同一套參數解析規則。
- 手動點在自動模式與手動模式下顯示相同的尺寸。
- 修改後不引入第二條參數解析路徑——三處共用 `preview_use_stored_top()` + `point_*()` 助手。

**Non-Goals:**

- 不改變 `preview_use_stored_top()` 的規則、`preview_sla_head_for_point()` 的解析順序或 clamp 規則。
- 不改變切片端行為。
- 不改變 auto 點的顯示（它們的 per-point 欄位皆為 `SUPPORT_POINT_USE_PRESET`，規則對齊後結果不變）。
- 不改變 picking 的命中判定範圍。

## Decisions

### D1. 移除前綴，而非新增非編輯模式專用分支

```cpp
// 改前
const bool use_stored_geometry = m_editing_mode && preview_use_stored_top(support_point, point_selected);
// 改後
const bool use_stored_geometry = preview_use_stored_top(support_point, point_selected);
```

- **為何不需要處理 `point_selected`**：它在上游已被寫成 `m_editing_mode ? m_editing_cache[i].selected : false`，非編輯模式恆為 false。因此 `preview_use_stored_top(sp, false)` 直接退化為 `sp.type == manual_add && sp.has_explicit_geometry()`——正是想要的規則，不需要額外條件。
- **為何不另寫一套非編輯模式的解析**：那會產生第二條必須與切片端同步維護的規則，正是本缺陷的成因類型。共用同一個判定函式讓 preview 與切片不可能再漂移。

### D2. auto 點的顯示不受影響（等價性論證）

`has_explicit_geometry()`（`SupportPoint.hpp:110`）第一個條件就是 `type == SupportPointType::manual_add`，因此 auto 生成的 island / slope 點恆回傳 false，`use_stored_geometry` 對它們仍為 false，走 preset 路徑——與改動前完全相同。

改動只影響 `manual_add` 且至少有一個 per-point 欄位被寫過實值的點。這正是預期的目標集合。

### D3. picking 半徑維持現狀

`update_point_raycasters_for_picking_transform()` 只在編輯模式下被有效執行（開頭即 `if (m_editing_cache.empty() || m_point_raycasters.empty()) return;`，而 `m_point_raycasters` 僅在編輯模式註冊），其內部呼叫 `preview_use_stored_top(sp, m_editing_cache[i].selected)` 時**沒有** `m_editing_mode &&` 前綴，本來就是正確的。

因此本 change 不需要改動 picking，但驗收必須確認命中範圍未被連帶改變。

### D4. 對 `perf-sla-support-points-preview-render` 建立的幾何快取的影響

該 change 的 `m_head_model_cache` 以幾何參數為 key。改動後：

- **改前**：非編輯模式全部點收斂為 1 個 key。
- **改後**：1 個 key（全部 auto 點）+ 每組相異的手動 explicit 幾何各一個 key。

這與該 change 在**編輯模式**下本來就要面對的 key 分布完全相同，其 design D3 已明確涵蓋：key 數的天然上界是「1 + 相異 manual explicit 幾何組數」，而手動點是使用者逐顆放置的，數量本就有限；64 筆門檻與超限整份 `clear()` 的機制不需調整。

**穩態每幀 `init_from()` 呼叫次數仍為 0**，效能特性不變。

## Risks / Trade-offs

- **[使用者察覺「自動模式下支撐點尺寸變了」]** → 這正是本 change 的目的：改後顯示的才是實際會列印的尺寸。但對已習慣舊行為的使用者是可見變化。緩解：此為 correctness fix，於 release note 說明「Points preview 現在正確反映手動點的個別參數」。

- **[手動點數量極多時快取 key 膨脹]** → 見 D4。上界受 `has_explicit_geometry()` 的定義約束（僅 `manual_add`），且相異**參數組合**數遠少於點數（使用者通常只用少數幾種尺寸）。64 筆門檻的退化路徑已驗證。

- **[誤改到 auto 點的顯示]** → D2 的論證顯示 auto 點不受影響，但驗收仍須明確比對改動前後 auto 點的 preview 尺寸完全相同。

- **[與 picking 不一致]** → 若 preview 尺寸改變而 picking 半徑未同步，會出現「看得到但點不到」。D3 說明 picking 端本來就是正確的，驗收需確認實際命中範圍與改後的錐體一致。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純顯示正確性修正，可直接隨版本發布。

回退策略：單一條件式的修改，revert 即恢復現況。

## Open Questions

無。
