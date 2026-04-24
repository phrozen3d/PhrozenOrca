## 1. 實作

- [x] 1.1 在 `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` line 712，將現有角度過濾條件改為僅對非 `manual_add` 點生效：
  ```cpp
  // Before:
  if (polar < M_PI / 2.0 + m_cfg.overhang_angle_threshold) return;
  // After:
  if (m_support_pts[fidx].type != SupportPointType::manual_add &&
      polar < M_PI / 2.0 + m_cfg.overhang_angle_threshold) return;
  ```

## 2. 驗證

- [x] 2.1 建置 slicer（`build_release_vs2022.bat slicer`）確認無編譯錯誤
- [x] 2.2 將 `support_critical_angle` 設為高值（如 60°），在傾斜角低於 60° 的面手動放置支撐點，切片後確認支撐柱生成
- [x] 2.3 確認自動支撐在同樣傾斜角低於 60° 的面仍被角度過濾丟棄（行為不變）
- [x] 2.4 確認手動支撐點放在朝上平面（`normal_cutoff_angle` 保護）仍不生成（幾何合理性保護保留）
- [x] 2.5 確認 Branching Support 模式下手動點同樣繞過角度過濾（N/A：此 fork UI 未暴露 support_tree_type 切換控制項；兩模式共用同一 filterfn，程式碼層面正確性已由靜態審查確認）
