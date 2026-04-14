## Context

SLA 支撐樹生成在 `classify()` 階段對所有 ground-facing 的支撐頭（Head）進行 XY 平面的 clustering，將水平距離小於 `2 × base_radius_mm`（預設 4mm）的支撐點歸為同一群組。在 `routing_to_ground()` 階段，每個群組只有 centroid 建立帶底座的地面柱，其他 sideheads 橋接到 centroid 柱。這個機制原本是為了防止底座圓錐互相重疊，但對手動放置的支撐點來說，使用者期望每個點都有自己的底座。

手動支撐點無底座有兩個獨立原因：
1. **clustering 問題**：`classify()` predicate 把距離 < 4mm 的手動點歸為同一 cluster，只有 centroid 得到底座
2. **Light 重量問題**：`create_ground_pillar()` 的 `eval_limits()` 判斷 `radius < head_back_radius_mm` 時設 `can_add_base = false`，Light 重量支撐柱半徑不足，即使成為 centroid 也無底座

底座圓錐（Pedestal）的 mesh 組合使用 `its_merge()`（簡單頂點拼接），SLA 切層取 2D 輪廓時重疊區域自然合併，因此底座重疊不影響切片或列印正確性。

**關鍵程式碼位置：**
- `classify()` predicate：[SupportTreeBuildsteps.cpp:824-830](src/libslic3r/SLA/SupportTreeBuildsteps.cpp#L824)
- `routing_to_ground()`：[SupportTreeBuildsteps.cpp:836-913](src/libslic3r/SLA/SupportTreeBuildsteps.cpp#L836)
- 底座建立：`create_ground_pillar()` → `add_pillar_base()`
- `can_add_base` 判斷：[SupportTreeBuildsteps.cpp:484-493](src/libslic3r/SLA/SupportTreeBuildsteps.cpp#L484)（`eval_limits` lambda）

## Goals / Non-Goals

**Goals:**
- 每個 `manual_add` 型支撐點各自成為獨立 cluster，均建立帶底座的地面柱
- Light 重量手動支撐點也一律生成底座圓錐，不受柱半徑限制
- 修改最小化：改 clustering predicate 與 `create_ground_pillar()` 兩處
- auto-generated 支撐點的行為完全不變

**Non-Goals:**
- 不修改 auto-generated 支撐的 clustering 邏輯
- 不處理 RC-1（`filterfn` 角度條件靜默丟棄手動點的問題）

## Decisions

### Decision 1：在 classify() predicate 中加入 manual 判斷

**做法：** 在 `classify()` 的 clustering predicate lambda 內，從 head ID 查回 `SupportPoint.type`，若任一方是 `manual_add` 則 return false（不合併）。效果：每個手動點各自成為獨立 cluster，各自通過 `routing_to_ground()` 建立 centroid ground pillar。

```cpp
// 新增邏輯（在現有 d2d/d3d 判斷之前）：
auto is_manual = [this](unsigned head_id) -> bool {
    long sp_id = m_builder.head(head_id).id;
    return sp_id >= 0 && size_t(sp_id) < m_support_pts.size() &&
           m_support_pts[size_t(sp_id)].type == SupportPointType::manual_add;
};
if (is_manual(e1.second) || is_manual(e2.second)) return false;
```

**替代方案考慮：**
- 修改 `routing_to_ground()`，讓每個 sidehead 在橋接後也呼叫 `add_pillar_base()`
  - 缺點：底座是孤立的（不連接到任何柱體），幾何語意不正確
- 在 `create_ground_pillar()` 中強制所有呼叫都產生底座
  - 缺點：影響範圍太廣，可能改變 auto 支撐的合理行為

**選擇理由：** predicate 修改最局部，只影響「哪些點可以 cluster 在一起」的決策，不動任何後續邏輯。

### Decision 2：在 create_ground_pillar() 強制手動點的 can_add_base = true

**做法：** 在 `create_ground_pillar()` 的第一次 `eval_limits()` 呼叫（約第 495 行）之後，查看 `head_id` 對應的 `m_support_pts` 類型，若為 `manual_add` 且 `can_add_base` 仍為 false，則強制覆寫：

```cpp
// 在 eval_limits(); 呼叫之後加入（約第 495 行後）：
if (head_id >= 0 && !can_add_base) {
    long sp_id = m_builder.head(head_id).id;
    if (sp_id >= 0 && size_t(sp_id) < m_support_pts.size() &&
        m_support_pts[size_t(sp_id)].type == SupportPointType::manual_add) {
        can_add_base = true;
        gndlvl       = m_builder.ground_level;
        jp_gnd       = gndlvl;
        gap_dist     = m_cfg.pillar_base_safety_distance_mm
                       + m_cfg.base_radius_mm + EPSILON;
    }
}
```

**為何安全：** Light 重量手動點的 `allow_widening = false`，不進入 widening path，第二次 `eval_limits()` 不會被呼叫，強制值不會被覆寫。Medium/Heavy 手動點的 radius 本來就 ≥ `head_back_radius_mm`，`can_add_base` 已為 true，此段不執行。auto 支撐的 `head_id` 對應點不是 `manual_add`，不受影響。

**已知邊界案例：** 當 `object_elevation_mm ≈ 0`（模型貼地列印）且修正橋接無法繞開 pad gap 時，`create_ground_pillar()` 的安全兜底邏輯（約第 553–557 行）會將 `can_add_base` 再度設回 false 並呼叫 `eval_limits(false)`，覆寫強制值。此屬正確行為——幾何空間真的無法容納底座時不應強制生成。SLA 列印實務上幾乎都有正的 object_elevation，此案例極少發生。

**替代方案考慮：**
- 修改 `eval_limits` lambda 加入 `force_base` 參數
  - 需要在呼叫端知道是否為 manual，傳遞資訊較繁瑣
- 在 `routing_to_ground()` 層面為 manual centroid 傳入特殊 flag
  - 需修改 `create_ground_pillar()` 函式簽名，影響所有呼叫端

**選擇理由：** 在函式內部查 `m_support_pts` 與 Decision 1 的查法完全一致，不需改函式簽名，影響範圍最小。

### Decision 3：允許手動底座重疊

底座 mesh 以 `its_merge()` 拼接，SLA 切層正確處理重疊幾何，因此允許重疊是安全的。不加任何底座間的碰撞檢查，保持程式碼簡單。

## Risks / Trade-offs

- **[風險] 手動密集放點時底座互相重疊** → 可接受，SLA 切層正確合併，平台附著力反而更好
- **[Trade-off] 手動點不再橋接到相鄰柱**：若使用者有意讓手動點共用柱，此行為消失。但手動放點的語意是「我要在這裡有一個支撐」，獨立柱更符合預期
- **[已知限制] Light 重量手動點底座尺寸為標準 base_radius_mm**：底座直徑固定（預設 4mm），但柱體仍為 Light 細柱，銜接處有明顯直徑差。屬設計取捨——使用者明確要求底座，接受此視覺效果