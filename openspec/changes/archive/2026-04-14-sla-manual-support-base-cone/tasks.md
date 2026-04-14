## 1. 修改 classify() clustering predicate

- [x] 1.1 開啟 `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`，找到 `classify()` 函式內的 `predicate` lambda（約第 824 行）
- [x] 1.2 在 predicate lambda 的開頭加入 manual 判斷：從 `e1.second` / `e2.second`（head ID）透過 `m_builder.head(hid).id` 取得 `m_support_pts` 索引，若任一方的 `type == SupportPointType::manual_add` 則 `return false`
- [x] 1.3 確認修改不影響 predicate 的其餘 d2d / d3d 距離判斷邏輯

## 2. 修改 create_ground_pillar() 強制手動點生成底座

- [x] 2.1 在 `create_ground_pillar()` 函式（約第 468 行）找到第一次 `eval_limits()` 呼叫（約第 495 行）
- [x] 2.2 在該呼叫之後加入 manual 強制邏輯：若 `head_id >= 0` 且 `!can_add_base`，查 `m_builder.head(head_id).id` 對應的 `m_support_pts[sp_id].type`，若為 `manual_add` 則強制設定 `can_add_base = true`、`gndlvl = m_builder.ground_level`、`jp_gnd = gndlvl`、`gap_dist = m_cfg.pillar_base_safety_distance_mm + m_cfg.base_radius_mm + EPSILON`
- [x] 2.3 確認此段只在第一次 `eval_limits()` 後執行，widening path 內的第二次 `eval_limits()` 呼叫位置不受影響（Light 重量手動點 `allow_widening = false`，不進入該分支）

## 3. 驗證

- [x] 3.1 編譯並開啟應用程式，對一個 SLA 物件進入 Manual editing 模式，放置兩個水平距離 < 4mm 的支撐點，確認兩個支撐均各自有底座圓錐（**BUG FIX 3.0 已修正**：原來的 predicate 缺少 self-check，手動點自身查詢返回 false → cluster 為空 → sindex 不移除 → 無限迴圈，現已在 predicate 最前加 `if (e1.second == e2.second) return true;`）
- [x] 3.2 在 Manual editing 模式以 **Light 重量**放置支撐點，確認底座圓錐正確生成（即使支撐柱細）
- [x] 3.3 確認 auto-generated 支撐的行為不變：使用「自動支撐」後確認靠近的支撐點仍然共用柱（bridging 仍在作用）
- [x] 3.4 確認手動點與 auto 點混合放置時，手動點各自有底座，auto 點的 clustering 不受影響