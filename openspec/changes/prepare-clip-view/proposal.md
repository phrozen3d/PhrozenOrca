# Proposal: Prepare View Global Z-Clip Slider（修訂版 v2）

## 背景

目前 SLA 工作流程中，Z 軸截面預覽（clipping view）被內嵌在各個 Gizmo 的 ImGui 面板裡：

- GLGizmoHollow：面板底部有 "View clipping" slider
- GLGizmoDrill：同樣有 "View clipping" slider
- GLGizmoSlaSupports：同樣有 "View clipping" slider

這些 slider 只在進入特定 Gizmo 時才能操作，且各自獨立——切換 Gizmo 後截面消失。
使用者無法在不進入 Gizmo 的情況下預覽截面效果。

---

## 目標（v2 修訂）

在 SLA 印表機模式的 Prepare（3D）view 中新增一個**全域 Z 軸截面 Slider**：

1. **常駐存在**：只要是 SLA 模式就顯示，不論是否選取物件、進入任何 Gizmo mode 皆不影響
2. **不需切片**：純 Z 軸 clipping plane，無需 G-code 或 SLA layer 資料
3. **場景範圍**：range 為 Z=0（最低）到場景所有物件 bounding box 的 max Z（最高）
4. **外觀與操作**：與 Preview 頁面右側的 layer slider 相同，支援從最高或最低位置拖曳兩個 bar
5. **Gizmo 整合**：進入 Hollow / Drill / SLA Support 模式時，gizmo 自己的 view clipping slider **停用（移除 UI）**，改由全域 Prepare Slider 統一控制
6. **不影響 Preview**：所有變更限於 prepare canvas，Preview tab 行為不受影響

---

## 與 v1 的主要差異

| 面向 | v1（舊計劃）| v2（新目標）|
|------|------------|------------|
| 顯示條件 | 需選取 SLA 物件 | SLA 模式下常駐顯示 |
| Range 計算 | 選取物件聯集 bbox | **全場景所有物件 max Z**，下限固定 Z=0 |
| 雙手柄 | upper + lower（基於 ratio）| 同 Preview：top bar + bottom bar |
| Gizmo slider | 移除 UI，同步 ObjectClipper | 移除 UI，ObjectClipper 由全域 slider 驅動 |

---

## 目標使用者場景

1. 載入 SLA 模型 → Prepare view 右側即出現 Z-clip slider（不需任何操作）
2. 拖動 top bar 向下 → 模型頂部被截掉（截面效果即時可見）
3. 進入 Hollow Gizmo → 截面保持原位，gizmo 面板無 "View clipping" 行
4. 在 Hollow Gizmo 內進行漏空設定 → 不影響截面 slider
5. 離開 Hollow Gizmo → 截面位置不變
6. 進入 Drill Gizmo → 可在截面暴露的內壁上點擊放置鑽孔

---

## 參數共用分析（Preview vs Prepare）

**結論：不共用，但機制相同。**

Preview canvas 和 Prepare（view3D）canvas 是**兩個獨立的 GLCanvas3D 實例**，
各自擁有獨立的 `m_clipping_planes[2]` 和 `m_use_clipping_planes`。

在 view3D canvas 上呼叫 `set_clipping_plane()` **不會影響** preview canvas 的任何狀態。
因此：
- Prepare Slider 設定 view3D 的 `m_clipping_planes[1]`
- Preview Slider 設定 preview canvas 的 `m_clipping_planes[1]`
- 兩者完全獨立，互不干擾

若強行共用（例如透過 Plater 共享位置參數），反而會讓 Preview slider 跟著 Prepare slider 聯動，
違背「不影響 Preview」的需求。

---

## 架構決策

### 雙系統並行（同 v1）

Prepare Slider 移動時同時驅動兩個系統：

```
Prepare Slider 移動
  → set_clipping_plane(0, ...) + set_clipping_plane(1, ...)  （視覺 Z-range，走 shader）
  → object_clipper()->set_position_by_ratio(ratio, true)     （raycasting filter + cap mesh）
```

### 場景 max Z 計算

```
scene_max_z = 所有 ModelObject 的 bounding_box().max.z()（含 instance transform）
              若場景無物件則 scene_max_z = 50.0（預設值）
slider range = [0.0, scene_max_z]
```

### Gizmo 行為

進入 Hollow / Drill / SLA Support 時：
- Gizmo 的 "View clipping" slider UI **不顯示**（移除渲染程式碼）
- ObjectClipper 從 `m_clipping_planes[1]` 同步（確保 raycasting 正確）
- Gizmo 的核心功能（漏空、鑽孔、支撐）**完全不受影響**

---

## Stage 1.5：Slider UI 視覺樣式決策

### 背景

Stage 1 實作的 `_render_prepare_clip_slider()` 使用原始 `ImGui::VSliderFloat`（灰色無樣式雙滑桿），
外觀與 Preview tab 的 IMSlider 差距過大。Stage 1.5 的目標是讓 Prepare slider 外型與 Preview slider 一致。

### 方案比較

#### 方案 A — 共用 IMSlider（Share）

直接在 Prepare view 建立 `IMSlider` 新實例，複用 Preview 所用的相同 class。

**關鍵障礙**：`IMSlider` 以整數 layer index 為值域（`m_values[index]`），
Prepare clip slider 操作的是連續浮點 Z 值（`m_prepare_clip_z_low` / `m_prepare_clip_z_high`），
兩者之間存在本質上的 impedance mismatch。

| 面向 | 評估 |
|------|------|
| 視覺一致 | ✅ 完全相同外觀 |
| 適配難度 | ❌ 高：需假造 `m_values[]` 陣列、設定 `DrawMode::dmSlaPrint`、處理整數/浮點轉換 |
| 狀態隔離 | ❌ 風險高：IMSlider 內含大量 tick/colorchange/gcode 狀態，Prepare 完全用不到 |
| 未來維護 | ❌ 上游 IMSlider 任何變更都可能破壞 Prepare 用法 |
| 影響 Preview | ⚠️ 低風險但需謹慎：共用 class 的 texture 靜態資源 |

#### 方案 B — 不共用，手寫仿 IMSlider 樣式（Replicate Style）✅ **採用**

在 `_render_prepare_clip_slider()` 內使用 ImGui DrawList API 直接繪製，
視覺模仿 IMSlider 的外觀（軌道、把手圓點、Z 值 label、半透明背景），邏輯完全獨立。

| 面向 | 評估 |
|------|------|
| 視覺一致 | ✅ 可達 90% 相似（手工調整顏色/尺寸） |
| 適配難度 | ✅ 低：直接操作浮點 Z 值，無轉換問題 |
| 狀態隔離 | ✅ 完全獨立，不碰 GCodeViewer / IMSlider 內部狀態 |
| 未來維護 | ✅ 只有 Prepare clip 自己用，上游改動不影響 |
| 影響 Preview | ✅ 零風險 |

### 決策

**採用方案 B**。

IMSlider 設計為 gcode/print preview 的 layer-index 驅動元件，強行套用到連續 Z 浮點 clip 值
會引入不必要的複雜度與耦合。方案 B 在合理工作量下可達到視覺一致，並完全隔離兩個 slider 的狀態。

### 視覺元素規格

使用 `ImGui::GetWindowDrawList()` 繪製以下元素：

| 元素 | 規格 |
|------|------|
| 背景面板 | 半透明深色圓角矩形（寬 34px，高度依畫布） |
| 垂直軌道 | 寬 4px，圓角，中央居中 |
| High handle | 圓形（半徑 8px），可拖曳，顏色 `#E0E0E0` |
| Low handle | 圓形（半徑 8px），可拖曳，顏色 `#A0A0A0` |
| 選中區間 | High/Low 之間的軌道填色（accent 色） |
| Z 值 label | Handle 左側顯示 `12.3mm`，字型 `ImGui::GetFont()` |

---

## 範圍邊界（Out of Scope）

- FDM 模式不顯示此 Slider
- 不修改 Preview tab 任何行為
- 截面角度旋轉（僅 Z 軸）
- 截面狀態不持久化（關閉後重置為全顯示）

---

## 成功標準

- [ ] SLA 模式 Prepare view 右側常駐顯示雙 bar Z-clip slider
- [ ] Slider 在 Hollow / Drill / Support Gizmo 進出之間保持位置
- [ ] Gizmo 面板不再顯示 "View clipping" slider
- [ ] 截面暴露的內壁在 Drill / Support 模式下可點擊
- [ ] FDM 流程完全不受影響
- [ ] Preview tab 行為與修改前一致
