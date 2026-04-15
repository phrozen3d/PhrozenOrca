## 1. 修改 `interconnect()` — 水平 crossbridge 半徑

- [x] 1.1 在 `SupportTreeBuildsteps.cpp` 的 `interconnect()` 函式開頭（行 301 之後）加入 `double bridge_r = std::min(pillar.r_start, nextpillar.r_start);`
- [x] 1.2 將行 359 的 `bridge_mesh_distance(sj, dirv(sj, ej), pillar.r_start)` 改為使用 `bridge_r`
- [x] 1.3 將行 361 的 `m_builder.add_crossbridge(sj, ej, pillar.r_start)` 改為使用 `bridge_r`
- [x] 1.4 將行 371 的 `bridge_mesh_distance(sjback, dirv(sjback, ejback), pillar.r_start)` 改為使用 `bridge_r`
- [x] 1.5 將行 373 的 `m_builder.add_crossbridge(sjback, ejback, pillar.r_start)` 改為使用 `bridge_r`

## 2. 修改 `connect_to_nearpillar()` — 斜向 bridge 半徑

- [x] 2.1 在 `SupportTreeBuildsteps.cpp` 的 `connect_to_nearpillar()` 函式中，將行 399 的 `double r = head.r_back_mm;` 改為 `double r = std::min(head.r_back_mm, nearpillar().r_start);`

## 3. 驗證

- [x] 3.1 編譯專案，確認無編譯錯誤
- [x] 3.2 在 SLA 模式下放置 Light（0.5x）與 Heavy（2.0x）相鄰手動支撐點，確認 crossbridge 直徑視覺上等於 Light 柱的直徑
- [x] 3.3 放置一個 Light 手動支撐點，確認斜向 bridge 連接到旁邊較粗柱時使用 Light 點的較細半徑
- [x] 3.4 確認原本可正常建立的 bridge 在修改後仍可正常建立（碰撞偵測未受負面影響）
