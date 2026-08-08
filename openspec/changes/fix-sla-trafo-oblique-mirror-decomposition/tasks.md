## 0. 狀態

**暫緩，不在 `fix-sla-support-point-issues` 分支的處理範圍內。** 診斷階段已完成並確認根因，見 proposal.md 與 design.md D1。恢復處理時機：`fix-sla-support-point-issues` 分支合併回 `resin-dev` 主分支後另外評估。

> 全域相依圖見 [`openspec/changes/README.md`](../README.md)——本 change 已從該分支的相依圖與待辦清單移除，僅留存於 `openspec/changes/` 供之後撿回。

## 1. 診斷（已完成）

- [x] 1.1 建立可重現案例（任意角度旋轉 → auto generate apply → 鏡射 → 重新 apply），記錄四項觀察（拉長、位置偏移、法向量歪斜、全變 island 同時發生）
- [x] 1.2 追加不同旋轉軸、不同鏡射軸組合比對，確認症狀與旋轉/鏡射的具體軸無關
- [x] 1.3（部分）純旋轉不鏡射（測試 3）確認無症狀；Z 軸 90° 旋轉 + XY 平移的乾淨對照組未執行，恢復處理時可補做但非阻塞（`sla_trafo()` 本身排除這兩者，理論上不受影響）
- [x] 1.4 候選 A（前端快取沒重載）已由行為觀察排除：點確實有重新產生，不是逐位元不變
- [x] 1.5 候選 B 判別：程式碼審查確認 `sla_trafo()` 的分解-重組邏輯是根因，非單純的兩端換算不對稱（見 design.md D1）
- [x] 1.6 已審查左手座標路徑（`is_left_handed()` 觸發的額外 `Scaling(-1,1,1)`）——確認這不是唯一問題，`get_rotation()`/`get_scaling_factor()`/`get_mirror()` 的極分解在非軸對齊旋轉下產生 skew 才是根本原因
- [x] 1.7 `relative_correction()` 非本症狀根因，未進一步深究（skew 問題發生在更早的分解步驟）
- [x] 1.8 已由行為觀察間接確認 `SLAPrint::apply()` 正確走進 `invalidate_all_steps()` 分支（點消失、需要重新 Apply，符合預期）
- [ ] 1.9 gizmo 開著 vs 關著時變換——未測試，非阻塞
- [x] 1.10 不會自我恢復，確認是持久性錯誤而非時序問題
- [ ] 1.11 先鏡像再生成——未測試，非阻塞
- [x] 1.12 複合旋轉 + 非均勻縮放（不鏡射）——大致正常，但發現 Structure 模式底板歪斜的新問題（見 proposal.md 測試 4 備註，待判斷是否同一根因）
- [x] 1.13 **產出結論**：根因為 `sla_trafo()`（`SLAPrint.cpp:235-269`）的矩陣分解-重組邏輯，在非軸對齊旋轉 + 奇數次鏡射（`is_left_handed()` true）時，因 Eigen `computeRotationScaling()` 產生的 skew 未被檢測（`has_skew()` 存在但未被呼叫），導致 `get_rotation()`/`get_scaling_factor()`/`get_mirror()` 分解結果失真，重組出的矩陣與原始 instance transform 不一致。完整推導見 design.md D1

## 2. 恢復處理前的第一步（尚未執行）

- [ ] 2.1 **嚴重度評估**：對「任意角度旋轉 + 鏡射」的物件實際切片一次，比對切出來的支撐樹是否與預期吻合，確認本問題是否影響實際列印輸出，或僅為 preview 顯示問題
- [ ] 2.2 依 2.1 結果決定優先度與處理方式（是否需要獨立、更高優先度的處理流程）

## 3. 修正階段（暫緩，待恢復處理時展開）

`sla_trafo()` 的具體修正方案、原範圍「前端快取失效時機對齊」是否併入、Test 4 的底板歪斜是否同一根因——皆待恢復處理時依當時決定的範圍重新規劃 tasks，不在此預先展開。

## 4. Follow-up（承接自其他 change，隨本 change 暫緩）

- `fix-sla-support-point-cone-picking` 的 6.12a/6.12b/6.12c（均勻/非均勻 scale、鏡像 instance 下 picking 一致性）依賴「支撐點不再變形」，隨本 change 一併暫緩，待本 change 恢復處理並修好後補測
- 支撐點 undo/redo 資料不正確 → `fix-sla-support-points-undo-snapshot`
