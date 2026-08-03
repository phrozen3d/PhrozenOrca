# Active changes — 相依關係與實施順序

`openspec list` 依修改時間排序，不表達相依關係；OpenSpec 本身也沒有原生的依賴機制。本檔為 **active change 之間先後順序的單一事實來源**。

各 change 的 `tasks.md` 第 0 節記載**自身**的前置條件（穩定的區域事實）；全域圖只維護在這裡，避免多份副本漂移。

新增 change 時請一併更新本檔。change archive 後從表中移除。

---

## 相依圖

```
fix-sla-support-preview-geometry-source-semantics   ← 硬前置已滿足（見下方註記）
fix-sla-support-points-invalidate-on-trafo-change   ← 建議前置已滿足（見下方註記）

fix-sla-support-points-undo-snapshot         ← 無前置（範圍待其 task 1.8 決定）
fix-sla-support-point-cone-picking           ← 前置已滿足，可立即進行
fix-sla-support-preview-visual-parity        ← 無前置，可立即進行
fix-sla-support-top-params-live-read-isolation ← 無前置，可立即進行
fix-sla-undo-redo                            ← 獨立（Hollow / Drill）
phrozen-build-variant-resin-split            ← 獨立，placeholder
```

## SLA 支撐點群組（有順序約束）

| 順序 | Change | 前置 | 說明 |
|---|---|---|---|
| 1 | `fix-sla-support-preview-geometry-source-semantics` | **硬**：`fix-sla-support-top-config-enum-set`（已於 2026-08-03 archive，硬前置已滿足） | crash 已修好，per-point 編輯路徑可正常測試 |
| 2 | `fix-sla-support-points-invalidate-on-trafo-change` | 無（建議前置已滿足） | 第 1 節為診斷階段 |
| 3 | `fix-sla-support-points-undo-snapshot` | 無 | 第 1 節為契約定義階段。**其 task 1.8（30 秒）會決定本 change 的範圍是否縮小**，建議最先執行該項 |

已完成：
- `fix-sla-support-top-config-enum-set`（選中支撐點編輯 Top 欄位並失焦即終止應用程式的 crash；2026-08-03 archive，45/45）
- `fix-sla-support-preview-stored-geometry-in-auto-mode`（非編輯模式下手動點不套用 per-point 幾何；2026-08-03 archive，23/23）——`fix-sla-support-preview-geometry-source-semantics` 與 `fix-sla-support-points-invalidate-on-trafo-change` 的建議前置皆已滿足

### 硬前置 vs 建議前置

- **硬前置**：未完成則後者無法進行或無法驗證。
- **建議前置**：反序可行，但會增加干擾變因或重工。

## 其他 active change（無順序約束）

| Change | 狀態 | 備註 |
|---|---|---|
| `fix-sla-support-point-cone-picking` | 前置已滿足 | 相依的 `perf-sla-support-points-preview-render` 已於 2026-07-29 archive，其 design D2 建立的擺放旋轉 `Rotation(q)` 正是本 change 所需（見其 design D5、tasks 2.1）。**可立即進行** |
| `fix-sla-support-preview-visual-parity` | 無前置 | manual 點與 auto 點在 preview 上的形狀／光影／顏色不對等（驗收 `fix-sla-support-preview-stored-geometry-in-auto-mode` 期間經 explore 討論發現）。與支撐點群組其他 change 獨立，只改渲染路徑不改幾何解析規則。**可立即進行** |
| `fix-sla-support-top-params-live-read-isolation` | 無前置 | 選定支撐點編輯 Top 參數時，其他 auto 點的 preview 外觀被連動污染（同一次 explore 討論發現）。根因是 live 讀值機制不知道 widget 目前被借用來顯示選中點的值。與 `fix-sla-support-preview-geometry-source-semantics` 是不同軸線（那邊決定「這顆點該用哪組參數」，這裡是「讀其他點的值時有沒有被污染」）。**可立即進行** |
| `fix-sla-undo-redo` | 接近完成 | 範圍為 GLGizmoHollow / GLGizmoDrill，與支撐點群組無交集。<br>⚠️ 其 proposal 與 design 記載「不修改 GLGizmoSlaSupports（已正確實作）」，該前提已被 `fix-sla-support-points-undo-snapshot` 推翻，需於後者實施時更正（見其 tasks 6.7） |
| `phrozen-build-variant-resin-split` | placeholder | 尚無 tasks，待專門的 exploration pass |

## 待決事項（阻擋對應 change 進入實作）

| Change | 待決 | 決定什麼 |
|---|---|---|
| `fix-sla-support-points-invalidate-on-trafo-change` | tasks 1.1、1.9~1.12 的診斷結果 | 症狀屬候選 A（快取未重載）、B（trafo 換算不對稱）或 C（兩者） |
| `fix-sla-support-points-undo-snapshot` | tasks 1.5 的語意決策 | S1 結果導向 / S2 設定導向 / S3 不可還原 |
| `fix-sla-support-points-undo-snapshot` | tasks 1.3 的量測 | 寫入 `mo->sla_support_points` 是否必然中斷背景運算，決定 S1 是否可行 |
| `fix-sla-support-preview-geometry-source-semantics` | tasks 1.4~1.7 | 選取是否應改變幾何來源、仲裁粒度是否改為逐欄位 |
