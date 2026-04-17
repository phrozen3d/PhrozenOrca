## 1. 移除 r_start 守衛條件

- [x] 1.1 刪除 `SupportTreeBuildsteps.cpp` line 1189 的 `if (neighborpillar.r_start < pillar.r_start) continue;`

## 2. 驗證

- [x] 2.1 Heavy 跨格測試：放置兩根 Heavy 柱子，中間夾一根 Medium 柱子，確認 Heavy 不跳過 Medium 橋接到另一根 Heavy
  - 備註一：移除 r_start 守衛不足 — Heavy A 同時連接 Medium 和 Heavy B，後者橋段穿越 Medium。加入 2D 幾何相交檢查後進行第二次測試。
  - 備註二：Medium 與 Heavy A 部分重疊時，`interconnect()` 回傳 false，Medium 未進 `connected_closer`，Heavy A 仍跨格。修正為：任何較近鄰居無論 interconnect 成敗均記錄為障礙物。請重新測試。
- [x] 2.2 Mixed weight 測試：Heavy + Medium + Light 並排，確認各柱子優先連接最近鄰居，不出現跨格橋
- [x] 2.3 Same-weight 回歸測試：Medium + Medium 並排，確認橋接行為與修改前一致
