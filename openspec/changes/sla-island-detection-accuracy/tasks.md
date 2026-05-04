## 1. 後端：偵測用重切片函式

- [x] 1.1 在 `SLAPrintSteps.cpp` 中新增 `build_detection_height_levels(const BoundingBoxf3& bb3d, float detect_lh, float initial_lh)` helper：依 `detect_lh` 建立與 `slice_model()` 相同邏輯的 Z 網格，回傳 `std::vector<float>`（Z 高度列表）
- [x] 1.2 在 `SLAPrintSteps.cpp` 中於 `SLAPrint::Steps` class 實作 `Steps::prepare_island_detection(SLAPrintObject& po, float detect_lh)`（Steps 為 SLAPrintObject 的 friend，可存取 `po.m_mesh_to_slice`）：
  1. 呼叫 `build_detection_height_levels()` 產生 `detect_heights`
  2. 以 `Range{po.m_mesh_to_slice.cbegin(), po.m_mesh_to_slice.cend()}` 建立 csg_range
  3. 呼叫 `csg::slice_csgmesh_ex(csg_range, detect_heights, params, thr)` 產生 `detect_slices`（local，不寫入 `po`）
  4. 呼叫 `sla::prepare_generator_data(std::move(detect_slices), detect_heights, prep_cfg, thr)` 產生 `temp_data`（local）
  5. 從 `temp_data.layers` 提取 `prev_parts.empty()` 的 island contours，呼叫 `po.set_island_contours()`
  6. 函式返回前 `temp_data` 與 `detect_slices` 自動銷毀

**驗收**：呼叫後 `po.m_model_slices`、`po.m_support_point_generator_data`、`po.m_model_height_levels` 均不改變；`po.island_contours().valid == true`。

---

## 2. 後端：保護 `prepare_for_generate_supports()` 不受影響

- [x] 2.1 確認 `prepare_for_generate_supports()` 仍使用 `po.m_model_height_levels`（原始切片高度），不接觸任何偵測用資料
- [x] 2.2 確認 `support_points()` 的 auto generate 路徑仍使用 `po.m_support_point_generator_data`，與 island detection 路徑完全獨立
- [x] 2.3 移除或調整 `slice_model()` 末段的原始 island 提取邏輯（原本以 `po.m_model_height_levels` 切片的提取），改為不在 `slice_model()` 提取（由 Gizmo 按需觸發）

**驗收**：auto generate support points 的點數和位置與修改前完全相同。

---

## 3. Gizmo：DetectionAccuracy 對應 detect_lh

- [ ] 3.1 在 `GLGizmoLcdOverhangDetection.cpp` 新增 `accuracy_to_layer_height(DetectionAccuracy acc) -> float`：
  ```
  High   → 0.05f
  Middle → 0.1f
  Low    → 0.5f
  ```
- [ ] 3.2 在 `sync_island_data_for_object(int obj_idx)` 中，改為呼叫 `wxGetApp().plater()->sla_print().redetect_islands(mo->id(), detect_lh)` 觸發偵測（Task 5 實作的公開入口），再讀取 `po->island_contours()` 取得結果

---

## 4. Gizmo：切換精度等級不自動重偵測

- [ ] 4.1 確認 `DetectionAccuracy` enum 切換（Low/Middle/High UI 按鈕）只更新 `m_detection_accuracy` 成員，**不呼叫** `sync_island_data_for_object()`
- [ ] 4.2 確認 Detect Selected 按鈕每次都重新呼叫 `sync_island_data_for_object()`（現有邏輯，無需修改），從而使用最新的 `m_detection_accuracy` 對應的 layer height

**驗收**：切換等級後 overlay 不改變；按下 Detect Selected 後 overlay 更新為新等級結果。

---

## 5. 存取路徑：`SLAPrint::redetect_islands()` 包裝 Steps（方案 B）

- [x] 5.1 在 `SLAPrintSteps.hpp` 的 `Steps` class 宣告 `prepare_island_detection(SLAPrintObject& po, float detect_lh)`（private，供 `SLAPrint` 呼叫）
- [x] 5.2 在 `SLAPrint.hpp` 的 `SLAPrint` class public 區段新增 `void redetect_islands(ObjectID obj_id, float detect_lh)` 宣告
- [x] 5.3 在 `SLAPrint.cpp` 實作 `SLAPrint::redetect_islands()`：
  ```cpp
  void SLAPrint::redetect_islands(ObjectID obj_id, float detect_lh) {
      auto it = std::find_if(m_objects.begin(), m_objects.end(),
          [&](SLAPrintObject* po){ return po->model_object()->id() == obj_id; });
      if (it == m_objects.end()) return;
      Steps steps(this);
      steps.prepare_island_detection(**it, detect_lh);
  }
  ```
- [x] 5.4 確認 `Plater.hpp` / `Plater.cpp` **不需要修改**（Gizmo 直接使用 `plater()->sla_print().redetect_islands()`，與現有 `m_parent.sla_print()->objects()[idx]->is_step_done()` 模式一致）

---

## 6. UI：精度等級顯示與預設值

- [ ] 6.1 將 `on_opening()` 中 `m_detection_accuracy` 的初始值從 `Accuracy_Low` 改為 `Accuracy_Middle`（預設使用 0.1mm 偵測）

- [ ] 6.2 在精度等級三個按鈕下方新增一行 layer height 提示文字：
  - 使用 Task 3.1 實作的 `accuracy_to_layer_height()` 取得當前值
  - 格式：`"Layer Height: 0.10mm"`（固定兩位小數）
  - 使用與其他標籤相同的 `m_imgui->text()` 風格，不加額外樣式
  - 此行每幀更新，隨切換等級即時反映

- [ ] 6.3 確認精度切換與 Detect Selected 的串接正確：
  - 切換等級只更新 `m_detection_accuracy`，**不**呼叫 `sync_island_data_for_object()`（符合 D4：切換不自動重偵測）
  - Detect Selected 的 handler 呼叫 `sync_island_data_for_object()` 時，內部讀取 `m_detection_accuracy` 計算 `detect_lh`，傳入 `plater()->sla_print().redetect_islands(mo->id(), detect_lh)`
  - 用手動驗證：低精度偵測 → 切換至高精度 → overlay 不變 → 再按 Detect Selected → overlay 更新為高精度結果

**驗收**：初次進入 gizmo 時「中」按鈕呈 focus 狀態；切換等級後 layer height 文字即時更新；Detect Selected 結果正確反映當前等級。

---

## 7. 驗收測試

- [ ] 7.1 高精度（0.05mm）的偵測結果與修改前行為一致（當模型 layer height = 0.05mm 時）
- [ ] 7.2 低精度（0.5mm）偵測結果的 island 面積 ≥ 高精度結果（對常見幾何形狀驗證）
- [ ] 7.3 切換等級後正式切片輸出（`po.m_model_slices`）不改變
- [ ] 7.4 auto generate support points 的點數和位置與修改前完全相同（regression）
- [ ] 7.5 Detect Selected → Add Overhang Supports 的完整流程在三個等級下均正常運作
- [ ] 7.6 重複點擊 Detect Selected（不同等級）不會 crash 或 memory leak
