## Context

`GLGizmoSlaSupports::on_render()`（`GLGizmoSlaSupports.cpp:557-603`）是每幀都會執行一次的渲染入口，其中：

```cpp
render_volumes();
render_points(selection);   // :595 — 每幀執行，內部即時讀取 Process tab Top 欄位
```

`render_points()`（`:624`）內部每幀呼叫 `read_preview_top_params_live()`，這個函式直接讀取 wx 欄位當下的值（`process_top_float_live()`，`:130`，透過 `Field::get_value()`，不快取），所以畫面上的錐體尺寸與位置是「即時」的——使用者在 Process 面板打字，下一幀就反映在畫面上。

負責計算可點選範圍的 `update_point_raycasters_for_picking_transform()`（`:2485`）**不在 `on_render()` 的呼叫路徑上**。目前呼叫端只有：

- `register_point_raycasters_for_picking()`（`:2458`，進入編輯模式、新增點後重新註冊時呼叫）
- `data_changed()`（`:461`，物件切換或背景運算結果回來時）
- 支撐點拖曳過程中的更新（`gizmo_event()` 內，拖曳當下逐次呼叫）

這些都是**離散事件**，沒有一個對應到「Process tab Top 欄位變更」。於是可點選範圍停在上一次離散事件觸發時的舊幾何，而視覺錐體因為 `render_points()` 每幀重讀而持續變化，兩者逐漸脫節。

已查證 `update_point_raycasters_for_picking_transform()` 本身的每幀成本很低：迴圈內對每顆點的法向量查詢（`get_closest_point()`，實際走 AABB raycast）被 `if (m_editing_cache[i].normal == Vec3f::Zero())` 擋住——只有法向量尚未快取的點才會觸發，一旦快取過就跳過；其餘只是 `preview_sla_head_for_point()`（純算術）與三個 `Transform3d` 矩陣運算。`render_points()` 本身已經是每幀執行、且用同一套「法向量快取後跳過查詢」的模式，所以把 `update_point_raycasters_for_picking_transform()` 也改成每幀執行，成本量級與 `render_points()` 既有的每幀迴圈相當，不是新增一個量級的開銷。

## Goals / Non-Goals

**Goals:**

- Manual Editing 模式下，Process tab 的 Top 欄位變更（Upper/Lower Diameter、Segment Length、Penetration…）即時反映到可點選範圍，不需要使用者做任何額外動作（拖曳、切換模式）才能「喚醒」picking。
- 確認每幀重算 picking transform 的成本可忽略，不在數百點量級下造成可感知的掉幀。

**Non-Goals:**

- 不改變 `pin_sphere`/`cone`/`back_sphere` 的幾何推導公式——那是 `fix-sla-support-point-cone-picking`（2026-08-04 archive）已完成且驗收過的範圍。
- 不處理 Top 欄位讀值本身的正確性（widget 被挪用顯示選中點的值導致讀到錯誤數值）——`fix-sla-support-top-params-live-read-isolation` 的範圍。
- 不處理手動點建立當下幾何被凍結、之後不再跟隨 live 參數的問題——`fix-sla-support-preview-geometry-source-semantics` 的範圍。
- 不變更非編輯模式（Points 檢視）下的 picking 行為——那個模式本來就不註冊 `m_point_raycasters`（`register_point_raycasters_for_picking()` 的 `m_editing_mode &&` 前置條件），與本 change 無關。

## Decisions

### D1. 每幀呼叫，而非掛在欄位變更事件上

考慮過兩種做法：

- **選項 A（掛在欄位變更事件）**：找出 Process tab Top 欄位的 `on_change` 回呼，額外掛一個呼叫 `update_point_raycasters_for_picking_transform()` 的 handler。
- **選項 B（每幀呼叫，比照 `render_points()`）**：在 `on_render()` 的 `render_points(selection);` 旁邊，於 `m_editing_mode` 為真時一併呼叫 `update_point_raycasters_for_picking_transform()`。

**選擇 B**，理由：

1. `render_points()` 已經是「每幀即時讀取」模式（見 Context），不是事件驅動——`read_preview_top_params_live()` 本身就不快取、每幀重讀。picking 只是還沒被同樣對待。選項 B 讓兩者在同一個時機、同一種模式下取值，天然保證兩者一致，不會有「render 已更新、picking 還沒更新」的競態。
2. 選項 A 需要找到並掛上正確的欄位變更事件——但 Top 欄位有好幾個（Upper/Lower Diameter、Segment Length、Penetration、Contact Diameter…），且 `read_preview_top_params_live()` 目前的設計就是「輪詢式即時讀取」而非「事件推播」，額外接事件等於引入第二套更新機制，與現有架構風格不符，維護成本更高。
3. 已於 Context 確認每幀成本可忽略（法向量查詢被快取跳過，其餘為純矩陣運算），選項 B 沒有明顯的效能代價需要用選項 A 的「只在變更時才算」去換。

### D2. 呼叫位置與守衛條件

於 `on_render()`（`:595` `render_points(selection);` 之後）新增：

```cpp
if (m_editing_mode)
    update_point_raycasters_for_picking_transform();
```

- **為何要 `m_editing_mode` 守衛**：`update_point_raycasters_for_picking_transform()` 內部本來就有 `if (m_editing_cache.empty() || m_point_raycasters.empty()) return;` 這道防線，理論上非編輯模式下 `m_point_raycasters` 也確實是空的（`register_point_raycasters_for_picking()` 只在編輯模式下才會填入），所以就算不加這道守衛也不會出錯——但顯式寫出 `m_editing_mode` 讓呼叫端的意圖一目瞭然（「這是編輯模式才需要做的事」），且省下每幀呼叫一次空函式的呼叫開銷（雖然微小）。

### D3. 實作時修正一：`pin_sphere` 本來就沒對準真正的球心（既有缺陷，經每幀呼叫放大而被發現）

驗收 3.3（反覆調整 Top 欄位）時，使用者實測發現：單獨調高 Contact Diameter 後，前端圓球視覺變大，但命中範圍出現偏差——「有點到卻判沒點到，或滑鼠剛靠近邊緣還沒觸碰就當被 hover 了」。

追查後發現這不是本 change 引入的問題，而是 `fix-sla-support-point-cone-picking`（2026-08-04 archive）就存在、未被驗收發現的既有缺陷：`pin_sphere` 從一開始就直接以 `scaled_pos`（世界偏移量 0，即錨點 `pos` 本身）為球心，但 pin 球（以及與其同心的 contact 球）在 `head_mesh_body()` 實際渲染出來的位置**不是**在錨點——用與 `cone`/`back_sphere` 相同的推導方式（見其 design.md D2）反推 `pinhead()` 的座標：

- pin 球在 `pinhead()` 內部先整體平移 `+h`（`h = r_back + r_pin + width_mm`，函式內局部變數），`head_mesh_body()` 再對整個網格套用外層 `z_shift = fullwidth() - r_back_mm`。兩者疊加後，pin 球的最終局部球心 z 座標 = `h - z_shift = penetration_mm - r_pin_mm`（代入 `fullwidth()` 展開可得），**不是 0**。
- 局部 z 座標透過 `head_mesh_local()` 的旋轉（局部 +Z 映至 `-head.dir`）轉換為世界偏移量：`世界偏移量 = -局部z`，故 pin/contact 球心的世界偏移量為 `r_pin_mm - penetration_mm`，沿 `head.dir` 方向。

以預設值計（`r_pin=0.2mm`、`penetration=0.4mm`）偏移量約 `-0.2mm`——絕對值不大，球本身半徑也小（`max(r_pin, r_contact)`，未開 contact sphere 時就是 `r_pin=0.2mm`），偏移相對球體大小的比例其實不小，但受限於預設下整體目標本來就很小、滑鼠精度也粗，一直沒被注意到。**Contact Diameter 調大後，球半徑變大、螢幕上的可視面積變大，同一個固定的 mm 級偏移量在畫面上造成的「看到的位置」與「打得到的位置」落差變得明顯可感知**——這正是 5.2 節後半段一直沒有專門測過 Contact Diameter 放大情境的緣故。

**修正**：`pin_sphere` 的球心改為 `scaled_pos + (head.r_pin_mm - head.penetration_mm) * head.dir`，與 `cone`/`back_sphere` 一樣用「真正的局部座標」而非「錨點本身」定位。`cone`/`back_sphere` 的公式本身不受影響（其推導從一開始就是基於真實局部座標，經 5.1/5.2/5.9 等多項驗收確認過），不需要跟著調整。

此修正雖然是既有 change 的缺陷，但因為只有在 `fix-sla-support-point-cone-picking` 已完成、且本 change 讓 picking 每幀都被驗證的情況下才會被使用者注意到並回報，且改動集中在同一個函式、由同一個推導脈絡延伸，故直接併入本 change 修正，不另開 change（不回頭修改已 archive 的 `fix-sla-support-point-cone-picking`，維持其作為歷史交付記錄的完整性——見其 README.md 條目後續會補一筆指向本 change 的註記）。

### D4. 實作時修正二：每幀呼叫本身會覆寫 clipping 的 inactive 狀態

同一輪驗收發現第二個問題：clipping plane 隱藏的支撐點，畫面上看不到，卻仍會攔截原本應該落在其他點上的 hover/click。

根因是本 change 自己引入的：`render_points()`（`:721`）每幀依 `is_mesh_point_clipped()` 正確設定 `pin_sphere`/`cone`/`back_sphere` 的 active 狀態（clipped 時設 `false`），但 `update_point_raycasters_for_picking_transform()` 原本（`fix-sla-support-point-cone-picking` 建立時）對三者一律 `set_active(true)`——這在「只在離散事件才呼叫」的舊架構下沒有問題，因為 `render_points()` 每幀都會在**之後**重新跑一次，最終以 clipping 為準（design.md D4 of `fix-sla-support-point-cone-picking` 當時的論證：「本處先開啟，`render_points()` 逐幀依 clipping 覆寫」）。

但 D2 把 `update_point_raycasters_for_picking_transform()` 的呼叫點放在 `on_render()`**同一幀內、`render_points()` 之後**——於是每一幀都變成：`render_points()` 先正確地把 clipped 點設成 inactive，緊接著 `update_point_raycasters_for_picking_transform()` 又無條件把它們全部設回 active，兩者順序原本的假設（「`render_points()` 說了算」）反過來被推翻。clipped 點因此在每一幀都被重新啟用，畫面上看不到卻仍可被 hit-test 命中。

**修正**：不依賴呼叫順序，讓 `update_point_raycasters_for_picking_transform()` 自己計算 `is_mesh_point_clipped(sp.pos.cast<double>())` 並據此 `set_active(!clipped)`，與 `render_points()` 各自獨立算出一致的結果，不論兩者的呼叫順序或呼叫頻率如何改變都不會互相推翻。比起「調整呼叫順序讓 `render_points()` 排在後面」，這個做法更穩固：本 change 的整個目的就是讓 `update_point_raycasters_for_picking_transform()` 變成跟 `render_points()` 一樣的「每幀常駐」函式，兩個獨立演化的每幀函式之間如果靠呼叫順序維持正確性，未來任何一方的呼叫點被搬動都可能重新引入這個 bug；讓每個函式自己對 clipping 負責，才是經得起未來修改的做法。

## Risks / Trade-offs

- **[每幀呼叫是否會在極端點數下造成掉幀]** → Context 已論證單幀成本量級與 `render_points()` 既有迴圈相當。驗收需在數百點量級下實測旋轉視角/拖曳流暢度，若有可感知落差則重新評估（例如節流為每 N 幀一次，但預期用不到）。

- **[每幀重算是否會讓拖曳中的點「跳掉」]** → 拖曳邏輯目前已有自己的即時 transform 更新路徑（`gizmo_event()` 內的逐次呼叫）。新增的每幀呼叫與其呼叫的是同一個函式、算的是同一組資料，不會互相打架——只是多了一個呼叫來源，兩者收斂到同一個結果。驗收需確認拖曳手感與修改前一致。

- **[非編輯模式下誤觸發]** → D2 已加上 `m_editing_mode` 守衛，且函式本身有 `m_point_raycasters.empty()` 防線，雙重保險。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純呼叫時機調整，可直接隨版本發布。

回退策略：移除 `on_render()` 新增的那兩行即可完全恢復原行為（回到只在離散事件時更新）。

## Open Questions

無。
