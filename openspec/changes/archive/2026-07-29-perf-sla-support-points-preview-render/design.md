## Context

`GLGizmoSlaSupports::render_points()` 目前的 per-point 迴圈把「幾何生成」與「擺放」綁在一起：`preview_sla_head_for_point()` 產生的 `sla::Head` 帶有 `pos` 與 `dir`，`sla::get_mesh()` 內部的 `head_mesh_local()` 在回傳前就把旋轉與平移**烘進頂點座標**（`SupportTreeMesher.hpp:75-81`）。因為每個點的 `pos` / `dir` 都不同，烘進去的結果自然人人不同，於是 GLModel 無法共用，只能每點每幀 `reset()` + `init_from()` 重建。

```
現行（每點每幀）
┌──────────────────────────────────────────────────────────────┐
│ preview_sla_head_for_point(sp, normal, ...)                   │
│   └─ process_top_float_live() ×4~6  ← Tab 線性搜尋 + WM_GETTEXT│
│ head.pos = S * sp.pos                                         │
│      ↓                                                        │
│ sla::get_mesh(head, 24)                                       │
│   └─ head_mesh_local():                                       │
│        pinhead() → sphere()×2 + robe + disc   ~910 tri        │
│        [+ contact sphere → its_merge()]                       │
│        v = quat(−Z→dir) * v + head.pos    ← 擺放烘進頂點      │
│      ↓                                                        │
│ m_cone.model.reset()        → glDeleteBuffers ×2              │
│ m_cone.model.init_from(its) → 展開成 3×indices 個 P3N3 頂點   │
│                                + 全頂點 bbox merge            │
│ m_cone.model.render()       → glGenBuffers ×2 + glBufferData  │
└──────────────────────────────────────────────────────────────┘
                    N = 500 → 每幀 ~35 MB（含 contact sphere ~72 MB）
```

本設計的核心是把這條鏈從中間切開：**幾何只依賴尺寸參數（可共用），擺放只依賴 `pos` / `dir`（改由 model matrix 承擔）**。

現行行為受 `fix-sla-support-points-preview-mode-gate` 建立的座標約定拘束（`:614-642` 的註解為其規範來源）：錨點跟隨 instance scale，cone 尺寸維持 mm，picking 使用同一套拆解。本設計必須在這個約定下維持逐點等價。

## Goals / Non-Goals

**Goals:**

- 穩態下（幾何參數與點集未變動）每幀不再呼叫 `GLModel::reset()` / `init_from()` / `send_to_gpu()`，收斂為每點一次 uniform 設定 + 一次 draw call。
- Process tab live 參數讀取與 AABB 法線查詢次數不隨 N 成長。
- Points 模式與 Manual 編輯模式同時獲得改善（兩者共用同一迴圈）。
- 渲染輸出與命中判定與最佳化前**逐點等價**，包含非均勻 scale 與鏡像 instance。

**Non-Goals:**

- GPU instancing（單次 draw call 繪製全部 cone）、LOD、frustum culling。
- 修改 `head_mesh_local()` / `get_mesh(Head)` / `get_mesh_preview(Head)` 的對外行為或切片管線。
- 為 `Tab::get_field()` 建立全域索引。
- 改變 Structure 模式、支撐生成 backend、Pad / Hollow / Drill 路徑。
- 修正「外露錐體無法被點選」的 picking 缺陷，或移除目前永遠 inactive 的 cone picking raycaster（見 D7）。

## Decisions

### D1. 從 `head_mesh_local()` 抽出 local-frame 幾何，而非新寫一份

`head_mesh_local()` 的前半段（segment_len / z_shift 計算、`pinhead()` 或 `pinhead_preview()`、z 平移、contact sphere merge）**只依賴 `h` 的尺寸欄位**，與 `pos` / `dir` 無關；旋轉平移集中在最後三行。抽出 `head_mesh_body(const Head&, size_t steps, bool preview)` 回傳該中間結果，`head_mesh_local()` 改為呼叫它再套用原本的 quaternion 與平移。

- **為何不另寫一份 preview 專用 mesher**：會產生兩份必須同步維護的幾何邏輯，preview 與切片結果將來必然漂移。抽取共用函式讓切片路徑逐位元不變。
- **驗證要求**：`get_mesh(Head)` / `get_mesh_preview(Head)` 與 `SupportTreeBuilder.cpp:133` 的呼叫結果必須完全不變。

### D2. 擺放改由 model matrix 承擔（等價性推導）

這是本設計最容易做錯的部分，等價性必須逐項成立。

令：
- `M_ns` = `instance_matrix_no_scale` = `T_zshift · T · R · mirror`
- `S` = `instance_scaling_matrix`
- `q` = `Quaternion::FromTwoVectors(−UnitZ, scaled_normal)`
- `v_local` = `head_mesh_body()` 的頂點（local frame，錨點在原點）

**現行**每個頂點的世界座標：

```
head.pos = S · sp.pos
v_world  = M_ns · ( q · v_local + S · sp.pos )
```

**改後**：

```
model_matrix = M_ns · Translation(S · sp.pos) · Rotation(q)
v_world      = M_ns · Translation(S · sp.pos) · Rotation(q) · v_local
             = M_ns · ( q · v_local + S · sp.pos )      ✓ 逐點恆等
```

代數上完全相同，不是近似。`scaled_normal` 的 inverse-transpose 修正（`normal_xform`，`:642`）仍在計算 `q` 之前套用，語意不變。

**連帶必須修正的一項**：`view_normal_matrix`（`:718-719`）目前由 `model_matrix.linear()` 推導。改後 `model_matrix.linear()` 從 `M_ns.linear()` 變成 `M_ns.linear() · q`，其 inverse-transpose 必須重新計算。因為 `q` 是純旋轉（正交），`(M·q)^{-T} = M^{-T} · q`，可沿用既有寫法直接對新的 `model_matrix` 取 `linear().inverse().transpose()`，不需特別處理；但**不可**繼續沿用以 `instance_matrix_no_scale` 算出的舊值。這是實作時最可能遺漏的一行。

### D3. 快取結構：以量化參數為 key 的 map，不設「退回逐點」的第二條渲染路徑

Key 為 `(r_pin_mm, r_back_mm, width_mm, penetration_mm, r_contact_mm, preview_flag)`，浮點量化到 1e-4 mm 後以整數 tuple 比較，避免浮點作為 map key。

- **為何 key 必須含 `preview_flag`**：`manual_add` 點走 `get_mesh_preview(head, 45)`，auto 點走 `get_mesh(head, 24)`，兩者 segment_len / z_shift 公式與 steps 皆不同，幾何不可共用。
- **為何不用單槽快取**：auto 點與帶 explicit geometry 的 manual 點在編輯模式下會交錯出現，單槽會 thrash 回每幀重建。
- **為何不保留「超過上限則退回逐點建構」**：那會製造第二條渲染路徑，必須額外證明其視覺結果與快取路徑一致——換來的是一條難以測試、實務上幾乎不會觸發的分支。key 數的天然上界是「1（全部 auto 點共用）+ 相異 manual explicit 幾何組數」，而 manual 點是使用者逐個手動放置的，數量本就有限。
- **改採的防禦**：設一個寬鬆的筆數門檻（建議 64）。超過時**整份 `clear()` 後重新填充**，而非切換渲染路徑。行為單一、無需額外等價性驗證，最壞情況退化為現行效能，不會更差。

### D4. 快取失效點集中管理

失效時機：
- `on_set_state()` 關閉 gizmo → 清空（同時釋放 GPU buffer）。
- `m_normal_cache` 內容被取代（`:2058`、`:2147`、`:2169`、`:2190`、`:2225`）→ 清空法線快取。
- live 幾何參數變動 → key 自然不命中；舊 key 由 D3 的門檻機制淘汰。

法線快取採 `std::vector<Vec3f>` 與 `m_normal_cache` 一一對應，以「size 不符或值為 `Vec3f::Zero()`」判定未填充。編輯模式維持既有 `m_editing_cache[i].normal` 語意，不動。

- **為何不用 dirty flag 追蹤 live 參數**：參數來源是 Process tab 的即時欄位值，沒有可靠的變更通知；以 key 比對本身就是最直接的偵測方式，且天然正確。

### D5. 顏色與 front face 在共用模型下的正確性

- **顏色**：已確認 `GLModel::reset()`（`GLModel.cpp:549-567`）與 `init_from()` 皆不重設 color，因此現行 `set_color → reset → init_from → render` 是有效的。改為共用模型後，每點 `set_color()` 再 draw 的行為與現行完全等價，不會互相污染——color 是 draw 當下讀取的 uniform，非模型的持久狀態。
- **front face**：`vol->is_left_handed()` 的 `glFrontFace(GL_CW)` 翻轉是 GL state，與模型是否共用無關，維持原位置即可。

### D6. live 參數一次性讀取的 live 語意保留

新增 `PreviewTopParams` 純資料結構與 `read_preview_top_params_live()`，內容即現行 `process_top_float_live()` / `process_contact_type_is_sphere()` / `default_contact_sphere_radius_mm()` 的彙整，clamp 規則（`clamp_segment_length_mm` / `clamp_contact_depth` / `clamp_support_diameter_mm`）完全不變。`preview_sla_head_for_point()` 新增接受該結構的多載，保留現行無參數版本轉呼叫新版，既有呼叫點不受影響。

因為讀取仍發生在**每幀**（只是每幀一次而非每點一次），使用者在 Process tab 修改文字欄位、尚未失焦時，下一幀 preview 仍即時更新——live 語意不退化。

### D7. 保留永遠 inactive 的 cone picking raycaster（已決議，不再是待決項）

初版設計曾把「移除永遠 inactive 的 cone raycaster」列為順帶清理。經查證後**推翻該想法，並明令禁止**：

- **效益實測為零。** `SceneRaycaster::hit()`（`SceneRaycaster.cpp:158-160`）的迴圈開頭即 `if (!item->is_active()) continue;`，停用項連 ray 測試都不會執行；且該迴圈只在滑鼠移動時跑，不在每幀渲染路徑上。與本 change 要解決的 lag 無關。
- **它不是冗餘，是缺失的一半。** `m_sphere.mesh_raycaster` 與 `m_cone.mesh_raycaster` 皆為單位幾何（`:535-544`，`its_make_sphere(1.0, PI/12)` 與 `its_make_cone(1.0, 1.0, PI/12)`），沿襲上游「sphere 管球、cone 管錐體」的分工。本 fork 註冊了 cone（`:2364`）卻從未給它 transform（始終為註冊時的 `Identity()`）也從未啟用（`:2412` 寫死 `set_active(false)`），導致命中判定只剩半徑約 `r_pin`（預設 0.2 mm）的球——使用者只能點到 pinhead 頂端，長約 `width_mm`（預設 2 mm）的外露錐體完全點不到。此為已回報的缺陷。
- **移除會破壞後續修復。** 該缺陷將由獨立的 correctness change 修正，作法正是「為 cone raycaster 補上 transform 並啟用」。若本 change 先行移除，後續必須整個加回，且兩個 change 的 revert 邊界會互相糾纏。

**與後續修復的介面**：cone 的 picking transform 需要擺放旋轉（cone 有方向，sphere 各向同性故現行 picking 不需要，見 `:2405-2407` 註解）。D2 建立的 `M_ns · Translation(S · sp.pos) · Rotation(q)` 正是它所需的矩陣，後續 change 可直接複用本 change 已驗證等價性的推導，再疊上單位錐至實際尺寸的縮放。這是本 change 應先於該修復實施的主要理由。

## Risks / Trade-offs

- **[等價性回歸：非均勻 scale 或鏡像下 cone 位置／朝向偏移]** → D2 的推導為代數恆等，但實作可能漏掉 `view_normal_matrix` 的重算（見 D2 末段）。驗收必須涵蓋均勻 scale、非均勻 scale `(2,1,1)` 與 `(1,1,3)`、鏡像 instance 三類情境，逐點比對修改前後的世界座標。

- **[快取失效遺漏 → 顯示過期幾何]** → 這是本設計最可能造成使用者可見錯誤的失敗模式（例如 Apply 產生新點後仍顯示舊尺寸）。緩解：失效點集中於 D4 列出的位置；驗收明列 Apply、Undo / Redo、切換 Auto / Manual 三個情境。

- **[live 參數連續變動（拖曳 slider）時 key 每幀改變]** → 退化為每幀重建一份幾何（**一份**，非 N 份），成本約為現行的 1/N，仍遠優於現況。可接受，不額外處理。

- **[VRAM 累積]** → key 數天然有界（D3），加上門檻清空與 gizmo 關閉時釋放。單一 head 模型約 65 KB VBO + 5.5 KB IBO，即使 64 筆亦僅約 4.5 MB。

- **[`head_mesh_body()` 抽取影響切片結果]** → 純函式拆分，無邏輯變更；風險低但後果嚴重（會改變實際列印的支撐幾何）。驗收要求確認切片管線輸出不變。

- **[誤刪 cone picking raycaster]** → 見 D7。該註冊看似冗餘（永遠 inactive），實為已回報 picking 缺陷的修復基礎。若在本 change 中順手移除，後續 correctness fix 必須整個加回，且會使兩個 change 的 revert 邊界互相糾纏。已於 proposal 的 Non-goals 明列禁止。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更，屬純渲染路徑最佳化，可直接隨版本發布。

回退策略：四項修改彼此獨立，可個別 revert。主要修正（幾何／GLModel 快取）若出現等價性問題，單獨 revert 該項即恢復現行行為，其餘三項不受影響。

## Open Questions

無。原本待決的「是否移除永遠 inactive 的 cone raycaster」已於 D7 決議為**不移除**，並在 proposal 的 Non-goals 明列禁止。
