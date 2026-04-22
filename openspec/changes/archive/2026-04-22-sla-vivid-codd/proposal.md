## Why

PhrozenOrca 在 `PrintConfig`（FDM 用途的類別）中加入了一批 SLA 支撐 UI 參數（`support_point_diameter`、`pillar_diameter`、`support_boss_height` 等），但 `SLAPrint::apply()` 的 `diff()` 只讀取 `SLAPrintObjectConfig` 已知的 key，導致這批 UI 參數從未傳入演算法，使用者在設定頁的調整完全無效。

## What Changes

- 將 SLA 支撐 UI 參數的 key 從 `PrintConfig` 遷移到 `SLAPrintObjectConfig`，確保 `make_support_cfg()` 能正確讀取
- 新增 `support_contact_type`（None / Sphere）與 `support_contact_diameter` 兩個欄位至 `SLAPrintObjectConfig`
- 在 `SupportTreeConfig` 新增 `contact_sphere_radius_mm`，在 pinhead 前球位置疊加更大的接觸球幾何
- 新增 `contact_sphere_radius_mm` 至 `invalidate_state_by_config_options()` 的重切白名單
- Light / Medium / Heavy radio button 改為全域 print config preset selector，取代舊有的每點倍率機制
- 移除 `SupportTreeBuildsteps.cpp` 中讀取 `SupportPoint::weight` 做倍率與叢集排序的邏輯

## Capabilities

### New Capabilities

- `sla-support-param-wiring`：將現有 UI 支撐參數（柱徑、底座尺寸、前球直徑等）正確接通至 `SLAPrintObjectConfig` → `SupportTreeConfig` → 支撐演算法
- `sla-contact-sphere`：`contact_type = Sphere` 模式，在標準 pinhead 前球的相同圓心位置疊加一個更大的接觸球，增加接觸面積
- `sla-weight-preset`：Light / Medium / Heavy 改為全域支撐參數 preset，切換時寫入 print config 並觸發重切，移除舊有的每點半徑倍率邏輯

### Modified Capabilities

（無既有 spec 需要修改）

## Impact

**受影響的源碼檔案**
- `src/libslic3r/PrintConfig.hpp / .cpp`：在 `SLAPrintObjectConfig` 新增欄位；舊 `PrintConfig` key 保留不刪（避免 preset 讀取爆炸）
- `src/libslic3r/SLAPrint.cpp`：`make_support_cfg()` 補上對應賦值；`invalidate_state_by_config_options()` 補上新 key
- `src/libslic3r/SLA/SupportTree.hpp`：`SupportTreeConfig` 新增 `contact_sphere_radius_mm`
- `src/libslic3r/SLA/SupportTreeBuilder.hpp`：`Head` struct 新增 `r_contact_mm`
- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`：移除 weight 倍率與叢集排序邏輯；在 `filterfn` 賦值 `r_contact_mm`
- `src/libslic3r/SLA/SupportTreeMesher.hpp`：`get_mesh<Head>` 新增接觸球幾何
- `src/slic3r/GUI/Tab.cpp`：更新 Support 設定頁的 UI key 對應
- `src/slic3r/GUI/GLGizmoSlaSupports.cpp`：L/M/H 切換改為呼叫 `apply_weight_preset()`；開啟 Gizmo 時同步 radio 狀態
