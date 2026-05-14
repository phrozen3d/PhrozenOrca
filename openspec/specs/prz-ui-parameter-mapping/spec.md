# Spec: prz-ui-parameter-mapping

## Purpose

Establish and maintain a living reference document (`.resin-devLog/PRZ_Parameter_Mapping.md`) that maps every SLA UI config key to its corresponding PRZ binary format field, records health status and known sync issues, and provides prioritized expansion recommendations for fields not yet exposed in the UI.

## Requirements

### Requirement: 建立 PRZ 參數映射文件於 .resin-devLog/

實作者 SHALL 建立或更新 `.resin-devLog/PRZ_Parameter_Mapping.md`，記錄所有 SLA UI 開放欄位與 PRZ 格式欄位之間的對應關係、健康度審查結果，以及未來擴充建議清單。

**文件路徑**：`.resin-devLog/PRZ_Parameter_Mapping.md`
**文件格式**：Markdown，以四個固定 Section 組織

#### Scenario: 文件存在於正確路徑

- **WHEN** 實作完成後
- **THEN** `.resin-devLog/PRZ_Parameter_Mapping.md` 存在於 repository 根目錄下的 `.resin-devLog/` 資料夾中

---

### Requirement: Section 1 — 健康對應欄位清單

文件 Section 1 SHALL 以 Markdown 表格列出「UI 有且 PRZ 有、對應正確無斷層」的所有欄位，每列必須包含以下欄位：

| 欄位名稱 | 型別 |
|---|---|
| UI Config Key | `display_width` 等 key 名稱 |
| UI 控件位置 | 對話框名稱 + 欄位標籤 |
| PRZ Header 欄位名稱 | 如 `PlatformYLength` |
| PRZ 資料型別 | 如 `float BE 4B` |
| 映射方式 | `direct` / `portrait-swap` / `derived` |
| 健康狀態 | `✅ 正常` |

#### Scenario: 平台尺寸欄位出現在 Section 1

- **WHEN** 映射文件完成後
- **THEN** `display_width` → `PlatformYLength`（portrait-swap）與 `display_height` → `PlatformXLength`（portrait-swap）出現在 Section 1 的表格中，健康狀態標記為 `✅ 正常`

#### Scenario: 像素解析度欄位出現在 Section 1

- **WHEN** 映射文件完成後
- **THEN** `display_pixels_x` → `YResolution` 與 `display_pixels_y` → `XResolution` 的 portrait-swap 對應出現在 Section 1，並附有說明其交換意圖的備注

---

### Requirement: Section 2 — 風險或斷層欄位清單

文件 Section 2 SHALL 以 Markdown 表格列出「對應存在但有潛在同步問題、型別風險或語意不明」的欄位，每列除 Section 1 基礎欄外，還需包含「風險說明」欄（自由文字）。若本次盤點後無任何風險欄位，SHALL 明確標記「（本次盤點無發現）」而非留空。

#### Scenario: 已修復的斷層欄位被記錄於 Section 2 或備注

- **WHEN** 映射文件完成後
- **THEN** `display_width`/`display_height` 修復前的同步斷層歷史（`sync_local_to_tab()` 未寫回）被記錄為已解決事項，或在 Section 1 對應列的備注欄中附上「曾有斷層，已於 fix-prz-platform-xy-mapping 修復」

---

### Requirement: Section 3 — PRZ 支援但 UI 未開放的欄位清單

文件 Section 3 SHALL 以 Markdown 表格列出「PRZ binary format 有定義，但 `SLAPrinterSettingsDialog` 或任何 SLA UI 目前未提供使用者設定入口」的所有欄位，每列必須包含：

| 欄位名稱 | 說明 |
|---|---|
| PRZ Header 欄位名稱 | 如 `BottomLayerCount` |
| PRZ 資料型別 | 如 `int16 BE 2B` |
| 語意說明 | 欄位的用途與對列印的影響 |
| 建議 UI Config Key | 若未來新增至 UI，建議使用的 key 名稱 |
| 擴充優先級 | `高` / `中` / `低` |
| 備注 | 相依性、限制或已知問題 |

#### Scenario: Section 3 至少列出五個 PRZ 欄位

- **WHEN** 映射文件完成後
- **THEN** Section 3 的表格至少包含五個 PRZ 欄位（如 BottomLayerCount、BottomExposureTime、LiftingSpeed、RetractSpeed、AntiAliasing 等），每列均有優先級標記

#### Scenario: 高優先級欄位有擴充建議

- **WHEN** Section 3 中存在優先級為「高」的欄位
- **THEN** 該欄位的「建議 UI Config Key」與「備注」欄均為非空，且備注說明為何此欄位對使用者具有高優先擴充價值

---

### Requirement: Section 4 — UI 開放但 PRZ 無對應的欄位清單

文件 Section 4 SHALL 記錄「SLA UI 上有，但 PRZ binary format 無對應欄位」的參數，以簡易表格列出 UI Config Key、UI 控件位置、說明與現狀（如：僅影響其他格式、已廢棄、僅供顯示用）。若本次盤點後無此類欄位，SHALL 明確標記「（本次盤點無發現）」。

#### Scenario: Section 4 包含完整結論

- **WHEN** 映射文件完成後
- **THEN** Section 4 存在且有明確內容（表格或「無發現」說明），不得為空白 Section

---

### Requirement: 映射文件須標記審查日期與版本基準

文件頂部 SHALL 包含一個 metadata 區塊，記錄：
- **審查日期**：文件建立或最後更新的日期
- **PRZ 版本基準**：本次盤點所對應的 PRZ format 版本（如 `V3.0`）
- **UI 版本基準**：對應的 `SLAPrinterSettingsDialog` 所在的 git commit hash 或 branch name

#### Scenario: Metadata 區塊存在且完整

- **WHEN** 映射文件完成後
- **THEN** 文件第一個 Section 或頂部 frontmatter 包含審查日期、PRZ 版本與 UI 版本基準三項資訊
