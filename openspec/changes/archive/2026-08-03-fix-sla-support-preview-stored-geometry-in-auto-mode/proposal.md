## Why

`render_points()`（`GLGizmoSlaSupports.cpp:750`）決定每顆點該用「自己儲存的 per-point 幾何」還是「preset 的即時參數」時，多掛了一個編輯模式前綴：

```cpp
const bool use_stored_geometry = m_editing_mode && preview_use_stored_top(support_point, point_selected);
```

`preview_use_stored_top()` 本身的規則是對的，註解也寫明其意圖是「Match slice」：

```cpp
if (point_selected) return true;
return sp.type == manual_add && sp.has_explicit_geometry();
```

但 `m_editing_mode &&` 讓它在非編輯模式下恆為 false，於是 `preview_sla_head_for_point()` 走到 `pin_r = use_stored_point ? sp.head_front_radius : live_upper_r`，**所有點——包含帶 explicit geometry 的手動點——一律套用 preset 的即時參數**，各自儲存的幾何被忽略。

### 為什麼這是缺陷而不是設計

切片端不是這樣算的。`SupportTreeBuildsteps.cpp:693-705` 建 head 時：

```cpp
for (const SupportPoint &sp : m_support_pts) {
    const double contact_r = point_contact_sphere_radius_mm(sp, m_cfg.contact_sphere_radius_mm);
    const double mesh_pen  = point_head_penetration_mesh_mm(sp, m_cfg.head_penetration_mm,
                                                           double(sp.head_front_radius), contact_r);
    heads.emplace_back(std::nan(""), sp.head_front_radius, 0., mesh_pen, ...);
}
```

`sp.head_front_radius` 是**無條件**取用的，而 `point_*()` 系列助手（`SupportPoint.hpp:126-164`）一律「per-point 值 ≥ 0 就用 per-point，否則退回 preset」。**backend 根本沒有「編輯模式」這個概念**，它永遠用 per-point 值。

結果是：

| | 手動模式 | 自動模式 | 實際切片 |
|---|---|---|---|
| 手動點的尺寸 | per-point ✅ | **preset ❌** | per-point |

使用者在手動模式放置了粗中細不同的支撐點，切回自動模式後看到全部變成同一個尺寸——但列印出來會是手動模式看到的樣子。**preview 在非編輯模式下誤導使用者。**

本缺陷於 `perf-sla-support-points-preview-render` 的驗收（任務 7.6）期間發現。該 `m_editing_mode &&` 前綴為既有程式碼，此前的 change 未觸碰它。

## What Changes

- 移除 `use_stored_geometry` 的 `m_editing_mode &&` 前綴，使非編輯模式與編輯模式、切片端三者的參數解析規則一致。
- `point_selected` 在非編輯模式下已由上游強制為 false（`const bool point_selected = m_editing_mode ? m_editing_cache[i].selected : false;`），因此規則自動退化為「`manual_add` 且 `has_explicit_geometry()` 才用儲存值」，無需額外分支。
- 同步檢查 `update_point_raycasters_for_picking_transform()` 的 picking 半徑解析是否需要對齊（該函式僅在編輯模式下執行，預期不需變更，但須明確確認）。

### Non-goals

- 不改變 `preview_use_stored_top()` 的規則本身。
- 不改變 `preview_sla_head_for_point()` 對各參數的解析順序或 clamp 規則。
- 不改變切片端行為——本 change 是讓 preview 對齊切片，不是反過來。
- 不改變 Structure 模式、支撐生成 backend、Pad / Hollow / Drill 路徑。
- 不處理 `sla_trafo` 改變後的快取失效（→ `fix-sla-support-points-invalidate-on-trafo-change`）。

## Capabilities

### New Capabilities

<!-- 無。本 change 為既有 capability 增補 requirement。 -->

### Modified Capabilities

- `sla-support-points-preview`：新增 requirement，規範 Points preview 錐體的**幾何參數來源**必須與切片端一致，且不得依 `m_editing_mode` 而異。該 capability 現有的 requirement（view-mode gate、anchor 跟隨 instance scale、尺寸維持 mm、picking 一致性）皆不變更。

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `render_points()` 的 `use_stored_geometry` 判定
- **與 `perf-sla-support-points-preview-render` 的互動**：非編輯模式的 `m_head_model_cache` key 數會由 1 變為「1 + 相異手動幾何組數」。手動點為使用者逐顆放置，數量天然有界，仍在該 change 設定的 64 筆門檻內，效能不受影響
- 不影響 libslic3r、切片輸出、檔案格式或 profile
- 無 public API 變更
