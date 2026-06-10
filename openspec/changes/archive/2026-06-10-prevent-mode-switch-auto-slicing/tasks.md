## 1. Pre-implementation Verification

- [x] 1.1 確認 `phrozen_apply_work_mode` 內部 Preview 觸發路徑 — 靜態分析結論：觸發來自 `load_current_presets` → `Tab::load_current_preset()` → `SetSelection(GetPageCount()-1)` → `EVT_GLVIEWTOOLBAR_PREVIEW`；詳見 design.md。
- [x] 1.2 確認 `close_with_confirm(nullptr)` 的 null safety — 已驗證：Plater.cpp:12085 有 `if (second_check && ...)` 保護，傳入 nullptr 安全。
- [x] 1.3 確認 `new_project(skip_confirm=true, silent=true)` 不會觸發 tab 導航 — 已驗證：`silent=true` 跳過 `select_tab(tp3DEditor)` 呼叫；詳見 design.md Decision 1。
- [x] 1.4 確認 `SetSelection(tpHome)` 不觸發 EVT_GLVIEWTOOLBAR_PREVIEW — 已驗證：MainFrame.cpp:1311 handler 對 tpHome 不 post 該 event；詳見 design.md。
- [x] 1.5 Runtime 驗證：確認 `load_current_presets` 期間 `set_current_panel(preview, false)` 觸發次數為一次（而非多次），確保 one-shot `m_skip_preview_reslice` flag 足夠抑制 — 實測確認：所有 mode switch 情境（含連續 FDM→Resin→FDM）均無 spurious slicing，one-shot flag 足夠。

## 2. Core Fix in MainFrame.cpp

- [x] 2.1 移除 dead `saved_plater_tab` 變數 (MainFrame.cpp:1894-1896) — 已完成。
- [x] 2.2 在 `load_current_presets` 前新增 `m_plater->set_skip_preview_reslice(true)` — 已完成，保留為第一層 guard。
- [x] 2.3 在 `phrozen_apply_work_mode` 函式開頭（null checks 之後，preset target 查找之前）呼叫 `m_plater->close_with_confirm(nullptr)`，並在結果為 `wxID_CANCEL` 時 `return`。
- [x] 2.4 在 `load_current_presets` + `set_printer_technology` 之後呼叫 `m_plater->new_project(/*skip_confirm=*/true, /*silent=*/true)`，清空平台物件與 dirty state。
- [x] 2.5 將現有 `m_tabpanel->SetSelection(tp3DEditor)` 改為 `m_tabpanel->SetSelection(tpHome)`（或移除並在 `Layout()` 之後以正確位置新增）。
- [ ] 2.6 若 task 1.5 發現多次 `set_current_panel(preview, false)` 觸發：改採在 `Plater::priv` 新增 `m_mode_switch_in_progress` flag 並在 do_reslice lambda 中檢查的方案，而非依賴 one-shot flag — **N/A**：task 1.5 確認單次觸發，本項不需執行。

## 3. Verification: Spec Scenarios

- [x] 3.1 測試情境 — Cancel 對話框中止切換：有未保存變更，切換模式，對話框選 Cancel，確認模式未切換、平台物件不變、preset 不變。
- [x] 3.2 測試情境 — Save 後繼續切換：有未保存變更，切換模式，對話框選 Save，確認切換完成、平台已清空、導航至 Home。
- [x] 3.3 測試情境 — Don't Save 後繼續切換：有未保存變更，切換模式，對話框選 Don't Save，確認切換完成、平台已清空、導航至 Home。
- [x] 3.4 測試情境 — 無未保存變更時不顯示對話框：在乾淨狀態下切換模式，確認無對話框出現、切換完成、平台清空、導航至 Home。
- [x] 3.5 測試情境 — 有物件時切換後平台清空：放置模型，切換模式（任意方向），確認物件被移除、切片狀態重置。
- [x] 3.6 測試情境 — 從 Preview 頁面切換：先導航至 Preview，再切換模式，確認導航至 Home 且未觸發新的切片。
- [x] 3.7 測試情境 — 空平台時切換：無物件情況下切換模式，確認導航至 Home 且未觸發切片。
- [x] 3.8 測試情境 — Preset 與 UI 狀態更新：FDM→Resin 與 Resin→FDM 兩個方向切換後，確認 printer preset、process preset、sidebar 控制項、toolbar 按鈕與 filament/resin slot 數量皆正確更新為新技術。
- [x] 3.9 測試情境 — mode switch 後手動切片：切換後加入物件，呼叫 Slice 動作，確認 slicing 正常執行且 Preview 正常開啟。
- [x] 3.10 測試情境 — mode switch 後手動點擊 Preview tab：切換後加入物件，手動點擊 Preview tab，確認現有的 auto-slicing 邏輯如常運作。

## 4. Regression Checks

- [x] 4.1 確認 Ctrl+R（MainFrame.cpp:831-842）仍能正常切片並導航至 Preview — 第 839 行現有的 `set_skip_preview_reslice` 呼叫不得受影響。
- [x] 4.2 確認 sidebar Slice 按鈕（MainFrame.cpp:2008-2016）仍能正常切片並導航至 Preview。
- [x] 4.3 確認在任何時間點手動切換至 Preview tab（非透過 mode switch）仍會如預期觸發 auto-reslice。
- [ ] 4.4 確認 SLA sidebar collapse/expand 行為在 mode switch 後不受影響（Plater.cpp:6885-6890）— 本輪實測未覆蓋，保留未勾選。
- [x] 4.5 確認 `m_skip_preview_reslice` flag 在 `set_current_panel` 中消耗後正確重設為 false，不影響後續正常切片流程 — 實測確認：mode switch 後 Ctrl+R 及 Preview tab 均正常，flag 重設正確。
- [x] 4.6 確認 `new_project(true, true)` 呼叫後 `update_saved_preset_from_current_preset()` 的行為符合預期（不會將錯誤的 preset 值寫入 app_config）— 實測確認：FDM↔Resin 雙向切換後 preset/sidebar/toolbar 全部正確，無 preset 污染。

## 5. Cleanup & Documentation

- [x] 5.1 移除調查期間新增的任何 debug logging 或 trace — 調查為純靜態分析，無臨時 log 被加入，此項不適用。
- [x] 5.2 移除 `openspec/changes/prevent-mode-switch-auto-slicing/specs/mode-switch-prepare-state/` 舊目錄（已由 `mode-switch-session-reset/` 取代）。
- [x] 5.3 隨著每項 task 完成，更新本 tasks.md 的 checkbox 狀態。
