# Step 2.5: SLA Layer Slider — 開發結果記錄

**日期**: 2026-02-25
**狀態**: ✅ 編譯通過，T1–T7 測試全部通過
**方案**: B（修改 IMSlider + Preview 架構）

---

## 實作目標

填補 Step 2.4 留下的已知缺口：`show_layers_sliders()` 為空 stub，
使用者無法在 SLA Preview 中逐層瀏覽切片結果。

**解決方式**：
- 修改 `IMSlider` 加入 visibility flag + callback 機制
- Preview 透過 `get_layers_slider()` 取得 IMSlider 指標，設定 SLA z-values 與 callback
- Callback 觸發時更新 `set_clipping_plane()` 實現逐層顯示
- 修復 `GCodeViewer::render()` 在 SLA 模式下的提早退出問題（Bug Fix）
- 修復 `Plater::record_slice_preset()` 在 SLA 模式下 nullptr crash（Bug Fix）
- 修復 `GLCanvas3D::load_sla_preview()` 未清除 FDM shell 殘影問題（Bug Fix）
- 修復 `show_moves_sliders()` 空 stub 導致 FDM 橫向 slider 在 SLA 模式下持續顯示（Bug Fix）
- 升級 `on_sla_layer_slider_changed()` 支援雙指頭範圍選取（下指頭 = 底部截面，上指頭 = 頂部截面）
- 修復 IMSlider 單/多層切換按鈕不觸發 callback 問題（Bug Fix）
- 修復 callback 在 ImGui render frame 內呼叫 `render()` 命中 re-entrancy guard 導致畫面不即時更新（Bug Fix）

---

## 修改清單

### IMSlider.hpp（共用元件，SLA-safe 修改）

**新增 `#include <functional>`**（line 8 後）：
```cpp
#include <functional>
#include <set>
```

**新增 public API**（`set_menu_enable()` 之後）：
```cpp
// Step 2.5: SLA layer preview visibility and callback.
// m_visible guards render(); defaults to true so FFF path is unaffected.
// m_on_change_callback fires when vertical slider changes value; nullptr = no-op.
void Show(bool show = true) { m_visible = show; }
void Hide() { m_visible = false; }
bool IsShown() const { return m_visible; }
void set_on_change_callback(std::function<void()> cb) { m_on_change_callback = std::move(cb); }
```

**新增 private 成員**（`m_render_as_disabled` 之後）：
```cpp
bool m_visible { true };                     // Step 2.5: controls render() visibility
std::function<void()> m_on_change_callback;  // Step 2.5: fired on value change (SLA only)
```

**FDM 安全性**：`m_visible` 預設 `true`、callback 預設 `nullptr` → FFF 路徑完全不受影響。

---

### IMSlider.cpp（render() 修改，含 Bug Fix）

**render() 開頭加 visibility guard + empty-values guard**（Bug Fix）：
```cpp
bool IMSlider::render(int canvas_width, int canvas_height)
{
    // Step 2.5: Visibility guard — returns false immediately when hidden.
    // m_visible defaults to true so FFF layers slider is unaffected.
    if (!m_visible)
        return false;

    // Step 2.5: Empty-values guard — don't render before any print is loaded.
    // Prevents rendering an empty/broken slider when m_roles is empty (pre-slice state).
    if (m_values.empty())
        return false;

    bool result = false;
    // ... 現有程式碼 ...
```

**render() 末尾（PopStyleColor 之後、return 之前）加 callback fire**：
```cpp
    // Step 2.5: Fire SLA callback when vertical slider value changes.
    // m_on_change_callback is nullptr for FFF (set only from load_print_as_sla).
    if (result && m_on_change_callback)
        m_on_change_callback();

    return result;
```

**one_layer_button 點擊時直接呼叫 callback（Bug Fix）**：

`switch_one_layer_mode()` 只改 flag，不設 `result = true`，原本 callback 完全不被觸發。
修正：在按鈕點擊時直接呼叫 `m_on_change_callback`（FFF 無影響，callback 為 nullptr）：

```cpp
        if (ImGui::ImageButton3(normal_id, hover_id, ONE_LAYER_BUTTON_SIZE * m_scale)) {
            switch_one_layer_mode();
            // Step 2.5: Notify SLA clipping planes of mode change.
            // result is not set here (mode change ≠ value change for FFF), so fire directly.
            if (m_on_change_callback)
                m_on_change_callback();
        }
```

---

### GCodeViewer.cpp（Bug Fix — render_slider 路徑修復）

**問題**：SLA 模式無 GCode，`m_roles.empty() == true`，`GCodeViewer::render()` 在提早 `return` 前未呼叫 `render_slider()`，導致 IMSlider 永遠不被渲染。

**修復**（`if (m_roles.empty())` block 內加 `render_slider()` 呼叫）：
```cpp
    if (m_roles.empty()) {
        // Step 2.5: SLA mode has no GCode roles but still needs the layer slider.
        // render_slider() is normally called at the end of this function, but since
        // we return early here, we call it explicitly so IMSlider can render in SLA mode.
        // IMSlider::m_visible and m_values.empty() guard against unwanted renders.
        render_slider(canvas_width, canvas_height);
        return;
    }
```

---

### GLCanvas3D.cpp（Bug Fix — FDM shell 殘影修復）

**問題**：`load_sla_preview()` 呼叫 `reset_volumes()` 清除 GLVolumeCollection，
但 `GCodeViewer` 是獨立物件，其 FDM 資料（m_shells.volumes、m_roles）未被清除。
`GCodeViewer::render()` 的 `render_shells()` 在任何 guard 之前執行，
導致 FDM model mesh 以半透明方式疊加在 SLA preview 上。

**觸發條件**：FDM 切片 → 切換 SLA printer → 切片 → 進入 Preview

**修復**（在 `load_sla_preview()` 的 `_load_sla_shells()` 之前加入 GCodeViewer 清除）：
```cpp
void GLCanvas3D::load_sla_preview()
{
    const SLAPrint* print = sla_print();
    if (m_canvas != nullptr && print != nullptr) {
        _set_current();
        // Release OpenGL data before generating new data.
        reset_volumes();
        // Step 2.5 Fix: Clear FDM GCodeViewer data when entering SLA mode.
        // GCodeViewer::render() calls render_shells() BEFORE the m_roles.empty() guard,
        // so stale FDM shell meshes from a previous FDM slice would render as a
        // semi-transparent overlay on top of the SLA preview.
        // reset()       — clears m_roles, m_buffers (toolpath GPU data)
        // reset_shell() — clears m_shells.volumes (the FDM semi-transparent model)
        m_gcode_viewer.reset();
        m_gcode_viewer.reset_shell();
        _load_sla_shells();
```

**GCodeViewer 資料清除說明**：

| 呼叫 | 清除內容 | 效果 |
|------|---------|------|
| `reset()` | m_roles, m_buffers, m_layers 等 toolpath 資料 | 確保 SLA early-return 路徑正確 |
| `reset_shell()` | m_shells.volumes（FDM 半透明 model mesh）| 消除 FDM 殘影 |
| `reset_volumes()`（原有）| GLVolumeCollection（canvas volumes）| 清除舊 SLA/FDM volumes |

---

### Plater.cpp（Bug Fix — record_slice_preset SLA crash 修復）

**問題**：`Plater::record_slice_preset()` 是 FFF 專用遙測函數，
在 SLA 模式下呼叫時，`bundle->filaments.find_preset()` 查找 SLA 材料名稱
（SLA 材料存在 `bundle->sla_materials`，非 `bundle->filaments`）→ 回傳 nullptr
→ `filament_preset->is_system` nullptr dereference crash。

**觸發條件**：FDM 切片 → 切換 SLA printer → 執行切片

**修復**（函數入口加 ptSLA guard）：
```cpp
void Plater::record_slice_preset(std::string action)
{
    // Step 2.5 Fix: This function records FFF-specific analytics (filament presets, bed type,
    // print process params). None of these concepts apply to SLA printers, and the filament
    // lookup (bundle->filaments.find_preset) returns nullptr for SLA material names because
    // SLA materials live in bundle->sla_materials, not bundle->filaments.
    // Skip entirely for SLA to avoid nullptr dereference crash.
    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA)
        return;
    // ...
```

---

### GUI_Preview.hpp（新增 private 成員與宣告）

```cpp
    void load_print_as_sla();   // Step 2.4: SLA preview support

    // Step 2.5: SLA layer slider (drives IMSlider via callback → clipping planes).
    std::vector<double> m_sla_layers_z; // SLA layer z-coordinates in mm
    void on_sla_layer_slider_changed(); // Updates clipping planes on IMSlider value change
};
```

---

### GUI_Preview.cpp（4 處修改）

#### 1. show_layers_sliders() — 實作 stub

```cpp
void Preview::show_layers_sliders(bool show)
{
    // Step 2.5: Control IMSlider visibility for SLA layer preview.
    // FFF path never calls this function — FFF slider visibility is managed by GCodeViewer.
    IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
    if (slider != nullptr)
        slider->Show(show);
}
```

#### 2. load_print_as_sla() — 完整改寫

新增 z-extraction 邏輯 + IMSlider 配置 + callback 註冊：

```cpp
void Preview::load_print_as_sla()
{
    const SLAPrint* print = m_process->sla_print();
    if (print == nullptr)
        return;
    if (m_loaded_print == print)
        return;

    m_canvas->reset_clipping_planes_cache();
    m_canvas->set_use_clipping_planes(true);

    // Step 2.5: Extract layer z-coordinates for IMSlider.
    m_sla_layers_z.clear();
    double initial_layer_height = print->material_config().initial_layer_height.value;
    for (const SLAPrintObject* obj : print->objects()) {
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty()) {
            auto low_coord = obj->get_slice_index().front().print_level();
            for (const auto& rec : obj->get_slice_index())
                m_sla_layers_z.emplace_back(
                    initial_layer_height + (rec.print_level() - low_coord) * SCALING_FACTOR);
        }
    }
    sort_remove_duplicates(m_sla_layers_z);

    if (IsShown()) {
        m_canvas->load_sla_preview();
        show_moves_sliders(false);

        IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
        if (!m_sla_layers_z.empty() && slider != nullptr) {
            slider->SetSliderValues(m_sla_layers_z);
            slider->SetMaxValue((int)m_sla_layers_z.size() - 1);
            slider->SetSelectionSpan(0, (int)m_sla_layers_z.size() - 1);
            slider->SetHigherValue((int)m_sla_layers_z.size() - 1); // top = show all

            m_canvas->set_clipping_plane(0, ClippingPlane::ClipsNothing());
            m_canvas->set_clipping_plane(1, ClippingPlane::ClipsNothing());

            slider->set_on_change_callback([this]() { on_sla_layer_slider_changed(); });
            show_layers_sliders(true);
        } else {
            show_layers_sliders(false);
        }

        m_loaded_print = print;
    }
}
```

#### 3. on_sla_layer_slider_changed() — 新函數（load_print_as_sla 之後）

雙指頭範圍選取 + 修正 re-entrancy 問題：

```cpp
void Preview::on_sla_layer_slider_changed()
{
    if (m_canvas == nullptr)
        return;

    IMSlider* slider = m_canvas->get_gcode_viewer().get_layers_slider();
    if (slider == nullptr)
        return;

    const int low_pos  = slider->GetLowerValue();
    const int high_pos = slider->GetHigherValue();
    const int max_pos  = static_cast<int>(m_sla_layers_z.size()) - 1;
    if (low_pos < 0 || high_pos < 0 || high_pos > max_pos || low_pos > max_pos)
        return;

    const double z_low  = m_sla_layers_z[low_pos];
    const double z_high = m_sla_layers_z[high_pos];

    // Bottom plane: clip below lower thumb.
    // When lower thumb is at 0, use ClipsNothing to avoid clipping into pad geometry.
    // ClippingPlane(+UnitZ, -z_low): clips where P.z < z_low.
    if (low_pos == 0)
        m_canvas->set_clipping_plane(0, ClippingPlane::ClipsNothing());
    else
        m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -z_low));

    // Top plane: clip above upper thumb.
    // When upper thumb is at max, use ClipsNothing to avoid clipping top of model.
    // ClippingPlane(-UnitZ, z_high): clips where P.z > z_high.
    if (high_pos == max_pos)
        m_canvas->set_clipping_plane(1, ClippingPlane::ClipsNothing());
    else
        m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high));

    // Do not call m_canvas->render() here — this callback fires inside an ImGui render frame,
    // so render() hits the m_in_render re-entrancy guard (sets m_dirty=true, returns early).
    // Instead: mark dirty + post wxEVT_PAINT so the next frame picks up the new clipping planes.
    m_canvas->set_as_dirty();
    m_canvas_widget->Refresh();
}
```

**Clipping Plane 公式說明**（雙指頭）：

| 位置 | 公式 | 截除條件 |
|------|------|---------|
| low_pos == 0 | `ClipsNothing()` | 不截（避免切到 pad 底部） |
| low_pos > 0 | `ClippingPlane(+UnitZ, -z_low)` | P.z < z_low |
| high_pos == max | `ClipsNothing()` | 不截（顯示至頂） |
| high_pos < max | `ClippingPlane(-UnitZ, z_high)` | P.z > z_high |

#### 4. show_moves_sliders() — 實作 stub（Bug Fix）

```cpp
void Preview::show_moves_sliders(bool show)
{
    // Step 2.5: Control IMSlider visibility for SLA mode.
    // Hides the horizontal moves slider when in SLA mode (SLA has no toolpath moves to scrub).
    IMSlider* slider = m_canvas->get_gcode_viewer().get_moves_slider();
    if (slider != nullptr)
        slider->Show(show);
}
```

#### 5. load_print_as_fff() — 加入 IMSlider 重置（mainframe guard 之後）

```cpp
    // Step 2.5: Reset IMSlider from SLA mode back to FFF mode.
    // Clears SLA callback and restores visibility for both sliders (hidden during SLA preview).
    {
        IMSlider* layers_slider = m_canvas->get_gcode_viewer().get_layers_slider();
        if (layers_slider != nullptr) {
            layers_slider->set_on_change_callback(nullptr); // clear SLA callback
            layers_slider->Show(true);                      // restore FFF layers slider
        }
        IMSlider* moves_slider = m_canvas->get_gcode_viewer().get_moves_slider();
        if (moves_slider != nullptr)
            moves_slider->Show(true);                       // restore FFF moves slider
    }
```

---

## 技術決策記錄

### 為何不用 ClipsNothing 的底部 plane 而要顯式設定？

`ClippingPlane::ClipsNothing()` 的 offset 為 `DBL_MAX`，表示「截面在無窮遠處 = 不截」。
頂部平面用 `ClippingPlane(-Vec3d::UnitZ(), z)` 截去 `P.z > z` 的所有幾何。

### 為何 render() callback 在 PopStyleColor 之後？

ImGui context 需要完整的 Push/Pop 對稱。在 `PopStyleColor` 之前呼叫 callback
可能觸發重新 render 導致 ImGui state 不一致。

### GCodeViewer::render() 在 SLA 模式下提早退出（Bug Fix 說明）

`GCodeViewer::render()` 有一個 `if (m_roles.empty()) return;` guard。
SLA 模式無 GCode，`m_roles` 永遠為空，導致後面的 `render_slider()` 永遠不被呼叫。

**修復方式**：在 early-return block 內先呼叫 `render_slider()`，確保 IMSlider 有機會渲染。
同時在 `IMSlider::render()` 加 `m_values.empty()` guard，防止未初始化時顯示廢棄 widget。

| 場景 | m_roles.empty() | m_values.empty() | m_visible | 結果 |
|------|:---:|:---:|:---:|:---:|
| SLA 已切片 | ✅ true | ❌ false（已設定）| ✅ true | **渲染 slider** |
| SLA 切片中 | ✅ true | ❌ false / ✅ true | ❌ false | 不渲染 |
| FFF 有 GCode | ❌ false | ❌ false | ✅ true | 渲染（原有路徑）|
| FFF 無 GCode | ✅ true | ✅ true | ✅ true | 不渲染（m_values guard）|

### load_print_as_fff() 為何要主動 Show(true)？

若使用者在 SLA 模式下讓 slider 隱藏（`m_visible=false`），
切換回 FFF 時若不主動恢復，FFF slider 也會消失。

### GCodeViewer 資料與 GLVolumeCollection 的架構差異

`GLCanvas3D::reset_volumes()` 只清除 `m_volumes`（GLVolumeCollection），
屬於 canvas 的幾何資料。`m_gcode_viewer` 是獨立的 GCodeViewer 物件，
有自己的 shell mesh（`m_shells`）和 toolpath data（`m_buffers`, `m_roles`）。
切換 printer technology 時，兩者都必須明確清除。

---

## 依賴確認（所有符號均無需新增 include）

| 符號 | 來源 | 狀態 |
|------|------|:---:|
| `IMSlider` | `IMSlider.hpp` (GUI_Preview.cpp line 4) | ✅ |
| `SCALING_FACTOR` | `libslic3r/libslic3r.h` | ✅ |
| `sort_remove_duplicates` | `libslic3r/libslic3r.h` | ✅ |
| `slaposSliceSupports` | `libslic3r/SLAPrint.hpp` | ✅ |
| `get_slice_index()` / `print_level()` | `libslic3r/SLAPrint.hpp` | ✅ |
| `ClippingPlane` / `Vec3d::UnitZ()` | via `GLCanvas3D.hpp` | ✅ |
| `get_layers_slider()` | `GCodeViewer.hpp` (line 842) | ✅ |
| `std::function` | `<functional>` — 已新增至 IMSlider.hpp | ✅ |

---

## 測試結果

| # | 測試項目 | 預期結果 | 狀態 |
|---|----------|----------|:----:|
| T1 | 選 SL1，切片後進入 Preview | ImGui 垂直 slider 出現，預設在最頂端 | ✅ |
| T2 | 拖動上指頭向下 | 頂部 clipping plane 下移，隱藏上指頭以上的 shells | ✅ |
| T3 | 拖動下指頭向上 | 底部 clipping plane 上移，隱藏下指頭以下的 shells（雙指頭範圍選取） | ✅ |
| T4 | 兩指頭回到最頂/最底 | 恢復顯示全部層（ClipsNothing）| ✅ |
| T5 | FDM 切片 → 切換 SLA → 切片 → Preview | 無 FDM 半透明殘影，SLA Preview 正常 | ✅ |
| T6 | SLA 切片進行中 | slider 隱藏，完成後自動出現 | ✅ |
| T7 | FDM 切片 Preview | callback=nullptr，FFF slider 正常，無 SLA clipping | ✅ |

**T5 補充**：包含兩個 bug fix 的驗證：
- `Plater::record_slice_preset()` — 不再 crash（ptSLA guard）
- `GLCanvas3D::load_sla_preview()` — 不再顯示 FDM 半透明模型（GCodeViewer reset）

**T6/T7 補充**：T6/T7 通過包含 moves slider bug fix 的驗證：
- `show_moves_sliders()` — 實作 stub，SLA 模式下正確隱藏橫向 slider
- `load_print_as_fff()` — 切換回 FFF 時同時恢復 layers + moves 兩個 slider 可見性

**雙指頭 + 畫面更新 bug fix 測試**：
- T3 驗證雙指頭底部 clipping plane 正確截除底層 shells
- T2/T3 拖動時畫面即時更新（`set_as_dirty() + Refresh()` 替代 re-entrant `render()`）
- 點擊單/多層切換按鈕時畫面即時更新（IMSlider.cpp 按鈕 callback 修正）

---

## 已知限制與後續計畫

### Step 2.5 已知限制

- IMSlider 在 SLA 模式下顯示 z 值標籤（FFF 設計），非 SLA 層號格式

### 未來升級：方案 C（DSForLayers）

詳見 `MergeLog/StepAnalyze/Step2_5_SLA_Layer_Slider_Plan.md` 的「B → C 遷移路徑分析」。

**升級至 C 需新增**：
- `ImGuiDoubleSlider.hpp/cpp`（~1000 行）
- `DoubleSliderForLayers.hpp/cpp`（~1500 行）
- GLCanvas3D → Plater → Preview render chain
- 缺失 ImGui 自訂 texture（DSRevert 等）

**當前 B 的程式碼可攜帶比例：~75-80%**（z-extraction + clipping logic 100% 保留）

---

## 相關檔案

| 路徑 | 說明 |
|------|------|
| `PhrozenOrca/src/slic3r/GUI/IMSlider.hpp` | +12 行（public API + private 成員） |
| `PhrozenOrca/src/slic3r/GUI/IMSlider.cpp` | +22 行（render() guard × 2 + callback + 按鈕 callback Bug Fix） |
| `PhrozenOrca/src/slic3r/GUI/GCodeViewer.cpp` | +7 行（Bug Fix：render_slider in early-return） |
| `PhrozenOrca/src/slic3r/GUI/GLCanvas3D.cpp` | +9 行（Bug Fix：reset GCodeViewer on SLA mode entry） |
| `PhrozenOrca/src/slic3r/GUI/Plater.cpp` | +8 行（Bug Fix：ptSLA guard in record_slice_preset） |
| `PhrozenOrca/src/slic3r/GUI/GUI_Preview.hpp` | +4 行（m_sla_layers_z + 方法宣告） |
| `PhrozenOrca/src/slic3r/GUI/GUI_Preview.cpp` | +90 行（5 處修改，含雙指頭範圍選取 + 畫面更新 Bug Fix） |
| `MergeLog/StepAnalyze/Step2_5_SLA_Layer_Slider_Plan.md` | 三方案分析 + B→C 遷移分析 |
| `MergeLog/StepAnalyze/Step2_5_B_IMSlider_Execution_Plan.md` | 方案 B 完整執行規格 |
