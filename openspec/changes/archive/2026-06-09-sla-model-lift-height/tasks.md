## 1. Config 與 UI 暴露

- [x] 1.1 在 `PrintConfig.cpp` 為 `support_object_elevation` 設定 label `Model Lift Height`、category Support、指定 tooltip 與 sidetext `mm`
- [x] 1.2 在 `TabSLAPrint` Raft Setting 區塊，於 `pad_wall_thickness` 上方加入 `support_object_elevation` 欄位
- [x] 1.3 在 `ConfigManipulation::toggle_print_sla_options()` 於 `pad_enable && pad_around_object` 時隱藏/禁用 Model Lift Height 欄位

## 2. Gizmo-aware Z Shift 機制

- [x] 2.1 在 `SelectionInfo` 新增 `set_use_config_elevation(bool)` 與內部 flag；`on_update()` 在 flag 為 true 時從 edited SLA process config 讀取 `support_object_elevation`（經 `is_zero_elevation` 守衛）
- [x] 2.2 在 `SelectionInfo::on_release()` 與 gizmo 關閉路徑重置 flag，避免殘留抬升
- [x] 2.3 確認 raycast / unproject / clipper 等使用 `get_sla_shift()` 的路徑在 gizmo 開啟時取得正確值

## 3. SLA Support Point Gizmo 生命週期

- [x] 3.1 在 `GLGizmoSlaSupports::on_set_state()` On 分支呼叫 `set_use_config_elevation(true)` 並觸發 canvas force update
- [x] 3.2 在 gizmo 實際關閉（Off 且未因 unsaved dialog 攔截）呼叫 `set_use_config_elevation(false)` 並觸發 canvas force update
- [x] 3.3 在 `GLGizmoSlaBase::update_volumes()` 確認 gizmo 內 volume 的 `set_sla_shift_z()` 與 `SelectionInfo` 一致

## 4. 參數變更即時預覽

- [x] 4.1 當 `support_object_elevation` 在 gizmo 開啟期間變更時，觸發 `SelectionInfo` 更新與 3D 視圖重繪

## 5. 驗證

- [x] 5.1 手動驗證：進入 SLA Support Point → 模型依 Model Lift Height 抬升 → 退出後貼地
- [x] 5.2 手動驗證：gizmo 開啟時調整 Model Lift Height，預覽即時更新
- [x] 5.3 手動驗證：`pad_around_object` 啟用時欄位隱藏且 elevation 為 0
- [x] 5.4 手動驗證：切片後支撐間隙與 Model Lift Height 設定一致
