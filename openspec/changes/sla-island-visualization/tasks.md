# Tasks：SLA 孤島偵測視覺化與局部支撐生成

## Phase 1：IslandDetectionService（核心偵測模組）

- [ ] 1.1 新增 `src/libslic3r/SLA/IslandDetectionService.hpp`：
  - 定義 `IslandResult` struct（`std::vector<std::pair<float, ExPolygons>> layers` + `int total_island_count`）
  - 定義 `IslandDetectionService` class，公開 `detect(const ModelObject*, PrepareSupportConfig)` 方法
- [ ] 1.2 新增 `src/libslic3r/SLA/IslandDetectionService.cpp`：
  - `detect()` 內部：取得模型切片資料 → 呼叫 `prepare_generator_data()` → 迭代 layers 收集 `prev_parts.empty()` 的 `part.shape`
  - 將每層的 island ExPolygon 與 `print_z` 組成 pair 填入 `IslandResult.layers`
  - 累計 `total_island_count`
- [ ] 1.3 偵測精度對應：`DetectionAccuracy` enum（Low=4.0mm / Middle=2.0mm / High=0.8mm）對應 `PrepareSupportConfig::discretize_overhang_step`
- [ ] 1.4 加入 CMakeLists.txt 編譯

## Phase 2：IslandSupportGenerator（局部支撐點生成）

- [ ] 2.1 新增 `src/libslic3r/SLA/IslandSupportGenerator.hpp`：
  - 定義 `IslandSupportGenerator` class，公開 `generate(const IslandResult&, const ModelObject*, SupportPointGeneratorConfig)` 方法
  - 回傳 `sla::SupportPoints`
- [ ] 2.2 新增 `src/libslic3r/SLA/IslandSupportGenerator.cpp`：
  - 對 `IslandResult.layers` 每層每個 ExPolygon 呼叫 `uniform_support_island(*shape, {}, config.island_configuration)`
  - 收集 `SupportIslandPoints`，轉換為 `LayerSupportPoints`（`SupportPointType::island`）
  - 呼叫 `move_on_mesh_surface(layer_pts, emesh, allowed_move)` 投影到 mesh 表面
  - 過濾空結果，回傳 `sla::SupportPoints`
- [ ] 2.3 加入 CMakeLists.txt 編譯

## Phase 3：GLGizmoLcdOverhangDetection UI 重構

- [ ] 3.1 在 `GLGizmoLcdOverhangDetection.hpp` 新增成員：
  - `DetectionAccuracy m_accuracy`（enum，預設 Middle）
  - `int m_selected_object_idx`
  - `std::unordered_map<ObjectID, ModelIslandState> m_island_states`
  - `ModelIslandState` struct（含 `IslandResult`、`GLModel` contour mesh、`sla::SupportPoints`、`GLModel` support mesh）
- [ ] 3.2 重構面板 `on_render_input_window()`：
  - 新增偵測精度 RadioButton（Low / Middle / High）
  - 新增模型切換器（`◀ ModelName ▶`）
  - 新增孤島數量顯示（`total_island_count` 或 `--`）
  - 新增 `[Detect All]` / `[Detect Selected]` 按鈕
  - 新增 `[Add Overhang Support]` 按鈕
- [ ] 3.3 實作模型切換邏輯：
  - `◀` / `▶` 更新 `m_selected_object_idx`，切換後顯示對應模型的偵測結果

## Phase 4：偵測觸發與視覺化渲染

- [ ] 4.1 實作 `GLGizmoLcdOverhangDetection::run_detection(ModelObject*)`：
  - 呼叫 `IslandDetectionService::detect()` 取得 `IslandResult`
  - 儲存至 `m_island_states[object_id].detection_result`
  - 呼叫 `rebuild_island_contour_mesh()` 重建渲染網格
- [ ] 4.2 實作 `rebuild_island_contour_mesh(const IslandResult&)`：
  - 對每層每個 ExPolygon 呼叫 `triangulate_expolygon_3d(shape, z + 0.05f)`
  - 合併為 `indexed_triangle_set` → `GLModel::init_from()`
  - 設定顏色：`ColorRGBA(1.0f, 0.95f, 0.0f, 0.45f)`
- [ ] 4.3 實作 `Detect All` 按鈕：迭代場景所有 `ModelObject`，依序呼叫 `run_detection()`
- [ ] 4.4 實作 `Detect Selected` 按鈕：僅對 `m_selected_object_idx` 對應的模型呼叫 `run_detection()`
- [ ] 4.5 在 `on_render()` 中渲染 island 輪廓網格：
  - `glDisable(GL_DEPTH_TEST)` + `glDisable(GL_CULL_FACE)` → 渲染 contour mesh → 還原狀態

## Phase 5：局部支撐生成與渲染

- [ ] 5.1 實作 `GLGizmoLcdOverhangDetection::run_island_support(ModelObject*)`：
  - 若無偵測結果，先呼叫 `run_detection()`
  - 呼叫 `IslandSupportGenerator::generate()` 取得 `sla::SupportPoints`
  - 儲存至 `m_island_states[object_id].support_points`
- [ ] 5.2 實作 `TreeSupportBuilder` 邏輯（可為 `GLGizmoLcdOverhangDetection` 內部 private 方法）：
  - 建立 `SupportData`（`IndexedMesh`）
  - 以 `support_points` 實例化並執行 `SupportTreeBuildsteps`
  - 取出 `support_mesh()` 回傳 `TriangleMesh`
- [ ] 5.3 將 `TriangleMesh` 轉換為 `GLModel`，儲存至 `m_island_states[object_id].support_gl_model`
- [ ] 5.4 在 `on_render()` 中渲染支撐樹 mesh：正常深度測試，顏色 `ColorRGBA(0.5f, 0.7f, 1.0f, 0.6f)`
- [ ] 5.5 實作 `[Add Overhang Support]` 按鈕觸發 `run_island_support()`

## Phase 6：邊界條件與安全防護

- [ ] 6.1 `slaposObjectSlice` 未完成時停用 Detect 按鈕，顯示提示文字
- [ ] 6.2 `total_island_count == 0` 時停用 `[Add Overhang Support]` 按鈕
- [ ] 6.3 偵測執行中防止重複觸發（`m_detection_running` flag）
- [ ] 6.4 gizmo 關閉時（`on_set_state(Off)`）清除所有 `m_island_states` 中的 GLModel
- [ ] 6.5 非 SLA 機種進入 gizmo 時，所有功能停用
- [ ] 6.6 保留舊版偵測流程（commit `9a07fe61b9`）：確認舊版程式碼未被刪除，新流程獨立運作

## Phase 7：整合驗證

- [ ] 7.1 編譯無錯誤、無警告
- [ ] 7.2 開啟含明確 island 的模型，執行 `Detect Selected`，確認輪廓正確覆蓋 island 區域
- [ ] 7.3 切換偵測精度後重新偵測，確認高精度輪廓更細緻
- [ ] 7.4 `Detect All` 對所有模型偵測完成，模型切換器能切換並顯示各自的孤島數量
- [ ] 7.5 `Add Overhang Support` 生成支撐樹，確認支撐柱僅出現在 island 區域
- [ ] 7.6 未偵測情況下點 `Add Overhang Support`，確認自動先偵測再生成支撐
- [ ] 7.7 切換不同模型，確認各模型的偵測結果與支撐 mesh 互相獨立不混用
- [ ] 7.8 關閉 gizmo 再重新開啟，確認狀態正確清除（無殘留 mesh）
- [ ] 7.9 FDM 機種下進入 gizmo，確認所有按鈕停用、無渲染錯誤
- [ ] 7.10 主切片流程（`slaposSupportPoints` / `slaposSupportTree`）行為與修改前完全一致（regression）
