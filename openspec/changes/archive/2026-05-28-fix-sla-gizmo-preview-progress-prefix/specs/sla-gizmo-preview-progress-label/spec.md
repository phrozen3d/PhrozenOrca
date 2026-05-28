## 新增需求

### 需求：Gizmo preview 進度通知不附加「Slicing: 」前綴
當 Support、Hollow 或 Drill gizmo 觸發 partial reslice（preview 操作）時，右下角進度通知**不得**在 SLA phase label 前顯示「Slicing: 」前綴字串。顯示的文字應直接使用對應步驟的 `OBJ_STEP_LABELS()` 輸出（例如「Hollowing model」、「Generating support points」、「Drilling holes into model.」）或其翻譯，不加任何前綴。

#### 情境：Support gizmo Apply 觸發 partial reslice
- **當** 使用者觸發 Support gizmo 的操作（例如 Apply、自動生成，或顯示 Structure view）而啟動 partial reslice 時
- **則** 進度通知顯示支撐階段 label（例如「Generating support points」、「Generating support tree」、「Generating pad」），**不附加**「Slicing: 」前綴

#### 情境：Support gizmo preview 中 "Slicing model" 為合法 phase label
- **當** Support gizmo 的 partial reslice 執行至 `slaposObjectSlice`（物件切片）步驟時
- **則** 進度通知顯示「Slicing model」（不附加任何前綴），此為合法的原始 phase label，**不視為前綴誤顯**

#### 情境：Hollow gizmo Apply 觸發 partial reslice
- **當** 使用者觸發 Hollow gizmo Apply 操作而啟動 partial reslice 時
- **則** 進度通知顯示「Hollowing model」（或其翻譯），**不附加**「Slicing: 」前綴

#### 情境：Drill gizmo Apply 觸發 partial reslice
- **當** 使用者觸發 Drill gizmo Apply 操作而啟動 partial reslice 時
- **則** 進度通知顯示「Drilling holes into model.」（或其翻譯），**不附加**「Slicing: 」前綴

---

### 需求：Gizmo preview 成功完成後顯示功能別完成通知
當 Support、Hollow 或 Drill gizmo 的 partial reslice **成功完成**時，右下角進度通知**不得**顯示完整切片的完成訊息「Slice complete」（msgid: "Slice ok."）。通知**應**顯示對應功能的完成文字。

| Preview 類型 | 成功完成顯示文字 |
|---|---|
| Support gizmo | "Support complete" |
| Hollow gizmo | "Hollow/Drill complete" |
| Drill gizmo | "Hollow/Drill complete" |

#### 情境：Support preview 成功完成顯示 "Support complete"
- **當** Support gizmo 的 partial reslice 成功完成時
- **則** 進度通知顯示「Support complete」（或其翻譯），**不顯示**「Slice complete」

#### 情境：Hollow preview 成功完成顯示 "Hollow/Drill complete"
- **當** Hollow gizmo 的 partial reslice 成功完成時
- **則** 進度通知顯示「Hollow/Drill complete」（或其翻譯），**不顯示**「Slice complete」

#### 情境：Drill preview 成功完成顯示 "Hollow/Drill complete"
- **當** Drill gizmo 的 partial reslice 成功完成時
- **則** 進度通知顯示「Hollow/Drill complete」（或其翻譯），**不顯示**「Slice complete」

---

### 需求：Gizmo preview 取消後顯示功能別取消通知
當 Support、Hollow 或 Drill gizmo 的 partial reslice 被使用者**取消**時，右下角進度通知**不得**顯示完整切片的取消訊息「Slicing Canceled」。通知**應**顯示對應功能的取消文字。Preview 取消**不得**顯示任何 ... complete 成功文字。

| Preview 類型 | 取消顯示文字 |
|---|---|
| Support gizmo | "Support cancelled" |
| Hollow gizmo | "Hollow/Drill cancelled" |
| Drill gizmo | "Hollow/Drill cancelled" |

#### 情境：Support preview 取消顯示 "Support cancelled"
- **當** Support gizmo 的 partial reslice 在完成前被使用者取消時
- **則** 進度通知顯示「Support cancelled」（或其翻譯），**不顯示**「Slicing Canceled」，**不顯示**「Support complete」

#### 情境：Hollow preview 取消顯示 "Hollow/Drill cancelled"
- **當** Hollow gizmo 的 partial reslice 在完成前被使用者取消時
- **則** 進度通知顯示「Hollow/Drill cancelled」（或其翻譯），**不顯示**「Slicing Canceled」，**不顯示**「Hollow/Drill complete」

#### 情境：Drill preview 取消顯示 "Hollow/Drill cancelled"
- **當** Drill gizmo 的 partial reslice 在完成前被使用者取消時
- **則** 進度通知顯示「Hollow/Drill cancelled」（或其翻譯），**不顯示**「Slicing Canceled」，**不顯示**「Hollow/Drill complete」

---

### 需求：完整切片進度通知使用「Slicing: 」冒號格式前綴
當使用者透過 Slice toolbar 動作或 export 路徑執行完整切片時，右下角進度通知**應**在每個 phase label 前加上「Slicing: 」（含冒號與空格）作為前綴。

「Slicing: Slicing model」與「Slicing: Slicing supports」為此格式下的合法預期結果，不視為錯誤。

#### 情境：完整切片每個 phase 顯示 "Slicing: " 前綴
- **當** 使用者啟動完整切片（例如按 Slice All）時
- **則** 進度通知在每個 phase label 前顯示「Slicing: 」前綴（例如「Slicing: Hollowing model」、「Slicing: Generating support points」）

#### 情境：完整切片 "Slicing model" phase 顯示 "Slicing: Slicing model"
- **當** 完整切片執行至 `slaposObjectSlice`（"Slicing model"）步驟時
- **則** 進度通知顯示「Slicing: Slicing model.」（含冒號格式）

#### 情境：完整切片成功完成後顯示「Slice complete」通知
- **當** 使用者啟動的完整切片成功完成時
- **則** 進度通知顯示「Slice complete」（msgid: "Slice ok."）完成訊息，與現有行為一致

#### 情境：完整切片取消後顯示「Slicing Canceled」通知
- **當** 使用者啟動的完整切片被取消時
- **則** 進度通知顯示「Slicing Canceled」（或其翻譯），與現有行為一致

---

### 需求：Gizmo preview 後的完整切片不受 preview 狀態影響
任何 gizmo preview partial reslice 完成、取消或失敗後，後續的完整切片**應**正確顯示「Slicing: 」前綴，並在完成後顯示「Slice complete」，取消後顯示「Slicing Canceled」。Preview 狀態**不得**殘留到下一次完整切片執行中。

#### 情境：成功的 gizmo preview 後執行完整切片
- **當** 使用者完成任何 gizmo preview（成功完成）後隨即啟動完整切片時
- **則** 完整切片的進度通知正確顯示「Slicing: 」前綴；完成後顯示「Slice complete」

#### 情境：取消的 gizmo preview 後執行完整切片
- **當** gizmo preview partial reslice 被取消或失敗後，使用者啟動完整切片時
- **則** 完整切片的進度通知正確顯示「Slicing: 」前綴；完成後顯示「Slice complete」；取消後顯示「Slicing Canceled」

#### 情境：多次 gizmo preview 後執行完整切片
- **當** 使用者連續執行兩次以上 gizmo preview 操作後啟動完整切片時
- **則** 完整切片的進度通知正確顯示「Slicing: 」前綴；完成後顯示「Slice complete」

---

### 需求：Support gizmo preview 從第一個可見進度事件起不短暫顯示前綴
**（待 diagnostic 確認 — 見 Task 10.1）**

當 Support gizmo 觸發 partial reslice 時，從第一個可見的進度事件起，進度通知**不得**短暫顯示「Slicing: 」前綴後再切換為無前綴的 phase label。

#### 情境：Support gizmo preview 無起始前綴閃現
- **當** 使用者觸發 Support gizmo 操作而啟動 partial reslice 時
- **則** 進度通知從第一個可見的 phase label 起即不附加「Slicing: 」前綴，**不得**出現短暫閃現前綴的情況
- **注意**：此 scenario 的實作策略視 Task 10.1 diagnostic 結果而定