## Context

SLA 支撐生成管線中，`SupportTreeBuildsteps::filter()` 的 `filterfn` lambda 是所有支撐點進入幾何計算前的唯一過濾咽喉點。目前 line 712 對所有點（包含 `manual_add`）套用 `overhang_angle_threshold`，但語義上此門檻僅應約束演算法自動生成的支撐。手動點代表使用者的主動幾何覆寫，應強制嘗試生成而不受此偏好性過濾干擾。

兩條上游路徑（`SLAPrintSteps.cpp` line 844 的自動路徑與直接讀取使用者點的手動路徑）在進入 `filter()` 之前已分流，但 `filterfn` 不感知來源，對所有點一視同仁。

## Goals / Non-Goals

**Goals:**
- 使 `SupportPointType::manual_add` 的點繞過 `overhang_angle_threshold` 過濾
- 覆蓋 Standard Support 與 Branching Support 兩種模式（共用 `filterfn`）
- 保留 `normal_cutoff_angle`（line 708）對所有點的幾何合理性保護

**Non-Goals:**
- 修正 cluster 去重邏輯（手動點與自動點距離過近時的 cluster 代表競爭問題）
- 保證手動點在幾何物理空間不足時一定能生成支撐柱
- 新增失敗時的使用者 UI 提示（屬 UX 優化，不在本次 scope）

## Decisions

### 修改點：`filterfn` line 712

**選擇 `filterfn` 作為修改點，而非上游分流路徑。**

`filterfn` 是最小且最精確的咽喉點。在上游（`SLAPrintSteps.cpp`）處理會需要改動多處路徑，且語義上 "手動點應繞過角度過濾" 是對 `filterfn` 行為的精確覆寫，不是上游資料結構的差異。此方式變更範圍最小、最容易理解和驗證。

---

### 條件：`!= manual_add`（Fail-Safe）而非 `== island || == slope`

條件寫為 `type != SupportPointType::manual_add`，而非正向列舉演算法點。

未來若新增 `SupportPointType`（如 `enforcer_generated`），預設應受最嚴格的角度過濾保護，而非意外繞過。只有明確由使用者介入（`manual_add`）的點才享有豁免特權。若改為正向列舉，新 type 靜默繞過過濾，導致難以追蹤的幾何問題。

---

### Drag-to-promote 的點同樣享有豁免

`GLGizmoSlaSupports.cpp:606` 將使用者拖曳自動點升格為 `manual_add`。此行為語義與手動放置 100% 等價（使用者主動的幾何覆寫），統一映射至同一 type 符合單一事實來源原則，無需為拖曳行為引入額外過濾或新 Enum 狀態。

---

### `normal_cutoff_angle`（line 708）對手動點保留

此檢查是幾何合理性 sanity check，非使用者偏好過濾。朝上平面（polar 過小）在數學上不可能生成向下支撐柱；強行進入 optimizer 會導致無解。此保護應無條件適用於所有點類型。

---

### 索引安全性：`m_support_pts[fidx]` 存取

`m_support_pts`（constructor line 39）與 `m_points`（line 42）由同一個 `sm.pts` 順序建構，大小相同、索引 1:1 對應。`fidx` 來自 `filtered_indices`（值域 = cluster 代表的 `m_points` 行索引），因此 `m_support_pts[fidx]` 恆合法。此模式已存在於同一 lambda 的 line 730：`m_support_pts[fidx].head_front_radius`。

---

### TBB 並行安全性

`filterfn` 由 `ccr::for_each` 並行執行。新增的 `.type` 讀取屬於 const read，C++ 標準保證多執行緒同時讀同一資料無 data race，無需 mutex。

## Risks / Trade-offs

**Cluster 去重可能靜默吞掉手動點**
→ 若手動點（`manual_add`）與自動點距離 < `D_SP`，被合併入同一 cluster 且自動點成為 `a.front()` 代表，手動點意圖被完全丟棄，與本次修改無關。此為已知限制，留待後續獨立 issue。

**手動點在 optimizer 空間不足時靜默失敗或生成斜出支柱**
→ 繞過角度過濾不等於保證生成：optimizer 的 `bridge_slope` 物理邊界仍適用。近水平面的手動點可能斜出或靜默失敗。這不是本次引入的新問題，屬既有幾何物理保護，可接受。
