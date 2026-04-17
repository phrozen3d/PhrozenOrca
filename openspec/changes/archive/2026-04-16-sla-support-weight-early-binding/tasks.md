## 1. 修正 interconnect() 柱間最小距離檢查

- [x] 1.1 在 `SupportTreeBuildsteps.cpp:321` 將 `2 * m_cfg.head_back_radius_mm` 改為 `pillar.r_start + nextpillar.r_start`
- [x] 1.2 驗證：放置兩個 Heavy 手動支撐點於 1.5mm 距離，確認切片後無橋段交錯

## 2. 修正 interconnect_pillars() 補強柱搜尋半徑

- [x] 2.1 在 `SupportTreeBuildsteps.cpp:1241` 將 `double r = 2 * m_cfg.base_radius_mm` 改為 `double r = std::max(2 * m_cfg.base_radius_mm, 2 * pillar().r_start)`
- [x] 2.2 驗證：High Heavy 孤立柱（需補強柱）切片後，補強柱不與主柱重疊
  - 備註：預設設定下 `base_radius_mm = 2.0mm`，`2 × base_radius_mm = 4.0mm` 已大於 `2 × r_start = 2.0mm`，max() 無效；修改為防禦性保護，非預設設定（`base_radius_mm < pillar.r_start`）才觸發

## 3. 修正 filter() 去重 cluster 保留邏輯

- [x] 3.1 在 `SupportTreeBuildsteps.cpp:657-659` 修改 cluster 遍歷邏輯：找出 `a` 中 `m_support_pts[idx].weight` 最大的 index，以其取代 `a.front()`
- [x] 3.2 驗證：在完全相同位置放置 Light 再放置 Heavy 點，確認保留 Heavy（觀察支撐柱粗細）
  - 備註：UI 驗證不可行。Hover 偵測球體半徑（`head_front_radius × 1.0` ≈ 0.2~0.5mm）大於 D_SP 閾值（0.1mm），導致第二次點擊必然命中既有點的 hover 區域而不觸發新增。程式碼審查確認邏輯正確：cluster 中遍歷找出 weight 最大的 index，Heavy(2) > Light(0) 分支覆蓋完整，防禦性保護針對舊 3mf 重疊點或程式化生成的鄰近點

## 4. 驗證整體行為

- [x] 4.1 Medium 支撐點行為回歸測試：確認修改後 Medium 支撐的柱間橋接、補強柱行為與修改前一致
- [x] 4.2 Light + Heavy 混搭場景：兩種 weight 的支撐點並排放置，確認各自呈現正確粗細且無物理重疊
  - 備註：驗證通過（柱子粗細正確、無物理重疊）。觀察到「跨越中間柱橋接」的視覺現象（Heavy 跳過 Light 柱連到另一根 Heavy），確認為 `cascadefn` 的預先存在行為：(1) `neighborpillar.r_start < pillar.r_start` 過濾使 Heavy 跳過較細柱子；(2) min_dist 失敗後繼續嘗試更遠鄰居。此問題存在於所有 weight 類型，與本次修改無關，已另立 `sla-bridge-skip-intermediate` 追蹤。
