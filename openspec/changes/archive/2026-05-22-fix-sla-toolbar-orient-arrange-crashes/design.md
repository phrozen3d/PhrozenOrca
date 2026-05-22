## Context

OrcaSlicer 的 toolbar Job 系統（`OrientJob`、`ArrangeJob`）在 BambuStudio 時期針對 FFF 開發，本專案加入 SLA / resin 模式後，這兩個 Job class 未做 printer technology 分支處理。直接根因各異：

- **OrientJob**：`get_orient_mesh()` 在 SLA 模式下讀取 FDM-only option `support_threshold_angle`；此 key 不存在於 SLA `full_config()`，`option()` 回傳 nullptr，後續 `opt_int()` 對 nullptr dereference → crash。
- **ArrangeJob**：`init_arrange_params()` 與 `prepare()` 呼叫 `get_current_fff_print()`，`PartPlate::get_print()` 只在 `printer_technology == ptFFF` 時設定 `Print*` 指標；SLA 下該指標保持未初始化，後續 `print.config()` dereference → crash。

**現有 config 分層**：
- FDM options（`PrintConfig`）：`support_threshold_angle`（coInt）、`scan_first_layer`（coBool）等
- SLA options（`SLAPrintConfig` via `PrintConfig` with SLA prefix）：`support_critical_angle`（coFloat）等
- `full_config()` 在 SLA 模式下為 SLA preset merged config，不包含 FDM-only options

**OrientMesh 單位**：`overhang_angle` 儲存 degrees；`Orient.cpp` 內部轉換為 radians（`cos(PI - angle * PI / 180)`）。`support_threshold_angle`（int degrees）與 `support_critical_angle`（float degrees）均為 degrees，可直接賦值。

**既有 safe pattern**：`EmbossJob`、`get_instance_arrange_poly()`、`prepare_wipe_tower()` 等已示範 `printer_technology() == ptSLA` 分支或 `config.has()` guard 的正確用法。

## Goals / Non-Goals

**Goals:**
- 消除 SLA 模式下 Auto orient all 與 Arrange all objects 的 crash
- SLA auto orient 使用語意正確的 `support_critical_angle` 作為 overhang angle 來源
- SLA arrange 以 build volume printable height 作為物件高度上限
- FFF 所有原有行為完全不受影響

**Non-Goals:**
- 優化 SLA arrange 的排列結果（clearance、wipe tower 等 FFF 功能於 SLA 無意義，保持預設 0 即可）
- 全面稽核或重構 `process()` 後段所有 helper 的 SLA 相容性（全面稽核不在本次範圍；本次僅處理已確認 crash 路徑及修正後直接可達的 FDM-only option 風險）
- 修改 `get_current_fff_print()` API 本身的語意或回傳行為

## Decisions

### D1：以 printer_technology() 作為分支判斷，不以 config key existence 作為主要分支

**選擇**：`wxGetApp().plater()->printer_technology() == ptSLA`

**理由**：意圖明確，與 `EmbossJob`（同目錄）、`Plater` 等既有慣例一致；config key existence（`config.has()`）僅作防禦性 guard，不作主分支邏輯。若以 key existence 為主分支，當 key 因版本遷移從 SLA config 移除時行為會靜默降級而不是明確分支，不利維護。

**替代方案**：`config.option(...) != nullptr` 判斷 → 否決，邏輯耦合度高，且 full_config() 在 SLA 下的 key 集合非顯而易見。

### D2：SLA arrange 的 printable_height 使用 BuildVolume::printable_height()

**選擇**：`p->build_volume().printable_height()`

**理由**：`BuildVolume` 由 `PrintConfig::printable_area` 與 `PrintConfig::printable_height` 初始化，代表實際可列印高度，與 FFF `print_config.printable_height.value` 語意等價。此 API 在同檔案 `bed_stride_x/y()` 已使用，無需新增 include。

**替代方案**：hardcode 大數（如 999）→ 否決，語意不正確；`SLAPrintConfig` 讀取 → 複雜度過高，`BuildVolume` 已封裝此資訊。

### D3：SLA arrange 的 FFF clearance params（nozzle、rod、lid、radius）保持預設 0

**選擇**：不設定，依 `ArrangeParams` default-constructed 值（0）

**理由**：SLA 列印無擠出頭，clearance 參數無意義。`arrangement::arrange()` 使用這些值計算擠出頭迴避半徑，0 表示無額外迴避需求，是正確語意。

### D4：scan_first_layer 改以 has() guard 而非 printer_technology() guard

**選擇**：`global_config.has("scan_first_layer") && global_config.opt_bool(...)`

**理由**：`has()` guard 直接防止 dereference，且對未來增加此 key 的 config 類型自動相容；若改用 `printer_technology()` 需維護兩個判斷點，且與 is_bbl 的複合條件邏輯更複雜。

## Risks / Trade-offs

**[Risk] SLA arrange 的 wipe tower / multi-material 路徑未完全驗證**
→ `prepare_wipe_tower()` 已有 `enable_prime_tower` option guard，SLA 下會 early return。目前已驗證不 crash，功能正確性以 runtime 測試為準。

**[Risk] process() 後段（update_arrange_params 等）在 SLA 下的 config 存取未深入確認**
→ 這些函式使用 `m_plater->config()`（merged config），其 SLA 兼容性未在本次修正中完整確認。若出現後續 crash，可作為獨立 follow-up 處理；本次修正已移除最直接的 crash 路徑。

**[Risk] support_critical_angle 不存在於 obj->config（per-object override）**
→ SLA support_critical_angle 為 preset-level 設定，obj->config.has() 永遠回傳 false；已 fallback 到 full_config()，行為正確。obj->config 的 guard 為防禦性，不影響正常路徑。

## Migration Plan

不需要 migration。修改均為 additive（新增分支），不改變現有 FFF 行為，不影響 project file 格式或 config schema。
