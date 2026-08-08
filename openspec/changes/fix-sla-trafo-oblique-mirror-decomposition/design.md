## Context

> 本文件記錄診斷階段已確認的根因，供暫緩期間保存證據、恢復處理時使用。修正方案（Decisions 以外的部分）尚未設計，待恢復處理時依當時的優先度與範圍重新展開。

### 資料流與原始懷疑（診斷起點）

支撐點資料在三個地方各有一份，生命週期不同：

| 位置 | 座標系 | 誰寫入 | 何時失效 |
|---|---|---|---|
| `SLAPrintObject::get_support_points()` | `sla_trafo` 空間 | backend（`slaposSupportPoints`） | `SLAPrint::apply()` 偵測 `sla_trafo_differs` → `invalidate_all_steps()` |
| `mo->sla_support_points` | raw object space | 手動 commit 路徑 | 隨 undo/redo 快照 |
| `GLGizmoSlaSupports::m_normal_cache` | raw object space | `reload_cache()` / `get_data_from_backend()` | 僅在 `mo->id()` 改變時 |

一開始懷疑根因在第三列（前端快取失效時機沒對齊引擎層），或 `get_data_from_backend()` 的 `po->trafo().inverse()` 換算與渲染端的完整 instance transform 不對稱。診斷過程詳見下方 D1，最終確認**根因不在這兩處，而在 `sla_trafo()` 本身的矩陣分解/重組邏輯**。

## Goals / Non-Goals

**Goals（本文件範圍）：**

- 把「支撐點被拉長/位置錯亂」定位到單一、精確的機制，並記錄完整推導過程。
- 評估這個根因的影響範圍是否超出 preview（是否影響實際切片輸出）。

**Non-Goals（暫緩期間）：**

- 不在本文件內設計 `sla_trafo()` 的具體修正方案——核心引擎程式碼的修正需要獨立评估，不在暫緩期間展開。
- 不處理原範圍的「前端快取失效時機對齊 `sla_trafo`」——這件事本身沒有錯，只是不是本症狀的根因，是否仍需要獨立處理待恢復時重新評估。
- 不處理 Test 4 發現的 Structure 模式底板歪斜是否為同一根因——待恢復處理時一併確認。

## Decisions

### D1. 診斷過程：從「前端快取/換算不對稱」到「`sla_trafo()` 本身分解錯誤」

**候選 A（前端快取根本沒重載）排除**：四組實測（見 proposal.md）都觀察到「離開再進 gizmo，點消失，重新 Apply 後重新產生」——這正是 `SLAPrint::apply()` 偵測 `sla_trafo_differs` → `invalidate_all_steps()` 該有的行為，前端快取確實有跟著重載，不是逐位元不變。真正異常出現在重新產生**之後**的點本身不對。

**候選 B 的原始假設（`get_data_from_backend()` 換算與渲染端不對稱）未被推翻，但不是完整根因**：即使兩端換算完全對稱，只要雙方共用的 `sla_trafo()` 矩陣本身算錯，換算結果依然是錯的——問題的層級比「兩端對不對稱」更底層。

**追加測試鎖定精確條件**：一開始懷疑是「旋轉軸與鏡射軸的特定組合」在作祟，但控制變數測試（同一 X 軸旋轉 80° 基準下，比較「Y 鏡射一次」vs.「X+Y 鏡射兩次」）排除了這個假設，確認規律**與旋轉軸、鏡射軸皆無關，只取決於「是否同時存在 `is_left_handed()`（鏡射奇數次）與非軸對齊旋轉」**。完整測試記錄見 proposal.md。

**程式碼審查鎖定精確機制**：`SLAPrint::sla_trafo()`（`SLAPrint.cpp:235-269`）呼叫 `model_instance.get_rotation()` / `get_scaling_factor()` / `get_mirror()` 分別分解同一個 instance transform 矩陣，再依固定順序（`translate → scale(corr) → rotateZ → rotateY → rotateX → scale(factor) → scale(mirror)`）重新組裝。

這三個 `get_*()`（`Geometry.cpp:471-562`）都呼叫 Eigen 的 `computeRotationScaling()`——極分解（polar decomposition），把矩陣拆成「純旋轉（正交、行列式 +1）」乘上「一個對稱矩陣（scale 部分）」。**這個對稱 scale 矩陣只有在旋轉軸對齊（90° 整數倍）時才會剛好落在對角線上**；任意角度旋轉時，scale 矩陣會產生非對角線的 skew 項，但 `get_mirror()` / `get_scaling_factor()` 只讀對角線三個元素（`Geometry.cpp:513-517`、`549-552`），把它們當成 X/Y/Z 各軸獨立的縮放/鏡射直接使用，**完全忽略非對角線的 skew**。`sla_trafo()` 再用這三個（可能已經因忽略 skew 而失真的）分解結果重新組裝矩陣——重組出來的矩陣因此可能跟原始 instance transform 不一致。

**佐證**：這個 codebase 本身就有 `has_skew()` / `contains_skew()`（`Geometry.cpp:444-469`）專門偵測「`computeRotationScaling()` 的 scale 分量是否非對角線」這個情況——代表這個陷阱是已知、有被考慮過的，只是 `sla_trafo()` 的分解-重組邏輯沒有呼叫這兩個函式做檢查，直接假設分解結果乾淨可用。

**與實測完全吻合**：
- 任意角度旋轉 + 奇數次鏡射（`is_left_handed()` true）→ 產生 skew，分解結果失真 → 壞
- 任意角度旋轉 + 不鏡射，或鏡射偶數次（`is_left_handed()` false，det > 0）→ 純旋轉（或無鏡射）的 scale 分解恆為單位矩陣，不會 skew → 正常
- 「全部變成 island」判定為下游效應：法向量因錯誤矩陣算歪後，overhang 角度判定連帶誤判，不是獨立根因

### D2. 暫緩，不在本分支處理

已與使用者確認（討論記錄於本 change 的對話歷程）：`sla_trafo()` 是核心引擎程式碼（`SLAPrint.cpp`），不屬於 `fix-sla-support-point-issues` 分支「support point GUI 問題」的範圍；且其影響可能延伸到實際切片輸出（見 proposal.md「影響範圍評估」），修正需要的測試範圍（含切片輸出比對）與風險層級都比本分支其餘 GUI 修正大，不適合混在同一批修正裡。

決定暫緩，待 `fix-sla-support-point-issues` 分支合併回 `resin-dev` 主分支後，另外評估處理時機。原分支相依圖中的 6.12a/6.12b/6.12c（`fix-sla-support-point-cone-picking` 承接的 picking 一致性驗收）因為依賴「支撐點不再變形」，也一併隨本 change 暫緩，待本 change 恢復處理並修好後再回頭補測。

## Risks / Trade-offs

- **[暫緩期間問題持續存在，使用者可能持續遇到]** → 已記錄完整重現步驟與根因，不會遺失；使用者已知悉此為暫緩狀態。

- **[`sla_trafo()` 的修正如果做錯，可能影響所有 SLA 物件的切片，不只是本症狀情境]** → 這正是暫緩、需要獨立分支與更完整測試的理由，不在此時倉促修正。

- **[影響範圍評估（是否真的影響切片輸出）尚未執行，優先度可能被低估或高估]** → proposal.md 已明列這是恢復處理時的第一步，非本文件現在要解決的事。

## Migration Plan

暫緩，無執行中的修正。恢復處理時需重新規劃 Migration Plan。

## Open Questions

- `sla_trafo()` 的具體修正策略：改用 `has_skew()` 偵測後採取不同分解方式？或改為直接對原始矩陣做代數操作（移除 Z 旋轉與 XY 平移分量），避免分解-重組的往返？兩者的權衡待恢復處理時評估。
- 「影響範圍評估」（是否影響實際切片輸出）的結果，決定本 change 恢復處理時的優先度與是否需要提升到比一般 GUI 修正更高的等級。
- Test 4 發現的 Structure 模式底板歪斜，是否為同一根因、或需要另外處理。
- 原範圍「前端快取失效時機對齊 `sla_trafo`」是否仍需要獨立處理，或整併進 `sla_trafo()` 修正後一併考慮。
