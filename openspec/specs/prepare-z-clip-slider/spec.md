# Spec: Prepare View Global Z-Clip Slider（修訂版 v2）

## 功能描述

在 SLA 的 Prepare（3D）view 右側新增一個**常駐全域雙 bar Z 軸截面 Slider**。
外觀與行為和 Preview 的 layer slider 一致（垂直雙 bar）。
使用者不需進入特定 Gizmo 即可即時預覽截面效果；
進入 Hollow / Drill / SLA Support 模式時，gizmo 不再顯示自己的 "View clipping" slider，
由此全域 slider 統一控制截面。

---

## 行為規格

### SLA-1：Slider 顯示條件

| 條件 | 結果 |
|------|------|
| `CanvasView3D` + `ptSLA`（任何 Gizmo 狀態）| Slider **顯示** |
| `CanvasPreview` | Slider **隱藏**（Preview 有自己的 layer slider）|
| `ptFFF` 任何狀態 | Slider **永不顯示** |
| 無物件載入（空場景）| Slider **顯示**（range 使用預設值，無視覺效果）|

> **v2 修訂**：移除「需選取 SLA 物件」的條件，改為 SLA 模式常駐顯示。

### SLA-2：Slider 外觀與操作

- **位置**：畫布右側浮動，與 Preview layer slider 相同位置
- **方向**：垂直雙 bar
  - top bar（z_high）：從頂部往下拖——截掉模型頂部
  - bottom bar（z_low）：從底部往上拖——截掉模型底部
- **標籤**：無；懸停 tooltip 顯示當前 Z 高度（mm）
- **初始值**：bottom=0，top=scene_max_z（無截面，顯示完整模型）
- **兩 bar 皆在端點時**：`m_use_clipping_planes = false`，截面完全取消

### SLA-3：Range 計算（全場景 max Z）

```
scene_max_z = 所有 ModelObject 在世界座標的 bounding_box().max.z()
              （含 instance transform，取所有物件聯集最大值）
              若場景無物件或計算結果 <= 0 → scene_max_z = 50.0（預設值）

slider range:
  bottom bar（z_low）∈ [0.0, z_high]
  top bar（z_high）  ∈ [z_low, scene_max_z]

截面計算：
  ClippingPlane[0] = ClippingPlane(+UnitZ, -z_low)   // 截掉 z < z_low
  ClippingPlane[1] = ClippingPlane(-UnitZ,  z_high)  // 截掉 z > z_high
```

> **v2 修訂**：
> - 下限固定為 Z=0（非物件 bbox min Z）
> - 上限為**場景所有物件** max Z（非選取物件）

### SLA-4：雙系統同步

兩個管線同時驅動：

1. `GLCanvas3D::m_clipping_planes[2]`（視覺 Z-range，走 shader）
2. `object_clipper()->set_position_by_ratio(ratio_high, false)`（raycasting filter + cap mesh）

注意：ObjectClipper 只同步 **top（z_high）**，bottom 僅由 shader Z-range 控制。

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

**各 Gizmo ImGui 面板的原有 "View clipping" slider 移除。**

**進入 Gizmo 時（on_set_state: On）：**
- 從 `m_clipping_planes[1]` 讀取 z_high → 計算 ratio_high = z_high / scene_max_z
- 若 ratio_high < 1.0：呼叫 `object_clipper()->set_position_by_ratio(ratio_high, false)`
- ObjectClipper 確保 raycasting 正確識別截面位置

**全域 Slider 移動時（含 Gizmo 激活中）：**
- 同步更新 `m_clipping_planes[0/1]`（視覺）
- 同步更新 `object_clipper()`（raycasting + cap）

**離開 Gizmo 時（on_set_state: Off）：**
- 從 `object_clipper()->get_position()` 讀 ratio → 反算 z_high
- 回寫 `m_clipping_planes[1]`（確保全域 slider 位置一致）
- 使用 `m_syncing_clipper` flag 防止遞迴

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
- `_render_prepare_clip_slider()` 在 FDM 模式下不執行
- Preview canvas 為獨立實例，不受影響

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

---

## 不在範圍（Out of Scope）

- 截面角度旋轉（僅 Z 軸）
- Preview tab 行為變更
- 截面狀態持久化（存入 3mf 檔案）
- 場景物件新增/刪除後 scene_max_z 即時動態調整（初版可在每次 render 前更新）
