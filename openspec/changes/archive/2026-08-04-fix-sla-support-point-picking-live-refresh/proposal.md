## Why

在 Manual Editing 模式下，調整 Process tab 的 Top 參數（例如 Upper Diameter、Lower Diameter）時，未被選取的支撐點（多半是 auto 生成的 island/slope 點）錐體外觀會**即時**跟著變大/變遠，但**可點選（hover/click）的範圍完全沒有跟著變**——外觀看起來直徑加大了，判定範圍卻停在原地。使用者實測描述：「產生的支撐點會離模型表面越遠，周圍越點不到，就像是外觀看起來有加大，可是可選取的範圍依然沒有加大」。

根因：`render_points()`（`GLGizmoSlaSupports.cpp:698`）每一幀都呼叫 `read_preview_top_params_live()`，這個函式直接讀取 wx 欄位當下的值（`process_top_float_live()`，`:130`），不快取——所以畫面上的錐體是即時的。但負責計算可點選範圍的 `update_point_raycasters_for_picking_transform()`（`:2485`）**不是每幀呼叫**，只在少數幾個離散時機（進入編輯模式、註冊 raycaster、拖曳支撐點、`data_changed()`）才會重算。Process tab 的 Top 欄位變更完全沒有接到任何一個會觸發它重算的路徑，於是可點選範圍停在「上一次離散觸發時」的舊幾何，與每幀都在更新的視覺錐體逐漸脫節。

本議題於驗收 `fix-sla-support-point-cone-picking`（2026-08-04 archive）期間發現。該 change 修的是 raycaster 擺放**公式**本身算錯（cone 高度、back 球未被涵蓋），本議題是**公式算對了，但沒有在對的時機重新套用**——根因不同，故另開一案。與 `fix-sla-support-top-params-live-read-isolation`（讀到**錯的**值，因為 widget 被挪用顯示選中點的值）也是不同軸線：本案是讀到**對的**值，但這個值從未被傳遞到 picking 路徑。

## What Changes

- 讓 `update_point_raycasters_for_picking_transform()` 比照 `render_points()` 既有的「每幀即時讀取」模式，在 Manual Editing 模式渲染期間隨每幀重新計算，而不是只在少數離散事件觸發時才更新。
- 量測每幀重算的實際成本（純 `Transform3d` 運算，不重建 mesh/AABB tree），確認在支撐點數百顆量級下不會造成可感知的效能落差；若有疑慮則評估節流（例如僅在編輯模式且面板可見時才逐幀更新）。

### Non-goals

- 不改變 raycaster 的擺放公式本身（`pin_sphere`/`cone`/`back_sphere` 的尺寸與位置推導）——那是 `fix-sla-support-point-cone-picking` 已完成的範圍，本 change 建立在其之上，不重新推導。
- 不處理「Top 欄位被挪用顯示選中點的值導致讀到錯誤數值」——`fix-sla-support-top-params-live-read-isolation` 的範圍。
- 不處理手動點建立當下幾何被凍結、之後不再跟隨 live 參數變化的問題——`fix-sla-support-preview-geometry-source-semantics` 的範圍。
- 不改變 Points preview 的視覺外觀、顏色規則或 clipping 行為。

## Capabilities

### New Capabilities

<!-- 無。本 change 為既有 capability 增補 requirement。 -->

### Modified Capabilities

- `sla-support-points-preview`：新增 requirement，規範 Points-preview 支撐點的 picking raycaster 變換必須隨 Process tab Top 欄位的即時編輯同步更新，不得停留在前一次離散事件觸發時的舊幾何。

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `update_point_raycasters_for_picking_transform()`（`:2485`）— 呼叫時機
  - 呼叫端（`on_render()` 或 `render_points()` 所在的每幀渲染路徑）— 新增每幀呼叫
- **Reference（不預期修改）**：`preview_sla_head_for_point()`、`head_geom_key()` 等既有幾何解析函式——本 change 只改變呼叫頻率，不改變計算內容
- 不影響切片輸出、檔案格式或 profile
- 無 public API 變更
