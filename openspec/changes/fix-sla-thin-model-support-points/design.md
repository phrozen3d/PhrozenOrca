## Context

本設計對應 proposal 的三組缺陷：支撐點被吸附至頂面、切片端支撐頭貫穿承載面、GUI 預覽與切片結果不一致。

### 現行資料流程與失效點

```
slice_model()                                    SLAPrintSteps.cpp:690
  |  minZ = bb.min(Z) - get_elevation()          <- 切片網格原點綁 elevation（相位來源）
  +-> m_model_height_levels = [slindex_it .. end]
  +-> m_supportdata = SupportData(get_mesh_to_print())   <- emesh：已中空、已鑽孔、已套 trafo
  +-> prepare_for_generate_supports()

support_points()                                 SLAPrintSteps.cpp:926
  +-> generate_support_points(...)               -> 正常產出
  +-> allowed_move = (levels.size() > 1) ? dLevel : support_head_front_diameter   <- 已有防護
  +-> move_on_mesh_surface(pts, emesh, allowed_move)
  |   X「最近面」無方向性 -> 點被吸到上表面                        <- 缺陷一 ★根因
  +-> Phase 3：以 support_critical_angle 過濾
      被吸到頂面的點法線為 (0,0,+1)、polar = 0 -> 整批被砍 -> 0 點

support_tree()                                   SLAPrintSteps.cpp:983
  +-> remove_bottom_points()（零抬升模式，會刪元素）
  +-> create_support_tree()
        X penetration 為純設定純量，無厚度感知 -> 刺穿頂面         <- 缺陷二
        X taildir 未正規化 -> query_ray_hit() 前置條件違反         <- 缺陷二前置

GLGizmoSlaSupports::render_points()              GLGizmoSlaSupports.cpp:675
  +-> preview_sla_head_for_point(sp, scaled_normal, live_preset)
        X 自行組頭，不經支撐樹 -> 切片端修好也照樣畫出刺穿         <- 缺陷三
```

### 關鍵約束

1. **`Head::penetration_mm` 是網格空間值，不是刺入深度。** 開接觸球時 `point_head_penetration_mesh_mm()` 會做 `front + r_pin - r_contact` 的換算。夾限對象必須是 front depth，不能直接夾 `penetration_mm`。

2. **切片管線在 GUI 工作流下會被硬性截斷。** 本 fork 未定義 `SUPPORT_BACKGROUND_PROCESSING`，`Plater::priv::background_processing_enabled()` 恆為 false，導致 `reslice_SLA_until_step()` 設定 `task.to_object_step = step` 而停在該步。支撐 gizmo 的主要路徑（`GLGizmoSlaSupports.cpp:2310`、`:2529` 於 Points 檢視）只叫到 `slaposSupportPoints`，`slaposSupportTree` 不執行。任何「切片端算完回寫給 GUI」的通道在該工作流下恆為空。

3. **切片端的 AABB 加速結構借不到。** `SLAPrintObject::m_supportdata` 為 private，無對外 accessor。GUI 必須自建。

4. **`CommonGizmosDataObjects::HollowedMesh` 目前是死碼。** `GLGizmoSlaBase::on_get_requirements()` 與 `GLGizmoHollow` 都已將其移除，`on_update()` 從不執行。

5. **`SLAPrintObject::set_trafo(trafo, left_handed)` 帶有 handedness 旗標**，代表鏡像變換確實會發生。

6. **支撐點座標系不同。** GUI 的 `sp.pos` 在 ModelObject 局部座標；切片端 `m_supportdata->pts` 已由 `transformed_support_points()` 套上 `trafo()`。

### 建置與測試環境結論（Phase 0 實測，2026-08-26）

實作前的環境勘查得到三項會影響本設計執行方式的結論，記錄於此以免日後重新踩坑。

**1. `tests/sla_print` 已失效，不納入本變更範圍。** `tests/CMakeLists.txt` 的 `add_subdirectory(sla_print)` 自 OrcaSlicer 上游 commit `b9e6108be`（"Feature/re enable tests"）起即被註解，該次只重新啟用 libnest2d / libslic3r / slic3rutils / fff_print。即使取消註解也無法編譯：`sla_test_utils.cpp` 與 `sla_supptgen_tests.cpp` 使用已被移除的 `sla::SupportPointGenerator` 類別與其 `::Config`，而本 fork 已遷移至自由函式 API（`prepare_generator_data()` / `generate_support_points()` / `SupportPointGeneratorConfig`）。**復活該目錄等同重寫整份測試工具，列為未來獨立變更的候選，不在本變更範圍內。**

**2. 本變更的新測試一律放進 `tests/libslic3r/`（target `libslic3r_tests`）。** 該 target 啟用中且正常建置，並已有 SLA 測試先例 `tests/libslic3r/sla_contact_spacing_tests.cpp`；SLA 程式碼本就編譯進 `libslic3r`，測試可直接 include `libslic3r/SLA/*.hpp`。撰寫須遵循 `tests/CLAUDE.md`（浮點比較用 `WithinAbs` / `WithinRel`，禁用 `Approx`；迴圈內 SECTION 用 `DYNAMIC_SECTION`）。

**3. Resin 建置目錄的相依函式庫以 directory junction 共用。** `deps/build-resin-dbginfo/` 原為空目錄，導致 `build_resin_release_vs2022.bat` 的 `CMAKE_PREFIX_PATH` 指向不存在的路徑而 configure 失敗。deps 均為第三方函式庫，`PHROZEN_ORCA_ENABLE_RESIN` 只影響應用程式碼，且兩個建置目錄的 deps 旗標一致（`DEP_DEBUG=OFF`、`PHROZEN_ORCA_INCLUDE_DEBUG_INFO=ON`、`RelWithDebInfo`），故以 junction 指向 `deps/build-dbginfo/PhrozenOrca_dep` 共用。另需注意批次檔從不傳 `-DSLIC3R_BUILD_TESTS=ON`，設定須手動執行 cmake（完整指令見 `tasks.md`）。

**回歸基線**：`build-resin-dbginfo` 於未修改任何程式碼的狀態下，`libslic3r_tests` 共 90 個測試，**85 通過、5 個既有失敗**。後續每個 Phase 的驗證皆以此為比較基準——判準是「無新增失敗」，而非「全數通過」。

## Goals / Non-Goals

**Goals:**

- 支撐點的表面投影結果**與切片網格相位無關**——同一模型在任意 `support_object_elevation` 相位下產出一致的支撐點集合。
- 切片端支撐頭幾何**不穿透承載面另一側**，在任意局部可用深度下皆成立，且在開／關 Contact Sphere、Default／Branching 兩樹下一致。
- GUI 預覽的支撐頭**不穿透預覽模型**，且量測輸入（網格）與切片端同源。
- 逐點刺入深度差異**不造成任何 GLModel 快取失效**。
- 量測與夾限**完全不寫入 `SupportPoint` 的任何持久化欄位**。
- 常態模型（可用深度 >= 2 x configured）的支撐點與支撐頭幾何**逐點不變**。

**Non-Goals:**

- 不改變支撐樹的路由、互連、pillar/bridge 演算法。
- 不改變切片網格的建構方式（`minZ` 綁 elevation 維持原狀）。相位敏感性由下游方向性約束吸收。
- 不新增設定參數、不改變既有參數語意或預設值、不動 UI 版面。
- 不修改 `SupportTreeMesher` 的網格生成邏輯。防貫穿完全由夾限 front depth 達成。
- 不解決「極薄件咬合不足可能脫落」的物理問題——明確接受此代價（D2）。
- 不修正「預覽軸向（表面法線）與真實支撐頭軸向（optimizer 調整後）不同」這項既有落差——屬獨立議題。
- 不修正「Branching 樹不讀 per-point 覆寫值」這項既有落差。
- 不移植來源變更的 #2、#3、#4（理由見 proposal）。

## Decisions

### D1｜表面吸附改用「方向性優先」而非「距離優先」

`move_on_mesh_surface()`（`SupportPointGenerator.cpp:1304-1305`）現行只比較距離：

```cpp
IndexedMesh::hit_result &hit =
    (!down || hit_up.distance() < hit_down.distance()) ? hit_up : hit_down;
```

支撐點的語意是「從下方頂住模型」，正確目標面是**朝下的面**，與距離無關。改為三層決策：

```
1. 命中面幾何法線 z 分量為負者（朝下面）優先
2. 兩者皆朝下 -> 取較近者
3. 兩者皆非朝下（垂直壁、退化幾何）-> 回退至現行「取較近者」
```

判準使用 `IndexedMesh::hit_result::normal()`（`IndexedMesh.hpp:99`），其值來自 `normal_by_face_id()`，為**幾何面法線**。面法線不隨射線方向翻轉，因此「從內側往上命中上表面」回傳的仍是 `(0,0,+1)`，可正確被排除。

第 3 層是刻意保留的相容出口：常態幾何下規則 1 與現行行為結果本就一致（支撐點產生於零件最低層，向下命中必為朝下面且距離較近），故常態路徑逐點不變。

*替代方案*：改為只往下打單一射線。**否決**——會喪失「點落在模型外側時往上拉回」的能力，屬行為縮減。

### D2｜夾限對象為 front depth，並以「實際最深點」為準

由 `pinhead()`（`SupportTreeMesher.cpp:158`）與 `head_mesh_body()`（`SupportTreeMesher.hpp:61`）推導。

`pinhead()` 局部座標：back 球心 z = 0；pin 球心 z = h = r_back + r_pin + length；pin 球頂 = h + r_pin。

`head_mesh_body()` 非預覽分支傳入 `length = h.width_mm`，並平移 `z -= fullwidth() - r_back`，其中
`fullwidth() = real_width() - pen = (2*r_pin + width + 2*r_back) - pen`，故
`z_shift = 2*r_pin + width + r_back - pen`。

```
平移後 pin 球頂 = (r_back + r_pin + width + r_pin) - (2*r_pin + width + r_back - pen) = pen
```

接觸球（`SupportTreeMesher.hpp:77-84`，條件為 `r_contact > r_pin`）球心 z = `pen - r_pin`，球頂 = `pen - r_pin + r_contact`。代入 `pen = front + r_pin - r_contact`（`point_head_penetration_mesh_mm()`）得 **球頂 = front**。

因此最深點的統一公式為：

```
offset(r_pin, r_contact) = (r_contact > EPSILON && r_contact <= r_pin) ? (r_pin - r_contact) : 0
deepest = front + offset
```

三種情形：

| 情形 | pen（網格空間） | 實際最高幾何 | deepest |
|---|---|---|---|
| 無接觸球（`r_contact <= EPSILON`） | `front` | pin 球頂 | `front` |
| 接觸球有效（`r_contact > r_pin`） | `front + r_pin - r_contact` | 接觸球頂 | `front` |
| 退化帶（`0 < r_contact <= r_pin`） | `front + r_pin - r_contact` | pin 球頂（接觸球不生成） | `front + r_pin - r_contact` |

**退化帶是既有的不一致**：`point_head_penetration_mesh_mm()` 只要 `contact_r > EPSILON` 就無條件換算，但 `head_mesh_body()` 只在 `r_contact > r_pin` 時才附加接觸球。本設計**不修正該不一致**，但夾限必須涵蓋它，否則在該組態下防貫穿會失效。

夾限式：

```
front_clamped = clamp(min(configured_front, local_thickness * 0.5 - offset), 0, configured_front)
penetration_mm = point_head_penetration_mesh_mm(front_clamped, r_pin, r_contact)
```

**`SupportTreeMesher` 完全不需修改。**

**政策：上表面零凸點優先。** 明確接受極薄件因咬合深度不足、列印時可能脫落的物理代價。

*替代方案 A*：在 `head_mesh_body()` 內夾限。**否決**——網格與邏輯模型脫鉤，`fullwidth()` / `junction_point()` 仍用未夾限值，主頭會與 pillar 產生最大 `configured` 的間隙。
*替代方案 B*：直接夾 `Head::penetration_mm`。**否決**——開接觸球時該值不是刺入深度，夾限量會錯 `(r_pin - r_contact)`。

### D3｜入模方向與量測射線

`head_mesh_local()`（`SupportTreeMesher.hpp:95`）的四元數為 `FromTwoVectors(Vec3f{0,0,-1}, h.dir)`，即局部 `-z` 映至 `h.dir`。故**局部 `+z`（刺入方向）映至 `-h.dir`**：

```
dir_in          = -head.dir                                  （已為單位向量）
local_thickness = query_ray_hit(hp + eps * dir_in, dir_in).distance() + eps
```

**符號必須為 `+eps`，不可為 `-eps`。** `query_ray_hit()` 無正/反面過濾，命中的是路徑上第一個三角形，不論朝向。若起點沿 `-dir_in` 退到模型**外側**，第一個命中必然是入射面本身，量得的「厚度」恆為 `2*eps` 而與真實壁厚無關，夾限會在所有模型上靜默失準。踏入內部後第一個命中才是出射面。此手法與程式庫既有慣用法一致（`SupportTreeUtils.hpp` 的 `query_ray_hit(ps + sd * n, n)`）。

`eps` 取 `1e-3 mm` 量級：遠小於最薄可列印壁厚（約 0.05 mm），且遠大於 `double` 在該尺度的表示誤差。

**沿頭軸量測而非沿表面法線量測**是刻意選擇：`penetration` 本身即定義在頭軸上，軸向距離才是正確的可用深度。此設計自然涵蓋兩種情形：

- **傾斜頭**：斜穿薄板的軸向可用深度大於垂直壁厚，夾限自動放寬，不會過度保守。
- **由側緣出射**：射線自板側面出射時，量得的仍是軸向可用深度，夾限依然保證尖端留在材料內。

**切片端量測所用網格**為 `m_supportdata->emesh`，來源 `get_mesh_to_print()`，已含 hollowing 與 drill holes。中空件量到實際壁厚、鄰近排水孔的點量到縮減後的實際厚度——皆為正確語意。

### D4｜射線無命中採 Fail-safe（切片端）

封閉流形網格上，自表面內側向內射出的射線**必定命中**。無命中即代表網格破損、非流形或自交。依「上表面零凸點優先」政策，此時 `front_clamped = 0`。

**必須輸出彙總警告日誌**（觸發點數量），每次支撐樹生成最多一行，不得逐點輸出。否則使用者只會看到支撐大量脫落而無從追查。

*替代方案*：fail-open（保留設定值）。**否決**——破面模型上仍可能穿透，違反既定政策。

### D5｜「先搜尋、後夾限」，四個提交點語意不同

夾限**只執行一次**，在所有角度搜尋結束、最終物件提交的當下。角度搜尋全程使用設定值，故搜尋行為完全不變。此決策同時消除了 `w`（`= real_width() - penetration`，頭部露在模型外的長度）與 optimizer 之間的循環相依——否則夾限依賴 `nn`、`nn` 由 optimizer 決定、optimizer 又以 `w` 為門檻。

**MUST NOT 將量測射線放入 optimizer 的目標函式內。**

四個提交點分為兩類：

```
【主頭 pinhead】提交點在 junction / pillar 計算之前
  SupportTreeBuildsteps.cpp:746-753   （Default 樹，t.distance() > w 的接受區塊）
  SupportTreeUtils.hpp:328-333        （Branching 樹，optimize_pinhead_placement 接受區塊）

  -> fullwidth() 連帶更新 -> junction_point() 外移 -> pillar 由新 junction 起算
  -> 頭與柱維持相連 [OK]
  -> 若不連帶更新，兩者將脫開最大 configured 的距離

【錨點 anchor】提交點在 add_anchor() 呼叫處
  SupportTreeBuildsteps.cpp:1044      （Default 樹）
  BranchingTreeSLA.cpp:304            （Branching / Organic 樹）

  -> 橋接端點已固定，不受影響 -> 拓撲零變化 [OK]
```

**Branching / Organic 樹的時序為硬性要求。** `BranchingTreeSLA.cpp:297` 讀取 `anchor->junction_point()` 建立 `sla::Junction toj` 作為橋接端點，而 `junction_point() = pos + (fullwidth() - r_back) * dir` 依賴 `penetration_mm`：

```
BranchingTreeSLA.cpp
  293  calculate_anchor_placement(...)                   <- 角度搜尋
  297  sla::Junction toj = {anchor->junction_point(), …} <- X 不可在此之前夾限
  298-303  （橋接可行性檢查）
  304  m_builder.add_anchor(*anchor)                     <- [OK] 夾限於此
```

若在 297 行之前夾限，`front` 變小 -> `fullwidth()` 變大 -> 橋接端點外移 -> 後續檢查結果改變，「後夾限」所要保住的搜尋穩定度即告失效。在 304 行夾限的幾何後果是安全的：`fullwidth()` 變大使 anchor 網格比 `toj` **更往外延伸**，橋接端點落在 anchor 罩體**內部**——是重疊而非脫開。

**Default 樹錨點的 `w` 需連帶重算。** `SupportTreeBuildsteps.cpp:1036-1037`：

```cpp
double dist = (hitp - endp).norm() + m_cfg.head_penetration_mm;
double w    = dist - 2 * head.r_pin_mm - head.r_back_mm;
```

`dist` 由 penetration 導出，夾限時 `w` 必須以夾限後的值重算，否則錨點長度與刺入深度不一致。

**兩棵樹的夾限程式碼形狀不同：**

- Default 樹主頭（`SupportTreeBuildsteps.cpp:689-691`）走 per-point 解析（`point_head_penetration_mesh_mm()`、`point_contact_sphere_radius_mm()`），且會設定 `h.r_contact_mm`。夾限需以該處已解析出的 `contact_r` 與 per-point front depth 為輸入，套 D2 公式後再換算寫回 `h.penetration_mm`。
- Branching 樹（`SupportTreeUtils.hpp:355`、`:688`）只讀 `sm.cfg.head_penetration_mm`，且從不設定 `r_contact_mm`。故該路徑 `r_contact = 0`、`offset = 0`、`pen` 恆等於 `front`，可直接夾限 `penetration_mm`。

### D6｜`taildir` 正規化獨立列項

`SupportTreeBuildsteps.cpp:1035` 的 `Vec3d taildir = endp - hitp;` 未正規化。`IndexedMesh::query_ray_hit()` 具 `assert(is_approx(dir.norm(), 1.))` 前置條件，Release build 下該斷言不存在，傳入非單位向量會使回傳的 `m_t` 為參數 t 而非實際長度，夾限計算將失準且無聲。

此為 D3 的**硬性前置**，仍採**獨立提交、獨立驗證、獨立回退**。

**實作後修正（Phase 1 code review）**：來源變更的 design 宣稱正規化「會改變既有 anchor 的朝向與網格」。**該結論在本 fork 不成立。** `taildir` 只成為 `Anchor::dir`，而 `m_anchors` 的唯一消費端是 `SupportTreeBuilder.cpp:166` 的 `get_mesh(anch, steps)`；其 `head_mesh_local()` 以 `Quaternion::FromTwoVectors()` 建立旋轉，該函式**內部即正規化兩個輸入向量**，故方向的長度不影響輸出網格。唯一會受長度影響的 `Anchor::junction_point()` 從未被讀取（`Head::Head()` 亦不對 `dir` 做正規化，故此結論非來自建構子）。

因此 #6 對現行輸出**逐點無變化**，回歸風險為零；它的價值在於成為 D3 量測射線的合法前置。驗證方式相應改為「確認網格逐點不變」，而非「比對朝向與網格的變化」。

### D7｜GUI 端獨立量測，不建立跨管線通道

因約束 2，任何「切片端算完回寫給 GUI」的通道在主要工作流下恆為空。故 GUI 端**獨立量測**，兩端共用 front depth 定義與 `point_head_penetration_mesh_mm()` 換算。

**保證的是「畫出來的支撐頭不穿透畫出來的模型」**，而非「與切片端數值逐位元相同」。殘差僅來自軸向差異（GUI 用表面法線、切片端用 optimizer 調整後的頭軸），而該落差在本變更之前既已存在於預覽的繪製方向。

*替代方案 A*：把 `reslice_until_step()` 的目標提升到 `slaposSupportTree`。**否決**——支撐樹生成是管線最慢的一步，使用者按一次「自動產生支撐點」就要等完整的樹算完；且與本 fork 既有的 `prevent-mode-switch-auto-slicing`、`sla-on-demand-preview` 方向相反。
*替代方案 B*：把量測前移到 `slaposSupportPoints` 步驟並存側通道。**否決**——該階段尚不知支撐頭軸向，只能沿表面法線量，等於為了形式上的單一真理來源而讓切片端的夾限本身失準。
*替代方案 C*：GUI 沿用現有的 `Raycaster`（建於 `mv->mesh()`，原始網格）。**否決**——中空件會量到實心厚度（不夾限）而切片端量到薄壁（夾限），方向相反的不一致，決策目標無法達成。

### D8｜HollowedMesh 生命週期與 GUI 加速結構

**啟用**：於 `GLGizmoSlaBase::on_get_requirements()` 加回 `CommonGizmosDataID::HollowedMesh`。`HollowedMesh::on_update()`（`GLGizmosCommon.cpp:734`）僅在 `po->is_step_done(slaposDrillHoles)` 為真時填入 `po->get_mesh_to_print()`，否則 `m_has_hollowed_mesh = false`。

**可用性**：`slaposDrillHoles` 排在 `slaposSupportPoints` 之前，而 `GLGizmoSlaSupports.cpp:473-476` 在 gizmo 開啟時即觸發 `reslice_until_step(get_min_sla_print_object_step())`。因此只要支撐點存在，中空網格幾乎必然可用。空窗期（載入專案後尚未觸發任何切片）走 D9 的降級路徑。

**加速結構**：以 `MeshRaycaster`（`MeshUtils.hpp`）包裝該網格，透過 `get_aabb_mesh().query_ray_hit(s, dir)` 取得含 `distance()` 的命中結果。`AABBMesh::hit_result` 與 `IndexedMesh::hit_result` 介面一致（`distance()` / `is_hit()` / `normal()`），兩端量測程式碼可共用同一形狀。

**重建判準**：仿 `Raycaster::on_update()`（`GLGizmosCommon.cpp:321`）的做法，保存上次使用的 `const TriangleMesh*` 指標；指標改變才重建 `MeshRaycaster`。AABB 樹建置是主要成本，**不可每幀執行**。

**厚度快取**：`std::vector<float>`，與繪製用的點集（非編輯模式為 `m_normal_cache`、編輯模式為 `m_editing_cache`）等長，哨兵值 `NaN` 表示「尚未量測」。與既有的 `m_normal_cache_normals`（`GLGizmoSlaSupports.cpp:800-804`）採完全相同的惰性填充模式：每點只在第一次繪製時打一次射線，之後每幀零射線成本。

**失效時機**：
- 點集重載（`reload_cache()` / `get_data_from_backend()`）-> 整份清空。
- `MeshRaycaster` 重建（網格指標改變）-> 整份清空。
- 編輯模式下單點被拖動 -> 僅該點失效。
- 物件變換（trafo）改變 -> 整份清空。
- **設定值（`support_head_penetration` 等）改變 -> 不失效。** 厚度與設定無關，只需重算一次 `min()`。

### D9｜GUI 降級路徑

GUI 端在下列情形無法量測，一律以**未夾限的設定深度**繪製（樂觀呈現）：

- `HollowedMesh` 不可用（`slaposDrillHoles` 未完成）。
- 射線無命中（破面／非流形網格）。

**GUI 的降級與切片端的 fail-safe 政策刻意不同**：切片端無命中時取 0（保證不穿透實際輸出），GUI 無命中時取設定值（避免把破面模型的所有支撐頭畫成貼在表面上、看起來像沒有支撐）。此差異必須明寫於 spec，不得被視為不一致的缺陷。

### D10｜快取 Key 與 Model Matrix 平移

**關鍵性質：改變 penetration 只造成支撐頭網格的剛體平移，不改變形狀。**

由 D2 的推導，`head_mesh_body()` 的所有頂點 z 都可寫成「與 pen 無關的量」加上 `pen`：

```
本體頂點   z = (pinhead 局部 z) - (2*r_pin + width + r_back) + pen
接觸球頂點 z = (sphere 局部 z) - r_pin + pen
```

（preview 分支的 `z_shift = 2*r_pin + segment_len + r_back - pen` 具相同結構，`segment_len` 僅依賴 width / r_pin / r_back，故同樣成立。）

因此：

- **canonical 網格以 `penetration_mm = 0` 建構**，快取 key 為 `(r_pin, r_back, width, r_contact, preview)`——**移除 `penetration` 欄位**。
- 繪製時在既有的 model matrix 尾端追加局部平移：

```cpp
const Transform3d model_matrix = instance_matrix_no_scale *
    Geometry::translation_transform(instance_scaling_matrix * support_point.pos.cast<double>()) *
    Transform3d(Eigen::Quaterniond::FromTwoVectors(-Vec3d::UnitZ(), head.dir)) *
    Geometry::translation_transform(head.penetration_mm * Vec3d::UnitZ());   // <- 新增
```

平移在**旋轉之後**施加，故為局部 `+z` 方向。世界方向為 `-head.dir`（局部 `+z` 經四元數映至 `-h.dir`），即「往模型內部」。penetration 越大、頭越深，符號正確。

`view_normal_matrix` 由 `model_matrix` 的線性部導出，而純平移不改變線性部，故該行**不需修改**。

**碰撞體不需額外處理。** `update_point_raycasters_for_picking_transform()` 的三個碰撞體位置本就由 `head.penetration_mm` 與 `head.fullwidth()` 解析導出：

```cpp
pin_center  = scaled_pos + (head.r_pin_mm - head.penetration_mm) * head.dir;
cone_height = head.fullwidth() - head.r_back_mm;
back_center = scaled_pos + cone_height * head.dir;
```

只要 `preview_sla_head_for_point()` 回傳的 `Head` 已攜帶夾限後的 `penetration_mm`，三者自動跟隨。**唯一需要確保的是：夾限對 `render_points()` 與 `update_point_raycasters_for_picking_transform()` 兩個呼叫端給出相同結果**（兩者在同一幀呼叫，讀同一份厚度快取即可）。

### D11｜座標映射：trafo、鏡像與非等比縮放

GUI 的支撐點座標與 HollowedMesh 不在同一個座標系，必須明確映射。

**起點**：

```
p_trafo = po->trafo() * sp.pos.cast<double>()
```

與 `SLAPrintObject::transformed_support_points()`（`SLAPrint.cpp:1365`）完全相同的變換，也是 `get_data_from_backend()`（用 `po->trafo().inverse()`）的逆運算。

**方向**：GUI 繪製用的 `head.dir = scaled_normal = S^-T * n_raw`（正規化），配合 `instance_matrix_no_scale = instance_matrix * S^-1` 使用。其世界方向為

```
M_ns.linear() * head.dir = (R*S * S^-1) * (S^-T * n_raw) = R * S^-T * n_raw = (R*S)^-T * n_raw
```

即**正確的世界表面法線**。由於 `trafo()` 與 `instance_matrix` 的線性部相同（兩者僅差一個平移：`instance_matrix` 額外含 `sla_shift` 與擺放位移），量測方向可直接寫為

```
n_trafo = (trafo().linear().inverse().transpose() * n_raw).normalized()
dir_in  = -n_trafo
```

**長度量綱**：trafo 空間即實際列印尺寸，量得距離為物理 mm；而 `Head` 的各半徑／寬度／penetration 也都是物理 mm（預覽刻意以 `instance_matrix_no_scale` 繪製，使支撐頭不隨模型縮放）。兩者同量綱，可直接比較與夾限。

**非等比縮放**：`n_raw` 是原始網格法線，`S^-T` 是法線的正確變換（covector）。上式已涵蓋，不需額外處理。但**縮放後的軸向厚度與縮放前不成比例**，這正是必須在 trafo 空間量測、而非在原始網格量測再乘倍率的原因。

**鏡像（left_handed）**：純鏡像 `M = diag(1,1,-1)` 的 `M^-T = M`，故 `M^-T * n` 仍給出鏡像後表面的正確朝外法線。`instance_scaling_matrix`（`get_scaling_factor_matrix()`）為非負量，鏡像走 `instance_matrix` 的帶號部分——既有程式碼的註解（「Mirror flows through the signed instance matrix」）與 `vol->is_left_handed()` 時切換 `glFrontFace` 的處理已印證此分工。上式因此對鏡像物件成立。**但此結論必須以實測驗證**（見驗證假設 A3），不得僅憑推導放行。

**`m_c->raycaster()` 維持不動**：法線查詢（`get_closest_point()`）仍用原始網格。外表面在中空前後一致，故法線正確；而厚度量測改在中空網格上進行，射線的出射點才會落在內壁——這正是所需語意。兩塊網格混用是刻意的，不是疏漏。

### D12｜施作順序

```
#6 taildir 正規化 -----------> 必須最先（射線需單位向量）
#1 方向性吸附 --- 獨立，但 #5 的效果需 #1 完成後才觀察得到
#5 切片端夾限 ---> 依賴 #6
GUI 預覽 --------> 依賴 #5（共用 front depth 定義與換算）

建議序：#6 -> #1 -> #5 -> GUI
```

每一步皆須可獨立建置並通過既有測試，使 bisect 在回歸發生時仍然可用。

## 驗證假設

以下三項為本設計所依賴、但尚未以實測確認的假設。**實作階段必須逐項驗證**；任何一項不成立都需回到本文件重新決議，而非就地判斷。

### A1｜`squared_distance` 回退分支對薄板不可達

D1 只修改射線命中的**選擇**邏輯，未改動 `hit.distance() > allowed_move` 時的 `squared_distance` 回退分支（`SupportPointGenerator.cpp:1311-1316`）。該分支投影到幾何最近三角形，在薄板上半部即為**上表面**——與缺陷一同樣的錯誤。

D1 的方向性優先會選中可能較遠的朝下面，因此**理論上會提高進入該分支的機率**。

**推導**：切片網格自 `minZ = bb.min(Z) - elevation` 起算、步長 `layer_height`，故第一個涵蓋模型的層距離模型底面的高度 `a` 落在 `[0, layer_height)`，嚴格小於一個層高。而 `allowed_move` 在多層時為 `dLevel = layer_height + eps`、單層時為 `support_head_front_diameter`（典型 0.4 mm，通常更大）。兩者皆不小於 `layer_height`，故 `a < allowed_move`，該分支不可達。

**驗證方式**：以 0.2 mm 薄板，`layer_height` 取 0.05 / 0.10 / 0.15，`support_object_elevation` 自 5.00 至 5.15 逐 0.01 掃描，記錄每個相位下實際走入的分支與最終點位 z。判準：全部產生有效切片層的相位皆未進入 `squared_distance` 分支，且所有支撐點 z = 0.00（下表面）。

**若不成立**：需為 `squared_distance` 分支補上同樣的方向性約束（改用朝下面的投影，或在投影結果朝上時改取向下射線命中），並回到本文件補記決策。

### A2｜`allowed_move` 現行替代值維持不變為安全選擇

來源變更將單層時的 `allowed_move` 定為 `layer_height`；PhrozenOrca 現行為 `support_head_front_diameter`（`SLAPrintSteps.cpp:937`）。後者通常較大，依 A1 的推導更不易落入回退分支，故**維持現狀**。

**驗證方式**：於 A1 的相位掃描中同時記錄 `allowed_move` 的實際值與 `hit.distance()`，確認 `hit.distance() <= allowed_move` 在所有相位成立。

**若不成立**：改採 `max(layer_height, support_head_front_diameter)`，並記錄理由。

### A3｜鏡像與非等比縮放下的量測方向正確

D11 的推導對鏡像與非等比縮放成立，但依賴「`get_scaling_factor_matrix()` 為非負、鏡像走帶號的 `instance_matrix`」這項前提。

**驗證方式**：對同一薄板模型分別施加 (a) X 軸鏡像、(b) 非等比縮放 (1.0, 1.0, 3.0)、(c) 鏡像 + 非等比縮放，比對 GUI 量得的厚度與切片端量得的厚度（在軸向一致的垂直面上兩者應相等），並目視確認預覽支撐頭未穿透。

**若不成立**：改以 `po->trafo()` 為唯一權威——將 GUI 的 `head.dir` 也由 `trafo()` 導出，而非由 `instance_scaling_matrix` 導出，代價是預覽軸向與現行繪製結果可能有微小差異，需獨立評估。

## Risks / Trade-offs

| 風險 | 界限 | 緩解 |
|---|---|---|
| **`w` 以未夾限值驗證**（D5 的必然後果）——提交後頭部向外多伸出 `configured - clamped`，超出淨空檢查所驗證的範圍 | 不超過 `configured_penetration`（典型 0.4 mm） | 針對薄壁模型的 anchor 增加幾何回歸測試；記錄為已知取捨而非缺陷 |
| **Fail-safe 使破面模型支撐零咬合**（D4） | 僅限非流形／破損網格 | 彙總警告日誌，使用者可據以修模；不靜默 |
| **夾限僅在可用深度 >= 2 x configured 時完全失效**——影響帶為 `(0, 2 x configured)` | 可用深度 < 0.8 mm（@ configured 0.4）會被靜默調降 | 門檻明寫入 spec 驗收條件；**不得**宣稱「常態零影響」，須表述為「可用深度 >= 2 x configured 時零影響」 |
| **GUI 與切片端量測軸向不同**——夾限值存在殘差 | 殘差為 `1/cos(theta)`，theta 為表面法線與 optimizer 調整後頭軸的夾角 | 明寫入 spec：預覽保證的是「不穿透預覽模型」，非數值一致；軸向落差為既有獨立議題 |
| **退化帶 `0 < r_contact <= r_pin` 的既有不一致** | 最深點多出 `r_pin - r_contact` | D2 的 `offset` 已涵蓋，夾限仍正確；但該組態下接觸球的外觀差異維持現狀 |
| **重新啟用 HollowedMesh 的記憶體與建置成本**——網格複製 + GUI 自建 AABB 樹 | 每次網格指標改變一次 | 以指標比對避免重複建置；此成本與 PrusaSlicer 既有的 `GLGizmoHollow` 做法相同 |
| **`head_fullwidth()` 全域估算偏樂觀**——`SLAPrint.cpp` 的 elevation 驗證使用未夾限的全域值，夾限後實際 `fullwidth()` 更大 | 輕微，僅影響 elevation 下限驗證 | 記錄；若實測出現貼底碰撞再處理 |
| **~~#6 改變既有 anchor 幾何~~（Phase 1 review 後判定不成立）** | 無——`get_mesh(Head)` 的 `FromTwoVectors()` 內部正規化，`junction_point()` 未被讀取，輸出逐點不變 | 仍獨立提交以便單獨回退；驗證改為「確認網格逐點不變」 |
| **`connect_to_model_body()` 的 `endp == hitp` 退化路徑** | 該路徑今日即已產生零向量方向並流入 `FromTwoVectors()`，屬既有的潛在缺陷 | 本變更維持現狀不修（以 `EPSILON` 判斷保留原零向量，不引入 NaN）；列為未來獨立變更的候選 |
| **與 upstream 的分歧擴大** | 4 個切片核心檔案 + 3 個 GUI 檔案 | 每項修改加註來源與意圖註解；未來 rebase 時逐項核對上游是否已修正 |
| **每個支撐點增加一次 raycast** | 切片端每點 1 次（僅在提交點）；GUI 端每點 1 次（惰性、可快取） | 相對於 pinhead optimizer 的數十至上百次評估可忽略；**不得**將量測放入 optimizer 目標函式內 |

## Migration Plan

無資料格式或設定遷移。部署即生效。

**回退策略**：四項修改（#6、#1、#5、GUI）採獨立提交。除 #5 依賴 #6、GUI 依賴 #5 外彼此可分離，任一項出現回歸可單獨 revert。若需整體回退，還原全部四項即回到現行行為。

**驗證流程**（依施作順序逐項驗證，不累積）：

1. **#6**：對照 `connect_to_model_body()` 產生的錨點，確認方向與網格變化符合預期，且 `query_ray_hit()` 收到的方向向量長度為 1。
2. **#1**：0.2 mm 薄板於 `support_object_elevation` 5.00 至 5.15 逐 0.01 全相位掃描，`layer_height` 取 0.05 / 0.10 / 0.15。判準：全部產生有效切片層的相位皆產出相同數量的支撐點，且點位 `zmin = zmax = 0`。同時採集 A1 / A2 的分支與距離數據。
3. **#5**：支撐網格與模型網格布林交集後，**承載面另一側無任何幾何**。涵蓋接觸球開／關、退化帶 `0 < r_contact <= r_pin`、Default 與 Branching 兩樹。破面模型確認 fail-safe 觸發並輸出彙總警告。
4. **GUI**：Points 檢視下目視確認預覽支撐頭未穿透模型；確認 `m_head_model_cache` 的項目數不隨支撐點數成長；確認滑鼠 hover 的命中位置與可見幾何一致；執行 A3 的鏡像／非等比縮放組合。
5. **迴歸**：可用深度 >= 2 x configured 且厚度除以 `layer_height` >= 2 的既有模型，支撐點數量與位置、支撐網格**逐點一致**。

## Open Questions

無需使用者裁決的技術分歧。

實作期間若出現以下情形，需回到本文件重新決議而非就地判斷：

- 驗證假設 A1、A2 或 A3 任一項不成立。
- D1 第 3 層回退分支（兩面皆非朝下）在實測中被頻繁觸發，代表存在未預期的幾何類別。
- D3 的 `eps` 選值在極薄件上造成量測誤差超過可接受範圍。
- D5 的 `w` 未夾限驗證在實測中確實產生可見碰撞（而非理論風險）。
- GUI 端厚度量測在大量支撐點（超過 5000 點）下造成可感知的首幀延遲。
