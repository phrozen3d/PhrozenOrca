## Context

支撐點資料在系統中有三份，只有一份會進 undo 快照：

| 位置 | 進 undo 快照？ | 誰寫入 |
|---|---|---|
| `mo->sla_support_points` | **是**（`ModelObject` 的一部分） | 手動 commit 路徑 |
| `SLAPrintObject::get_support_points()` | 否（backend 狀態） | `slaposSupportPoints` |
| `m_normal_cache` | 部分（gizmo `on_save` / `on_load` 序列化，僅在 gizmo stack 內有效） | `reload_cache()` / `get_data_from_backend()` |

自動生成的點只存在於第二、三列。`get_data_from_backend()` 的註解說明了為何不寫進第一列：

> `// We don't copy the data into ModelObject, as this would stop the background processing.`

寫入 `ModelObject` 會使 `SLAPrint::apply()` 偵測到模型變更並中斷正在進行的背景運算——這是一個真實的技術約束，不是疏忽。

於是形成當前的斷裂：`auto_generate()` 拍了快照（宣告「這是一個可還原的操作」），但該操作真正產生的資料從不進入快照涵蓋的範圍。快照拍下的其實只有「按下 Apply 之前的模型狀態」，而還原到那個狀態的效果是「支撐點全空」。

案例 2 更揭露了一個複合失效：手動 commit 會把當時 `m_normal_cache` 的內容（含 auto 點）寫進 `mo->sla_support_points`，等於**auto 點是搭著手動 commit 的便車才第一次被持久化**。undo 越過該次 commit 就把它們一起帶走了。

## Goals / Non-Goals

**Goals:**

- 定義並文件化支撐點相關操作的 undo 契約，作為實作與驗收的共同依據。
- undo 一次 = 回到前一個「使用者認知中的支撐點狀態」，而非回到某個內部中間狀態。
- undo 不得抹除非該次操作所產生的支撐點。
- 修正過程不得中斷背景運算（尊重 `get_data_from_backend()` 註解所述的約束）。

**Non-Goals:**

- 不處理作用中 gizmo 的 undo 路由與顯示刷新（`sla-supports-apply-undo-stack` 已 out-of-scope，指向 `fix-sla-supports-active-undo-routing`）。
- 不改變 Manual Apply 已定義的快照行為。
- 不改變 backend 生成演算法或 `SLAPrint::apply()` 的失效判定。
- 不改變 Points preview 的渲染路徑。

## Decisions

### D1. 先定義契約，再選實作——這是產品決策而非技術偏好

現況之所以難以直接修，是因為「auto generate 應該是什麼樣的可還原操作」本身沒有定義。三種合理的產品語意會導向不同的實作：

| 語意 | undo 一次的效果 | 實作方向 |
|---|---|---|
| **S1 結果導向** | 回到前一次 Apply 產出的那組點 | auto 點必須持久化，快照在結果產生後拍 |
| **S2 設定導向** | 回到前一次的密度／權重設定並重跑生成 | 快照記錄設定值，還原後重新觸發生成 |
| **S3 不可還原** | auto generate 不進 undo | 移除該快照，但需另提供「回到上一組點」的途徑 |

**S1 最符合使用者在案例 1 中的預期**（「undo 一次應該回到 100% 那組點」），也最符合一般 undo 的心智模型。但它需要解決持久化與背景運算的衝突（見 D2）。

S2 的還原是非同步的，undo 之後畫面要等重算才更新，體驗不一致；且若生成演算法有隨機性，還原結果未必與原本相同。

S3 與現況最接近（現況等於「壞掉的 S3」），成本最低，但使用者已明確表達期待 undo 有作用。

**本 change 的第一階段就是把這個決定做出來並記錄。** tasks 第 1 節在得出結論前不進入實作。

### D2. 若採 S1：持久化時機必須避開背景運算

`get_data_from_backend()` 不寫回 `ModelObject` 的理由是會中斷背景運算。若採 S1，需要一個「背景運算已完成、寫入不會造成中斷」的時點。

候選：

- **D2-a：在 `slaposSupportPoints` 完成後寫入。** 此時該步驟已 done，寫入 `sla_support_points` 會使 `SLAPrint::apply()` 認為模型變更——需確認是否真的會使已完成的步驟失效，或因內容相同而被 `diff()` 判定為無變化。**這是必須先量測的關鍵事實**，決定 S1 是否可行。
- **D2-b：只在使用者離開 gizmo / 切換 gizmo 時寫入。** 避開運算中的時窗，但快照時點會與使用者按 Apply 的時點脫節，undo 的粒度變得不直覺。
- **D2-c：不寫入 `ModelObject`，改為擴充 gizmo 的 `on_save` / `on_load` 使 `m_normal_cache` 在主 stack 上也能還原。** 目前 `on_load` / `on_save`（`:2062` 附近）確實有序列化 `m_normal_cache`，但那只在 gizmo stack 內有效。此路徑需釐清 gizmo stack 與主 stack 的快照範圍差異。

**為何 D2-a 是首選但需先驗證**：它讓資料真正成為模型的一部分，undo/redo 自然正確，且與 `UserModified` 路徑一致。但若寫入必然中斷運算，S1 就得改走 D2-b 或 D2-c。

### D3. 快照時點：拍在狀態改變之後，而非之前

無論採哪個語意，`auto_generate()` 現行「先拍快照、再非同步產生結果」的順序都是錯的——快照捕捉不到該操作的產物。

`Plater::TakeSnapshot` 的慣例是「在改變發生前拍下舊狀態」，對同步操作正確；對非同步操作則需要在結果抵達時補拍，或改用其他機制。這是本缺陷的結構性成因，實作時必須正面處理，不能只調整資料流。

### D4. 手動 commit 不得成為 auto 點的持久化途徑

即使 auto 點最終仍不持久化（S2 / S3），也必須切斷「手動 commit 順便把 auto 點寫進去」這條隱含路徑造成的副作用——它使 undo 的影響範圍超出該次手動操作，正是案例 2 的成因。

若採 S1，auto 點在生成時就已持久化，此問題自然消失。若採 S2 / S3，則需明確定義手動 commit 時 `mo->sla_support_points` 應包含哪些點。

### D5. 與既有 capability 的邊界

`sla-supports-apply-undo-stack` 已規範 Manual Apply 的快照必須進主 stack、多次 Apply 各自可獨立還原、以及 crash-safety。本 change **不修改**其任何 requirement，只補上它未涵蓋的 auto 生成情境。

驗收時兩者必須同時成立——特別是案例 2 這種混合流程，會同時觸及兩個 capability。

## Risks / Trade-offs

- **[未定契約就實作，改完仍不符預期]** → 本 change 最主要的風險。緩解：D1 明列三種語意與取捨，tasks 第 1 節為獨立的決策階段，須產出書面結論才進入實作。

- **[持久化 auto 點中斷背景運算]** → 可能造成切片流程反覆重跑，比原缺陷更嚴重。緩解：D2-a 列為必須先量測的事實；若證實會中斷，改走 D2-b 或 D2-c。

- **[修改快照時點引入 crash]** → `sla-supports-apply-undo-stack` 已記錄過 undo/redo 相關的 crash 史（見 archive 的 `fix-sla-gizmo-undo-redo-crash`、`fix-sla-supports-apply-undo-clear`）。緩解：驗收必須涵蓋反覆 undo/redo 至堆疊邊界。

- **[與作用中 gizmo undo 路由的症狀混淆]** → 「手動模式下 undo 沒反應」屬另一個 out-of-scope 範圍，驗收時容易與本 change 的症狀混為一談。緩解：驗收步驟明確指定在何種模式下操作，並與 `fix-sla-supports-active-undo-routing` 的範圍劃清。

- **[還原後 `sla_points_status` 不一致]** → 該欄位決定 `reload_cache()` 走 backend 或 `sla_support_points`，還原成錯誤的值會導致顯示與資料脫節。緩解：契約必須明確定義每個還原點的 `sla_points_status`。

## Migration Plan

無資料遷移。若採 S1 並改變 `sla_support_points` 的寫入時機，既有專案檔的讀取行為不變（欄位語意未變）。

回退策略：第一階段無程式碼變更；第二階段的修改集中於 gizmo 的快照與持久化路徑，可獨立 revert 回現況。

## Open Questions

- **採用 S1、S2 或 S3？** 進入實作的前置條件，由 tasks 第 1 節解決。
- **寫入 `mo->sla_support_points` 是否必然中斷背景運算？** 若 `SLAPrint::apply()` 的 `diff()` 對內容相同的寫入不判定為變更，D2-a 即可行。需實測。
- **密度／權重變更（`:1437` 的 `"Support density change"` 快照）與 auto generate 的快照關係為何？** 兩者是否應合併為單一可還原操作，取決於 D1 的結論。
