## Context

`interconnect_pillars()` 中的 `cascadefn` lambda 負責將每根支撐柱與鄰近柱子橋接（`src/libslic3r/SLA/SupportTreeBuildsteps.cpp` ~line 1148–1209）。

執行邏輯：

1. 以目前柱子的 `r_start` 比例計算搜尋半徑 `max_d`（Heavy 可達 20mm，Medium 10mm，Light 5mm）
2. 對所有在 `max_d` 內的鄰居按距離排序
3. 逐一嘗試橋接；若目前柱子已達最大連接數則停止

第 1189 行存在一個過濾條件：

```cpp
if (neighborpillar.r_start < pillar.r_start) continue;
```

此行使較粗的柱子跳過所有較細的鄰居，只嘗試連接等粗或更粗的柱子。由於 Heavy 柱子的搜尋半徑高達 20mm，實際結果是 Heavy 柱子會跨越中間所有 Medium/Light 鄰居，與遠處另一根 Heavy 柱子橋接。

## Goals / Non-Goals

**Goals:**
- 移除使柱子無條件跳過較細鄰居的 `r_start` 守衛
- 確保柱子優先連接最近的合法鄰居，不論 weight 差異
- 修正影響 Heavy、Medium、Light 全部 weight 類型的跨格橋接問題

**Non-Goals:**
- 不改變橋接的物理合法性驗證（`interconnect()` 的 min-dist 邏輯不動）
- 不修改 `max_d` 的搜尋半徑計算
- 不改變 `pillar_cascade_neighbors` 的連接上限
- 不調整已合法建立的橋接幾何形狀

## Decisions

### 決策 1：移除第 1189 行的 `r_start` 守衛

**理由：** `r_start` 守衛使較粗的柱子完全跳過所有較細的鄰居，直接橋接到更遠的同重量柱子——這本身就是跳格問題。移除此守衛是必要前提。

**替代方案（排除）：** 半徑比值門檻、反轉守衛方向 — 仍會造成部分跳格。

### 決策 2：加入 2D 幾何相交檢查，防止橋段穿越中間柱子

**背景（測試中發現）：** 移除 r_start 守衛後，由於 `max_d` 縮放讓 Heavy 的搜尋半徑達 20mm，Heavy A 在連接 Medium（4mm）之後仍會嘗試連接 Heavy B（8mm）——因為兩者都在 Heavy A 的搜尋範圍與 `max_pillar_link_distance_mm = 10mm` 內。結果是 Heavy A 對兩者都建立橋接，Heavy A → Heavy B 的橋段實體穿越中間的 Medium 柱子。

**修正：** 在 `cascadefn` 的候選遍歷迴圈中，維護 `connected_closer` — 目前柱子所有已連接的較近鄰居（位置 + 半徑）。對每個新候選進行橋段線段與已連接柱子的 2D 圓柱相交檢查：

```
距離線段最近點 = from + clamp(AP·AB/|AB|², 0, 1) · AB
若 dist(最近點, 中間柱中心) < bridge_r + intermediate_r → 跳過
```

**`connected_closer` 的來源（修訂後）：**
- 已在 `pairs` set 中（由其他柱子的 cascade 建立的連接）→ 在 `pairs.find() != end` 分支記錄
- 本次 cascade 嘗試過的每個較近鄰居 → **無論 `interconnect()` 成功或失敗，均記錄**

**為什麼必須記錄失敗的 interconnect：** 若 Medium 與 Heavy A 部分重疊，`interconnect()` 因 `pillar_dist < r1 + r2` 而回傳 false。初始版本只在成功後記錄，導致 Medium 未出現在 `connected_closer`——Heavy A 對 Heavy B 的橋接時看不到 Medium 作為障礙物，仍會跨格。

修正後，任何在距離排序中比當前候選更近的柱子（无論是否连接成功、是否 links 已滿、是否已配對）均會被加入 `connected_closer`，確保後續更遠的候選均受到物理障礙檢查。

此設計無論哪個柱子先執行 cascade、Medium 是否與鄰近柱重疊，均能正確阻擋跨格橋段。

**考慮但排除的替代方案：**
- 方向角度門檻（dot > 0.5）：不夠精確，無法保證物理不相交
- 限縮 `max_d`：改變搜尋行為，影響正常連接
- 修改 `interconnect()` 加入柱子相交檢查：影響範圍過大，且 interconnect 不感知 pillar index

### 決策 3：不另加「最近鄰居失敗即停止」的邏輯

loop 在 `interconnect()` 失敗後繼續嘗試下一鄰居（用於處理：已達連接上限、已配對、物理 min-dist 不足等正常情況），不需修改。幾何相交檢查已處理跨格問題。

## Risks / Trade-offs

- **Heavy-to-Light 橋段較細**：`interconnect()` 使用 `min(r1, r2)` 作為橋段半徑，此為正確行為。
- **柱子完全遮蔽不連接**：若 Medium 完全處於 Heavy A 和 Heavy B 的連線上，Heavy A 不會橋接 Heavy B。若 Medium 後續失去連接（例如高度條件使 links 未增加），Heavy B 可能孤立。但這是邊緣情況，且原本架構中不孤立的柱子不受影響。
- **Same-weight 回歸**：Medium + Medium 兩者方向完全相同時，`connected_closer` 為空（沒有更近的已連接柱子），行為與修改前一致。

## Migration Plan

- 純邏輯修正，不涉及序列化格式或設定欄位
- 無需遷移計劃；若需 rollback，還原 line 1188–1209 區段即可
