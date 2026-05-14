# Spec: Prepare View Global Z-Clip Slider（修訂版 v3）

## 功能描述

在 SLA 的 Prepare（3D）view 右側使用與 Preview 相同的 **`IMSlider` 垂直雙拇指層滑桿**（非自繪連續 mm 滑桿）。
層 Z 由**全場景 max Z** 與 **SLA 列印 `layer_height` + 材料 `initial_layer_height`** 估算；進入 Hollow / Drill / SLA Support 時改為右側 **單柄 gizmo 滑桿**（沿用舊自繪），此時隱藏 `IMSlider`。

---

## 行為規格

### SLA-1：Slider 顯示條件

| 條件 | 結果 |
|------|------|
| `CanvasView3D` + `ptSLA`、非 Gizmo | **`IMSlider` 顯示** |
| `CanvasView3D` + `ptSLA`、Gizmo（Hollow/Drill/Support）| **單柄自繪滑桿**（`IMSlider` 隱藏）|
| `CanvasPreview` | Slider **隱藏**（Preview 有自己的 layer slider）|
| `ptFFF` 任何狀態 | Slider **永不顯示** |
| 無物件載入（空場景）| Slider **顯示**（range 使用預設值，無視覺效果）|

> **v3**：非 Gizmo 時為 `IMSlider`；Gizmo 時為單柄自繪。仍無需選取物件即可顯示（空場景亦顯示）。

### SLA-2：Slider 外觀與操作（非 Gizmo）

- **元件**：`GCodeViewer::get_layers_slider()`（`IMSlider`），與 Preview 左側 3D 相同渲染管線。
- **方向**：垂直雙拇指；滾輪以**層索引**為步進（與 Preview 一致）。
- **額外 UI**（Prepare 專用）：滑桿左側獨立欄位顯示**當前層（1-based）**與**當前高度（mm）**；**▲ / ▼** 按鈕分離排版，不與拇指熱區重疊。
- **單層**：雙拇指同一索引時，可見區間為該層 \[z_{i-1}, z_i\]（首層 z_{-1}=0）；其餘語意對齊 Preview `on_sla_layer_slider_changed` 之雙平面邏輯。
- **初始值**：下拇指 = 第一層、上拇指 = 最後一層（顯示完整模型，無截面）。

### SLA-3：Range 與層 Z 列表

```
scene_max_z = 與 v2 相同（全場景 max Z，空場景 50 mm）

層頂 Z 列表 zs[]（估算）：
  zs[0] = initial_layer_height（材料預設）
  zs[k] = zs[k-1] + layer_height（列印預設），直到 zs[last] >= scene_max_z

滑桿索引 i 對應 zs[i]；截面計算：
  - 下索引 = 上索引 = i：層 i 單層 slab（見 SLA-2）
  - 下索引 lo < 上索引 hi：與 Preview 相同（lo=0 時底平面 ClipsNothing；hi=max 時頂平面 ClipsNothing）
```

> **v3**：連續 mm 拖曳已移除；Gizmo 內仍為單柄比例滑桿（舊 `_render_prepare_clip_slider` 之 gizmo 分支）。

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

---

## Delta 合併紀錄（change: `resin-prepare-layer-slider-align-preview`，2026-05-14）

以下為封存前自 `openspec/changes/.../specs/prepare-z-clip-slider/spec.md` 合併之摘要；實質條文已反映於上文 **v3** 之 SLA-1～SLA-3、驗收表。

| Delta 區塊 | 合併方式 |
|------------|----------|
| REMOVED：舊 SLA-2（連續 mm 雙 bar）、舊 SLA-3（連續 Z range） | 已由 v3 **SLA-2／SLA-3**（`IMSlider`、離散 `zs[]`）取代 |
| ADDED：Prepare 與 Preview 共用 `IMSlider`、離散層 Z、層序／高度顯示、單層與按鈕熱區、裁剪語意對齊 Preview | 已併入 **SLA-1、SLA-2、SLA-3** 與驗收 **T1～T4** |
| MODIFIED：SLA-1 顯示條件、SLA-8 與 Preview 隔離 | 已併入 **SLA-1**（含 Gizmo 列）與 **SLA-8** |

若需完整 REMOVED/ADDED/MODIFIED 原文，見封存目錄內之 `specs/prepare-z-clip-slider/spec.md`。
