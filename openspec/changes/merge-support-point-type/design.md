## 背景

PhrozenOrca 的 `SupportPoint` struct 儲存了一個 `bool is_new_island` 欄位。這只是 PrusaSlicer 三值 `SupportPointType` 列舉的子集（`manual_add`、`island`、`slope`）。此 bool 只能區分 island 與非 island，無法區分使用者手動放置的點與自動生成的 slope 點。

3mf/bbs_3mf/AMF 格式將此欄位編碼為浮點數（`0.0` = 非 island，`1.0` = island）。PrusaSlicer 2.9.1 延伸了此編碼：`1.x` = island（不變），`2.x` = manual_add，`3.x` = slope。只含 `0.0`/`1.0` 的舊檔案必須仍可正確載入。

此變更不得影響 `SupportTreeBuildsteps`——該模組完全不使用 `type` 欄位。

## 目標 / 非目標

**目標：**
- 將 `SupportPoint` 中的 `bool is_new_island` 替換為 `SupportPointType type`
- 完整向後相容現有 3mf 檔案
- 在 `GLGizmoSlaSupports` 編輯模式中以三種不同顏色渲染
- 在支撐 gizmo 面板顯示各類型統計資訊
- 拖曳自動生成的點後升格為 `manual_add`
- 透過 `is_island()` 謂詞保留 `lock_unique_islands` 行為

**非目標：**
- 修改支撐樹建構邏輯（`SupportTreeBuildsteps`）
- 新增 `SupportWeight`（輕/中/重）——此為後續獨立變更
- 修改 FDM 支撐程式碼
- 修改任何 PhrozenOrca 特有的自訂功能

## 設計決策

### D1：列舉值與對應關係

使用 PrusaSlicer 的完整列舉定義：
```cpp
enum class SupportPointType {
    manual_add,   // 0 — 使用者手動放置或拖曳升格
    island,       // 1 — 自動生成於 prev_parts.empty() 區域
    slope,        // 2 — 自動生成於懸臂/半島區域
};
```

保留 `SupportPoint` 上的 `is_island()` 輔助方法，供鎖定邏輯使用。

**考慮過的替代方案**：保留 `bool is_new_island` 並新增第二個欄位。已拒絕——會增加儲存空間並造成混淆；直接替換更簡潔。

### D2：檔案格式遷移策略

完全遵循 PrusaSlicer 2.9.1 的編碼方式（來自 3mf.hpp 的註解）：
```
浮點數範圍          → SupportPointType
0.9999–1.0001   → island      （與舊格式相同）
1.9999–2.0001   → manual_add  （2.9.1 新增）
2.9999–3.0001   → slope       （2.9.1 新增）
其他所有值        → slope       （舊 0.0 值的回退處理）
```

舊檔案中儲存的 `0.0`（非 island，可能是 slope 或手動）載入後視為 `slope`。這有輕微的語意損失（舊手動點變成 slope），但可接受——使用者下次編輯時可以拖曳來重新升格為 `manual_add`。

**考慮過的替代方案**：版本升級格式並以整數儲存。已拒絕——會導致舊版 PrusaSlicer/OrcaSlicer 無法載入。浮點數範圍編碼維持相容性。

### D3：新 SupportPoint 的預設值

預設值為 `SupportPointType::manual_add`，與 PrusaSlicer 一致。使用者在 `GLGizmoSlaSupports` 中點擊放置的點自動取得 `manual_add`。生成器會明確覆寫為 `island` 或 `slope`。

### D4：顏色方案

遵循 PrusaSlicer 的三色方案：
- `manual_add` → 醒目亮色（如青色/白色）—— 使用者自行管理
- `island` → 警示色（如橘色/黃色）—— 自動生成的關鍵區域
- `slope` → 中性色（如灰色）—— 自動生成的一般區域

確切顏色值需符合 PhrozenOrca 現有的顏色慣例。

## 風險 / 取捨

- **舊檔案載入的語意損失**：舊的非 island 點（可能是使用者手動放置的）載入後視為 `slope`。→ 可接受的取捨；使用者可透過拖曳重新升格。應在版本說明中記錄。
- **cereal 序列化**：`serialize()` 方法使用 cereal archive。將 `bool` 改為 `enum class` 會改變記憶體中 undo/redo 快照的二進位格式。→ 由於這些快照僅在工作階段中存在（不持久化），不需要遷移。升級後開啟中的工作階段將失效，此為正常行為。
- **PhrozenOrca SupportIslands 中缺少 SupportPointType**：PhrozenOrca 的 SupportIsland 程式碼可能引用 `is_new_island`。實作前必須進行審查。→ 已納入任務清單。

## 遷移計畫

1. 實作 struct 變更 + 序列化遷移
2. 確認舊 3mf 檔案可正確載入（以已知含 island 的模型進行視覺測試）
3. 確認新 3mf 檔案可正確儲存並以正確類型重新載入
4. 無部署疑慮——桌面應用程式，隨新版本發布
