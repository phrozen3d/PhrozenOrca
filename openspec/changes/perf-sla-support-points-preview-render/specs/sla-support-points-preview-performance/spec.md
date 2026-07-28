# sla-support-points-preview-performance

## Overview

`GLGizmoSlaSupports::render_points()` 每一幀對「作用中的支撐點快取」（編輯模式為 `m_editing_cache`，否則為 `m_normal_cache`，令其大小為 N）繪製 Points preview。

本 capability 規範該路徑的**每幀成本上界**，以及這些成本削減不得改變任何可見結果或互動結果的等價性不變式。與其相鄰的 `sla-support-points-preview` capability 規範「畫什麼、畫在哪、能不能被點到」；本 capability 規範「畫這些東西每幀要付多少代價」。

成本來源的既有定義：

- Pinhead 網格：`sla::get_mesh(Head, steps)` / `sla::get_mesh_preview(Head, steps)`。
- GPU buffer：`GLModel::reset()` 釋放 VBO/IBO，`GLModel::render()` 首次呼叫時經 `send_to_gpu()` 重新配置。
- Process tab live 參數讀取：`Tab::get_field(key, Page**)` 為跨 page 線性搜尋，`TextCtrl::get_value()` 觸發原生控制項文字讀取。
- 表面法線：`MeshRaycaster::get_closest_point()` 為 AABB tree 最近點查詢。

## ADDED Requirements

### Requirement: Pinhead 幾何依參數快取，不隨點數重建

`render_points()` SHALL 以 head 幾何參數（pin 半徑、back 半徑、寬度、penetration、contact 球半徑、preview 旗標）為 key 快取已建構的 `GLModel`。幾何參數相同的支撐點 SHALL 共用同一個 `GLModel` 實例。

穩態下（幾何參數未變動），每幀的 `GLModel::init_from()` 呼叫次數 SHALL NOT 隨 N 成長。

快取 SHALL 有筆數門檻。超過門檻時 SHALL 整份清空後重新填充，且 SHALL NOT 切換到另一條渲染路徑——任何情況下畫面結果皆由同一條快取路徑產生。

#### Scenario: 參數一致的 auto 點共用同一模型

- **GIVEN** 非編輯模式，500 個 auto 點皆使用同一組 live 幾何參數
- **WHEN** 渲染一幀
- **THEN** 快取命中 499 次
- **AND** `GLModel::init_from()` 呼叫 0 或 1 次（視前一幀是否已建構）

#### Scenario: 帶 explicit geometry 的 manual 點

- **GIVEN** 編輯模式，含若干帶 explicit geometry 的 manual 點，其 pin/back 半徑各不相同
- **WHEN** 渲染一幀
- **THEN** 每一組相異參數對應一個快取項
- **AND** 每個點顯示的 cone 尺寸與逐點建構的舊行為相同

#### Scenario: preview 旗標不同的點不共用

- **GIVEN** 同時存在 auto 點（`get_mesh`，steps 24）與 `manual_add` 點（`get_mesh_preview`，steps 45）
- **WHEN** 渲染一幀
- **THEN** 兩者使用不同的快取項
- **AND** 各自的幾何與舊行為一致

#### Scenario: 超過快取門檻

- **GIVEN** 相異幾何參數組數超過快取門檻
- **WHEN** 渲染一幀
- **THEN** 快取整份清空後重新填充
- **AND** 畫面結果與未超過門檻時完全一致

### Requirement: 穩態下不得每幀重配置 GPU buffer

在幾何參數與點集皆未變動的連續幀之間，Points preview 路徑 SHALL NOT 呼叫 `GLModel::reset()`，亦 SHALL NOT 觸發 `send_to_gpu()` 重新配置 VBO/IBO。

每幀成本 SHALL 收斂為每點一次 uniform 設定與一次 draw call。

#### Scenario: 靜止畫面重繪

- **GIVEN** 非編輯模式，500 個點，Points preview 顯示中且快取已暖機
- **WHEN** 連續渲染 60 幀且不改變任何參數或點集
- **THEN** 這 60 幀中 `glGenBuffers` / `glBufferData` / `glDeleteBuffers` 的呼叫次數為 0

#### Scenario: 旋轉視角互動流暢度

- **GIVEN** 非編輯模式，500 個 auto 點已由 Apply 生成並顯示於 Points 模式
- **WHEN** 使用者以滑鼠拖曳旋轉視角
- **THEN** 畫面更新無可感知的停頓
- **AND** 切換至 Manual Support 模式後旋轉視角具相同流暢度

#### Scenario: live 參數連續變動時的退化上界

- **GIVEN** 500 個 auto 點顯示中
- **WHEN** 使用者連續拖曳 Process tab 的 `support_head_front_diameter` 使每幀 key 皆改變
- **THEN** 每幀 `GLModel::init_from()` 呼叫次數為 1，而非 500

### Requirement: Process tab live 參數每幀最多讀取一次

`render_points()` SHALL 在進入 per-point 迴圈之前，將所有 Process tab live 參數（`support_segment_length`、`support_head_penetration`、`support_head_front_diameter`、`support_head_back_diameter`、`support_contact_diameter`、`support_contact_type`）讀取一次，並在整個迴圈中重複使用。

每個 opt key 的 `Tab::get_field()` 與 `Field::get_value()` 呼叫次數 SHALL NOT 隨 N 成長。

`update_point_raycasters_for_picking_transform()` SHALL 遵循相同規則。

#### Scenario: 大量 auto 點下的欄位讀取次數

- **GIVEN** 非編輯模式，`m_normal_cache` 含 500 個點
- **WHEN** 渲染一幀
- **THEN** 每個 opt key 的 `Tab::get_field()` 呼叫次數為 1，而非 500

#### Scenario: live 參數編輯仍即時反映

- **GIVEN** Points preview 正在顯示
- **WHEN** 使用者在 Process tab 修改 `support_head_front_diameter` 的文字欄位且尚未失焦
- **THEN** 下一幀的 preview cone 直徑即反映新值
- **AND** 與逐點讀取的舊行為產生相同的幾何

#### Scenario: clamp 規則不變

- **GIVEN** 使用者將 `support_head_front_diameter` 設為小於 0.01 mm 的值
- **WHEN** 渲染一幀
- **THEN** 套用與舊行為相同的 clamp 下限
- **AND** 產生的 cone 幾何與逐點讀取路徑一致

### Requirement: Auto 點法線快取

非編輯模式下，`render_points()` SHALL NOT 對已有快取法線的支撐點重複呼叫 `get_closest_point()`。

法線快取 SHALL 在 `m_normal_cache` 內容被取代時失效，並在下一次渲染時重新填充。

#### Scenario: 連續多幀旋轉視角

- **GIVEN** 非編輯模式，`m_normal_cache` 含 500 個點，法線快取已填充
- **WHEN** 使用者拖曳旋轉視角 60 幀
- **THEN** `get_closest_point()` 在這 60 幀中的總呼叫次數為 0

#### Scenario: Apply 後快取重建

- **GIVEN** 法線快取已針對前一組支撐點填充
- **WHEN** 使用者在 Auto Support 面板按下 Apply，`m_normal_cache` 被新的一組點取代
- **THEN** 舊法線 SHALL NOT 被沿用
- **AND** 新點的法線在下一次渲染時計算

#### Scenario: 編輯模式法線維持既有語意

- **GIVEN** 編輯模式，`m_editing_cache[i].normal` 已有非零值
- **WHEN** 渲染一幀
- **THEN** 不呼叫 `get_closest_point()`，與既有行為一致

### Requirement: 最佳化不得改變可見結果與命中判定

套用本 capability 的所有最佳化後，Points preview 的渲染輸出與互動結果 SHALL 與最佳化前的逐點建構路徑完全等價。對任一組支撐點與任一 instance transform：

- 每個 preview cone 的世界座標位置、朝向、尺寸 SHALL 與最佳化前逐點建構的結果一致。
- 顏色規則（hover / selected / island / manual_add / slope）SHALL 不變。
- 逐點 clipping 判定（`is_mesh_point_clipped()`）與其對 raycaster active 狀態的影響 SHALL 不變。
- Hover 與點擊的命中結果 SHALL 與最佳化前一致。
- `vol->is_left_handed()` 時的 front face 翻轉 SHALL 不變。
- 光照法線矩陣 SHALL 依實際使用的 model matrix 推導，不得沿用未含擺放旋轉的舊值。

#### Scenario: 均勻 scale 下的等價性

- **GIVEN** 物件套用均勻 instance scale
- **WHEN** 以最佳化後路徑渲染 Points preview
- **THEN** cone 錨點貼合縮放後模型表面
- **AND** cone 直徑與長度維持支撐參數的 mm 尺寸，與 scale 無關

#### Scenario: 非均勻 instance scale 下的等價性

- **GIVEN** 物件套用非均勻 instance scale
- **WHEN** 以最佳化後路徑渲染 Points preview
- **THEN** cone 錨點落在可見表面上，且 cone 截面仍為圓形
- **AND** cone 軸沿縮放後可見表面法向
- **AND** 與最佳化前的 `head.pos` 覆寫加 `instance_matrix_no_scale` 路徑產生相同的世界座標

#### Scenario: 鏡像 instance

- **GIVEN** 物件套用鏡像變換，`vol->is_left_handed()` 為 true
- **WHEN** 渲染 Points preview
- **THEN** front face 翻轉行為與最佳化前一致
- **AND** cone 不出現法線反轉造成的著色錯誤

#### Scenario: Structure 模式 gate 不受影響

- **GIVEN** `m_show_support_structure == true` 且 `m_editing_mode == false`
- **WHEN** 渲染一幀
- **THEN** `render_points()` 仍於入口 early return
- **AND** 不建構任何 head 網格、不查詢任何法線、不讀取任何 live 參數

#### Scenario: 切片管線不受幾何抽取影響

- **GIVEN** `head_mesh_local()` 已改為呼叫抽出的 local-frame 幾何函式
- **WHEN** 執行支撐生成與切片
- **THEN** `get_mesh(Head)` 與 `get_mesh_preview(Head)` 的輸出與抽取前完全相同
- **AND** 實際列印的支撐幾何不變

#### Scenario: Undo / Redo 後不顯示過期幾何

- **GIVEN** 支撐點集在 Undo 或 Redo 後被取代
- **WHEN** 渲染下一幀
- **THEN** preview 反映新的點集與其幾何參數
- **AND** picking 結果與顯示一致

### Requirement: 效能最佳化不得改動 picking raycaster 的註冊結構

`register_point_raycasters_for_picking()` 為每個支撐點註冊 sphere 與 cone 兩個 raycaster。其中 cone raycaster 目前 transform 始終為 `Identity()` 且永遠 `set_active(false)`。

以效能為由的最佳化 SHALL NOT 移除該 cone raycaster 的註冊，即使它在當前程式碼中永遠處於 inactive 狀態。

理由：命中判定目前僅由半徑約 `r_pin` 的 sphere 承擔，導致 pinhead 外露錐體無法被點選；該 cone raycaster 是修復此缺陷的既有基礎設施，將由獨立的 correctness change 補上 transform 並啟用。

移除永遠 inactive 的 raycaster 所能節省的成本為零——`SceneRaycaster::hit()` 在迴圈開頭即跳過 inactive 項，且該迴圈不在每幀渲染路徑上。

#### Scenario: 編輯模式註冊數量維持不變

- **GIVEN** 編輯模式，`m_editing_cache` 含 500 個點
- **WHEN** 呼叫 `register_point_raycasters_for_picking()`
- **THEN** 註冊 1000 個 raycaster（每點一個 sphere 與一個 cone），與最佳化前相同

#### Scenario: hover 行為不變

- **GIVEN** 編輯模式且效能最佳化已套用
- **WHEN** 使用者將滑鼠移至某個支撐點的頂端球上
- **THEN** 該點被 hover 標示
- **AND** `m_hover_id` 與最佳化前相同

#### Scenario: clipping 下的 active 狀態

- **GIVEN** 編輯模式且 object clipper 已啟用，部分支撐點被裁切
- **WHEN** 渲染一幀
- **THEN** 被裁切點的 raycaster 依既有規則設為 inactive
- **AND** 未裁切點依既有規則維持 active
