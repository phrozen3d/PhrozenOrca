## 0. 前置條件與實施順序

- 本 change **無硬前置**，可獨立進行第 1 節的契約定義
- 但 **1.8 應優先執行**：它只需 30 秒，卻會決定本 change 的**範圍**是否要縮小

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 契約定義（產品決策，須先完成才進入實作）

見 design.md D1。現況難以直接修，是因為「auto generate 應該是什麼樣的可還原操作」本身沒有定義。

- [ ] 1.1 列出全部與支撐點相關、目前有拍快照的操作及其時點：`"Autogenerate support points"`（`:2364`）、`"Support density change"`（`:1437`）、`"Add support point"`（`:879`）、`"Delete support point"`（`:1038`）、`"Move support point"`（`:2045`）、`"Remove all support points"`（`:1482`）、`"Support points edit"`（`:2155` / `:2237`）
- [ ] 1.2 逐項標註各快照拍在主 stack 還是 gizmo stack，以及拍下時 `mo->sla_support_points` 的內容
- [ ] 1.3 **關鍵事實量測**：寫入 `mo->sla_support_points` 是否必然中斷背景運算？測試在 `slaposSupportPoints` 完成後寫入相同內容，觀察 `SLAPrint::apply()` 的 `diff()` 是否判定為變更、已完成步驟是否失效
- [ ] 1.4 依 1.3 結果評估 design.md D2 的三個候選（D2-a 生成完成後寫入 / D2-b 離開 gizmo 時寫入 / D2-c 擴充 gizmo 序列化）
- [ ] 1.5 **決定語意**：S1 結果導向 / S2 設定導向 / S3 不可還原（見 design.md D1 對照表）。與需求方確認並記錄理由
- [ ] 1.6 決定密度／權重變更快照（`:1437`）與 auto generate 快照的關係——是否應合併為單一可還原操作
- [ ] 1.7 定義每個還原點的 `sla_points_status` 應為何值
- [ ] 1.8 **判別「手動模式下 undo 沒反應」屬於哪一種**（30 秒即可完成，**建議最先做**）：
      進手動模式 → 新增一個點 → Ctrl+Z（看似沒反應）→ **直接退出編輯模式**，觀察那個點還在不在
      → **點不在了**：undo 有跑，只是顯示沒刷新 → 屬既有候選 change `fix-sla-supports-active-undo-routing`
        （見 `sla-supports-apply-undo-stack` 的 [KB-4] 註記），本 change 的 Non-goals 維持現狀
      → **點還在**：undo 真的沒跑 → **必須納入本 change**，proposal 的 Non-goals 與 Impact 需一併修改
- [ ] 1.9 **產出書面契約**並回填至 design.md。未完成前不進入第 2 節

## 2. 快照時點修正

- [ ] 2.1 依契約修正 `auto_generate()`（`:2353`）的快照時點——現行「先拍快照、再非同步產生結果」使快照捕捉不到該操作的產物（見 design.md D3）
- [ ] 2.2 若採 S1：在 `get_data_from_backend()` 或等效時點補上結果快照
- [ ] 2.3 若採 S2：確認快照涵蓋密度／權重設定，且還原後會重新觸發生成
- [ ] 2.4 若採 S3：移除 `auto_generate()` 的快照，並依契約提供替代的「回到上一組點」途徑
- [ ] 2.5 確認 `Plater::TakeSnapshot` 的 RAII 生命週期與非同步結果抵達的時序不衝突

## 3. 持久化路徑

- [ ] 3.1 依 1.3 / 1.4 的結論實作 auto 點的持久化（或明確記錄不持久化的理由與替代機制）
- [ ] 3.2 驗證持久化動作不中斷背景運算、不造成切片步驟反覆失效重跑
- [ ] 3.3 **切斷隱含路徑**：`commit_manual_edits_keep_editing()`（`:2145`）目前會把當時 `m_normal_cache` 的內容（含 auto 點）寫進 `mo->sla_support_points`，使 auto 點搭手動 commit 的便車才第一次被持久化——這是案例 2 的成因。依契約明確定義手動 commit 時 `sla_support_points` 應包含哪些點（見 design.md D4）
- [ ] 3.4 `editing_mode_apply_changes()`（`:2228`）套用同一規則
- [ ] 3.5 確認 `reload_cache()` 依還原後的 `sla_points_status` 走向正確的資料來源

## 4. 驗收：案例 1（純自動模式）

- [ ] 4.1 密度 100% Apply → 密度 150% Apply → undo 一次：支撐點回到 100% 那組，**必須有可見變化**，不得是無反應的空操作
- [ ] 4.2 再 undo 一次：回到無支撐點狀態
- [ ] 4.3 redo 一次：回到 100% 那組；再 redo：回到 150% 那組
- [ ] 4.4 每個還原點的 `sla_points_status` 與實際資料一致，顯示與資料不脫節

## 5. 驗收：案例 2（自動 + 手動混合）

- [ ] 5.1 auto apply（N 點）→ 手動加 3 點 Apply → 再加 3 點 Apply → undo 一次：支撐點為 N + 3
- [ ] 5.2 再 undo 一次：支撐點為 N，**原本自動生成的 N 個點必須仍然存在**
- [ ] 5.3 再 undo 一次：回到無支撐點
- [ ] 5.4 全程 redo 可逐步還原，狀態與 undo 路徑對稱
- [ ] 5.5 確認 `sla-supports-apply-undo-stack` 既有的 Manual Apply requirement 在此混合流程中仍全部成立（兩個 capability 同時被觸及）

## 6. 驗收：穩定性與迴歸

- [ ] 6.1 連續 undo 至堆疊邊界不崩潰，邊界後為安全的無操作
- [ ] 6.2 undo 與 redo 交錯執行多次不崩潰，每一步狀態正確
- [ ] 6.3 Remove All 之後 undo 可還原
- [ ] 6.4 手動增／刪／移動單點的既有 undo 行為不退化
- [ ] 6.5 Points preview 與 picking 在每個還原點皆與資料一致
- [ ] 6.6 切片輸出與還原後的支撐點一致
- [ ] 6.7 更正既有 change `fix-sla-undo-redo` 的 Impact 段落——其「不影響範圍：GLGizmoSlaSupports（已正確實作）」的前提已被本 change 的發現推翻

## 7. Follow-up（out of scope）

- 作用中 gizmo 的 undo 路由與顯示刷新（編輯模式內 Ctrl+Z 的即時行為，即「手動模式下 undo 沒反應」）→ 既有候選 change `fix-sla-supports-active-undo-routing`（見 `sla-supports-apply-undo-stack` 的 [KB-4] 註記）
- Structure 模式的 undo/redo 顯示刷新 → 同上 capability 已標記 out of scope
- `sla_trafo` 變換後前端快取不失效 → `fix-sla-support-points-invalidate-on-trafo-change`
- 非編輯模式下手動點不套用 per-point 幾何 → `fix-sla-support-preview-stored-geometry-in-auto-mode`
- `cfg.set()` 對 `coEnum` 誤用 → `fix-sla-support-top-config-enum-set`
