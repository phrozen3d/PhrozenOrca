## Why

極薄模型（例如 Ø21 × 厚 0.2 mm 的圓片）在 PhrozenOrca Resin 模組切片時會連續踩到三個缺陷，最終結果是「支撐完全生不出來」或「支撐頭刺穿模型頂面」，而使用者從錯誤訊息與畫面上都看不出真正原因。

**缺陷一：支撐點被吸附到模型頂面。** `move_on_mesh_surface()` 把支撐點投影到模型表面時，只比較幾何距離、不看命中面朝向。當 island 取樣層落在薄板中面之上時，「最近的面」變成上表面，全部支撐點被吸到模型頂部。這些點的法線為 `(0,0,+1)`、polar = 0，隨即在角度過濾階段被整批砍掉，`Automatic support points: 0`，切片作廢。失敗與否只取決於切片網格相位——僅調整 `support_object_elevation` 的小數位（與支撐語意毫無關聯）即可翻盤，使用者無從得知原因。

**缺陷二：支撐頭刺穿模型。** `support_head_penetration` 是純設定純量，底層完全沒有厚度感知。設定刺入深度大於局部壁厚時，支撐頭直接貫穿承載面另一側，在 0.2 mm 薄板上留下明顯凸點。

**缺陷三：GUI 預覽與實際切片不一致。** 支撐點預覽（`GLGizmoSlaSupports::render_points()`）自行依「支撐點 + 目前參數」組出支撐頭來畫，不經過支撐樹。即使切片端修好了防貫穿，畫面仍會顯示刺穿，使用者無法在下刀前判斷支撐是否正確。

三者互為前置：不修缺陷一，薄模型連支撐都沒有，缺陷二觀察不到；不修缺陷三，缺陷二的修復在使用者眼中等於沒發生。

## What Changes

**納入範圍**

- **支撐點表面投影加入方向性約束**：`move_on_mesh_surface()` 改為優先吸附至**朝下的承載面**（命中三角形幾何面法線 z 分量為負），而非幾何最近面。兩面皆朝下時取較近者；兩面皆非朝下（垂直壁、退化幾何）時回退至既有「取較近者」行為，保證常態幾何逐點不變。此為治本項，缺少它其餘修復對極薄模型皆無效。

- **切片端動態防貫穿夾限**：於支撐頭幾何提交當下，以射線沿支撐頭軸線量測局部可用深度，並依 `front_depth = min(configured_front_depth, local_thickness × 0.5)` 夾限。採「上表面零凸點優先」政策，明確接受極薄件因咬合深度不足、列印時可能脫落的物理代價。射線無命中（破面／非流形網格）時 fail-safe 為 0，並輸出彙總警告日誌。

  - **Contact Sphere 適配**：PhrozenOrca 的 `Head::penetration_mm` 是**已換算的網格空間值**（`front + r_pin − r_contact`），非刺入深度本身。夾限對象為 front depth，再經既有的 `point_head_penetration_mesh_mm()` 換算寫回。此換算使「最深點恰等於 front depth」的不變量在開／關接觸球兩種情形下皆成立。

  - **Branching 樹適配**：夾限適用於 Default 樹與 Branching/Organic 樹的主頭與錨點，共四個提交點。Branching 樹的錨點夾限**必須**施加於 `add_anchor()` 呼叫處，不得於讀取 `junction_point()` 建立橋接端點之前施加，否則橋接可行性檢查結果改變。

  - **時序**：夾限於所有角度搜尋（optimizer）完成後僅執行一次，搜尋過程全程使用設定值，搜尋行為完全不變。

- **`taildir` 正規化**：`connect_to_model_body()` 的 `Vec3d taildir = endp - hitp;` 未正規化。`IndexedMesh::query_ray_hit()` 具單位向量前置條件，且該斷言在 Release 建置下不存在，傳入非單位向量會使回傳距離為參數 t 而非實際長度。此為既有缺陷修正，**且是防貫穿量測的硬性前置**。

- **GUI 預覽自洽防貫穿**：預覽端獨立套用同一套夾限，保證「畫出來的支撐頭不穿透畫出來的模型」。
  - 重新啟用 `CommonGizmosDataObjects::HollowedMesh`（目前已無任何 gizmo 需要它，`on_update()` 從不執行），使預覽量測所用網格包含中空與鑽孔結果，與切片端輸入一致。
  - 沿預覽自身繪製的軸向量測，並共用 `point_head_penetration_mesh_mm()` 換算，確保兩端公式同源。
  - **快取優化**：刺入深度只造成支撐頭網格的**剛體平移**，不改變形狀。因此將 `penetration` 移出 `HeadGeomKey`，改由 model matrix 的平移量承載，使逐點夾限不造成任何快取失效；`m_point_raycasters` 的碰撞體位置同步平移。

**排除範圍（附理由）**

- **`allowed_move` 越界讀取（來源 #2）**：PhrozenOrca 已有防護，且其替代值為 `support_head_front_diameter`（典型 0.4 mm），較來源方案的 `layer_height` 更寬鬆、更不易落入未加方向性約束的 `squared_distance` 分支。**維持現狀不動**，並將此判斷寫入驗收條件。
- **`filter_support_points_by_modifiers()` 無聲丟棄（來源 #3）**：PhrozenOrca 無此函式，改以角度過濾承接，無對應程式碼。
- **切片失敗訊息改寫（來源 #4）**：PhrozenOrca 的訊息與來源不同，無對應程式碼。

**方法：雙軌幾何判定，共用換算邏輯。** 切片端依真實支撐頭軸向 `nn`（經角度飽和與 optimizer 調整後）量測；GUI 端依預覽自身軸向（表面法線）量測。兩端各自量測而非共用計算結果——因為 PhrozenOrca 未定義 `SUPPORT_BACKGROUND_PROCESSING`，`reslice_SLA_until_step()` 會將管線硬性截斷於指定步驟，而支撐 gizmo 的主要工作流僅叫到 `slaposSupportPoints`，防貫穿夾限所在的 `slaposSupportTree` 不會執行；任何跨管線的計算結果回寫通道在該工作流下恆為空。兩端共用 front depth 定義與 `point_head_penetration_mesh_mm()` 換算，殘差僅來自軸向差異——而預覽軸向與真實支撐頭軸向的落差為本變更之前既已存在的獨立議題。

**不改變**：任何設定參數的語意、預設值、preset 檔案與 UI 版面；不新增設定項目；不修改 `SupportTreeMesher` 的網格生成邏輯；不改變支撐樹的路由與互連演算法、pad 生成、光柵化與 PRZ 輸出。**無 BREAKING**。

## Capabilities

### New Capabilities
- `sla-support-point-placement`: 規範 SLA 自動支撐點由取樣層投影至模型實際表面的行為契約——支撐點必須落在朝下的承載面而非幾何最近面；規範切片網格相位、層高與模型厚度之間的邊界條件下支撐點必須維持穩定；規範常態模型逐點不變的驗收條件，以及 `allowed_move` 現行替代值維持不變的理由。
- `sla-support-head-penetration`: 規範切片端支撐頭刺入模型的深度契約——依局部可用深度動態夾限 front depth，保證支撐頭幾何不穿透承載面另一側；規範沿頭軸的單位向量射線量測與退化情形的 fail-safe；規範夾限施加時序（角度搜尋完成後）；規範 Contact Sphere 的換算不變量；規範 Default 與 Branching 兩樹、主頭與錨點共四個提交點的拓撲影響邊界。
- `sla-support-preview-penetration`: 規範 GUI 支撐點預覽的防貫穿契約——預覽支撐頭不得穿透預覽模型；規範量測所用網格必須包含中空與鑽孔結果；規範量測軸向為預覽自身繪製軸向；規範與切片端共用 front depth 換算；規範量測結果不可寫入 `SupportPoint` 的任何持久化欄位；規範網格不可用時的樂觀降級行為。

### Modified Capabilities
- `sla-support-points-preview-performance`: 現行需求明定支撐頭 `GLModel` 快取的 key 包含 `penetration`。逐點夾限會使該 key 幾乎每點一個而導致快取失效，故需求變更為：key **不含** `penetration`，改由 model matrix 平移承載；並補上「逐點刺入深度差異不得造成快取失效」與「`m_point_raycasters` 碰撞體位置必須與平移後的可見幾何一致」兩項約束。

## Impact

**切片核心（libslic3r）**

- `src/libslic3r/SLA/SupportPointGenerator.cpp` — `move_on_mesh_surface()`：命中面選擇加入方向性約束。
- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` — 主頭接受區塊、`connect_to_model_body()` 的 `taildir` 正規化與 `add_anchor()` 呼叫處：Default 樹主頭與錨點夾限。
- `src/libslic3r/SLA/SupportTreeUtils.hpp` — `optimize_pinhead_placement()` 接受區塊：Branching 樹主頭夾限。
- `src/libslic3r/SLA/BranchingTreeSLA.cpp` — `m_builder.add_anchor(*anchor)` 呼叫處：Branching 樹錨點夾限（時序為硬性要求）。
- `src/libslic3r/SLA/SupportPoint.hpp` — `point_head_penetration_mesh_mm()` 為兩端共用的換算入口，**不修改其語意**。

**GUI（slic3r）**

- `src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp` — `on_get_requirements()`：加回 `HollowedMesh`。
- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `render_points()`、`head_geom_key()`、`preview_sla_head_for_point()`、`update_point_raycasters_for_picking_transform()`：預覽端量測、夾限、快取 key 調整與碰撞體同步。
- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` — `HeadGeomKey` 移除 `penetration` 欄位；新增每點厚度快取。

**下游行為（無程式碼修改，但結果改變）**

- `SLAPrint::validate()`：極薄模型不再誤報 `There are unprintable objects`。
- `SLAPrint.cpp` 的 elevation 驗證使用全域 `head_fullwidth()`，per-head 夾限會使該估算略偏樂觀。影響輕微，需記錄。

**已知殘留風險（皆為所選政策的有界後果）**

- 支撐頭淨空檢查所用的 `w` 以未夾限值驗證，提交後頭部向外多伸出 `(configured − clamped)`，上界為 `configured_penetration`。
- Fail-safe 使破面模型支撐零咬合；以彙總警告日誌使其可診斷，不靜默。
- 夾限僅在 `局部可用深度 >= 2 x configured_penetration` 時完全失效。可用深度落於 `(0, 2 x configured)` 區間的模型會被夾限影響，此為預期行為；**不得宣稱「常態零影響」**，須表述為「可用深度 >= 2 x configured 時零影響」。
- GUI 與切片端的量測軸向不同（表面法線 vs optimizer 調整後的頭軸），夾限值可能有殘差。此落差在本變更之前既已存在於預覽的繪製方向，屬獨立議題。
- 重新啟用 `HollowedMesh` 會複製一份網格並由 GUI 自建 AABB 樹（切片端的 `m_supportdata->emesh` 為私有，無對外介面可借用）。
- 鏡像物件（`SLAPrintObject::set_trafo()` 的 `left_handed`）與非等比縮放會影響方向與距離的座標映射，需個別處理。

**與上游的關係**

三項缺陷均存在於 upstream PrusaSlicer 2.9.6。本變更為 fork 端的獨立修復，未來 rebase 時需檢查上游是否已修正，以避免衝突或重複修補。
