## 1. 資料結構

- [ ] 1.1 在 `SupportPoint.hpp` 中新增 `SupportWeight` 列舉（`Light=0`、`Medium=1`、`Heavy=2`）
- [ ] 1.2 在 `SupportPoint` struct 中新增 `SupportWeight weight = SupportWeight::Medium` 欄位
- [ ] 1.3 更新 `operator==`，加入 `weight` 比較
- [ ] 1.4 更新 cereal `serialize()`：`ar(pos, head_front_radius, type, weight)`

## 2. 支撐樹建構

- [ ] 2.1 在 `SupportTreeBuildsteps.cpp` 的 `filterfn` 中，於計算 `back_r` 後加入 weight 縮放邏輯：
  - 若 `pt.type == manual_add`：`back_r *= weight_scale(pt.weight)`（Light=0.6, Medium=1.0, Heavy=1.4）
  - 否則不縮放

## 3. 檔案格式

- [ ] 3.1 在 `3mf.cpp` 寫入路徑：每個支撐點寫入第六個浮點數（`Light=0.0`、`Medium=1.0`、`Heavy=2.0`）
- [ ] 3.2 在 `3mf.cpp` 讀取路徑：若存在第六個浮點數則解碼 weight，否則預設 `Medium`
- [ ] 3.3 對 `bbs_3mf.cpp` 套用相同的寫入/讀取變更
- [ ] 3.4 對 `AMF.cpp` 套用相同的寫入/讀取變更

## 4. GLGizmo UI

- [ ] 4.1 在 `GLGizmoSlaSupports.hpp` 中新增 `m_new_point_weight`（`SupportWeight`，預設 Medium）
- [ ] 4.2 在 `on_render_input_window()` 手動模式區塊新增 Light/Medium/Heavy 選擇器（RadioButton 或 SegmentedControl）
- [ ] 4.3 在點擊放置新點時將 `m_new_point_weight` 設入新 `SupportPoint.weight`

## 5. 驗證

- [ ] 5.1 編譯確認零錯誤
- [ ] 5.2 放置 Light 點並生成支撐，確認柱子明顯細於 Medium
- [ ] 5.3 放置 Heavy 點並生成支撐，確認柱子明顯粗於 Medium
- [ ] 5.4 自動生成的 island/slope 點使用全域粗細（weight 欄位被忽略）
- [ ] 5.5 儲存 3mf 並重新載入，確認 weight 正確還原
- [ ] 5.6 載入舊 3mf（5 欄位）確認預設為 Medium，程式不崩潰
- [ ] 5.7 確認 FDM 切片不受影響
