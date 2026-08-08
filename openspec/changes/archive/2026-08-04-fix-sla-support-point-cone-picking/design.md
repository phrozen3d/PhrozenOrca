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

### D2. Cone transform 推導（實作時修正：高度不是 `width_mm`，而是 `fullwidth() - r_back_mm`）

已查證單位錐的幾何約定（`TriangleMesh.cpp:1063-1087`，`its_make_cone(1.0, 1.0, PI/12)`）：

```
底面圓心在原點 (0,0,0)，半徑 r=1，位於 z=0 平面
尖端在 (0,0,h) = (0,0,1)
軸向為 +Z（底 → 尖）
```

- **旋轉**：單位錐的 `+Z` 需映至 `-dir`（尖端朝錨點）。`Quaternion::FromTwoVectors(UnitZ, -dir)` 與 render 路徑使用的 `q = FromTwoVectors(-UnitZ, dir)` 對 Z 軸的作用完全相同（皆將 Z 映至 `-dir`），兩者僅差一個繞軸 roll，而錐體為旋轉對稱故無影響。**直接複用 render 路徑的 `q`。**

**原提案打算用 `width_mm` 當作 cone 高度與平移量，實作時查證 `pinhead()`（`SupportTreeMesher.cpp:158-229`）的實際幾何後發現這個假設是錯的**：`width_mm` 只是傳給 `pinhead()` 的「robe 名義長度」參數，不是 robe 實際占據的軸向範圍。

用 `pinhead()` 自身的座標推導實際端點（皆為精確值，非近似）：

- pin 球（`s2`）整體在呼叫前先 `+= h`（`h = r_back + r_pin + width_mm`，函式內部變數），其球心落在（相對 back 球球心）局部 z = `h`；pin 球頂端（尖端）落在局部 z = `h + r_pin`。
- `head_mesh_body()` 對整個網格套用的 `z_shift = fullwidth() - r_back_mm`（`preview=false` 分支，見 `SupportTreeMesher.hpp:69`）。減去 `z_shift` 後，錨點（`pos`，對應局部 z=0）與 pin 球頂端的相對關係為：`(h + r_pin) - z_shift = r_back + 2·r_pin + width_mm - (fullwidth() - r_back) = penetration_mm`（代入 `fullwidth() = 2·r_pin + width_mm + 2·r_back - penetration_mm` 化簡可得）。也就是說 **pin 球頂端在錨點沿 `dir` 反方向 `penetration_mm` 處**——這正是「penetration」這個欄位名稱本身的意涵（針尖沒入模型表面多深），與現有 `sphere` raycaster（半徑 `max(r_pin, r_contact)`、圓心在錨點）的涵蓋範圍高度重疊。
- back 球（`s1`）未經额外平移，球心在局部 z=0（相對 back 球自身），減去 `z_shift` 後，其球心對應世界偏移量 `fullwidth() - r_back_mm` 沿 `dir` 方向——這正是 `Head::junction_point()` 既有定義的 `pos + (fullwidth() - r_back) · dir`（`SupportTreeBuilder.hpp:109-112`）。這是 back 球最寬處（半徑恰為 `r_back`），再往外（偏移量增至 `fullwidth()`）球面才收窄回一個點（球的另一極）。

**修正後的決定**：cone 底面（半徑 `r_back`）置於 `junction_point()` 的偏移量（`fullwidth() - r_back_mm`），尖端（半徑 0）置於錨點本身（偏移量 0，與 sphere 圓心重合）。相比原提案的 `width_mm`（以預設值計僅 2mm），新高度 `fullwidth() - r_back_mm`（以預設值計約 2.5mm）涵蓋了 robe 實際占據的軸向範圍中更大的一段——原提案會在 back 球端留下明顯的漏觸區（back 球本身完全沒有另一個 sphere raycaster 補足，只能靠 cone 涵蓋），而非只是「多一點誤差」。

尖端到 back 球最寬處之間、與 back 球最寬處再往外收窄到球極點之間，各留有一小段未被 cone 精確貼合的區域（前者由 sphere raycaster 補足，後者是球極點附近本來就很小的可視面積，未特別處理）——這是圓錐近似圓台、圓極端不含入計算的既有取捨（見 D3），非本次修正試圖消除的範圍。

組合形式：

```
cone_height = head.fullwidth() - head.r_back_mm
cone_matrix = pick_matrix
            · Translation(scaled_pos + dir · cone_height)
            · Rotation(q)
            · Scale(r_back, r_back, cone_height)
```

其中 `pick_matrix` 與 `scaled_pos` 沿用現行 sphere 路徑既有的定義，`dir` 為經 `normal_xform` inverse-transpose 修正後正規化的 `scaled_normal`（透過 `preview_sla_head_for_point()` 產生 `head.dir`，與 render 路徑用同一份邏輯，不重覆手寫正規化/退化保護）。

### D3. 圓錐近似圓台，兩端各由一顆 sphere 補足（實作時修正：back 端原本漏了一顆）

視覺 robe 實際上是圓台（半徑自 `r_pin` 過渡至 `r_back`），而 raycaster 幾何是圓錐（自 0 過渡至 `r_back`）。差異集中在兩端：

- **pin 端**：圓錐在該處偏窄，涵蓋不到半徑 `r_pin` 的區域。該區域由既有的 pin-end sphere raycaster 涵蓋（半徑 `max(r_pin, r_contact)`，圓心在錨點 `pos`）——這是本 change 修改前就存在、一直有效的既有機制。

- **back 端（原設計遺漏，經使用者實測發現）**：cone 的底面（半徑 `r_back`）只能是一個**平面圓盤**，精確貼合 back 球的最寬處（赤道）；但 back 球是完整的球面，過了赤道之後球面會**彎回收窄**到自己的極點（見 D2 對 `pinhead()` 幾何的推導）。這一段彎回收窄的球殼，圓錐（直邊）完全無法涵蓋，而原設計又只在 pin 端放了一顆 sphere，back 端什麼都沒有——導致 back 球從赤道往外（即靠近模型內部/pillar 那一側）整顆完全點不到。使用者驗收 5.2 時實測到「貼在模型表面的球到中段都能點，但最外側的球點不到」，正是這個遺漏。

  **修正**：新增第三個 raycaster——back-end sphere，圓心與 cone 底面同一點（`scaled_pos + cone_height · head.dir`，即 `head.junction_point()` 對應的偏移量），半徑 `r_back_mm`，與可見的 back 球完全重合（不是近似）。`m_point_raycasters` 型別由 `std::pair` 改為三欄位的 `PointRaycasterSet{pin_sphere, cone, back_sphere}`，三者以同一個 picking id 註冊，故 hover 語意不變（見 D1 註記）。

**兩顆 sphere 與 cone 三者聯集後，pin 端與 back 端皆無空隙**，且圓錐恆內含於圓台，不會產生誤觸。這是保留 sphere（而非以 cone 完全取代兩端）的原因。

**效能影響**：`add_raycaster_for_picking()` 重覆使用同一個共用的 `m_sphere.mesh_raycaster`（單位球面的 `MeshRaycaster`，AABB tree 只建一次，供所有點、所有 sphere raycaster 共用同一份，只是各自套不同的 `Transform3d`）——新增第三個 raycaster **不會**新建任何 mesh 或 AABB tree，只是 `SceneRaycaster` 內部 `m_gizmos` 這個 vector 多一個輕量條目（id + transform + active flag + 共用 mesh_raycaster 指標）。`SceneRaycaster::hit()` 對這個 vector 做線性掃描，逐一測試 ray-vs-transformed-sphere；每點的 raycaster 數從 2 個增加到 3 個，增加約 50% 的逐點測試量，且只在滑鼠移動觸發 hover 判定時執行一次（不是每幀、不是每個三角形），對支撐點數量在數百量級的實際使用情境而言可忽略不計。

### D4. Active 狀態與 clipping 同步

`:2412` 的 `set_active(false)` 改為與 `.first`（現為 `pin_sphere`）相同的 clipping 連動規則。`render_points()`（`:651-653`）目前已對 `.first` 與 `.second` 同時設定 active 狀態，D3 新增的 `back_sphere` 一併納入同一段管理——該處無需另外修改邏輯，只是三個欄位都要設，原本管兩個地方現在管三個。

修正後三處規則一致：被 clipping 裁切的點，其 `pin_sphere`、`cone`、`back_sphere` 三個 raycaster 皆為 inactive。

### D5. 相依於 `perf-sla-support-points-preview-render`

該 change 的 design D2 會建立並驗證擺放矩陣 `M_ns · Translation(S · sp.pos) · Rotation(q)`，其中的 `Rotation(q)` 正是本 change D2 所需（見該節說明兩者旋轉等價）。

- **順序實施（建議）**：本 change 直接複用已驗證等價性的推導，只需疊上單位錐至實際尺寸的縮放。
- **反序實施**：需在 picking 路徑先行寫一套旋轉推導，待 render 路徑改動後再同步驗證兩邊一致——重複工作且增加不一致風險。

該 change 已於其 proposal 的 Non-goals 與 design D7 明令不得移除 cone picking raycaster，即為保全本 change 的修復基礎。

## Risks / Trade-offs

- **[誤觸：cone 命中體積超出視覺體]** → D3 已論證圓錐恆內含於視覺圓台。仍需驗收確認錐體外側緊鄰的空白處不會命中，尤其在支撐點密集時。

- **[非均勻 scale 下 picking 與視覺錯位]** → cone 具方向性，是本 change 引入旋轉至 picking 路徑的原因。若沿用 sphere 路徑「不需要 non-uniform-scale 修正」的假設（`:2405-2407` 註解）會直接錯位。驗收須涵蓋 `(2,1,1)` 與 `(1,1,3)`。

- **[鏡像 instance 下錐體朝向反轉]** → `pick_matrix` 經 signed `get_matrix()` 保留鏡像，旋轉疊加後方向是否仍正確需實測驗證。

- **[`width_mm` 與視覺 robe 長度的對應]** → 已於 D2 修正：cone 高度改用 `fullwidth() - r_back_mm`（精確值，非近似），而非原提案的 `width_mm`。

- **[hover 優先順序改變]** → 兩個 raycaster 共用同一 `id = i`，命中任一皆回報同一點，`m_hover_id` 語意不變。風險低，但驗收仍須確認相鄰點重疊時的命中結果符合直覺（取最近者，由 `SceneRaycaster::hit()` 的 `is_closest` 既有邏輯處理）。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。屬 picking 行為修正，可直接隨版本發布。

回退策略：將 `.second` 的 `set_active()` 改回 `false` 即完全恢復現行行為，transform 設定成為無作用碼。回退粒度為單一函式內的數行。

## Open Questions

無。原提及「manual 點（`get_mesh_preview`）與 auto 點（`get_mesh`）robe 軸向長度公式不同，需分別處理」這個疑問已由 `fix-sla-support-preview-visual-parity`（2026-08-04 archive）連帶解決：該 change 將 `render_points()` 統一為所有點皆呼叫 `head_mesh_body(head, 24, /*preview=*/false)`，`get_mesh_preview()` 已整個移除，Points preview 渲染路徑不再有 `preview=true` 分支。因此本 change 的 cone 高度公式（D2）天然只需涵蓋單一情況，不需要依 `manual_add` 分別計算。
