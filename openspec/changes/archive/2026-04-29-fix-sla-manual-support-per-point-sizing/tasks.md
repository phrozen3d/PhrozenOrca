## 1. SupportPoint 新增 pillar_radius 欄位

- [x] 1.1 在 `src/libslic3r/SLA/SupportPoint.hpp` 的 `SupportPoint` struct 加入 `float pillar_radius = 0.f;`（`0.f` 代表使用全域設定，auto 點預設值）
- [x] 1.2 更新 `SupportPoint::serialize()` 以包含 `pillar_radius`：`ar(pos, head_front_radius, type, weight, pillar_radius);`
- [x] 1.3 確認 `filterfn` 第三個參數（back_radius）的語意：`head_back_radius_mm` 是 pillar 的起始半徑，與 `pillar_radius` 直接對應，可直接作為 `back_r` 傳入

## 2. 後端算法：filter() 使用 per-point pillar_radius

- [x] 2.1 在 `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` 的 `filter()` 函數中，找到 `filterfn(fidx, i, m_cfg.head_back_radius_mm)` 呼叫
- [x] 2.2 對 `manual_add` 類型且 `pillar_radius > 0.f` 的點，使用 `double(sp.pillar_radius)` 作為 `back_r`；其餘點維持使用 `m_cfg.head_back_radius_mm`
- [x] 2.3 修正 `widen` 邏輯（驗證發現原設計有誤）：
  - [x] 2.3a `filterfn` 中 `back_r < head_back_radius_mm` 時將 `lmax` 從 `head_penetration_mm` 改為 `head_width_mm`，避免優化器搜尋空間過窄
  - [x] 2.3b 在 `make_iheads_on_ground()` 的 `create_ground_pillar` 呼叫前，對 `manual_add` 且 `pillar_radius < head_back_radius_mm` 的點設定 `widen=false`
  - [x] 2.3c 在 `connect_to_ground(Head, dir)` 中，對同條件的手動點設定 `widen_ctg=false`，並改用全長 `max_bridge_length_mm`（不按 `r/head_back_radius_mm` 縮放），修正 `routing_to_model` 路徑的加寬與消失問題

## 3. UI：apply_weight_preset() 新增 m_new_point_pillar_diameter 更新

- [x] 3.1 在 `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` 的 `apply_weight_preset()` 中，保留所有 `cfg.set(...)` 呼叫與 `CallAfter` tab 刷新區塊（行為不變）
- [x] 3.2 在 `m_new_point_head_diameter = p.head_front_diameter;` 之後加入 `m_new_point_pillar_diameter = p.pillar_diameter;`

## 4. UI：Gizmo 面板 per-point 顯示

- [x] 4.1 在 `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` 新增成員變數 `float m_new_point_pillar_diameter;`
- [x] 4.2 在 `apply_weight_preset()` 中加入 `m_new_point_pillar_diameter = p.pillar_diameter;`
- [x] 4.3 在 `on_set_state()` 初始化時，從全域 cfg 讀取 `support_pillar_diameter` 填入 `m_new_point_pillar_diameter`（作為初始值）
- [x] 4.4 在 `select_point()` 中，選取點時同步更新 `m_new_point_pillar_diameter`（從 `sp.pillar_radius * 2.f`；若 `pillar_radius == 0.f` 則 fallback 到全域 cfg 值）
- [x] 4.5 在 `gizmo_event()` 新增點時，設定 `sp.pillar_radius = m_new_point_pillar_diameter / 2.f;`
- [x] 4.6 在 `render_manual_support_panel()` 的 head diameter 滑桿下方，加入 pillar diameter 的唯讀顯示 label（顯示 `m_new_point_pillar_diameter` 的值）
- [x] 4.7 在 `on_save()` / `on_load()` 中加入 `m_new_point_pillar_diameter` 的序列化

## 5. 3MF 格式升級：version 1 → 2，新增 pillar_radius 欄位

- [x] 5.1 在 `src/libslic3r/Format/3mf.hpp` 將 `support_points_format_version` 從 1 改為 2
- [x] 5.2 在 `src/libslic3r/Format/3mf.cpp` 的寫入函數中，將 `"%f %f %f %f %f"` 改為 `"%f %f %f %f %f %f"`，新增第 6 個 float 為 `sp.pillar_radius`
- [x] 5.3 在讀取函數中加入 version 2 分支：每筆讀 6 個 token，第 6 個直接賦值給 `sp.pillar_radius`
- [x] 5.4 確認 version 1 讀取分支（5 個 token）仍正常執行，`pillar_radius` 設為 `0.f`

## 6. 驗證

- [x] 6.1 手動新增 Heavy / Medium / Light 三個支撐點後切片，確認三根柱體粗細不同
- [x] 6.2 存檔為 .3mf 後重開，確認各點 pillar_radius 保留，切片結果相同
- [x] 6.3 開啟舊版（version 1）.3mf 檔案，確認行為正常（所有手動點以全域設定大小切片）
- [x] 6.4 確認自動支撐的尺寸不受手動點 pillar_radius 影響（仍使用全域設定）
- [x] 6.5 切換 L/M/H 後確認全域 Support 設定頁的 `support_pillar_diameter` 等參數值更新為對應 preset 值，且選取既有手動點時支撐頁面不因此改動
- [x] 6.6 重新驗證（低傾斜面）：自動 Middle/Heavy 時，手動 Light 可正常產生（不再消失）；手動 Middle 在自動 Heavy 時使用 Middle 尺寸（不再變成 Heavy）
- [x] 6.7 重新驗證（高傾斜面）：自動 Middle/Heavy 時，手動 Light 使用 Light 尺寸、手動 Middle 在自動 Heavy 時使用 Middle 尺寸（皆不再被加寬至全域尺寸）
