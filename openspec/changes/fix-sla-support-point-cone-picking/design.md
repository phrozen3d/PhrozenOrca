## Context

`GLGizmoSlaSupports` 為每個支撐點註冊兩個**單位幾何** raycaster（`GLGizmoSlaSupports.cpp:535-544`、`:2362-2364`），沿襲上游「sphere 管球、cone 管錐體」的分工。本 fork 只完成了 sphere 那一半：

```
register_point_raycasters_for_picking()  :2362-2364
   ├─ .first  = sphere raycaster,  id = i     ┐
   └─ .second = cone   raycaster,  id = i     ┘ 兩者共用同一個 id

update_point_raycasters_for_picking_transform()  :2397-2417
   ├─ .first  → set_transform(sphere_matrix)  ✓  set_active(!clipped)  ✓
   └─ .second → （從未設定 transform，停留在 Identity()）
                set_active(false)  ← :2412 寫死
```

結果是命中體積僅為半徑 `max(head.r_pin_mm, head.r_contact_mm)`（`:2409`）的球，而視覺 pinhead 還包含長約 `width_mm` 的 robe 錐體與半徑 `r_back` 的 back 球。以預設值計，可點面積不到視覺體的十分之一。

**兩個 raycaster 以同一個 `id = i` 註冊**（`:2363-2364`）。`SceneRaycaster::hit()` 回傳的 `raycaster_id` 因此在命中球或命中錐時完全相同，`m_hover_id` 的語意不需要任何調整——這是本修復能保持極小侵入性的關鍵。

## Goals / Non-Goals

**Goals:**

- 使用者點擊視覺 pinhead 的任一部位（pin 球、robe 錐體、back 球）皆能命中該支撐點。
- 命中體積在均勻 scale、非均勻 scale、鏡像 instance 下皆與視覺體一致。
- 不引入誤觸：錐體外側空白區域不得命中。
- `m_hover_id`、拖曳、右鍵刪除等既有互動語意完全不變。

**Non-Goals:**

- 改變 Points preview 的視覺外觀、顏色規則或 clipping 行為。
- 以放大 sphere 半徑的方式近似（見 D1）。
- 改動 `register_point_raycasters_for_picking()` 的註冊結構——cone raycaster 已在註冊中，本 change 只補上 transform 與啟用。
- 處理 Points preview 每幀渲染成本（由 `perf-sla-support-points-preview-render` 承接）。

## Decisions

### D1. 啟用既有 cone raycaster，而非放大 sphere 半徑

替代方案是把 `pick_r` 從 `max(r_pin, r_contact)` 放大到涵蓋整顆 head 的包絡半徑（約 `width_mm/2 + r_back`）。一行改動、無方向問題，但：

- 球體包絡一個細長 pinhead 會嚴重過度膨脹。以預設值計，包絡半徑約 1.5 mm，而 robe 最寬處僅 `r_back = 0.5 mm`。
- 結果是點在錐體側邊的空白處也會命中，**以誤觸換掉漏觸**。支撐點密集時尤其惡化——相鄰點的包絡球會互相重疊，命中結果變得不可預測。

cone raycaster 已經存在且已註冊，補上 transform 的成本與風險都低於重新設計命中體積。

### D2. Cone transform 推導

已查證單位錐的幾何約定（`TriangleMesh.cpp:1063-1087`，`its_make_cone(1.0, 1.0, PI/12)`）：

```
底面圓心在原點 (0,0,0)，半徑 r=1，位於 z=0 平面
尖端在 (0,0,h) = (0,0,1)
軸向為 +Z（底 → 尖）
```

視覺 pinhead 的 robe 段：尖端側靠近錨點（半徑 `r_pin`），粗端側在外側 `width_mm` 處（半徑 `r_back`）。因此單位錐需：

- **縮放**：`(r_back, r_back, width_mm)`——底面半徑取 `r_back`，高度取 `width_mm`。
- **旋轉**：單位錐的 `+Z` 需映至 `-dir`（尖端朝錨點）。`Quaternion::FromTwoVectors(UnitZ, -dir)` 與 render 路徑使用的 `q = FromTwoVectors(-UnitZ, dir)` 對 Z 軸的作用完全相同（皆將 Z 映至 `-dir`），兩者僅差一個繞軸 roll，而錐體為旋轉對稱故無影響。**可直接複用 render 路徑的 `q`。**
- **平移**：尖端須落在錨點。旋轉後尖端位於「底面圓心 + (-dir) × height」，故底面圓心置於 `scaled_pos + dir × width_mm`。

組合形式（實作時需以實測驗證，特別是 `width_mm` 與 `z_shift` 的對應關係）：

```
cone_matrix = pick_matrix
            · Translation(scaled_pos + dir · width_mm)
            · Rotation(q)
            · Scale(r_back, r_back, width_mm)
```

其中 `pick_matrix` 與 `scaled_pos` 沿用現行 sphere 路徑既有的定義（`:2394-2395`、`:2410`），`dir` 為經 `normal_xform` inverse-transpose 修正後正規化的 `scaled_normal`。

### D3. 圓錐近似圓台，由 sphere 補足 pin 端

視覺 robe 實際上是圓台（半徑自 `r_pin` 過渡至 `r_back`），而 raycaster 幾何是圓錐（自 0 過渡至 `r_back`）。差異集中在 pin 端：圓錐在該處偏窄，涵蓋不到半徑 `r_pin` 的區域。

該區域正是 sphere raycaster 涵蓋的範圍（半徑 `max(r_pin, r_contact)`，圓心在錨點）。**兩者聯集後無空隙**，且圓錐恆內含於圓台，不會產生誤觸。這是保留 sphere 而非以 cone 完全取代的原因。

### D4. Active 狀態與 clipping 同步

`:2412` 的 `set_active(false)` 改為與 `.first` 相同的 clipping 連動規則。`render_points()`（`:651-653`）目前已對 `.first` 與 `.second` 同時設定 active 狀態，該處無需修改——它一直在正確地管理 `.second`，只是 `.second` 隨後又被 `update_point_raycasters_for_picking_transform()` 無條件關閉。

修正後兩處規則一致：被 clipping 裁切的點，其 sphere 與 cone raycaster 皆為 inactive。

### D5. 相依於 `perf-sla-support-points-preview-render`

該 change 的 design D2 會建立並驗證擺放矩陣 `M_ns · Translation(S · sp.pos) · Rotation(q)`，其中的 `Rotation(q)` 正是本 change D2 所需（見該節說明兩者旋轉等價）。

- **順序實施（建議）**：本 change 直接複用已驗證等價性的推導，只需疊上單位錐至實際尺寸的縮放。
- **反序實施**：需在 picking 路徑先行寫一套旋轉推導，待 render 路徑改動後再同步驗證兩邊一致——重複工作且增加不一致風險。

該 change 已於其 proposal 的 Non-goals 與 design D7 明令不得移除 cone picking raycaster，即為保全本 change 的修復基礎。

## Risks / Trade-offs

- **[誤觸：cone 命中體積超出視覺體]** → D3 已論證圓錐恆內含於視覺圓台。仍需驗收確認錐體外側緊鄰的空白處不會命中，尤其在支撐點密集時。

- **[非均勻 scale 下 picking 與視覺錯位]** → cone 具方向性，是本 change 引入旋轉至 picking 路徑的原因。若沿用 sphere 路徑「不需要 non-uniform-scale 修正」的假設（`:2405-2407` 註解）會直接錯位。驗收須涵蓋 `(2,1,1)` 與 `(1,1,3)`。

- **[鏡像 instance 下錐體朝向反轉]** → `pick_matrix` 經 signed `get_matrix()` 保留鏡像，旋轉疊加後方向是否仍正確需實測驗證。

- **[`width_mm` 與視覺 robe 長度的對應]** → `head_mesh_local()` 在 preview 模式下的軸向長度為 `width_mm - 2·r_pin - 2·r_back`（`SupportTreeMesher.hpp:53-55`），與非 preview 模式不同。cone 的高度取值須與**實際被繪製的**幾何一致，manual 點（`get_mesh_preview`）與 auto 點（`get_mesh`）可能需分別處理。這是本 change 最可能出錯的細節。

- **[hover 優先順序改變]** → 兩個 raycaster 共用同一 `id = i`，命中任一皆回報同一點，`m_hover_id` 語意不變。風險低，但驗收仍須確認相鄰點重疊時的命中結果符合直覺（取最近者，由 `SceneRaycaster::hit()` 的 `is_closest` 既有邏輯處理）。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。屬 picking 行為修正，可直接隨版本發布。

回退策略：將 `.second` 的 `set_active()` 改回 `false` 即完全恢復現行行為，transform 設定成為無作用碼。回退粒度為單一函式內的數行。

## Open Questions

- Manual 點（`get_mesh_preview`，steps 45）與 auto 點（`get_mesh`，steps 24）的 robe 軸向長度公式不同（見 Risks）。實作時需確認是否以單一 cone 高度公式涵蓋兩者，或依 `manual_add` 分別計算。
