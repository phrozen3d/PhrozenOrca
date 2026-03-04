# Step 2.4: SLA Preview Fix — 結果報告

**執行日期**: 2026-02-19
**前置條件**: Step 2.3 通過（Test 1~9 Pass），Test 10 發現 Slice Plate 後模型消失

---

## 問題描述

按下 "Slice Plate" 後，3D 場景中的模型消失，畫面變為空白。

**現象**：
- 按 Slice Plate → 模型立即從場景消失
- Preview 面板同樣空白（無任何 mesh）
- 返回 3D Editor 也是空白
- 重新選擇 SLA 印表機才能恢復模型

---

## 根因分析

### 主要根因：`load_print()` 缺少 `ptSLA` 分支

**完整呼叫鏈**：

```
on_action_slice_plate()               // Plater.cpp:7435
  │
  ├─ get_current_fff_print()          // ⚠️ SLA 模式卻存取 FFF print
  ├─ reslice()                        // 啟動背景切片
  └─ select_view_3D("Preview")        // ← 強制切換到 Preview（問題所在）
       │
       └─ set_current_panel(preview)
            │
            └─ preview->reload_print(false)
                 │
                 ├─ m_canvas->reset_volumes()    // ← 清除所有 3D 模型！
                 └─ load_print()                 // GUI_Preview.cpp:325
                      │
                      ├─ if (tech == ptFFF) load_print_as_fff()
                      └─ // ptSLA → 什麼都不做！← 💥 模型消失，畫面空白
```

### 次要問題：`on_action_slice_plate()` 和 `select_view_3D()` 無條件存取 FFF 資源

兩處函數在 SLA 模式下仍呼叫：
- `get_current_fff_print()` — SLA 無效呼叫
- `Model::setExtruderParams()` / `setPrintSpeedTable()` — FFF 專屬

---

## PrusaSlicer vs PhrozenOrca 流程比較

### PrusaSlicer 正確 SLA 切片流程

```
使用者點擊 "Slice now"
  │
  ├─ reslice()                              // 啟動背景切片
  │   └─ 【不主動切換視圖，留在 3D Editor】
  │
  ├─ 切片過程中：RELOAD_SLA_PREVIEW 事件
  │   └─ preview->reload_print() → load_print_as_sla() → 更新 Preview
  │
  ├─ 切片完成：update_sla_scene() → 3D Editor 即時顯示 support + pad
  │
  └─ 使用者手動切換到 Preview tab
       └─ load_print()
            └─ if (tech == ptSLA) load_print_as_sla()   ← 關鍵！
                 ├─ load_sla_preview() → 載入 object + support + pad mesh
                 ├─ 設定上下 clipping planes
                 └─ 顯示 layer slider，隱藏 moves slider
```

### PhrozenOrca 問題對照

| 項目 | PrusaSlicer | PhrozenOrca | 影響 |
|------|-------------|-------------|------|
| `Preview::load_print()` | 有 `ptSLA` 分支 | **只有 `ptFFF` 分支** | **模型消失的直接原因** |
| `load_print_as_sla()` | 存在（line 1021） | **不存在** | Preview 無法顯示 SLA 結果 |
| Slice 按鈕行為 | 不切換視圖，留在 3D Editor | **強制切換到 Preview** | 觸發 reset_volumes |
| 切片前 FFF 參數 | 不存取 FFF print | 無條件 `get_current_fff_print()` | SLA 模式存取錯誤物件 |

---

## 實作方案

### 修改 1：`GUI_Preview.hpp` — 新增 `load_print_as_sla()` 宣告

**位置**: `Preview` class private 區塊，`load_print_as_fff()` 宣告之後（line 184）

```cpp
void load_print_as_fff(bool keep_z_range = false, bool only_gcode = false);
void load_print_as_sla();   // Step 2.4: SLA preview support
```

---

### 修改 2：`GUI_Preview.cpp` — `load_print()` 加入 `ptSLA` 分支

**位置**: `load_print()` 函數（line 325-334）

```cpp
// BEFORE:
void Preview::load_print(bool keep_z_range, bool only_gcode)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range, only_gcode);
    Layout();
}

// AFTER:
void Preview::load_print(bool keep_z_range, bool only_gcode)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range, only_gcode);
    else if (tech == ptSLA)     // Step 2.4: SLA preview branch
        load_print_as_sla();

    Layout();
}
```

---

### 修改 3：`GUI_Preview.cpp` — 實作 `load_print_as_sla()`

**位置**: `load_print_as_fff()` 函數結束後，`AssembleView` 建構函數之前（line 785-817）

**參考**: PrusaSlicer `GUI_Preview.cpp:1021-1057`

**PhrozenOrca 適配說明**:
- `m_loaded` (bool) → 改為 `m_loaded_print` (const PrintBase*) — PhrozenOrca 的快取機制
- `m_moves_slider->Hide()` → 改為 `show_moves_sliders(false)` — PhrozenOrca API
- `update_layers_slider(zs)` → 跳過（IMSlider 是 GCodeViewer 專屬，不支援 SLA clipping planes）
- `m_canvas->set_use_clipping_planes()`, `reset_clipping_planes_cache()`, `load_sla_preview()` 皆已存在

```cpp
// Step 2.4: Load SLA print preview shells and set up clipping planes.
// Ported from PrusaSlicer GUI_Preview.cpp:1021 (load_print_as_sla).
// Adapted for PhrozenOrca: use m_loaded_print instead of m_loaded bool;
// skip FFF-specific IMSlider (GCodeViewer slider cannot drive SLA clipping planes).
void Preview::load_print_as_sla()
{
    const SLAPrint* print = m_process->sla_print();
    if (print == nullptr)
        return;

    // Cache check: avoid redundant reloads within the same slicing result.
    // m_loaded_print is reset to nullptr by reload_print(keep_volumes=false).
    if (m_loaded_print == print)
        return;

    m_canvas->reset_clipping_planes_cache();
    m_canvas->set_use_clipping_planes(true);

    if (IsShown()) {
        // Load SLA object mesh + support tree + pad meshes into Preview canvas.
        // Internally calls reset_volumes() then _load_sla_shells().
        // Only loads objects whose slaposSliceSupports step has completed.
        m_canvas->load_sla_preview();

        // SLA has no GCode moves slider
        show_moves_sliders(false);
        // SLA layer slider (clipping-plane driven) not yet implemented.
        // The GCodeViewer IMSlider is FFF-specific and cannot drive SLA clipping planes.
        show_layers_sliders(false);

        m_loaded_print = print;
    }
}
```

---

### 修改 4：`Plater.cpp` — `on_action_slice_plate()` SLA guard

**位置**: `on_action_slice_plate()` (line 7437-7458)

**改動**:
1. SLA 模式跳過 FFF extruder/speed table 設定
2. SLA 切片後不強制切換到 Preview（符合 PrusaSlicer 行為：留在 3D Editor）

```cpp
void Plater::priv::on_action_slice_plate(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received slice plate event\n";
        // Step 2.4: SLA mode does not use FFF extruder/speed table params.
        // Also skip Preview auto-switch for SLA: stay in 3D Editor during slicing
        // (supports appear incrementally); user manually switches to Preview for layer view.
        if (this->printer_technology != ptSLA) {
            const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
            auto& print = q->get_partplate_list().get_current_fff_print();
            auto print_config = print.config();
            int numExtruders = wxGetApp().preset_bundle->filament_presets.size();
            Model::setExtruderParams(config, numExtruders);
            Model::setPrintSpeedTable(config, print_config);
        }
        m_slice_all = false;
        q->reslice();
        if (this->printer_technology != ptSLA)   // Step 2.4: SLA stays in 3D editor
            q->select_view_3D("Preview");
    }
}
```

---

### 修改 5：`Plater.cpp` — `select_view_3D("Preview")` SLA guard

**位置**: `select_view_3D()` 函數的 `"Preview"` 分支（line 3652-3665）

**改動**: SLA 模式跳過 FFF extruder/speed table 設定

```cpp
else if (name == "Preview") {
    BOOST_LOG_TRIVIAL(info) << "select preview";
    // Step 2.4: SLA mode has no FFF extruder/speed table.
    if (this->printer_technology != ptSLA) {
        const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
        auto& print = q->get_partplate_list().get_current_fff_print();
        auto print_config = print.config();
        int numExtruders = wxGetApp().preset_bundle->filament_presets.size();
        Model::setExtruderParams(config, numExtruders);
        Model::setPrintSpeedTable(config, print_config);
    }
    set_current_panel(preview, no_slice);
}
```

---

## 確認可用的 PhrozenOrca API

| API | 位置 | 狀態 |
|-----|------|------|
| `m_canvas->load_sla_preview()` | `GLCanvas3D.cpp:2921` | ✅ 存在 |
| `m_canvas->set_use_clipping_planes(bool)` | `GLCanvas3D.hpp:802` | ✅ 存在 |
| `m_canvas->reset_clipping_planes_cache()` | `GLCanvas3D.hpp:801` | ✅ 存在 |
| `show_moves_sliders(bool)` | `GUI_Preview.cpp:433` | ✅ 存在（TODO 空實作） |
| `show_layers_sliders(bool)` | `GUI_Preview.cpp:438` | ✅ 存在（TODO 空實作） |
| `m_loaded_print` (const PrintBase*) | `GUI_Preview.hpp:109` | ✅ 存在 |
| `m_process->sla_print()` | `BackgroundSlicingProcess.hpp` | ✅ 存在 |

---

## 修改檔案總覽

| 檔案 | 修改內容 | 位置 |
|------|----------|------|
| `src/slic3r/GUI/GUI_Preview.hpp` | 修改 1：新增 `load_print_as_sla()` 宣告 | line 184 |
| `src/slic3r/GUI/GUI_Preview.cpp` | 修改 2：`load_print()` 加入 `ptSLA` 分支 | line 330-331 |
| `src/slic3r/GUI/GUI_Preview.cpp` | 修改 3：實作 `load_print_as_sla()` | line 785-817 |
| `src/slic3r/GUI/Plater.cpp` | 修改 4：`on_action_slice_plate()` SLA guard | line 7441-7456 |
| `src/slic3r/GUI/Plater.cpp` | 修改 5：`select_view_3D("Preview")` SLA guard | line 3654-3665 |

---

## 測試計畫與結果

| # | 測試項目 | 預期結果 | 實測結果 | 備註 |
|---|----------|----------|----------|------|
| 10.1 | SLA: 按 Slice Plate 後模型不消失 | 留在 3D Editor，模型仍可見 | ✅ Pass | |
| 10.2 | SLA: 手動切換 Preview 後模型可見 | Preview 顯示 SLA shells (object + support + pad) | ✅ Pass | |
| 10.3 | FDM 回歸：Slice Plate 流程 | 自動切換到 Preview，GCode toolpaths 正常顯示 | ✅ Pass | |
| 10.4 | SLA 切片進度更新（進階） | 切片過程中 3D Editor 逐步顯示 support mesh | ✅ Pass | 兩項 Step 2.5 修復後通過，詳見下節 |

---

## Test 10.4 根因分析：SLA 切片進度無更新

### 現象

SLA 切片過程中，3D Editor 不更新 support mesh；切片完成後也無任何場景更新。

### 根因：`sla_print.set_status_callback()` 從未被呼叫

**完整事件鏈**（正確應有的流程）：

```
SLAPrintSteps 執行各步驟
  │
  ├─ slaposSupportTree 完成 → set_status(RELOAD_SCENE)
  ├─ slaposGeneratePad 完成 → set_status(RELOAD_SCENE)
  ├─ slaposSliceSupports 完成 → set_status(RELOAD_SLA_PREVIEW)
  └─ slaposIndexSlices 完成 → set_status(RELOAD_SLA_PREVIEW)
       │
       └─ status_callback(status)          ← ⚠️ callback 是 nullptr！
            │
            └─ wxQueueEvent(EVT_SLICING_UPDATE)  ← 永遠不會發出！
                 │
                 └─ on_slicing_update()    ← 永遠不會被呼叫！
                      ├─ update_sla_scene()     ← 3D Editor 場景不更新
                      └─ preview->reload_print()  ← Preview 不更新
```

### 根因細節

**PhrozenOrca `Plater.cpp` 約第 3060-3076 行，整個 callback 設定區塊被注解：**

```cpp
// BBS: to be checked. Not follow patch.
/*
background_process.set_fff_print(&fff_print);
background_process.set_sla_print(&sla_print);
...
auto statuscb = [this](const Slic3r::PrintBase::SlicingStatus &status) {
    wxQueueEvent(this->q, new Slic3r::SlicingStatusEvent(EVT_SLICING_UPDATE, 0, status));
};
fff_print.set_status_callback(statuscb);
sla_print.set_status_callback(statuscb);   // ← 永遠不執行
*/
```

**FFF 為何能正常運作？**

`PartPlate::update_slice_context()`（PartPlate.cpp:2851-2865）在每次切片前替 FFF print 設定 callback：

```cpp
// PartPlate.cpp:2865
m_print->set_status_callback(statuscb);   // m_print 是 FFF print，SLA 沒有對應
```

PartPlate 沒有 `m_sla_print`，且 Step 2.3 的 SLA guard 使 SLA 完全不經過 `update_slice_context()`，所以 SLA callback 始終為 `nullptr`。

### PrusaSlicer 正確做法（Plater.cpp:672-677）

```cpp
auto statuscb = [this](const Slic3r::PrintBase::SlicingStatus &status) {
    wxQueueEvent(this->q, new Slic3r::SlicingStatusEvent(EVT_SLICING_UPDATE, 0, status));
};
// 同時設定 FFF 和 SLA
for (auto& p : fff_prints) p->set_status_callback(statuscb);
for (auto& p : sla_prints) p->set_status_callback(statuscb);
```

### 後端確認（無問題）

`SLAPrintSteps.cpp` 兩個 repo 完全相同，callback 發送點正確：

| 步驟 | 位置 | 發送旗標 |
|------|------|---------|
| `slaposSupportTree` | line 702 | `RELOAD_SCENE` |
| `slaposGeneratePad` | line 740 | `RELOAD_SCENE` |
| `slaposSliceSupports` | line 771 | `RELOAD_SLA_PREVIEW` |
| `slaposIndexSlices` | line 1052 | `RELOAD_SLA_PREVIEW` |

後端正確，問題完全在於 callback 從未被設定。

### PrusaSlicer vs PhrozenOrca 對照

| 項目 | PrusaSlicer | PhrozenOrca | 影響 |
|------|-------------|-------------|------|
| `sla_print.set_status_callback()` | ✅ 在 Plater 初始化時設定 | ❌ **整個區塊被注解** | SLA 進度事件無法送達 UI |
| FFF callback 設定 | Plater 初始化設定 | PartPlate::update_slice_context() 在每次切片前設定 | FFF 正常，SLA 斷鏈 |
| `EVT_SLICING_UPDATE` 接收 | 正常 | SLA 永遠不會觸發 | `on_slicing_update()` 不處理 SLA |

### Step 2.5 修復實作

#### 修復 1：`Plater.cpp` — 註冊 SLA status callback

**位置**：`Plater.cpp` lines 3060-3071，緊接 `background_process.set_sla_print(&sla_print)` 之後

**問題**：SLA callback 在被注解的區塊中，且 PhrozenOrca 的 PartPlate 系統只在 FFF 切片前設定 FFF callback，SLA 沒有對應路徑，始終為 `nullptr`。

**修復**（已套用）：

```cpp
// Step 2.5: Register SLA slicing progress callback.
// The commented-out block below registers this for both FFF and SLA, but
// PhrozenOrca's PartPlate system handles FFF via update_slice_context() instead.
// SLA has no PartPlate equivalent, so we register sla_print's callback here directly.
// Without this, SLAPrintSteps emits RELOAD_SCENE/RELOAD_SLA_PREVIEW but the
// EVT_SLICING_UPDATE events are never posted → on_slicing_update() never fires for SLA.
{
    auto statuscb_sla = [this](const Slic3r::PrintBase::SlicingStatus &status) {
        wxQueueEvent(this->q, new Slic3r::SlicingStatusEvent(EVT_SLICING_UPDATE, 0, status));
    };
    sla_print.set_status_callback(statuscb_sla);
}
```

**結果**：SLA 切片過程中 `EVT_SLICING_UPDATE` 事件正確送達 UI，`on_slicing_update()` 開始處理 SLA 進度。

---

#### 中間問題：進度卡在 30% 很久後直接完成

修復 1 後，進度出現，但 `slaposSliceSupports`（步驟權重最大，佔 30%）沒有任何中間進度回報，導致進度條停在 slaposSupportTree 結束點，然後等 `slice_supports()` 完成後才以 `RELOAD_SLA_PREVIEW` 跳過這 30%。

**步驟權重分佈**（`OBJ_STEP_LEVELS`）：

| 步驟 | 權重 | 進度回報 |
|------|------|---------|
| slaposHollowing | 10 | 有 |
| slaposDrillHoles | 10 | 有 |
| slaposObjectSlice | 10 | 有 |
| slaposSupportPoints | 20 | 有 |
| slaposSupportTree | 10 | 有 |
| slaposGeneratePad | 10 | 有 |
| **slaposSliceSupports** | **30** | **無（僅步驟結束時的 RELOAD_SLA_PREVIEW）** |

**根因**：`slice_supports()` 的主要計算是單一不可中斷的 `support_tree_ptr->slice()` 呼叫，沒有 callback 注入點；步驟開始前未調用任何正數 percent 的 `report_status()`。

#### 修復 2：`SLAPrintSteps.cpp` — slice_supports() 步驟開始進度回報

**位置**：`SLAPrintSteps.cpp` line 764，`sd->support_tree_ptr->slice()` 呼叫前

**修復**（已套用）：

```cpp
// Step 2.5: Report that slice_supports is starting so the progress bar
// advances from slaposSupportTree (prev step) into this step's range.
// Without this, the UI stays stuck at the slaposSupportTree end-percent
// until RELOAD_SLA_PREVIEW fires at the very end of this step.
report_status(current_status(), OBJ_STEP_LABELS(slaposSliceSupports));
```

**說明**：
- `current_status()` 回傳當前累積進度值（即 slaposSupportTree 結束點）
- 這個呼叫發送正數 percent 的事件，讓 UI 進度條移動到 slaposSliceSupports 的起始範圍
- `support_tree_ptr->slice()` 本身無法細分，步驟內仍無法顯示細粒度進度
- 步驟結束時的 `RELOAD_SLA_PREVIEW`（負數 percent）觸發 Preview 更新

**已確認**：`on_slicing_update()` 的現有 SLA 分支（`ptSLA` + `RELOAD_SCENE`、`RELOAD_SLA_PREVIEW`）皆正確，無需修改。

---

## 已知限制（後續任務）

### SLA Layer Slider 尚未實作

**現況**: `show_layers_sliders(false)` — 滑動條隱藏，無法操作

**原因**: PhrozenOrca 的 IMSlider 嵌入在 GCodeViewer 內，屬 FFF 專屬架構，無法直接用於 SLA clipping planes。

**PrusaSlicer 的做法**:
- 使用 `DSForLayers` (DoubleSlider) 控制上下兩個 clipping planes
- `on_layers_slider_scroll_changed()` 監聽 slider 移動，動態調整 `set_clipping_plane()` 呼叫

**PhrozenOrca 需要的工作**:
- 新增 SLA 專屬 slider 或擴展現有 IMSlider 機制
- 實作 SLA clipping plane 連動邏輯
- **這是獨立的後續任務**，不在本次範圍

---

## 風險評估

| 風險 | 機率 | 影響 | 緩解 |
|------|------|------|------|
| FDM 功能受影響 | 極低 | 高 | 修改 4/5 均有 `ptSLA` 條件守衛，FDM 路徑完全不變 |
| `load_sla_preview()` 在切片未完成時呼叫 | 低 | 低 | `_load_sla_shells()` 只載入已完成 `slaposSliceSupports` 的物件 |
| `m_loaded_print` 快取失效 | 低 | 低 | `reload_print(keep_volumes=false)` 時 `m_loaded_print = nullptr` 重設，下次正常重載 |
| `show_layers_sliders(false)` 隱藏 FDM slider | 無 | 無 | `show_layers_sliders()` 目前是空實作，對 FDM 無影響 |

---

## 備註

- 修改 2/3 解決模型消失的直接原因（`load_print()` 缺少 SLA 分支）
- 修改 4 解決 SLA 模式下不應強制切換到 Preview 的問題（符合 PrusaSlicer 行為）
- 修改 5 解決 SLA 模式進入 Preview 時存取 FFF 資源的問題
- SLA Layer Slider 功能（clipping plane 連動）延後至後續步驟實作
- 所有 FDM 功能的行為完全不受影響
