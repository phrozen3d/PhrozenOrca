## 新增需求

### 需求：SupportPoint 儲存三值類型分類
`SupportPoint` struct 應以 `SupportPointType` 列舉欄位（`manual_add`、`island`、`slope`）取代 `bool is_new_island` 欄位，預設值應為 `manual_add`。

#### 情境：使用者手動放置點的預設類型
- **當** 使用者在 SLA 支撐 gizmo 中點擊放置支撐點時
- **則** 產生的 `SupportPoint.type` 應為 `SupportPointType::manual_add`

#### 情境：自動生成時為 island 指派類型
- **當** 自動生成演算法處理 `prev_parts.empty()` 的 `LayerPart` 時
- **則** 產生的 `SupportPoint.type` 應為 `SupportPointType::island`

#### 情境：自動生成時為 slope 指派類型
- **當** 自動生成演算法處理連接部件上的懸臂或半島時
- **則** 產生的 `SupportPoint.type` 應為 `SupportPointType::slope`

---

### 需求：保留 Island 謂詞
`SupportPoint` 應提供一個 `is_island()` const 方法，當 `type == SupportPointType::island` 時回傳 `true`，供鎖定和 UI 邏輯使用。

#### 情境：對 island 類型點呼叫 island 檢查
- **當** 對 `type == island` 的點呼叫 `is_island()` 時
- **則** 應回傳 `true`

#### 情境：對非 island 點呼叫 island 檢查
- **當** 對 `type == manual_add` 或 `type == slope` 的點呼叫 `is_island()` 時
- **則** 應回傳 `false`

---

### 需求：編輯模式中的三色渲染
在 `GLGizmoSlaSupports` 編輯模式中，每個支撐點應依其類型以對應顏色渲染。

#### 情境：手動點顏色
- **當** `type == manual_add` 的支撐點在編輯模式中渲染時
- **則** 應使用手動顏色（與 island 和 slope 顏色不同）

#### 情境：Island 點顏色
- **當** `type == island` 的支撐點在編輯模式中渲染時
- **則** 應使用 island 顏色（與手動和 slope 顏色不同）

#### 情境：Slope 點顏色
- **當** `type == slope` 的支撐點在編輯模式中渲染時
- **則** 應使用 slope/非活躍顏色

---

### 需求：Gizmo 面板顯示各類型統計
SLA 支撐 gizmo 面板應顯示一行統計資訊，包含總點數、手動點數和 island 點數。

#### 情境：混合類型點的統計
- **當** 支撐 gizmo 面板渲染且存在支撐點時
- **則** 應顯示格式為 `"N(M manual) support points (I on islands)"` 的字串，其中 N=總數、M=manual_add 數、I=island 數

#### 情境：無手動點時的統計
- **當** 所有支撐點皆為自動生成（無 `manual_add`）時
- **則** 應顯示 `"N support points (I on islands)"`

---

### 需求：拖曳將自動生成點升格為 manual_add
當使用者拖曳自動生成的支撐點時，其類型應升格為 `manual_add`。

#### 情境：拖曳 island 點
- **當** 使用者拖曳 `type == island` 的支撐點時
- **則** 拖曳後 `type` 應為 `manual_add`

#### 情境：拖曳 slope 點
- **當** 使用者拖曳 `type == slope` 的支撐點時
- **則** 拖曳後 `type` 應為 `manual_add`

---

### 需求：Lock Islands 行為使用 is_island() 謂詞
`lock_unique_islands` 功能應防止拖曳和刪除 `is_island()` 回傳 `true` 的點。

#### 情境：Island 點被鎖定
- **當** 啟用 `lock_unique_islands` 且使用者嘗試拖曳 `type == island` 的點時
- **則** 拖曳應被阻止（點不移動）

#### 情境：手動點不被鎖定
- **當** 啟用 `lock_unique_islands` 且使用者拖曳 `type == manual_add` 的點時
- **則** 拖曳應正常執行

---

### 需求：3mf/bbs_3mf/AMF 向後相容序列化
檔案格式應使用與 PrusaSlicer 2.9.1+ 相容的範圍型浮點數編碼來編碼 `SupportPointType`，並應能正確載入舊版格式寫入的檔案。

#### 情境：載入含 is_new_island=0.0 的舊檔案
- **當** 載入 3mf 檔案，其中支撐點第五個浮點數為 `0.0` 時
- **則** 該點應載入為 `SupportPointType::slope`

#### 情境：載入含 is_new_island=1.0 的舊檔案
- **當** 載入 3mf 檔案，其中第五個浮點數位於 `0.9999–1.0001` 範圍時
- **則** 該點應載入為 `SupportPointType::island`

#### 情境：載入含 manual_add 的新檔案
- **當** 載入 3mf 檔案，其中第五個浮點數位於 `1.9999–2.0001` 範圍時
- **則** 該點應載入為 `SupportPointType::manual_add`

#### 情境：載入含 slope 的新檔案
- **當** 載入 3mf 檔案，其中第五個浮點數位於 `2.9999–3.0001` 範圍時
- **則** 該點應載入為 `SupportPointType::slope`

#### 情境：儲存並重新載入的來回測試
- **當** 含混合類型支撐點的模型儲存至 3mf 並重新載入時
- **則** 所有點的類型應完整保留
