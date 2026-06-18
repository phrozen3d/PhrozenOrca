# Spec: Prepare View Global Z-Clip Slider（修訂版 v3）

## 功能描述

在 SLA 的 Prepare（3D）view 右側使用與 Preview 相同的 **`IMSlider` 垂直雙拇指層滑桿**（非自繪連續 mm 滑桿）。
層 Z 由 **SLA 列印 `layer_height` + 材料 `initial_layer_height`** 估算；非 gizmo 狀態下範圍取自**全場景 max Z**，進入 Hollow / Drill / SLA Support 時 **同一個 `IMSlider` instance 仍保持顯示**，但 zs[] 範圍切換為所選物件 bbox world Z（含 `SelectionInfo::get_sla_shift()`）。

---

## 行為規格

### SLA-1：Slider 顯示條件

| 條件 | 結果 |
|------|------|
| `CanvasView3D` + `ptSLA`、非 Gizmo | **`IMSlider` 顯示**（range = 全場景 max Z）|
| `CanvasView3D` + `ptSLA`、Gizmo（Hollow/Drill/Support）| **同一個 `IMSlider` 仍顯示**（range = 所選物件 bbox world Z）|
| `CanvasPreview` | Slider **隱藏**（Preview 有自己的 layer slider）|
| `ptFFF` 任何狀態 | Slider **永不顯示** |
| 無物件載入（空場景）| Slider **顯示**（range 使用預設值，無視覺效果）|

> **v3**：非 Gizmo 與 Gizmo session 共用同一個 `GCodeViewer::get_layers_slider()` 回傳的 `IMSlider` instance，僅 zs[] 範圍與 ObjectClipper 同步策略不同；不需選取物件即可顯示（空場景亦顯示）。

### SLA-2：Slider 外觀與操作（非 Gizmo）

- **元件**：`GCodeViewer::get_layers_slider()`（`IMSlider`），與 Preview 左側 3D 相同渲染管線。
- **方向**：垂直雙拇指；滾輪以**層索引**為步進（與 Preview 一致）。
- **額外 UI**（Prepare 專用）：滑桿左側獨立欄位顯示**當前層（1-based）**與**當前高度（mm）**；**▲ / ▼** 按鈕分離排版，不與拇指熱區重疊。
- **單層**：雙拇指同一索引時，可見區間為該層 \[z_{i-1}, z_i\]（首層 z_{-1}=0）；其餘語意對齊 Preview `on_sla_layer_slider_changed` 之雙平面邏輯。
- **初始值**：下拇指 = 第一層、上拇指 = 最後一層（顯示完整模型，無截面）。

### SLA-3：Range 與層 Z 列表

```
scene_max_z（非 Gizmo / 一般 Prepare 模式）：
  遍歷整個 view3D canvas 的 m_volumes.volumes，
  忽略 !is_active / is_modifier / is_wipe_tower / bbox 未定義的 GLVolume；
  取所有剩餘 printable volume transformed_convex_hull_bounding_box() 的聯集 max.z()。
  若聯集為空（場景無 printable 物件） → 50.0 mm（fallback）。

gizmo_range（Hollow / Drill / SLA Support session 中）：
  使用所選物件 instance bounding box 的世界 Z 範圍：
    obj_z_min = mo->instance_bounding_box(active_instance).min.z() + SelectionInfo::get_sla_shift()
    obj_z_max = mo->instance_bounding_box(active_instance).max.z() + SelectionInfo::get_sla_shift()
  slider min_z / max_z 取 [obj_z_min, obj_z_max]；
  若 obj_z_max <= obj_z_min（無效 bbox） → 0~50 fallback。

層頂 Z 列表 zs[]（估算）：
  zs[0] = min_z + initial_layer_height（材料預設）
  zs[k] = zs[k-1] + layer_height（列印預設），直到 zs[last] >= max_z

滑桿索引 i 對應 zs[i]；截面計算：
  - 下索引 = 上索引 = i：層 i 單層 slab（見 SLA-2）
  - 下索引 lo < 上索引 hi：與 Preview 相同（lo=0 時底平面 ClipsNothing；hi=max 時頂平面 ClipsNothing）
```

> **v3**：連續 mm 拖曳已移除；Gizmo session 沿用同一個 `IMSlider`，僅 zs[] 由 selected object bbox + `get_sla_shift()` 估算（見下方 v3.1 註記）。舊 `_render_prepare_clip_slider()` 自繪 slider 已 retired，不再為任何路徑使用。
>
> **v3.1（2026-06-18 bugfix）**：
> - `scene_max_z` **永遠遍歷全場景**——不再有「無 selection 才遍歷全部」的舊分支，避免退 gizmo 後因 selection 仍非空而把 range 卡在「選中物件 max」。
> - Hollow / Drill / SLA Support 進入 gizmo 時，`InstancesHider::set_hide_full_scene(true)` 會把所有 model object GLVolumes 設成 `is_active = false`；此時 `scene_max_z` 會落到 50 mm fallback，不可作為 slider range，改用 `gizmo_range`（所選物件 bbox + `SelectionInfo::get_sla_shift()`）。
> - `SelectionInfo::get_sla_shift()` 對 Support 為 elevation lift（物件浮空高度）；對 Hollow / Drill 為 0。

### SLA-4：雙系統同步

兩個管線同時驅動：

1. `GLCanvas3D::m_clipping_planes[2]`（視覺 Z-range，走 shader）
2. ObjectClipper（raycasting filter + cap mesh）

注意：ObjectClipper 只同步 **top（z_high）**，bottom 僅由 shader Z-range 控制。

> **v3.1（2026-06-18 bugfix）— Gizmo session 的 ObjectClipper 同步座標系**
>
> Gizmo session 中對 ObjectClipper 改用 `set_range_and_pos(Vec3d(0, 0, 1), z_high_eff, ratio)` 直接設定 world Z plane，不再走 `set_position_by_ratio(ratio, ...)` 的內部公式。
>
> 原因：`set_position_by_ratio` 的內部公式 `plane.offset = dist + half - 2*half*ratio`（其中 `dist = instance_offset.z + z_shift`、`half = (mo->max_z()-mo->min_z())/2`）隱含「model centered at z=0」假設；SLA 物件常為 bed-aligned（`mo->min_z() ≈ 0`），且 Support 模式有非零 `z_shift`（elevation lift），結果是 ObjectClipper 的 m_clp 與 `m_clipping_planes[1]` 的 visual cut 落在不同 world Z，cap mesh / raycaster cut 位置與 shader z_range 不一致，產生「拖動 slider 時頂部多一條薄片」與「物件比實際早被切掉」。
>
> `set_range_and_pos(+Z, z_high_eff, ratio)` 設的 `m_clp = ClippingPlane(+Z, z_high_eff)`，`GLGizmosManager::get_clipping_plane()` 反向後 shader 看到 `ClippingPlane(-Z, z_high_eff)`，與 `set_clipping_plane(1, ClippingPlane(-Z, z_high_mm))` 完全一致，三系統（shader z_range / m_clp / cap mesh）對齊到同一個世界 Z。
>
> 非 Gizmo 的 prepare mode 仍以 `set_range_and_pos(-Z, z_high, rough_ratio)` 設定（用於 cap mesh 場景；其 ratio 主要用於觸發 `get_clipping_plane()` 的 `ratio == 0 → ClipsNothing` 早退分支）。

### SLA-5：狀態持久性

在以下操作中**保持截面狀態**：
- 切換 SLA Gizmo（Hollow → Drill → Support Tree → 無 Gizmo）
- 重新選取物件
- Undo/Redo 操作

在以下操作中**重置**（bottom=0, top=scene_max_z）：
- 切換到 FDM 印表機
- 使用者手動將兩 bar 拖回端點
- 程式重啟（UI 狀態不持久化）

### SLA-6：Gizmo 整合（Hollow / Drill / SLA Support）

**各 Gizmo ImGui 面板的原有 "View clipping" slider 移除。** Prepare mode 與 Hollow / Drill / SLA Support gizmo session 共用同一個 `GCodeViewer::get_layers_slider()` 的 `IMSlider`；gizmo 進場時不切換到另一種 UI，只改變該 IMSlider 的 zs[] 範圍（selected object bbox + `get_sla_shift()`）與 ObjectClipper 同步策略（見 G3）。

下列規則 Hollow / Drill / SLA Support **共用**，不寫個別 gizmo 特例：

**G1：Gizmo session 偵測由 `GLGizmosManager::update_data()` 集中處理**

`update_data()` 內順序依賴詳見 SLA-12。session 開關依據兩個訊號：

- `just_entered = oc_valid && !m_oc_was_valid_last_frame` — 從非 SLA-gizmo 狀態進入第一個 SLA gizmo
- `gizmo_changed_while_oc_valid = oc_valid && m_oc_was_valid_last_frame && m_current != m_last_oc_gizmo_type` — Hollow / Drill / Support 互切（ObjectClipper 仍持有，連續 valid，不會觸發 `just_entered`/`just_left`）

兩者任一成立 → 呼叫 `enter_gizmo_slider_mode(obj_z_min, obj_z_max)`；`!oc_valid && m_oc_was_valid_last_frame` → 呼叫 `exit_gizmo_slider_mode()`。詳見 SLA-11。

**G2：Range 取自所選物件 world Z bbox（含 SLA shift）**

```
obj_z_min = mo->instance_bounding_box(active_instance).min.z() + SelectionInfo::get_sla_shift()
obj_z_max = mo->instance_bounding_box(active_instance).max.z() + SelectionInfo::get_sla_shift()
```

不使用 `scene_max_z`：gizmo session 中 `InstancesHider::set_hide_full_scene(true)` 已把所有 model object GLVolumes 設成 `is_active = false`，`scene_max_z` 必然 fallback 50 mm。詳見 SLA-3 v3.1。

**G3：ObjectClipper 同步以 world Z plane**

Slider 拖動觸發 `_apply_sla_prepare_clip_from_layers_slider()` 時，ObjectClipper 走：

```
oc->set_range_and_pos(Vec3d(0, 0, 1), z_high_eff, m_gizmo_clip_ratio)
```

其中 `z_high_eff` 為 zs[high_pos] 的 world Z（含 z_shift）、`m_gizmo_clip_ratio = (obj_z_max - z_high_eff) / (obj_z_max - obj_z_min)`。`GLGizmosManager::get_clipping_plane()` 反向後 shader 看到 `ClippingPlane(-Z, z_high_eff)`，與 `set_clipping_plane(1, ClippingPlane(-Z, z_high_mm))` 落同一 world Z；cap mesh / raycaster / shader z_range 三者對齊。詳見 SLA-4 v3.1。

**G4：退出 gizmo 時的端點映射**

`exit_gizmo_slider_mode()` 依 `m_gizmo_clip_ratio` 決定如何映射回 prepare mode：

- `m_gizmo_clip_ratio <= 1e-6` → `restore_high = m_prepare_scene_max_z`（完整顯示全場景，即使 prepare 場景比所選物件高）
- 否則 → `restore_high = clamp(z_cur_abs, 0, m_prepare_scene_max_z)`，保留 gizmo 內的 world Z 裁切位置

詳見 SLA-10。

**G5：Preview / G-code Preview layer slider 不受影響**

Prepare 與 Preview 為獨立 `GLCanvas3D` 實例，狀態完全隔離；本整合僅影響 view3D canvas + ptSLA path。詳見 SLA-8。

### SLA-7：Gizmo 功能不受影響

進入 Hollow / Drill / SLA Support 後：
- 漏空（Hollow）功能正常
- 鑽孔放置（Drill）功能正常；截面暴露的內壁可點擊
- 支撐點放置（SLA Support）功能正常；截面暴露的內壁可點擊
- 移除 "View clipping" slider 不影響上述任何核心功能

### SLA-8：參數與 Preview 隔離

- Prepare slider 操作的是 **view3D canvas** 的 `m_clipping_planes[2]`
- Preview slider 操作的是 **preview canvas** 的 `m_clipping_planes[2]`
- 兩個 canvas 為獨立的 `GLCanvas3D` 實例，**狀態完全隔離**
- 不共用任何位置參數；Preview tab 行為與修改前完全一致

### SLA-9：FDM 無影響保證

- 所有新程式碼均有 `current_printer_technology() == ptSLA` + `m_canvas_type == CanvasView3D` guard
- FDM 模式下 `m_use_clipping_planes` 永遠維持 false
- `update_sla_prepare_layers_slider()` 在 FDM 模式下對 IMSlider 呼叫 `set_sla_prepare_mode(false)` 並提早 return，不渲染 prepare-mode 額外 UI
- Preview canvas 為獨立實例，不受影響

### SLA-10：Gizmo 退出端點映射（2026-06-18）

退出 Hollow / Drill / SLA Support 回 prepare mode 時，top handle 位置依「gizmo 內是否確實裁切」決定，**Hollow / Drill / Support 三者共用**此規則：

- **Full visible**（gizmo 內 `m_gizmo_clip_ratio <= 1e-6`，即 top handle 在最高位置、沒有裁切所選物件）：
  - `restore_high = m_prepare_scene_max_z`（重新整理過的全場景 max Z，於退出時透過 `_update_prepare_scene_max_z()` 取得）。
  - 配合 IMSlider lower handle 仍在 0，`_apply_sla_prepare_clip_from_layers_slider()` 內 `full_low && full_high == true` → `m_use_clipping_planes = false`，視覺完全不裁切。
  - 目的：避免「在 30 mm 物件裡 full visible → 退出後 100 mm 物件被裁到 30 mm 處」這種違反直覺的隱藏。
- **Non-full visible**（`m_gizmo_clip_ratio > 1e-6`，使用者確實在 gizmo 內拖動了 top handle）：
  - `z_cur_abs = m_gizmo_obj_z_max - m_gizmo_clip_ratio * (m_gizmo_obj_z_max - m_gizmo_obj_z_min)`（世界 Z）。
  - `restore_high = clamp(z_cur_abs, 0, m_prepare_scene_max_z)`。
  - 目的：保留使用者刻意設定的裁切位置（world Z），符合 T6 的「進出 gizmo 截面位置一致」精神。

### SLA-11：Hollow / Drill / SLA Support 互切時的 Gizmo Slider Mode 重建（2026-06-18）

`GLGizmosManager::update_data()` 除了既有的 ObjectClipper valid 邊緣偵測（`just_entered` / `just_left`），額外維護 `m_last_oc_gizmo_type`（上一幀 ObjectClipper valid 時的 `m_current` gizmo type）：

```
gizmo_changed_while_oc_valid := oc_valid && m_oc_was_valid_last_frame
                                          && m_current != m_last_oc_gizmo_type
```

當 `just_entered || gizmo_changed_while_oc_valid` 成立時，重新讀取 `selection_info()->get_sla_shift()` 與 `instance_bounding_box()`，呼叫 `enter_gizmo_slider_mode(obj_z_min, obj_z_max)`：

- 第一次進入 session：保存 `m_saved_clip_z_low/high`、設定 session active、建立 zs、reset IMSlider 為 full visible。
- 已在 session 內（gizmo 互切）：**不**覆寫 `m_saved_clip_z_*`，只更新 `m_gizmo_obj_z_min/max` 與 IMSlider 範圍；重置為 full visible。

理由：Hollow / Drill / SLA Support 都把 ObjectClipper 列入 `on_get_requirements()`，互切時 `CommonGizmosDataPool::update()` 不會 release ObjectClipper → `oc_valid` 連續為 true → 原本只用 ObjectClipper 邊緣偵測會漏掉「同 session 內的 gizmo 切換」，使 `m_gizmo_obj_z_min/max` 沿用前一個 gizmo 的 z_shift。

此規則對 Hollow / Drill / SLA Support 三者共用，不寫個別 gizmo 特例。

### SLA-12：Support z_shift 讀取時機（2026-06-18）

`update_data()` 中三段順序如下，**不可調換**：

1. `m_common_gizmos_data->update(...)` — 更新 SelectionInfo / InstancesHider / ObjectClipper。
2. **`m_gizmos[m_current]->data_changed(...)`** — Gizmo 自身 hook；`GLGizmoSlaSupports::data_changed()` 在此呼叫 `SelectionInfo::set_use_config_elevation(true)`，觸發 `recompute_z_shift()` 把 `m_z_shift` 設成含 elevation lift 的值。
3. SLA session 偵測（SLA-11）→ 讀 `SelectionInfo::get_sla_shift()` → `enter_gizmo_slider_mode(obj_z_min, obj_z_max)`。

若 (3) 早於 (2)：Support 進入時 `get_sla_shift()` 仍回傳「未浮空」的舊值，`enter_gizmo_slider_mode` cache 的 bbox 與實際物件世界 Z 範圍不一致，slider 拖曳會出現「未到底就整個隱藏」與「拖到中段提前消失」。

Hollow / Drill 的 `data_changed()` 不呼叫 `set_use_config_elevation()`，z_shift 對它們維持 0，因此此順序對 Hollow / Drill 行為等效。

---

## 驗收測試

| ID | 步驟 | 預期結果 |
|----|------|---------|
| T1 | SLA 模式，不選取任何物件 | Prepare 右側出現雙 bar 垂直 Slider（常駐）|
| T2 | 拖動 top bar 向下（z_high = 50%）| 模型頂部 50% 消失，下半部可見 |
| T3 | 拖動 bottom bar 向上（z_low = 20%）| 模型底部 20% 消失，顯示中間區段 |
| T4 | 兩 bar 回端點 | 截面完全取消，模型完整顯示 |
| T5 | top bar 設 60%，進入 Hollow Gizmo | 截面維持在 60%，Gizmo 面板**無** "View clipping" 行 |
| T6 | 在 Hollow Gizmo 不動 Slider，離開 Gizmo | 全域 slider top bar 仍在 60% |
| T7 | top bar 設 50%，進入 Drill Gizmo，點擊截面暴露的內壁 | 鑽孔成功放置於內壁 |
| T8 | top bar 設 50%，進入 SLA Support Gizmo，點擊截面暴露的內壁 | 支撐點成功放置 |
| T9 | 切換到 FDM 印表機 | Slider 消失，FDM 物件正常顯示 |
| T10 | 切換 Preview tab | Preview layer slider 正常，與 Prepare Slider 無關聯 |
| T11 | top bar 設 50%，切換 Preview 再切回 Prepare | Prepare slider 位置**保持**在 50% |
| T12 | SLA Gizmo 激活時截面有效 | 截面有 cap mesh（Hollow gizmo 的 ObjectClipper::render_cut）|
| T13 | 載入 FDM 物件切片 | 切片結果正確，無截面副作用 |
| T14 | 快速切換 Hollow → Drill → Support → 無 Gizmo | 截面位置一致，無閃爍或重置 |
| T15 | 場景含 30 mm + 100 mm 兩物件，選 30 mm 進 Hollow，slider 在最高位置，退出 Hollow | Prepare slider 上限 = 100 mm，視覺完整顯示兩個物件（`m_use_clipping_planes = false`）|
| T16 | 同 T15，但 gizmo 內把 top handle 拖到 20 mm 後退出 | Prepare slider top handle 對應 world Z ≈ 20 mm，100 mm 物件高於 20 mm 的部分被裁切；裁切位置不被自動拉到 100 mm（SLA-10 non-full 分支）|
| T17 | Hollow → Support（不離開 gizmo session） | Support 浮空後 slider 範圍為 lifted 後的 world Z，slider 與 cap mesh 對齊浮空後物件；T15/T16 行為對 Support 仍成立 |
| T18 | Support → Hollow（不離開 gizmo session） | Hollow 模式下 slider 範圍不再含 Support 的 lift；slider 對應未浮空 world Z |
| T19 | Support 模式下拖曳 top handle 至中段 | 截面 cap 與 visual clip 對齊同一 world Z；不出現「頂部多出一條薄片」也不出現「未到底就整個隱藏」（SLA-4 v3.1）|

---

## 不在範圍（Out of Scope）

- 截面角度旋轉（僅 Z 軸）
- Preview tab 行為變更
- 截面狀態持久化（存入 3mf 檔案）
- 場景物件新增/刪除後 scene_max_z 即時動態調整（初版可在每次 render 前更新）
- **SLA Support gizmo「Points」預覽縮放錯位**（既有 / 另案問題）：在 Support gizmo 中改變物件 scale 後，僅 Points 預覽模式的支撐點柱狀預覽位置 / 尺寸對應未放大前的物件。Structure 預覽與實際支撐生成不受影響。**本能力的 2026-06-18 bugfix 完全未碰** `GLGizmoSlaSupports.cpp`、`GLGizmoSlaBase.cpp` 或任何 raycaster / scaling / picking transform 邏輯，故不在 Z-clip slider 修正範圍。建議另案追 `update_point_raycasters_for_picking_transform()` 或 `register_volume_raycasters_for_picking()` 是否在 scale 事件後重新呼叫。

---

## Delta 合併紀錄（change: `resin-prepare-layer-slider-align-preview`，2026-05-14）

以下為封存前自 `openspec/changes/.../specs/prepare-z-clip-slider/spec.md` 合併之摘要；實質條文已反映於上文 **v3** 之 SLA-1～SLA-3、驗收表。

| Delta 區塊 | 合併方式 |
|------------|----------|
| REMOVED：舊 SLA-2（連續 mm 雙 bar）、舊 SLA-3（連續 Z range） | 已由 v3 **SLA-2／SLA-3**（`IMSlider`、離散 `zs[]`）取代 |
| ADDED：Prepare 與 Preview 共用 `IMSlider`、離散層 Z、層序／高度顯示、單層與按鈕熱區、裁剪語意對齊 Preview | 已併入 **SLA-1、SLA-2、SLA-3** 與驗收 **T1～T4** |
| MODIFIED：SLA-1 顯示條件、SLA-8 與 Preview 隔離 | 已併入 **SLA-1**（含 Gizmo 列）與 **SLA-8** |

若需完整 REMOVED/ADDED/MODIFIED 原文，見封存目錄內之 `specs/prepare-z-clip-slider/spec.md`。

---

## v3.1 Bugfix 合併紀錄（2026-06-18，無對應 active change）

本批為 archive `2026-04-20-prepare-clip-view` 落地後在多物件 / Support 浮空場景發現的回歸修正，直接 in-place 補進 main spec（沿用 `2026-05-14` delta 之後的慣例：bugfix 不另開 change，由 main spec 反映現狀）。

| Delta 區塊 | 落點 |
|------------|------|
| MODIFIED：SLA-3 `scene_max_z` 計算永遠遍歷全場景；gizmo session 改用 selected object bbox + `get_sla_shift()` | SLA-3 v3.1 註記 |
| MODIFIED：SLA-4 gizmo session 中 ObjectClipper 改用 `set_range_and_pos(+Z, z_high_eff, ratio)` 對齊 visual world Z | SLA-4 v3.1 註記 |
| ADDED：SLA-10 Gizmo 退出端點映射（full visible → `m_prepare_scene_max_z`；non-full → world Z 保留）| 新增 SLA-10 |
| ADDED：SLA-11 Hollow / Drill / Support 互切時 re-enter Gizmo Slider Mode（`m_last_oc_gizmo_type` 追蹤）| 新增 SLA-11 |
| ADDED：SLA-12 `data_changed()` 須先於 SLA session 偵測，使 Support 的 `set_use_config_elevation(true)` 在 `enter_gizmo_slider_mode()` 讀 `get_sla_shift()` 之前已套用 | 新增 SLA-12 |
| ADDED：驗收 T15–T19（多物件退出 range、世界 Z 保留、gizmo 互切、cap 對齊）| 驗收表 |
| ADDED：Out of Scope 註明 SLA Support Points 預覽縮放錯位為既有 / 另案問題 | Out of Scope |

對應實作檔案：`src/slic3r/GUI/GLCanvas3D.cpp`、`src/slic3r/GUI/GLCanvas3D_SlaPrepareSlider.cpp`、`src/slic3r/GUI/Gizmos/GLGizmosManager.cpp/.hpp`。

封存目錄 `openspec/changes/archive/2026-04-20-prepare-clip-view/` 內 `specs/prepare-z-clip-slider/spec.md` 為原始 delta，**不修改**（保持封存歷史完整）；`design.md` 與 `tasks.md` 末尾加註指向本段的 post-archive notes。
