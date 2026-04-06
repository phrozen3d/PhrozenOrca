## 背景

`SupportPoint` 目前有兩個關鍵欄位：`head_front_radius`（pin 半徑，per-point）與 `type`（來自 merge-support-point-type）。柱子粗細由全域 `head_back_radius_mm` 決定，在 `SupportTreeBuildsteps.cpp` 的 `filterfn` 中透過 `back_r` 傳入：

```cpp
// SupportTreeBuildsteps.cpp ~657
filterfn(filtered_indices[i], i, m_cfg.head_back_radius_mm);
// 在 filterfn 內：
// double back_r = ...;  // 目前全域固定值
// h.r_back_mm = back_r; // 決定 Head 的後球半徑 → Pillar.r_start
```

`Head.r_back_mm` 透過 `add_pillar()` 直接成為柱子半徑，因此在 `filterfn` 這個位置縮放 `back_r` 可以精準控制 per-point 的柱子粗細。

## 目標 / 非目標

**目標：**
- 新增 per-point `SupportWeight` 欄位（Light/Medium/Heavy）
- 僅 `type == manual_add` 的點套用縮放（自動生成點不受影響）
- GLGizmo 提供 UI 選擇器
- 3mf 向後相容序列化

**非目標：**
- 自動生成點（island/slope）的 weight 控制
- 超過三檔的精細調整
- 修改 `head_front_radius`（pin 半徑）縮放

## 決策

### D1：縮放因子

```
Light  = 0.6x  → back_r * 0.6
Medium = 1.0x  → back_r * 1.0（不縮放，維持現有行為）
Heavy  = 1.4x  → back_r * 1.4
```

縮放僅在 `type == manual_add` 時套用。island/slope 點不縮放（保持全域參數）。

**考慮過的替代方案**：讓使用者輸入任意倍數。已拒絕——三檔簡單易懂，符合使用場景。

### D2：3mf 序列化策略

新增第六個浮點數欄位（index 5），編碼 `SupportWeight`：
```
0.0 → Light
1.0 → Medium（預設，舊檔案不含此欄位時使用）
2.0 → Heavy
```

舊格式只有 5 個浮點數，讀取時若欄位缺失預設為 `Medium`（1.0），完全向後相容。

**考慮過的替代方案**：編碼進 `head_front_radius` 小數部分。已拒絕——`head_front_radius` 是實際幾何值，混入語意資訊會造成歧義。

### D3：UI 設計

在 GLGizmo 面板的「手動模式」區塊新增 Radio/SegmentedControl 選擇器（Light / Medium / Heavy）。選擇後影響**後續點擊放置**的新點，不影響已存在的點。已存在的點可透過右鍵選單或選取後修改。

### D4：依賴 SupportPointType

weight 縮放的啟用條件為 `type == manual_add`，因此此 change 在實作上依賴 `merge-support-point-type` 先完成。若尚未完成，可暫以 `is_new_island == false` 作為臨時條件（但語意不完全相同）。

## 風險 / 取捨

- **Heavy 支撐過粗**：`1.4x back_r` 在某些模型上可能導致支撐相互碰撞或超出模型邊界。→ 碰撞偵測（`filterfn` 內的 `pinhead_mesh_intersect`）已存在，Heavy 點若碰撞仍會被過濾。縮放因子可後續根據實際測試調整。
- **cereal 序列化**：新增 `weight` 欄位改變 undo/redo 快照的二進位格式。→ 快照為 session-only，升級後重開即可，不需遷移。
- **舊 3mf 的 Medium 預設**：舊檔案沒有第六個欄位，讀取時預設 Medium，行為與舊版一致。

## 遷移計畫

1. 先完成 `merge-support-point-type`（或至少 struct 部分）
2. 新增 `SupportWeight` 欄位
3. 實作 `filterfn` 縮放
4. 實作序列化
5. 實作 GLGizmo UI
