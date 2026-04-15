## Context

SLA 支撐樹由「支撐頭（Head）→ 支撐柱（Pillar）→ 橫向連接橋（Bridge / Crossbridge）」組成。不同重量（Light/Medium/Heavy）的手動支撐點會產生不同半徑的支撐柱。

現行邏輯：

- `interconnect(pillar, nextpillar)`：在兩根相鄰支撐柱之間建立水平 zigzag crossbridge，橋的半徑固定使用 `pillar.r_start`（呼叫方傳入的第一根柱）
- `connect_to_nearpillar(head, nearpillar_id)`：建立從 head 斜向連到旁邊支撐柱的 bridge，橋的半徑固定使用 `head.r_back_mm`

兩者都未考慮另一端的柱半徑，當兩端半徑不一致時，橋的半徑可能超過較細那端，產生幾何不合理的連接。

## Goals / Non-Goals

**Goals:**
- 水平 crossbridge 的半徑 = `min(pillar.r_start, nextpillar.r_start)`
- 斜向 bridge 的半徑 = `min(head.r_back_mm, nearpillar.r_start)`
- 碰撞偵測（`bridge_mesh_distance`）使用同一個修正後的半徑，避免空間判斷與實際幾何不一致

**Non-Goals:**
- 不改變 `DiffBridge`（漸變橋接）的行為，本次修改使用的仍是均勻半徑的 `Bridge`
- 不修改 SLA branching tree 策略（`BranchingTreeSLA`）的橋接邏輯
- 不修改手動支撐點的 clustering 邏輯（已確認現行行為正確）
- 不引入新的設定參數

## Decisions

### 決策 1：使用 `std::min` 而非 `DiffBridge` 漸變

**選擇**：橋的整體半徑取兩端最小值（均勻橋接），不採用兩端不同半徑的 `DiffBridge`。

**理由**：
- 現行 mesh 渲染對 `Bridge`（均勻圓柱）已充分測試，`DiffBridge`（圓錐形）使用場景不同
- 「橋直徑 = 較細柱直徑」是使用者描述的期望行為，均勻橋接即可滿足
- 修改量最小，風險最低

**替代方案**：使用 `DiffBridge` 讓橋從粗端漸變至細端 → 視覺更自然，但改動較大，且需確認 mesh 生成的正確性，超出本次需求範圍。

### 決策 2：在 `interconnect()` 函式進入點計算 `bridge_r`

**選擇**：在函式開頭一次計算 `double bridge_r = std::min(pillar.r_start, nextpillar.r_start)`，後續所有 4 處用到 `pillar.r_start` 的地方統一替換。

**理由**：集中在一處計算，邏輯清晰且不重複，也方便未來擴展（例如改用不同計算策略）。

### 決策 3：`connect_to_nearpillar()` 在取得 nearpillar 參照後立即計算 `r`

**選擇**：行 399 從 `double r = head.r_back_mm` 改為 `double r = std::min(head.r_back_mm, nearpillar().r_start)`。

**理由**：`r` 的後續用途包含橋長限制計算（`max_len`）、碰撞偵測、以及最終 `add_bridge` 呼叫，全部同步使用修正後的值，確保一致性。

## Risks / Trade-offs

- **更細的橋可能導致更多碰撞偵測失敗** → `bridge_mesh_distance` 使用更小半徑，空間需求反而更寬鬆（不是更嚴），此方向對碰撞偵測是有利的，不會造成原本可通過的橋被拒絕
- **橋變細可能降低結構強度** → 這是符合預期的物理行為：橋的強度不應超過它所連接的最細那端；使用者若需要更強的連接，應提高較細柱的重量等級
- **`pillar.r_start` 與 `pillar.r_end` 的選擇** → 橋接發生在柱的頂端附近，`r_start` 代表柱頂半徑，是正確的比較對象；`r_end` 是底部（地面）半徑，不相關

## Migration Plan

純粹的程式碼修改，無需資料遷移或格式版本升級：

1. 修改 `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`（兩處函式）
2. 重新編譯
3. 手動測試：搭配不同 weight 的手動支撐點，確認橋直徑視覺正確
4. 若結果不符預期，直接 revert 兩處修改即可還原

## Open Questions

無。
