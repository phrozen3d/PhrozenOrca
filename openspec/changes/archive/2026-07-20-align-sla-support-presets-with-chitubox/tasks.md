## 1. C++ preset struct 擴充（GLGizmoSlaSupports.cpp）

- [x] 1.1 於 `struct SupportWeightPreset`（約 36–43 行）尾端新增兩欄位 `float head_penetration;`、`float segment_length;`
- [x] 1.2 **即時驗證**：僅此步先建置 slicer（`build_release_vs2022.bat slicer`），確認新增欄位後、初始化列表尚未補齊時的編譯狀態（預期會因聚合初始化缺值而可編過但補 0）；用以確立基準，避免與後續改動混淆（使用者手動編譯通過）

## 2. 更新三檔 CHITUBOX 數值（GLGizmoSlaSupports.cpp）

- [x] 2.1 更新 `k_weight_presets[3]`（約 45–49 行）三列為 8 欄完整值：
  - Light  `{ 0.8, 0.3, 0.5, 2.0, 0.5, 0.5, 0.3, 2.0 }`
  - Middle `{ 1.2, 0.4, 0.8, 3.0, 1.0, 1.0, 0.4, 2.0 }`
  - Heavy  `{ 1.5, 0.6, 1.0, 4.0, 1.5, 1.5, 0.6, 3.0 }`
- [x] 2.2 目視檢查：三列各 8 個值、欄位順序對應 struct（pillar, head_front, contact, base_dia, base_h, head_width, head_penetration, segment_length），無漏值（防「靜默補 0」）
- [x] 2.3 **即時驗證**：建置 slicer，確認編譯通過（使用者手動編譯通過）

## 3. apply_weight_preset 寫入新參數（GLGizmoSlaSupports.cpp）

- [x] 3.1 於 `apply_weight_preset`（約 1846–1852 行）既有 `cfg.set(...)` 區塊補上兩行：
  - `cfg.set("support_head_penetration", (double)p.head_penetration, true);`
  - `cfg.set("support_segment_length",   (double)p.segment_length,   true);`
- [x] 3.2 確認 `support_head_back_diameter` 仍維持 `= p.pillar_diameter`（不新增欄位；CHITUBOX 較低直徑=柱徑）
- [x] 3.3 **即時驗證**：建置 slicer；啟動 App → resin 模式 → 支撐點 Gizmo，逐一按 Light/Middle/Heavy，於 Support 分頁核對六項值符合 spec 數值表（特別確認接觸深度與段長度已隨檔變動）（使用者手動編譯通過、UI 核對正確）

## 4. Profile JSON 對齊 Middle（sla_print_common + 子 process）

- [x] 4.1 `resources/profiles/PhrozenSLA/process/sla_print_common.json`：`support_pillar_diameter` `1→1.2`、`support_segment_length` `3→2`、`support_head_back_diameter` `1→1.2`
- [x] 4.2 6 個子 process（`Speed Plus - Black@…`×3、`Tough ABS-like+@…`×3）：各自 `support_pillar_diameter` `"1"→"1.2"`
- [x] 4.3 **即時驗證**：`git diff resources/profiles/PhrozenSLA` 檢視改動集中在預期 key；用 JSON 解析（如 `python -m json.tool`）確認 7 個檔案語法有效

## 5. Vendor 版號 patch bump（PhrozenSLA.json）

- [x] 5.1 `resources/profiles/PhrozenSLA.json`：`version` `01.00.05→01.00.06`（**只動 patch，維持 `01.00.0X` 格式**，勿升 minor/major）
- [x] 5.2 **即時驗證**：確認 `PresetUpdater` 更新條件（maj.min 相同且 installed<bundled）成立；JSON 語法有效

## 6. 端對端整合驗證

- [x] 6.1 全新設定檔啟動（模擬新安裝）：載入 Phrozen SLA 系統預設、開啟 Gizmo，確認 **Middle radio 高亮**、Support 分頁柱徑 1.2 / 段長度 2 / 較低直徑 1.2（使用者確認通過）
- [x] 6.2 切換 Light→Heavy，確認 ConfigManipulation 驗證規則不彈警告（接觸深度 ≤ 段長度、上端直徑 ≤ 較低直徑 均成立）（使用者確認通過）
- [x] 6.3 放置一個新支撐點後切片，確認支撐幾何依新段長度/柱徑生成、無崩潰（使用者確認通過）
- [x] 6.4 回歸檢查：`support_head_width` 未被納入 CHITUBOX 調整；base_diameter/base_height 仍隨檔變動（非目標，符合預期）（使用者確認通過）

## 7. OpenSpec 收尾

- [x] 7.1 `openspec validate align-sla-support-presets-with-chitubox` 通過
- [x] 7.2 確認實作與 design.md 決策一致（D1–D5）後，交由後續 `/opsx:archive` 流程處理