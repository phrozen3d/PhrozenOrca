## Context

`SLAPrinterSettingsDialog` 的 Size X/Y 欄位讀取自 `printable_area` 的 bounding box（`reload_from_preset()` 路徑），但寫回時（`sync_local_to_tab()`）只更新 `printable_area`，從未寫入 `display_width` / `display_height`。PRZ 匯出器（`prz_header()`）與 rasterization pipeline（`SLAPrintSteps.cpp` lines 1393–1421）直接讀取後兩者，導致使用者的修改對匯出結果完全無效。

**已知的 portrait 交換規則**（刻意設計，不得更動）：
- `PlatformXLength` ← `display_height`（長邊 mm）
- `PlatformYLength` ← `display_width`（短邊 mm）
- `XResolution` ← `display_pixels_y`（長邊像素）
- `YResolution` ← `display_pixels_x`（短邊像素）

此交換使 cv::Mat 的 width/height 與平台 X/Y 方向一致，是 PhrozenOrca resin 輸出的基礎假設。

**既有 JSON profile 狀態**：所有 Phrozen 機器 profile 中，`display_width`/`display_height` 已與 `printable_area` 一致（e.g., Mighty Revo 16K: 211×118），故修復後不影響未修改設定的使用者行為。

## Goals / Non-Goals

**Goals:**
- 修復 `sync_local_to_tab()` 中 `display_width`/`display_height` 寫回遺漏，使三個相關 config key（`printable_area`、`display_width`、`display_height`）在使用者修改 Size X/Y 後保持一致
- 產出一份持久性的 UI↔PRZ 參數映射文件（Markdown），全面記錄健康度審查結果與未來擴充建議
- 確保 rasterization center 計算與 SL1 匯出同樣受益（無需額外修改，因其讀取同一 config key）

**Non-Goals:**
- 不修改 `prz_header()` 的讀取邏輯或 portrait 交換規則
- 不修改 config 繼承機制（`resolve_profile_float_chain()` 等）
- 不將 PRZ 未覆蓋參數立即新增至 UI（僅文件化，未來由獨立 change 處理）
- 不修改 SL1 匯出或 rasterization pipeline（間接受益即可）
- 不觸及 `resetField` / `reset_to_default` 的觸發時機（已正確呼叫 `sync_local_to_tab(true)`）

## Decisions

### D1：修復點選在 `sync_local_to_tab()`，而非讀取端

**決策**：在 `SLAPrinterSettingsDialog::sync_local_to_tab()` 補上兩行：
```cpp
cfg.set_key_value("display_width",  new ConfigOptionFloat(size_x));
cfg.set_key_value("display_height", new ConfigOptionFloat(size_y));
```
位置：緊接現有 `set_key_value("printable_area", ...)` 之後。

**考慮過的替代方案**：
- **在 `prz_header()` 改讀 `printable_area`**：需同步修改 `SLAPrintSteps.cpp` 與 `SL1.cpp`，改動面廣，且 portrait 交換邏輯需要重寫。
- **在 `generate_prz()` 呼叫前從 `printable_area` 衍生值**：仍需三處修改，且破壞「單一真實來源」原則。
- **在 `PhrozenOrca.cpp` 修復層補回衍生邏輯**：該段程式碼已被有意移除（lines 2968-2972 現為 comment），重新引入會製造兩個寫入點。

**選擇理由**：單點修復（兩行），受益面覆蓋 PRZ、SL1、rasterization 三條下游路徑；符合「config key 在 UI 層寫回後即正確，下游只讀」的 Orca 設計慣例。

---

### D2：映射文件格式與路徑

**決策**：將 UI↔PRZ 完整參數映射審查與未來擴充建議統一寫入：
```
.resin-devLog/PRZ_Parameter_Mapping.md
```

**文件結構**（Markdown 分段）：
1. **Section 1：健康對應欄位**（UI 有且 PRZ 有，對應正確）— Markdown 表格，欄位為：UI key、PRZ 欄位名稱、方向、型別、健康狀態
2. **Section 2：風險/斷層欄位**（對應存在但有潛在問題）— 同格式，附風險說明欄
3. **Section 3：PRZ 支援但 UI 未開放的欄位**（擴充建議清單）— 表格含：PRZ 欄位名稱、資料型別、建議對應 UI key、優先級（高/中/低）、備注
4. **Section 4：UI 開放但 PRZ 無對應的欄位**（僅供記錄，低優先級）

**選擇 `.resin-devLog/` 路徑的理由**：
- 同目錄已存放 `SLA_PRZ_Format_Export.md` 等 PRZ 技術文件，保持知識集中
- 屬於開發內部文件，不應進入 `openspec/`（後者為流程規格）或 `resources/`（後者為 runtime 資源）
- Markdown 格式版本控制友善，日後可直接在 PR 中審閱

**替代方案考慮**：
- 放在 `openspec/changes/fix-prz-platform-xy-mapping/` 下：生命週期與 change 掛鉤，change 歸檔後知識消失，不適合作為長期參考文件
- 放在 `docs/`：目錄不存在，需新建

---

### D3：精度保證

`size_x` / `size_y` 的來源為 `m_size_x_ctrl` / `m_size_y_ctrl`（`wxTextCtrl` → `double`），寫入 `ConfigOptionFloat`（內部 `double`）與 `ConfigOptionPoints → Vec2d`（內部 `double`），全鏈路 64-bit，無 downcast 風險。

---

### D4：`sync_local_to_tab()` 的 `size_x`/`size_y` 語意確認

在 `reload_from_preset()` 中，`m_size_x_ctrl` 讀自 `printable_area` bounding box 的 width（即平台 X mm），`m_size_y_ctrl` 讀自 height（平台 Y mm）。portrait 交換只發生在 PRZ 讀取端，UI 層不執行任何交換，因此：
- `display_width` ← `size_x`（平台 X mm，短邊）
- `display_height` ← `size_y`（平台 Y mm，長邊）

這與既有 JSON profile 中 `display_width < display_height` 的值域一致（e.g., Mighty Revo 16K: width=211, height=118 → 橫向機器；portrait 交換後 XLength=118 long side、YLength=211 short side？）

> **注意**：Mighty Revo 16K 的 `display_width=211, display_height=118`，但這台機器是 **橫向（landscape）** LCD。Portrait 交換後 `PlatformXLength=display_height=118`（短邊），`PlatformYLength=display_width=211`（長邊）。這個對應關係在 `.resin-devLog/SLA_PRZ_Format_Export.md` 中有明確記載，本 change 不更動此邏輯。

## Risks / Trade-offs

**[風險 1] 使用者未透過對話框修改 Size X/Y 的路徑仍有斷層**
→ 例如：直接編輯 JSON profile、透過 config bundle 匯入、或 preset 繼承覆蓋。這些路徑不經過 `sync_local_to_tab()`，無法受益於本修復。
→ **緩解**：這些路徑本就應在 JSON profile 中保持 `display_width`/`display_height` 與 `printable_area` 一致；既有所有 Phrozen profile 均已對齊，風險限於使用者手動編輯的非標準情境。

**[風險 2] 未來新增 Size X/Y 相關 UI 控件若不經過 `sync_local_to_tab()`**
→ 斷層會重現。
→ **緩解**：`sla-printer-dim-sync` spec 明確定義「三個 key 必須同步更新」為 invariant，code review 應審查任何新增的 Size X/Y 寫回路徑。

**[風險 3] 映射文件與程式碼不同步**
→ `.resin-devLog/PRZ_Parameter_Mapping.md` 為靜態文件，未來 PRZ 格式擴充或 UI 新增欄位時若未更新，文件將過時。
→ **緩解**：在 `tasks.md` 中加入「更新映射文件」為未來 PRZ-related change 的標準 checklist 項目。

## Migration Plan

無需資料遷移或版本升級。

- 既有 `.prz` 檔案不受影響（匯出時重新計算）
- 既有 JSON profile 的 `display_width`/`display_height` 已正確，修復後行為不變
- 無需 config schema version bump

## Open Questions

- **Q1（已確認，記錄備查）**：`display_width`=211 / `display_height`=118 對 Mighty Revo 16K（landscape LCD）搭配 portrait swap 後，`PlatformXLength=118`（短邊）/ `PlatformYLength=211`（長邊）是否符合 Phrozen firmware 的預期？根據 `.resin-devLog/SLA_PRZ_Format_Export.md` 的記載，此交換為刻意設計，確認正確。
- **Q2（待 mapping 文件完成後回答）**：`prz-ui-parameter-mapping` 盤點是否會發現其他類似 `display_width`/`display_height` 的斷層欄位？如有，需另立 change 處理。
