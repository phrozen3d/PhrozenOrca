# Phase 1 - SLA UI 穩定性修復與 Gizmo 啟用 — 執行記錄

**計劃文件**: [Phase1_SLA_UI_Stability_and_Gizmo_Plan.md](../StepAnalyze/Phase1_SLA_UI_Stability_and_Gizmo_Plan.md)
**開始日期**: 2026-02-12

---

## 前置修復（commit 28e6472d4c）

在進入 Gizmo 任務前，已修復 SLA profile 切換的 5 個 crash：

| # | 檔案 | 修復內容 |
|---|------|---------|
| 1 | Tab.cpp | `on_preset_loaded()` - nozzle_diameter 加 ptFFF guard |
| 2 | Tab.cpp | `toggle_options()` - SLA early return |
| 3 | BackgroundSlicingProcess.cpp | `apply()` - mainframe/plater nullptr guard |
| 4 | BackgroundSlicingProcess.cpp | `execute_ui_task()` - mainframe/plater nullptr guard + cancel fallback |
| 5 | Plater.cpp | `on_config_change()` - 加 m_plater 存在檢查 |
| 6 | PresetBundle.cpp | `reset_project_embedded_presets()` - SLA guard 跳過 filament_presets |
| 7 | Plater.cpp | Sidebar SLA Print / SLA Material 面板初始化 + show/hide 切換 |

---

## Task 1: 修復 BrimEars crash

**狀態**: ✅ 完成

### 問題分析
啟動時載入 SL1 preset → crash 在 `GLGizmoBrimEars.cpp:1157`
- 呼叫鏈：`GLGizmosManager::init()` → 對所有 gizmo 呼叫 `on_init()` → `get_brim_default_radius()` 存取 `nozzle_diameter`
- SLA printer 無 `nozzle_diameter` 選項 → nullptr crash
- 根因：`on_init()` 對所有 gizmo 無條件呼叫，BrimEars 未判斷 printer technology 就存取 FDM 參數
- 其他 FDM gizmo（FdmSupports/Seam/FuzzySkin）在 `GLGizmoPainterBase` 基類已有 ptFFF 檢查，不受影響
- BrimEars 是 PhrozenOrca 自有 gizmo（PrusaSlicer 無此功能）

### 修改（兩處）
**檔案**: `src/slic3r/GUI/Gizmos/GLGizmoBrimEars.cpp`

**修改 1 - `on_init()` (line 41)**: SLA 下跳過 FDM config 存取，給安全預設值
```cpp
bool GLGizmoBrimEars::on_init()
{
    // BrimEars is FDM-only; skip FDM config access for SLA printers (on_init is called for all gizmos)
    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        m_new_point_head_diameter = 0.f;
    else
        m_new_point_head_diameter = get_brim_default_radius();
    // ... rest unchanged
}
```

**修改 2 - `on_is_activable()` (line 828)**: SLA 下不可啟用此 gizmo
```cpp
bool GLGizmoBrimEars::on_is_activable() const
{
    // BrimEars is FDM-only - requires nozzle_diameter and print settings
    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF)
        return false;

    const Selection &selection = m_parent.get_selection();
    if (!selection.is_single_full_instance()) return false;
    return true;
}
```

### 迭代過程
1. 第一次嘗試：僅修改 `on_is_activable()` 加 ptFFF 檢查 → 仍 crash，因為 `on_init()` 在 `on_is_activable()` 之前被呼叫
2. 第二次修正：在 `on_init()` 內加 printer technology 判斷，SLA 下設 `m_new_point_head_diameter = 0.f` → 成功

### 測試結果
- [x] 編譯成功
- [x] 啟動載入 SL1 preset 不 crash
- [ ] 切換回 FDM printer 後 BrimEars 仍可正常使用（待驗證）

---

## Task 2: 啟用 SLA Gizmos

**狀態**: 待執行

---

## Task 3: 驗證 3D Viewer SLA 功能

**狀態**: 待執行
