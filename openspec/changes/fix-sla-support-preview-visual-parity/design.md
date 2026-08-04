## Context

`render_points()`（`GLGizmoSlaSupports.cpp`）目前把「這顆點是不是 `manual_add`」當成三個獨立視覺屬性的開關：

```cpp
const bool manual_preview = support_point.type == sla::SupportPointType::manual_add;
static constexpr size_t kManualPreviewSteps = 45;
...
const indexed_triangle_set top_its = sla::head_mesh_body(
    head, manual_preview ? kManualPreviewSteps : 24, manual_preview);
...
use_shader(manual_preview ? flat_shader : gouraud_shader);
```

而顏色分流則是另一組完全獨立的開關，包在 `m_editing_mode` 底下：

```cpp
else if (m_editing_mode && support_point.type == sla::SupportPointType::manual_add) render_color = CYAN;
else if (m_editing_mode && support_point.type == sla::SupportPointType::island) render_color = ORANGE;
else if (m_editing_mode) render_color = LIGHT_GRAY;
else render_color = {0.5f, 0.5f, 0.5f, 1.f};
```

已查證 `pinhead_preview()` / `get_mesh_preview()`（`SupportTreeMesher.hpp/.cpp`）全專案僅有 `render_points()` 這一個呼叫端，`grep` 確認無切片管線、無其他 GUI 依賴。

已查證 `perf-sla-support-points-preview-render`（2026-07-29 archive）建立的 `HeadGeomKey`（`GLGizmoSlaSupports.hpp`）含 `preview` 布林欄位，由 `head_geom_key(head, manual_preview)` 填入，是 `m_head_model_cache` 這個 map 的 key 組成之一。

## Goals / Non-Goals

**Goals:**

- manual 點與 auto 點使用相同的 mesh 建構函式（`pinhead()`）與相同的 shader（`gouraud_shader`）。
- `manual_add` / `island` / `slope` 的顏色分流在編輯模式與非編輯模式下皆生效。
- 孤島鎖定提示（`BLUEISH`）維持只在編輯模式生效，不隨本 change 變動。
- 移除因此變成無呼叫者的死碼（`pinhead_preview()`、`get_mesh_preview()`）。
- 確認 `HeadGeomKey` 的 `preview` 維度移除或恆定後，快取行為不受影響（穩態每幀 `init_from()` 呼叫次數仍為 0）。

**Non-Goals:**

- 不改變支撐點的幾何尺寸解析規則（`preview_use_stored_top()` / `has_explicit_geometry()`），那是 `fix-sla-support-preview-geometry-source-semantics` 的範圍。
- 不改變 hover（CYAN）/ selected（REDISH）的既有顏色規則。
- 不改變 picking 命中判定或 `update_point_raycasters_for_picking_transform()`。
- 不處理「選定支撐點編輯參數時其他 auto 點外觀被連動」的 Top 欄位讀值副作用問題。
- 不引入新的 shader 或新的幾何生成函式——統一的目標就是「manual 點沿用 auto 點現有的畫法」，不是「兩者都改成第三種畫法」。

## Decisions

### D1. 統一沿用 auto 點現有的畫法，而非新寫一套

`render_points()` 改為對所有點無條件使用 `pinhead()` + `gouraud_shader`，移除 `manual_preview` 這個分支變數，以及對 `flat_shader` 的引用。

- **為何不是「兩者都改成新樣式」**：`pinhead()` + `gouraud_shader` 已經是 auto 點驗證過、使用者熟悉的樣式；改成第三種樣式沒有額外好處，只會增加風險與測試面。
- **為何 manual 點原本的簡化樣式可以直接捨棄**：見 proposal——PrusaSlicer 原始的用途分工（auto=只表位置、manual=表位置+方向）在 PhrozenOrca 加入逐點參數功能後已經不成立，且 PrusaSlicer 原版 manual 模式本來就有完整 polygon + 正確光影，現在的簡化版是移植疏漏而非刻意設計。

### D2. steps 統一為多少

現行 auto 點 `get_mesh(head, 24)`，manual 點 `get_mesh_preview(head, kManualPreviewSteps=45)`。統一後只有一個路徑，需要決定新的固定 steps 值。

**決定：統一使用 24**（沿用 auto 點現行值），而非 45。

- **理由**：24 是目前份量最大的族群（所有 auto 點）已經在用、視覺上驗證過夠用的精細度。若改採 45，形狀最接近的效果只是弧面更平滑，但每個支撐點的三角形數量會顯著增加（更多 ring steps），而 Points preview 常見場景是同時顯示數百個支撐點——寧可維持目前 auto 點已驗證的成本量級，不要讓「更平滑」變成「更貴」。
- 若之後實際比對發現 24 在放大檢視下明顯不夠平滑，可另外調整，但那是獨立的視覺品質決策，不影響本 change 的正確性目標（manual 與 auto 一致）。

### D3. `HeadGeomKey` 的 `preview` 欄位如何處理

`head_geom_key(head, preview)` 目前把 `preview` 量化進 key。統一畫法後，`render_points()` 內唯一的呼叫點會**恆定傳入 `false`**（因為不再有 preview 分支）。

- **決定：保留 `HeadGeomKey.preview` 欄位與參數簽章，不刪除。** 只是呼叫端永遠傳 `false`。
- **為何不順手把這個欄位也拿掉**：`head_geom_key()` 與 `HeadGeomKey` 是 `perf-sla-support-points-preview-render` 那個 change 建立、已驗收過的既有機制。動它的資料結構屬於效能快取的修改，不屬於本 change「畫法統一」的範圍；且保留欄位、恆定傳 `false` 是零風險的作法——key 空間變小（等同少一個維度的分歧），不影響任何既有行為，也不需要重新驗證快取機制本身。
- **效果**：auto 點與 manual 點只要尺寸參數相同，現在會自然落在同一個快取項目——比修改前的兩套快取（auto 用 `preview=false`、manual 用 `preview=true`）更精簡。

### D4. 顏色分流跨模式：拆開被綁在一起的兩件事

現行程式碼把「這是什麼類型的點」（type-based）與「使用者現在有沒有鎖定孤島」（`m_lock_unique_islands`，一個只在編輯模式操作面板上才有意義的開關）綁在同一段 `m_editing_mode` 判斷式底下。拆法：

```cpp
// 選取/hover：維持不變，本來就該只在編輯模式生效
if (m_editing_mode && size_t(m_hover_id) == i) render_color = CYAN;
else if (m_editing_mode && point_selected) render_color = REDISH;
// 孤島鎖定：維持只在編輯模式生效（本 change 的決定）
else if (m_lock_unique_islands && support_point.is_island() && m_editing_mode) render_color = BLUEISH;
// type 分流：移出 m_editing_mode，跨模式生效
else if (support_point.type == manual_add) render_color = CYAN;
else if (support_point.type == island) render_color = ORANGE;
else render_color = LIGHT_GRAY;   // 取代原本非編輯模式的固定灰色
```

- **為何孤島鎖定維持編輯模式限定**：使用者已確認——鎖定操作本身只在編輯模式的面板上才能觸發，非編輯模式沒有「動到孤島」這回事，跨模式顯示鎖定狀態沒有對應的操作語意。
- **為何不需要額外的旗標或狀態機**：`m_lock_unique_islands` 和 `is_island()` 都是既有可讀的狀態，純粹是條件式重新排列，不需要新增資料。

### D5. 死碼移除的範圍（實作中修正：`pinhead_preview()` 其實不能單獨移除）

`get_mesh_preview()`（`SupportTreeMesher.hpp`）與 `kManualPreviewSteps` 常數，在 D1 生效後確認無呼叫者，已移除：

- `render_points()` 內的 `manual_preview` 分支與 `kManualPreviewSteps`
- `SupportTreeMesher.hpp` 的 `get_mesh_preview()` inline 函式

**`pinhead_preview()` 原提案打算一併移除，實作時發現這會導致編譯錯誤**：`head_mesh_body(h, steps, preview)` 的 `preview=true` 分支內部就是呼叫 `pinhead_preview()`——這兩者不是各自獨立的死碼，而是同一條呼叫鏈。保留前者的簽章卻拿掉後者的定義，會讓 `head_mesh_body()` 編不過。

修正後的決定：**`pinhead_preview()` 保留**，`head_mesh_body(h, steps, preview)` 的簽章與雙分支行為維持原樣。目前確實沒有任何呼叫者會傳入 `preview=true`（`get_mesh()` 恆傳 `false`，`render_points()` 本 change 後也恆傳 `false`），`pinhead_preview()` 因此是**編譯期仍可達、執行期不可達**的程式碼——與 D3 保留 `HeadGeomKey.preview` 欄位的理由一致：`head_mesh_body()` 是同時服務切片管線（`get_mesh()`）的共用函式，收斂它的雙分支屬於更大範圍的重構，不在本 change（純 GUI 渲染路徑的視覺統一）範圍內。已於 `pinhead_preview()` 宣告處加註解說明現況與原因，避免日後有人誤以為可以安全刪除卻忽略 `head_mesh_body()` 仍呼叫它。

## Risks / Trade-offs

- **[使用者已經習慣 manual 點的簡化外觀，改動是可見的視覺變化]** → 這是本 change 的目的：manual 點目前的簡化外觀是移植疏漏而非刻意設計，統一後才是「應有的樣子」。屬 correctness fix，非新增功能。

- **[24 vs 45 steps 的選擇日後被質疑]** → D2 已記錄理由（成本與現有驗證過的精細度），若之後有具體的視覺品質回饋，可另開獨立的視覺調整 change，不影響本 change 的正確性目標。

- **[HeadGeomKey 的 preview 欄位變成恆定值，日後有人誤以為可以安全拿掉卻忽略還有其他呼叫端]** → D3 已明確保留欄位、只改呼叫端傳值，且本 change 之後 `head_geom_key()` 全專案只有一個呼叫點（`render_points()`），下一個真正想精簡這個資料結構的人可以查證後再動。

- **[移除 pinhead_preview()/get_mesh_preview() 後，若日後 PrusaSlicer 上游同步帶回類似需求，需要重新實作]** → 這兩個函式的邏輯本身沒有被本 change 抹除概念（`head_mesh_body()` 仍支援 `preview=true` 路徑），只是移除了「manual 點用簡化版」這個呼叫慣例；若日後真的需要，恢復呼叫端邏輯的成本很低。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純渲染路徑的外觀修正，可直接隨版本發布。

回退策略：形狀統一、光影統一、顏色跨模式三者可個別 revert（各自獨立 commit）。死碼移除若需要回退，一併恢復對應的呼叫端與函式定義即可。

## Open Questions

無。
