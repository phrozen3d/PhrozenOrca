## Why

SLA / resin profile 下，上方 toolbar 的 **Auto orient all** 與 **Arrange all objects** 兩項功能按下後會造成程式 crash，使 SLA 使用者完全無法使用這兩項工具。兩個 Job class 均包含只適用於 FFF 的執行假設，但直接根因不同：**OrientJob** 在 SLA 模式下讀取不存在於 SLA config 的 FDM-only option（`support_threshold_angle`），`opt_int()` 對 nullptr dereference；**ArrangeJob** 在 SLA 模式下進入需要有效 FFF `Print` 物件的初始化路徑（`get_current_fff_print()`），SLA 下無有效物件可供使用，後續 dereference 未初始化指標而 crash。

## What Changes

- **OrientJob**：`get_orient_mesh()` 新增 SLA 分支，改讀語意等效的 SLA option `support_critical_angle`（`coFloat`，degrees）；FFF 分支維持讀取 `support_threshold_angle`（`coInt`，degrees）不變。
- **ArrangeJob**：
  - `init_arrange_params()` 新增 SLA early-return，使用 `BuildVolume::printable_height()` 提供 printable height，settings-based params 正常讀取，完全跳過 `get_current_fff_print()`。
  - `prepare()` 的 FFF print config 存取（`get_current_fff_print()`、`setExtruderParams`、`setPrintSpeedTable`）包入 `printer_technology() != ptSLA` guard。
  - `process()` 的 `scan_first_layer` 讀取前加 `has()` existence guard，防止 SLA 可達路徑對 FDM-only key 進行 `opt_bool()` 呼叫。

## Capabilities

### New Capabilities

- `sla-orient-job-config-safety`：OrientJob 根據 printer technology 選擇正確的 overhang angle config option，SLA 使用 `support_critical_angle`，FFF 使用 `support_threshold_angle`，消除 SLA auto orient crash。
- `sla-arrange-job-compat`：ArrangeJob 在 SLA 模式下跳過所有 FFF print config 存取，以 build volume printable height 替代 FFF print config 的 printable height，消除 SLA arrange crash。

### Modified Capabilities

（無現有 spec 層行為變更）

## Impact

- **修改檔案**：
  - `src/slic3r/GUI/Jobs/OrientJob.cpp` — `OrientJob::get_orient_mesh()`
  - `src/slic3r/GUI/Jobs/ArrangeJob.cpp` — `init_arrange_params()`、`ArrangeJob::prepare()`、`ArrangeJob::process()`
- **不影響範圍**：FFF orient / arrange 行為、`get_instance_arrange_poly()`、`Model::setExtruderParams()`（已有 guard）、`prepare_wipe_tower()`（已有 guard）、Plater、GLCanvas3D toolbar 層、UndoRedo 系統
- **風險**：極低。所有改動為 additive（新增分支或 guard），FFF 原有程式碼路徑完全不受影響。
