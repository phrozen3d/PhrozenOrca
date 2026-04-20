# Tasks: Prepare View Global Z-Clip Slider（修訂版 v2）

> 共 5 個階段，每個階段結束後需人工確認再進行下一階段。

---

## Stage 1：Slider UI + 視覺截面（m_clipping_planes）

> **目標**：Prepare view 右側出現 Z-clip slider，拖動後模型視覺上被截掉。
> ObjectClipper 尚未同步（raycasting 暫不正確）。

### 1.1 新增 Slider 狀態成員（GLCanvas3D）

- [ ] 在 `GLCanvas3D.hpp` 新增：
  ```cpp
  double m_prepare_clip_z_low  = 0.0;     // 下截面（固定 0.0 初始值）
  double m_prepare_clip_z_high = -1.0;    // 上截面（-1 = 未初始化，等場景有物件時設定）
  double m_prepare_scene_max_z = 50.0;    // 快取：場景所有物件 max Z
  ```

### 1.2 新增 scene_max_z 計算（GLCanvas3D）

- [ ] 在 `GLCanvas3D.cpp` 新增私有方法 `_update_prepare_scene_max_z()`：
  - 遍歷 `m_model->objects`，取所有 instance 世界座標的 max Z
  - 結果存入 `m_prepare_scene_max_z`（最小值 1.0，避免除零）
  - 若 `m_prepare_clip_z_high < 0`（未初始化），設為 `m_prepare_scene_max_z`
- [ ] 在適當時機呼叫（物件增刪時、每次 render 前或 `_refresh_if_shown_on_screen()`）

### 1.3 新增 Slider 渲染方法（GLCanvas3D）

- [ ] 在 `GLCanvas3D.hpp` 宣告 `void _render_prepare_clip_slider()`
- [ ] 在 `GLCanvas3D.cpp` 實作：
  - 條件：`m_canvas_type == CanvasView3D && current_printer_technology() == ptSLA`
  - UI：兩個垂直 `ImGui::VSliderFloat`（或 IMSlider 實例）
    - top bar：控制 `m_prepare_clip_z_high`，範圍 `[m_prepare_clip_z_low, m_prepare_scene_max_z]`
    - bottom bar：控制 `m_prepare_clip_z_low`，範圍 `[0.0, m_prepare_clip_z_high]`
    - tooltip：懸停顯示當前 Z 值（mm）
  - 值變化時呼叫 `_on_prepare_clip_changed(z_low, z_high)`
  - 位置：畫布右側浮動（仿 Preview slider）

### 1.4 新增截面更新方法（GLCanvas3D）

- [ ] 新增 `void _on_prepare_clip_changed(double z_low, double z_high)`：
  - 若 `z_low <= 0.0 && z_high >= m_prepare_scene_max_z`：
    → `m_use_clipping_planes = false`
  - 否則：
    → `set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -z_low))`
    → `set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high))`
    → `m_use_clipping_planes = true`
  - 更新 `m_prepare_clip_z_low`、`m_prepare_clip_z_high`
  - `set_as_dirty()`

### 1.5 連接 ImGui overlay（GLCanvas3D）

- [ ] 在 `_render_imgui_overlay()`（或同等渲染函式）中加入對 `_render_prepare_clip_slider()` 的呼叫

---

### ✅ Checkpoint 1

**請執行編譯：**
- [x] C1：全專案編譯通過，無錯誤

**請手動測試：**
- [x] M1-1：載入 SLA 物件 → Prepare view 右側出現雙 bar Z-clip slider
- [x] M1-2：**不選取任何物件** → slider 仍然顯示（常駐）
- [x] M1-3：拖動 top bar 向下 → 模型頂部視覺上消失
- [x] M1-4：拖動 bottom bar 向上 → 模型底部視覺上消失
- [x] M1-5：兩 bar 回到端點 → 模型完整顯示（`m_use_clipping_planes = false`）
- [x] M1-6：載入 FDM 物件 → **slider 不顯示**
- [x] M1-7：切換到 Preview tab → Preview slider 行為正常，不受影響

> 注意：此階段進入 Hollow/Drill Gizmo 後截面仍會顯示，但 raycasting 可能不正確，屬正常。

### 🐛 Stage 1 補充修正（點擊底板 slider 消失）

**問題**：點擊底板網格第二次後，slider 消失。

**根本原因**：`PartPlate::update_slice_context()` 呼叫
`process.select_technology(this->printer_technology)`，而 `this->printer_technology`
預設 ptFFF 且從不隨 printer preset 更新，導致 `m_printer_tech` 被重設回 ptFFF，
`current_printer_technology()` 回傳 ptFFF，slider 的 SLA 條件判斷失敗。

**修法**（`PartPlate.cpp`）：
- `select_technology(this->printer_technology)` → `select_technology(process.current_printer_technology())`
- 加入 `m_print != nullptr` guard（SLA mode 下 PartPlate 無 FFF Print）

- [x] 補充修正已驗證通過

---

## Stage 1.5：Slider UI 視覺樣式重構（方案 B — DrawList 仿 IMSlider）

> **目標**：將 `_render_prepare_clip_slider()` 中原始的 `ImGui::VSliderFloat` 替換為
> 以 ImGui DrawList 手繪的自訂滑桿，使外觀與 Preview tab 的 IMSlider 一致。
>
> **採用方案 B（不共用 IMSlider）**：直接操作浮點 Z 值，不引入 layer-index 轉換邏輯。
> 詳細分析見 `proposal.md`「Stage 1.5 Slider UI 視覺樣式決策」章節。

### 1.5.1 定義視覺常數

- [x] 在 `_render_prepare_clip_slider()` 頂端（或匿名 namespace）定義以下常數：
  ```cpp
  constexpr float SLIDER_W        = 34.f;   // 背景面板寬（px）
  constexpr float TRACK_W         = 4.f;    // 軌道寬（px）
  constexpr float HANDLE_R        = 8.f;    // 把手圓半徑（px）
  constexpr float LABEL_OFFSET_X  = 10.f;  // label 距把手左側距離（px）
  // 顏色
  const ImU32 COL_BG        = IM_COL32( 40,  40,  40, 160);  // 背景（半透明深色）
  const ImU32 COL_TRACK     = IM_COL32(100, 100, 100, 220);  // 軌道底色
  const ImU32 COL_RANGE     = IM_COL32( 88, 166, 255, 200);  // High~Low 之間的高亮軌道
  const ImU32 COL_HANDLE_HI = IM_COL32(224, 224, 224, 255);  // High handle（較亮）
  const ImU32 COL_HANDLE_LO = IM_COL32(160, 160, 160, 255);  // Low handle（較暗）
  const ImU32 COL_LABEL     = IM_COL32(255, 255, 255, 200);  // Z 值文字
  ```

### 1.5.2 改寫背景面板與軌道繪製

- [x] 移除原本的 `ImGui::BeginChild` / `ImGui::VSliderFloat` 區塊
- [x] 改用 `ImGui::SetNextWindowPos` + `ImGui::BeginChild`（或直接取得 DrawList）：
  ```cpp
  // 位置：畫布右側，垂直置中
  float panel_x = canvas_w - SLIDER_W - 8.f;
  float panel_y = 40.f;
  float panel_h = canvas_h - 80.f;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  // 背景圓角矩形
  dl->AddRectFilled({panel_x, panel_y},
                    {panel_x + SLIDER_W, panel_y + panel_h},
                    COL_BG, 6.f);

  // 軌道（中央 X = panel_x + SLIDER_W/2）
  float track_x = panel_x + SLIDER_W * 0.5f;
  float track_y0 = panel_y + HANDLE_R + 4.f;
  float track_y1 = panel_y + panel_h - HANDLE_R - 4.f;
  float track_len = track_y1 - track_y0;

  dl->AddRectFilled({track_x - TRACK_W * 0.5f, track_y0},
                    {track_x + TRACK_W * 0.5f, track_y1},
                    COL_TRACK, 2.f);
  ```

### 1.5.3 繪製 High / Low handle 與 High~Low 高亮區間

- [x] 將 `m_prepare_clip_z_high` / `m_prepare_clip_z_low` 映射到螢幕 Y 座標：
  ```cpp
  auto z_to_y = [&](double z) -> float {
      float t = (float)std::clamp(z / m_prepare_scene_max_z, 0.0, 1.0);
      return track_y1 - t * track_len;   // Z=0 → 底部, Z=max → 頂部
  };
  float y_hi = z_to_y(m_prepare_clip_z_high);
  float y_lo = z_to_y(m_prepare_clip_z_low);
  ```
- [x] 繪製高亮區間（High 到 Low 之間）：
  ```cpp
  dl->AddRectFilled({track_x - TRACK_W * 0.5f, y_hi},
                    {track_x + TRACK_W * 0.5f, y_lo},
                    COL_RANGE, 2.f);
  ```
- [x] 繪製兩個把手圓：
  ```cpp
  dl->AddCircleFilled({track_x, y_hi}, HANDLE_R, COL_HANDLE_HI);  // High
  dl->AddCircleFilled({track_x, y_lo}, HANDLE_R, COL_HANDLE_LO);  // Low
  ```

### 1.5.4 Z 值 label 顯示

- [x] 在每個 handle 左側顯示 Z 值（mm）：
  ```cpp
  char buf_hi[16], buf_lo[16];
  std::snprintf(buf_hi, sizeof(buf_hi), "%.1f mm", m_prepare_clip_z_high);
  std::snprintf(buf_lo, sizeof(buf_lo), "%.1f mm", m_prepare_clip_z_low);
  dl->AddText({panel_x - LABEL_OFFSET_X - ImGui::CalcTextSize(buf_hi).x, y_hi - 7.f},
              COL_LABEL, buf_hi);
  dl->AddText({panel_x - LABEL_OFFSET_X - ImGui::CalcTextSize(buf_lo).x, y_lo - 7.f},
              COL_LABEL, buf_lo);
  ```

### 1.5.5 滑鼠拖曳互動

- [x] 新增兩個拖曳狀態成員至 `GLCanvas3D.hpp`：
  ```cpp
  bool m_prepare_dragging_high = false;
  bool m_prepare_dragging_low  = false;
  ```
- [x] 在 `_render_prepare_clip_slider()` 中，使用 `ImGui::IsMouseHoveringRect` + `ImGui::IsMouseDown(0)` 偵測拖曳：
  - High handle 命中區（圓形 hit box `HANDLE_R + 4px`）→ 設 `m_prepare_dragging_high = true`
  - Low handle 命中區 → 設 `m_prepare_dragging_low = true`
  - Mouse release → 兩個 flag 清除
  - 拖曳時：`mouse_y` → 反算 Z 值，夾入合法範圍後呼叫 `_on_prepare_clip_changed(z_low, z_high)`
    ```cpp
    auto y_to_z = [&](float y) -> double {
        float t = std::clamp((track_y1 - y) / track_len, 0.f, 1.f);
        return (double)t * m_prepare_scene_max_z;
    };
    ```

### 1.5.6 滾輪微調（可選）

- [x] 若滑鼠在 slider 面板範圍內，捕捉滾輪事件微調 High handle（每格 0.1mm）
- [x] 確認滾輪事件不傳遞到 3D viewport（消耗掉）

---

### ✅ Checkpoint 1.5

**請執行編譯：**
- [x] C1.5：全專案編譯通過，無錯誤、無警告

**請手動測試：**
- [x] M1.5-1：SLA Prepare view → 右側出現**深色半透明面板** + 垂直軌道 + 兩個圓形把手
- [x] M1.5-2：High handle 旁顯示當前 Z 值（mm），Low handle 旁同樣顯示
- [x] M1.5-3：拖曳 High handle 向下 → 高亮區間隨之縮短，模型頂部截面隨動
- [x] M1.5-4：拖曳 Low handle 向上 → 高亮區間隨之縮短，模型底部截面隨動
- [x] M1.5-5：兩 handle 回到端點 → 模型完整顯示，高亮區間佔滿整條軌道
- [x] M1.5-6：Preview tab 的 layer slider **外觀與行為完全不受影響**
- [x] M1.5-7：FDM 模式 → 自訂 slider **不顯示**

---

## Stage 2：ObjectClipper 同步（raycasting 整合）

> **目標**：Slider 移動時同步 ObjectClipper，確保 Drill/Support Gizmo 的 raycasting 正確識別截面。

### 2.1 新增同步 flag（GLCanvas3D）

- [x] 在 `GLCanvas3D.hpp` 新增 `bool m_syncing_clipper = false`

### 2.2 在 `_on_prepare_clip_changed` 中加入 ObjectClipper 同步

- [x] 在 `_on_prepare_clip_changed()` 末端加入：
  ```cpp
  if (!m_syncing_clipper) {
      m_syncing_clipper = true;
      double ratio = (m_prepare_scene_max_z > 0) 
                     ? z_high / m_prepare_scene_max_z 
                     : 1.0;
      ratio = std::clamp(ratio, 0.0, 1.0);
      // ratio >= 1.0 時傳 -1（等同 ClipsNothing）
      auto* oc = m_gizmos.get_common_gizmos_data()
                  ? m_gizmos.get_common_gizmos_data()->object_clipper()
                  : nullptr;
      if (oc)
          oc->set_position_by_ratio(ratio < 1.0 ? ratio : -1.0, false);
      m_syncing_clipper = false;
  }
  ```
- [x] 確認 `m_gizmos.m_common_gizmos_data.get()` 的 null 安全性（無物件選取時可能為 nullptr）

---

---

## Stage 2.x：Gizmo Slider Mode（設計變更補充）

> **背景**：Stage 2 原始設計為「prepare slider 驅動 ObjectClipper」。
> 實作後發現兩套系統（prepare 全域截面 vs gizmo ObjectClipper）難以保持一致，
> 改採「進入 gizmo 時，右側 slider 切換為 gizmo 專用模式」的設計。

### 2.x.1 GLCanvas3D：Gizmo Slider Mode 狀態與方法

- [x] `GLCanvas3D.hpp` 新增成員：
  - `m_slider_in_gizmo_mode` — 是否處於 gizmo mode
  - `m_gizmo_obj_z_min / z_max` — 被選物件的 bbox Z 範圍（world coords）
  - `m_gizmo_clip_ratio` — 單一 handle 位置（0=頂端不裁切，1=底端全裁切）
  - `m_saved_clip_z_low / z_high` — 儲存 prepare mode 的兩個 handle 位置（離開 gizmo 時還原）
- [x] 新增 `enter_gizmo_slider_mode(obj_z_min, obj_z_max)`：儲存 prepare 狀態、設定 gizmo 模式、停用全域 clipping planes
- [x] 新增 `exit_gizmo_slider_mode()`：還原 prepare 狀態（呼叫 `_on_prepare_clip_changed`）

### 2.x.2 GLCanvas3D：`_render_prepare_clip_slider()` 雙模式

- [x] 新增 `m_slider_in_gizmo_mode` 分支：
  - **Gizmo mode**：單一 handle、物件 bbox Z 範圍、拖動時呼叫 `set_position_by_ratio(ratio, true)`
  - **Prepare mode**：原有雙 handle 行為不變
- [x] Label 在 gizmo mode 顯示 `obj_z_max - ratio × (obj_z_max - obj_z_min)` mm 值

### 2.x.3 GLGizmosManager：Just-entered / Just-left 偵測

- [x] `GLGizmosManager.hpp` 新增 `m_oc_was_valid_last_frame`
- [x] `GLGizmosManager.cpp` `update_data()` 新增：
  - `just_entered`：呼叫 `enter_gizmo_slider_mode(obj_z_min, obj_z_max)` + 初始化 ObjectClipper ratio=0
  - `just_left`：呼叫 `exit_gizmo_slider_mode()`
  - obj bbox 來源：`selection_info()->model_object()->instance_bounding_box(active_inst)` + z_shift

### 2.x.4 ObjectClipper：修正 set_position_by_ratio 的 radius 計算

- [x] `GLGizmosCommon.cpp`：在 `set_position_by_ratio` 中，
  **改用被選物件 Z 高度的一半**（`z_half_h`）取代 3D 包圍球半徑（`m_active_inst_bb_radius`）
- [x] 同時將 `dist` 改為 Z 中心值（`z_center`），確保平面掃描範圍對齊 bbox Z 範圍
- [x] `GLGizmosCommon.hpp`：對應函式 signature 改為 `vertical_normal=true` 作為預設

---

### ✅ Checkpoint 2

**請執行編譯：**
- [x] C2：全專案編譯通過

**請手動測試：**
- [x] M2-1：SLA 物件 → 進入 Hollow Gizmo → 右側 slider 切換為單 handle，range 為物件 bbox Z 範圍
- [x] M2-2：Gizmo mode 中拖動 handle → 截面從頂部往下掃描，label 顯示正確 Z mm 值
- [x] M2-3：離開 Hollow Gizmo → 右側 slider 還原為雙 handle + prepare mode range
- [x] M2-4：prepare mode 中 top/bottom handle 位置在 gizmo 進出後保持不變
- [x] M2-5：SLA 物件（已 Hollow）→ gizmo mode 拖動 handle → 截面正確顯示內壁截面
- [x] M2-6：FDM 物件 → 切片 → Preview 截面正常，無副作用

---

## Stage 3：移除 Gizmo 內部 View Clipping Slider UI

> **目標**：Hollow / Drill / SlaSupports gizmo 面板不再顯示 "View clipping" slider 行，改由全域 slider 統一控制。

### 3.1 GLGizmoHollow.cpp

- [x] 找到 `on_render_input_window()` 中 "view_clipping" 相關渲染區塊（約 line 480–492）
- [x] 移除整個 "View clipping" row 的渲染程式碼（保留 ObjectClipper 基礎設施和滾輪事件處理）
- [x] 確認 `m_desc["view_clipping"]` 初始化可移除（若僅用於 UI 顯示）

### 3.2 GLGizmoDrill.cpp

- [x] 找到 `on_render_input_window()` 中 "view_clipping" 相關渲染區塊（約 line 670–682）
- [x] 移除整個 "View clipping" row 的渲染程式碼
- [x] 確認 `m_desc["view_clipping"]` 初始化可移除

### 3.3 GLGizmoSlaSupports.cpp

- [x] 確認是否有 "View clipping" slider（grep 確認：約 line 576–588 的 set_position_by_ratio 是否有對應 UI）
- [x] 若有，同樣移除 UI 渲染程式碼

---

### ✅ Checkpoint 3

**請執行編譯：**
- [x] C3：全專案編譯通過，確認無因移除 m_desc 使用而造成的未宣告變數錯誤

**請手動測試：**
- [x] M3-1：進入 Hollow Gizmo → 面板中**沒有** "View clipping" 行
- [x] M3-2：進入 Drill Gizmo → 面板中**沒有** "View clipping" 行
- [x] M3-3：進入 SLA Support Tree Gizmo → 面板中**沒有** "View clipping" 行（若原有）
- [x] M3-4：Hollow 的 Enable hollow、Offset 等功能正常（確認只移除 clipping slider，其他功能不受影響）
- [x] M3-5：Drill 的鑽孔放置、刪除功能正常
- [x] M3-6：Support Tree 的支撐點新增、刪除功能正常

---

## Stage 4：Gizmo 進出截面狀態同步

> **目標**：進出 Gizmo 時，截面位置保持一致（不跳動、不重置）。

### 4.1 GLGizmoSlaBase.cpp - on_set_state(On)

- [ ] 在 `on_set_state(On)` 中加入進場同步：
  ```cpp
  // 從全域 prepare slider 同步 ObjectClipper
  if (m_parent.get_use_clipping_planes()) {
      double z_high = m_parent.get_clipping_planes()[1].get_data()[3];
      double scene_max_z = m_parent.get_prepare_scene_max_z(); // 新增 getter
      if (scene_max_z > 0) {
          double ratio = std::clamp(z_high / scene_max_z, 0.0, 1.0);
          if (ratio < 1.0)
              m_c->object_clipper()->set_position_by_ratio(ratio, false);
          else
              m_c->object_clipper()->set_position_by_ratio(-1.0, false);
      }
  }
  ```
- [ ] 在 `GLCanvas3D.hpp` 新增 getter：`double get_prepare_scene_max_z() const { return m_prepare_scene_max_z; }`

### 4.2 GLGizmoSlaBase.cpp - on_set_state(Off)

- [ ] 在 `on_set_state(Off)` 中加入離場同步：
  ```cpp
  // 從 ObjectClipper 回寫 m_clipping_planes（確保全域 slider 保持正確位置）
  if (m_c && m_c->object_clipper()) {
      double pos = m_c->object_clipper()->get_position();
      if (pos > 0.0 && pos <= 1.0) {
          double scene_max_z = m_parent.get_prepare_scene_max_z();
          double z_high = pos * scene_max_z;
          m_parent.set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high));
          m_parent.set_use_clipping_planes(true);
      }
  }
  ```

---

### ✅ Checkpoint 4

**請執行編譯：**
- [ ] C4：全專案編譯通過

**請手動測試（逐一執行，任一失敗立即停止）：**
- [ ] M4-1：全域 slider top bar 設 50% → 進入 Hollow Gizmo → 截面維持在 50%
- [ ] M4-2：全域 slider top bar 設 50% → 進入 Hollow Gizmo → 離開 Gizmo → 截面維持在 50%
- [ ] M4-3：全域 slider top bar 設 70% → 進入 Drill Gizmo → 離開 → 進入 SLA Support → 離開
  → 截面全程維持在 70%（不跳動）
- [ ] M4-4：快速切換 Hollow → Drill → Support → 無 Gizmo（各停留約 1 秒）
  → 截面位置一致，無閃爍或跳回 0
- [ ] M4-5：全域 slider 設 100%（全顯示）→ 進入各 Gizmo → 截面無效（完整顯示）

---

## Stage 5：FDM + Preview 隔離驗證（最終確認）

> **目標**：確認所有修改只影響 SLA Prepare view，FDM 和 Preview 完全不受影響。

### 5.1 FDM 影響確認

- [ ] 確認所有 Stage 1–4 的新程式碼均有 `ptSLA && CanvasView3D` guard
- [ ] 確認 `m_prepare_clip_z_low/high/scene_max_z` 在 FDM 模式下不被修改
- [ ] 確認 `_render_prepare_clip_slider()` 在 FDM 模式下不被呼叫

### 5.2 Preview 影響確認

- [ ] 確認 `set_clipping_plane()` 呼叫都在 view3D canvas 上（非 preview canvas）
- [ ] 確認 `on_sla_layer_slider_changed()` 邏輯未被修改
- [ ] 確認 IMSlider（preview 的 m_layers_slider）未被修改

---

### ✅ Checkpoint 5（最終驗收）

**請執行自動測試：**
- [ ] Auto-1：`cd build && ctest -R "sla_" --output-on-failure`（SLA 相關測試全數通過）
- [ ] Auto-2：`cd build && ctest -R "fff_" --output-on-failure`（FDM 相關測試全數通過）

**請手動執行完整驗收流程：**
- [ ] F-1：SLA 模式 → Prepare view 右側**常駐**雙 bar Z-clip slider（不需選取物件）
- [ ] F-2：top bar 拖到 60% → 模型頂部 40% 消失
- [ ] F-3：bottom bar 拖到 20% → 模型底部 20% 消失，顯示中間 40%
- [ ] F-4：兩 bar 回端點 → 模型完整顯示
- [ ] F-5：top bar 設 50% → 進入 Hollow → 面板**無** "View clipping" 行 → 截面保持在 50%
- [ ] F-6：離開 Hollow → 截面仍在 50%
- [ ] F-7：top bar 設 50% → 進入 Drill Gizmo → 點擊截面暴露的**內壁** → 鑽孔成功
- [ ] F-8：切換到 FDM 印表機 → slider **不顯示**，FDM 物件正常顯示
- [ ] F-9：FDM 切片 → 確認切片結果正確，無截面副作用
- [ ] F-10：切換到 Preview tab → Preview layer slider 正常，與 Prepare slider **完全獨立**
- [ ] F-11：SLA Prepare → 切到 Preview → 切回 Prepare → 全域 slider 位置**保持不變**

---

## 附錄：修改檔案清單

| 檔案 | 修改內容 |
|------|---------|
| `GLCanvas3D.hpp` | 新增 m_prepare_clip_* 成員、m_prepare_dragging_high/low、m_syncing_clipper、getter |
| `GLCanvas3D.cpp` | 新增 _render_prepare_clip_slider()（Stage 1.5 改為 DrawList 自訂樣式）、_on_prepare_clip_changed()、_update_prepare_scene_max_z() |
| `GLGizmoSlaBase.cpp` | 修改 on_set_state(On/Off) 加入同步 |
| `GLGizmoHollow.cpp` | 移除 "View clipping" UI 區塊 |
| `GLGizmoDrill.cpp` | 移除 "View clipping" UI 區塊 |
| `GLGizmoSlaSupports.cpp` | 移除 "View clipping" UI 區塊（若有）|

**不修改：**
- `GUI_Preview.cpp`（Preview slider 邏輯完全不動）
- `GCodeViewer.hpp/.cpp`（IMSlider 定義不動，若不直接複用）
- 所有 FDM 相關檔案
