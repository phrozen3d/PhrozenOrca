## Why

手動新增 SLA 支撐點時，切換 Light / Middle / Heavy preset 會直接覆寫全域 print config，導致切片時所有手動點都套用最後一次選擇的尺寸，而非各點放置當下的設定。`SupportPoint::weight` 欄位雖已儲存，但後端算法仍只讀全域 `head_back_radius_mm`，且 3MF 格式並未保存 weight，存檔重開後資訊完全遺失。

## What Changes

- `apply_weight_preset()` 保留 `cfg.set(...)` 呼叫（支撐頁面即時更新），新增更新 Gizmo 狀態變數 `m_new_point_pillar_diameter = p.pillar_diameter`
- Gizmo 面板在選取既有點時顯示該點存儲的 `pillar_radius` 值（不觸碰全域 cfg，支撐頁面維持上次 L/M/H 設定）
- `SupportPoint` 新增 `float pillar_radius = 0.f` 欄位，放置手動點時存入當下的 pillar 半徑值
- `SupportTreeBuildsteps::filter()` 對手動放置點讀取 `sp.pillar_radius > 0` 作為 back_radius；自動生成點繼續使用全域 `head_back_radius_mm`
- **BREAKING**（3MF format v1 → v2）：`Slic3r_PE_sla_support_points.txt` 格式由 5 個 float 擴展為 6 個（新增 `pillar_radius` 欄位，mm 值），舊版檔案（version=1）讀入時 `pillar_radius` 預設為 `0.f`（使用全域設定）

## Capabilities

### New Capabilities

無新增獨立 capability。

### Modified Capabilities

- `sla-weight-preset`：移除「L/M/H 切換 SHALL 寫入全域 print config 並觸發重切」的需求；改為「L/M/H 切換只更新 Gizmo UI 狀態，不觸碰全域設定」
- `sla-support-weight-geometry`：算法規格改為——手動點使用 `SupportPoint::pillar_radius` 直接作為 back_radius 而非全域值；新增 3MF `pillar_radius` 欄位持久化需求

## Impact

- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp / .cpp`：`apply_weight_preset()`、`render_manual_support_panel()`、`gizmo_event()`、`select_point()`、`on_set_state()`
- `src/libslic3r/SLA/SupportPoint.hpp`：新增 `pillar_radius` 欄位、更新 `serialize()`
- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`：`filter()` 使用 `sp.pillar_radius > 0` 判斷式
- `src/libslic3r/Format/3mf.cpp / .hpp`：格式版本 1 → 2，新增 `pillar_radius` float
- 向後相容：3MF version=1 讀入時 `pillar_radius = 0.f`，切片行為等同修改前
