## 1. OrientJob — SLA/FFF config 分支（sla-orient-job-config-safety）

- [x] 1.1 在 `OrientJob::get_orient_mesh()` 新增 `printer_technology() == ptSLA` 頂層分支
- [x] 1.2 SLA 分支：先以 `obj->config.has("support_critical_angle")` 查詢 object-level override，有則以 `opt_float()` 讀取
- [x] 1.3 SLA 分支 fallback：以 `full_config().option("support_critical_angle")` null guard 後讀取 `opt_float()`；無 key 時維持 `overhang_angle` 預設值（30 度）
- [x] 1.4 FFF 分支：保留原有 object config override → full_config 的 `opt_int("support_threshold_angle")` 邏輯不變
- [x] 1.5 確認 SLA / FFF 路徑無多餘 degree↔radian 轉換

## 2. ArrangeJob — init_arrange_params SLA early-return（sla-arrange-job-compat）

- [x] 2.1 在 `init_arrange_params()` 取得 `settings` 後、`get_current_fff_print()` 前插入 `if (p->printer_technology() == ptSLA)` 分支
- [x] 2.2 SLA 分支設定 `params.printable_height = p->build_volume().printable_height()`
- [x] 2.3 SLA 分支設定 settings-based params：`allow_rotations`、`allow_multi_materials_on_same_plate`、`avoid_extrusion_cali_region`、`min_obj_distance`、`align_to_y_axis`
- [x] 2.4 SLA 分支明確設定 `params.is_seq_print = false`
- [x] 2.5 SLA 分支以 `return params` 提早離開，FFF 路徑後續程式碼不受影響
- [x] 2.6 確認 SLA 分支不呼叫 `get_current_fff_print()` 或任何 `PrintConfig` field access

## 3. ArrangeJob — prepare() FFF guard（sla-arrange-job-compat）

- [x] 3.1 將 `prepare()` 中的 `get_current_fff_print()`、`print.config()`、`setExtruderParams()`、`setPrintSpeedTable()` 包入 `if (m_plater->printer_technology() != ptSLA)` block
- [x] 3.2 確認 SLA 路徑下 `params = init_arrange_params(m_plater)` 仍正常執行（不在 guard 內）

## 4. ArrangeJob — process() scan_first_layer guard（sla-arrange-job-compat）

- [x] 4.1 在 `process()` 的 `opt_bool("scan_first_layer")` 前加入 `global_config.has("scan_first_layer") &&`
- [x] 4.2 確認 FFF 路徑行為不變（FFF full_config 包含此 key，`has()` 為 true，條件評估不受影響）

## 5. 編譯與 Runtime 驗證

- [x] 5.1 Build 修正版並確認不再出現由 `bounding_box().size().z()` 引起的 `C2338 OUT_OF_RANGE_ACCESS`（曾在中間版本觸發，已改為 `BuildVolume::printable_height()` 後消除）
- [x] 5.2 執行修正版程式並完成 SLA / FFF runtime regression checks：
  - SLA profile / Auto orient all：PASS（不 crash，功能正常）
  - SLA profile / Arrange all objects：PASS（不 crash，功能正常）
  - FFF profile / Auto orient all：PASS（原有行為不受影響）
  - FFF profile / Arrange all objects：PASS（原有行為不受影響）
