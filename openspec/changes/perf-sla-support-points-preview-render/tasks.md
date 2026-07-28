## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認 `GLGizmoSlaSupports::render_points()`（`GLGizmoSlaSupports.cpp:645-728`）的 per-point 迴圈是唯一延遲來源；Structure 模式因 `:581` 的 view-mode gate early return 而不受影響，Points 與 Manual 共用同一迴圈故症狀一致
- [x] 1.2 確認 `:701-703` 每點每幀重建 pinhead 網格（`SupportTreeMesher.hpp:50` `head_mesh_local()`），steps=24 時單顆 head 約 910 三角形；contact sphere 啟用時再 `its_merge()` 一顆完整球
- [x] 1.3 確認 `:708-723` 每點每幀 `GLModel::reset()`（`GLModel.cpp:549`）+ `init_from()`（`GLModel.cpp:441`，展開為 `3×indices` 個 P3N3 頂點 + 全頂點 bbox merge）+ `render()` → `send_to_gpu()`；`init_from()` 開頭的 `assert(is_initialized())` 使每幀重建成為強制路徑
- [x] 1.4 量級估算：單點單幀約 65 KB VBO + 5.5 KB IBO（index 收斂為 USHORT）；N=500 時每幀約 35 MB（啟用 contact sphere 約 72 MB）buffer 配置／上傳／銷毀，另加約 136 萬次頂點展開與 bbox merge
- [x] 1.5 確認 `preview_sla_head_for_point()`（`:221`）每點呼叫 `process_top_float_live()` ×4 + `process_contact_type_is_sphere()`，contact sphere 啟用時再 +1；`process_top_float_live()`（`:129`）→ `Tab::get_field()`（`Tab.cpp:1452`）為跨全部 page 的線性搜尋
- [x] 1.6 確認 `:683` / `:686` 每點每幀呼叫 `MeshRaycaster::get_closest_point()`，非編輯模式無任何法線快取
- [x] 1.7 確認快取 key 收斂前提：`SupportPoint.hpp:38-52` 的 `SUPPORT_POINT_USE_PRESET = -1.f` 為 `head_penetration_mm` 預設值且 auto 路徑不覆寫，故非編輯模式下全部點收斂為 1 組幾何參數；編輯模式下僅 `manual_add && has_explicit_geometry()` 的點分岔
- [x] 1.8 確認 `GLModel::reset()` 與 `init_from()` 皆不重設 color，故共用模型後每點 `set_color()` 再 draw 不會互相污染

## 2. 幾何抽取（主要修正的前置）

- [ ] 2.1 於 `SupportTreeMesher.hpp` 抽出 `head_mesh_body(const Head &h, size_t steps, bool preview)`，回傳 local frame（原點在錨點、軸向 −Z）網格，即現行 `head_mesh_local()` 在 quaternion 旋轉與 `pos` 平移之前的結果（含 segment_len / z_shift 計算、`pinhead()` 或 `pinhead_preview()`、z 平移、contact sphere merge）
- [ ] 2.2 `head_mesh_local()` 改為呼叫 `head_mesh_body()` 再套用原本的 quaternion 旋轉與 `pos` 平移
- [ ] 2.3 驗證 `get_mesh(Head)` / `get_mesh_preview(Head)` 輸出與抽取前完全相同，且 `SupportTreeBuilder.cpp:133` 的切片路徑行為零變更

## 3. Pinhead GLModel 快取與 model matrix 擺放（主要修正）

- [ ] 3.1 新增 `HeadGeomKey`：`(r_pin_mm, r_back_mm, width_mm, penetration_mm, r_contact_mm, preview_flag)` 量化到 1e-4 mm 的整數 tuple，避免浮點作為 map key
- [ ] 3.2 `GLGizmoSlaSupports.hpp` 新增 `std::map<HeadGeomKey, GLModel> m_head_model_cache`
- [ ] 3.3 `render_points()` 迴圈改為查表取得 `GLModel&`；未命中才以 `head_mesh_body()` + `init_from()` 建構並插入
- [ ] 3.4 擺放改由 model matrix 承擔：`model_matrix = instance_matrix_no_scale * Translation(instance_scaling_matrix * sp.pos) * Rotation(FromTwoVectors(-UnitZ, scaled_normal))`；移除 `head.pos` 覆寫
- [ ] 3.5 **重算 `view_normal_matrix`**：改後 `model_matrix.linear()` 含擺放旋轉，必須依新的 `model_matrix` 重新取 `linear().inverse().transpose()`，不得沿用以 `instance_matrix_no_scale` 算出的舊值
- [ ] 3.6 保留 `scaled_normal` 的 inverse-transpose 修正（`normal_xform`，`:642`）於計算旋轉之前套用
- [ ] 3.7 快取筆數門檻設為 64；超過時整份 `clear()` 後重新填充，**不得**新增第二條逐點渲染路徑
- [ ] 3.8 `on_set_state()` 關閉 gizmo 時清空快取並釋放 GPU buffer
- [ ] 3.9 確認 `vol->is_left_handed()` 的 `glFrontFace()` 翻轉維持原位置，共用模型下仍正確
- [ ] 3.10 確認每點 `set_color()` 後 draw 的顏色行為與舊路徑一致

## 4. Process tab live 參數提到迴圈外

- [ ] 4.1 新增 `struct PreviewTopParams`，欄位對應 `support_segment_length` / `support_head_penetration` / `support_head_front_diameter` / `support_head_back_diameter` / `support_contact_diameter` / `support_contact_type is sphere`，保留現行 clamp 規則（`clamp_segment_length_mm` / `clamp_contact_depth` / `clamp_support_diameter_mm`）
- [ ] 4.2 新增 `static PreviewTopParams read_preview_top_params_live()`，內容為現行 `process_top_float_live()` / `process_contact_type_is_sphere()` / `default_contact_sphere_radius_mm()` 的一次性彙整
- [ ] 4.3 `preview_sla_head_for_point()` 新增接受 `const PreviewTopParams&` 的多載；保留現行簽章並轉呼叫新版，確保既有呼叫點不受影響
- [ ] 4.4 `render_points()` 於 per-point 迴圈前呼叫一次 `read_preview_top_params_live()`
- [ ] 4.5 `update_point_raycasters_for_picking_transform()`（`:2378`）於迴圈前呼叫一次
- [ ] 4.6 驗證：在 Process tab 直接修改文字欄位（尚未失焦）時，下一幀 preview 幾何仍即時更新，live 語意不得退化

## 5. Auto 點法線快取

- [ ] 5.1 `GLGizmoSlaSupports.hpp` 新增 `std::vector<Vec3f> m_normal_cache_normals`，與 `m_normal_cache` 一一對應
- [ ] 5.2 `render_points()` 非編輯模式改讀 `m_normal_cache_normals[i]`；僅在未填充（size 不符或值為 `Vec3f::Zero()`）時呼叫 `get_closest_point()` 並寫回
- [ ] 5.3 於所有取代 `m_normal_cache` 的位置清空該向量：`:2058`、`:2147`、`:2169`、`:2190`、`:2225`
- [ ] 5.4 `on_set_state()` 關閉 gizmo 時清空
- [ ] 5.5 編輯模式路徑維持既有 `m_editing_cache[i].normal == Vec3f::Zero()` 判斷，不變更語意
- [ ] 5.6 驗證：Apply 產生新點後法線正確重算，cone 朝向與修改前一致

## 6. Cone picking raycaster：保留，不得移除

- [x] 6.1 已決議**不移除**永遠 inactive 的 cone raycaster（見 design.md D7）。查證結果：`SceneRaycaster::hit()`（`SceneRaycaster.cpp:158-160`）在迴圈開頭即 `if (!item->is_active()) continue;`，停用項成本為零，且該迴圈不在每幀渲染路徑上——移除無任何效能效益
- [x] 6.2 已確認該 raycaster 並非冗餘，而是既有 picking 缺陷的修復基礎：`m_cone.mesh_raycaster`（`:543`，單位錐 `its_make_cone(1.0, 1.0, PI/12)`）雖已註冊（`:2364`）卻從未被賦予 transform（始終為 `Identity()`）也從未啟用（`:2412` 寫死 `set_active(false)`），導致命中判定僅由半徑約 `r_pin`（預設 0.2 mm）的 sphere 承擔，外露錐體無法被點選
- [ ] 6.3 實作時確認 `register_point_raycasters_for_picking()`（`:2351`）、`:651-653`、`:2412` 三處的 raycaster 註冊與存取結構**維持現狀**，不因本 change 的其他修改而被動更動
- [ ] 6.4 於 `update_point_raycasters_for_picking_transform()` 導入 `PreviewTopParams`（任務 4.5）時，確認未順帶改動 `m_point_raycasters[i].second` 的處理方式

## 7. 驗收

- [ ] 7.1 500 點 auto 生成後於 Points 模式旋轉視角，無可感知停頓
- [ ] 7.2 切換至 Manual Support 模式後旋轉視角，流暢度相同
- [ ] 7.3 穩態連續 60 幀不呼叫 `GLModel::reset()` / `init_from()`，`glGenBuffers` / `glBufferData` / `glDeleteBuffers` 次數為 0
- [ ] 7.4 連續拖曳 live 幾何參數時每幀 `init_from()` 呼叫次數為 1（非 500）
- [ ] 7.5 Structure 模式仍 early return，不建構網格、不查法線、不讀 live 參數
- [ ] 7.6 均勻 scale、非均勻 scale `(2,1,1)` 與 `(1,1,3)`、鏡像 instance 三類情境下，cone 位置、朝向與尺寸與修改前逐點路徑一致
- [ ] 7.7 鏡像 instance 下 front face 不黑、無法線反轉著色錯誤
- [ ] 7.8 Clipping 開啟時逐點 clipped 判定與 raycaster active 狀態不變
- [ ] 7.9 Undo / Redo 後 preview 與 picking 仍正確，不顯示過期幾何
- [ ] 7.10 切片輸出與修改前完全相同（`head_mesh_body()` 抽取無副作用）

## 8. Follow-up（out of scope）

- GPU instancing（單次 draw call 繪製全部 cone）— 若本 change 之後仍不足再評估
- View frustum culling 與點數過多時的 LOD（steps 24 → 12）
- `Tab::get_field()` 全域加索引（影響整個 Tab 系統，風險與範圍遠大於本 change）
- Structure mode undo/redo async reslice 不刷新（[KB-2]）→ 沿用既有 `fix-sla-supports-structure-view-undo-refresh` 候選
- Pinhead 外露錐體無法被點選 → `fix-sla-support-point-cone-picking`。**建議於本 change 之後實施**：該 change 的 cone picking transform 需要本 change design D2 建立的擺放旋轉 `q`，順序實施可直接複用已驗證等價性的推導
