## 背景

`GLGizmoLcdOverhangDetection` 的「Detect selected」按鈕觸發 `Plater::reslice_SLA_until_step(slaposObjectSlice, *mo, false)`。在前一個 change（`2026-05-28-fix-sla-gizmo-preview-progress-prefix`）中，`reslice_SLA_until_step()` 的 step 分類邏輯只覆蓋 `slaposSupportPoints`/`slaposPad`（→ `Support`）與 `slaposDrillHoles`（→ `HollowOrDrill`）；`slaposObjectSlice` 落入 `else` fallback，被歸類為 `HollowOrDrill`。

```
slaposObjectSlice  →  else  →  HollowOrDrill  ← 錯誤
```

此外，`HollowOrDrill` preview 不覆蓋 `evt.status.text`，因此 pipeline 實際執行的 `slaposHollowing`（"Hollowing model"）等 step label 會直接顯示，誤導使用者。

## 設計決策

### 決策一：新增 `OverhangDetect` enum 值

擴展 `SlaGizmoPreviewType` enum：

```cpp
enum class SlaGizmoPreviewType {
    None,
    Support,
    HollowOrDrill,
    OverhangDetect   // 新增：Overhang Detection gizmo partial reslice
};
```

### 決策二：明確分類 `slaposObjectSlice`

在 `reslice_SLA_until_step()` 加入顯式分支，不依賴 `else` fallback：

```cpp
else if (step == slaposObjectSlice)
    this->p->m_sla_gizmo_preview_type = SlaGizmoPreviewType::OverhangDetect;
```

`else` fallback 保留為 `HollowOrDrill`，作為未知 step 的安全預設。

目前整個 repo 中，傳入 `slaposObjectSlice` 的呼叫唯一來源為 `GLGizmoLcdOverhangDetection.cpp:588`，已由精準 grep 確認。

### 決策三：運作中文字完整 override

`HollowOrDrill` 與 `Support` preview 不蓋寫 step label，顯示 pipeline 原始 label（如 "Hollowing model"、"Generating support points"）。Overhang Detection 的 SLA pipeline 同樣會先執行 `slaposHollowing`、`slaposDrillHoles`，若照搬此邏輯，使用者仍會看到 "Hollowing model" 等無關文字。

因此改為在 `on_slicing_update()` 中，當 preview type 為 `OverhangDetect` 時，完整覆蓋 `evt.status.text` 為固定字串 `"Overhang Detecting"`，不拼接任何 step label：

```cpp
} else if (m_sla_gizmo_preview_type == SlaGizmoPreviewType::OverhangDetect) {
    evt.status.text = _u8L("Overhang Detecting");
}
```

此路徑僅在 `OverhangDetect` 時生效，不影響 `Support` / `HollowOrDrill` / `None` 的現有邏輯。

### 決策四：翻譯字串

新增三條 msgid，其他語系補空 msgstr（依既有 change 慣例）：

| msgid | zh_TW msgstr |
|---|---|
| `"Overhang Detecting"` | `"懸空偵測中"` |
| `"Overhang detection complete"` | `"懸空偵測完成"` |
| `"Overhang detection cancelled"` | `"懸空偵測已取消"` |

## 非目標

- 修改 `SLAPrintSteps.cpp` 的 step label 文字。
- 修改 `HollowOrDrill` 或 `Support` 的現有覆蓋邏輯。
- 修改 `SlicingProgressNotification` / `NotificationManager`（取消文字路徑不需變更，複用既有機制即可）。
