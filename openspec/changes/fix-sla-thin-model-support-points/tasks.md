> **建置由使用者手動執行。** 本文件中所有標示「【建置檢查點】」的項目，AI 不得自行執行建置、設定（configure）或測試指令，必須停下並請使用者操作。
>
> ---
>
> ## 建置環境現況與已知問題
>
> ### 問題 1：`build_resin_release_vs2022.bat slicer debuginfo` 的 configure 會失敗
>
> **根因**：`deps/build-resin-dbginfo/` 是**空的**——resin 變體的相依函式庫從未建置過。批次檔把 `CMAKE_PREFIX_PATH` 指向 `deps/build-resin-dbginfo/PhrozenOrca_dep/usr/local`（不存在），所有 `find_package()` 失敗 → `Configuring incomplete, errors occurred!` → 不產生任何 `.vcxproj` → 後續的 `cmake --build` 找不到 `ALL_BUILD.vcxproj` 與 `install.vcxproj`。
>
> 加上 `slicer` 參數時，批次檔的 `if "%1"=="slicer" GOTO :slicer` 會**跳過建置 deps 的整段流程**，所以缺的 deps 永遠不會被補上。
>
> ### 問題 2：`sla_print_tests` 不存在，且無法直接復活
>
> **根因有兩層。**
>
> 第一層：`tests/CMakeLists.txt:35` 的 `add_subdirectory(sla_print)` 是**被註解掉的**（繼承自 OrcaSlicer 上游 commit `b9e6108be` "Feature/re enable tests"，該次只重新啟用了 libnest2d / libslic3r / slic3rutils / fff_print）。因此該 target 不會被產生，手動加進 Solution 也沒有對應的 CMake 目標。
>
> 第二層：即使取消註解也編不過。`tests/sla_print/sla_test_utils.cpp` 與 `sla_supptgen_tests.cpp` 使用 `sla::SupportPointGenerator` **類別**與 `sla::SupportPointGenerator::Config`，但 PhrozenOrca 已遷移到自由函式 API（`prepare_generator_data()` / `generate_support_points()` / `SupportPointGeneratorConfig`）。`src/libslic3r/SLA/SupportPointGenerator.hpp:5` 的註解明寫「Removes old class SupportPointGenerator」。復活該目錄等同重寫整份測試工具，屬**另一個獨立變更**的範圍，不納入本變更。
>
> `build-dbginfo/tests/sla_print/` 是舊版設定殘留的資料夾，不是現行產物。
>
> **決定：本變更的所有新測試放進 `tests/libslic3r/`（target 名 `libslic3r_tests`）。** 該 target 啟用中且正常建置，並已有 SLA 測試先例 `tests/libslic3r/sla_contact_spacing_tests.cpp`。SLA 程式碼本就編譯進 `libslic3r`，測試可直接 include `libslic3r/SLA/*.hpp`。撰寫前必須先讀 `tests/CLAUDE.md`（浮點比較用 `WithinAbs` / `WithinRel`，禁用 `Approx`；迴圈內的 SECTION 用 `DYNAMIC_SECTION`）。
>
> ### 建置目錄對照
>
> | 目錄 | `PHROZEN_ORCA_ENABLE_RESIN` | deps 來源 | `SLIC3R_BUILD_TESTS` | 狀態 |
> |---|---|---|---|---|
> | `build-dbginfo/` | OFF（非 resin） | `deps/build-dbginfo/`（已建好） | ON | 可用 |
> | `build-resin-dbginfo/` | ON（resin 變體） | `deps/build-resin-dbginfo/`（空） | OFF | configure 失敗 |
> | `build/` | — | — | — | 舊 VS 2019 快取殘留，勿用 |
>
> 本變更屬 Resin 模組，**GUI 驗證與端到端驗收必須在 `build-resin-dbginfo/`（resin 變體）進行**。純 `libslic3r` 的單元測試兩邊皆可跑，但為求一致統一以 resin 建置目錄為準。
>
> ---
>
> ## 修復後的建置與測試指令
>
> ### 步驟 1｜設定（configure）——僅在首次或改動 `CMakeLists.txt` 後需重跑
>
> 開啟「x64 Native Tools Command Prompt for VS 2022」：
>
> ```bat
> cd /d d:\repos\PhrozenOrca
> cmake -S . -B build-resin-dbginfo -G "Visual Studio 17 2022" -A x64 ^
>   -DBBL_RELEASE_TO_PUBLIC=1 ^
>   -DPHROZEN_ORCA_ENABLE_RESIN=ON ^
>   -DSLIC3R_BUILD_TESTS=ON ^
>   -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
>   -DCMAKE_INSTALL_PREFIX=./PhrozenOrcaResin ^
>   -DCMAKE_PREFIX_PATH="d:/repos/PhrozenOrca/deps/build-resin-dbginfo/PhrozenOrca_dep/usr/local" ^
>   -DWIN10SDK_PATH="C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\"
> ```
>
> 與批次檔的差異：多了 `-DSLIC3R_BUILD_TESTS=ON`。批次檔從不開測試，這正是 `build-resin-dbginfo` 的 `SLIC3R_BUILD_TESTS` 為 OFF 的原因。`WIN10SDK_PATH` 的值取自 `build-dbginfo/CMakeCache.txt`，若本機 SDK 版本不同請自行替換。
>
> 必須看到 `-- Configuring done` 與 `-- Generating done` 才算成功。
>
> ### 步驟 2｜建置
>
> **方式 A｜命令列（推薦，可只建單一目標）**
>
> ```bat
> cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target libslic3r -- -m
> cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target libslic3r_tests -- -m
> cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target libslic3r_gui -- -m
> cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target PhrozenOrca -- -m
> ```
>
> **方式 B｜Visual Studio 2022 IDE（適合逐步除錯）**
>
> 1. 開啟 `d:\repos\PhrozenOrca\build-resin-dbginfo\PhrozenOrca.sln`
> 2. 組態選 `RelWithDebInfo`、平台選 `x64`
> 3. 方案總管 → 對目標專案右鍵 → 建置（`libslic3r` / `libslic3r_gui` / `libslic3r_tests` / `PhrozenOrca`）
> 4. 要執行程式：對 `PhrozenOrca` 右鍵 →「設為啟始專案」→ F5
>
> **方式 C｜批次檔全建（僅在 Phase 0 完成後可用）**
>
> ```bat
> build_resin_release_vs2022.bat slicer debuginfo
> ```
>
> 注意：此批次檔**不帶** `-DSLIC3R_BUILD_TESTS=ON`，會把該選項覆寫回預設的 OFF。跑過之後若要再跑測試，必須重新執行步驟 1。
>
> ### 步驟 3｜測試
>
> ```bat
> cd /d d:\repos\PhrozenOrca\build-resin-dbginfo
> ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"
> ```
>
> 或直接執行測試執行檔並以 Catch2 標籤篩選：
>
> ```bat
> d:\repos\PhrozenOrca\build-resin-dbginfo\tests\libslic3r\RelWithDebInfo\libslic3r_tests.exe "[ThinModel]"
> ```
>
> 全專案測試（含 fff_print / slic3rutils / libnest2d）：
>
> ```bat
> ctest -C RelWithDebInfo --output-on-failure
> ```
>
> ---
>
> ## 每個 Phase 的收尾規則
>
> 1. 該 Phase 的程式碼修改完成後，先請使用者建置。
> 2. 建置通過才執行該 Phase 的驗證項目。
> 3. 驗證全數通過後，**該 Phase 獨立提交一次 commit**，再進入下一個 Phase。
> 4. 任一 Phase 出現回歸，僅需 revert 該 Phase 的 commit。

## 0. Phase 0：建置環境修復（前置，一次性）

- [x] 0.1 【建置檢查點】請使用者刪除壞掉的 resin CMake 快取：`rmdir /s /q d:\repos\PhrozenOrca\build-resin-dbginfo`（不刪除的話錯誤的 `CMAKE_PREFIX_PATH` 會一直被沿用）
- [x] 0.2 【建置檢查點】請使用者補上 resin deps。**修法 1（推薦，數分鐘）**：以系統管理員身分執行 `mklink /J d:\repos\PhrozenOrca\deps\build-resin-dbginfo\PhrozenOrca_dep d:\repos\PhrozenOrca\deps\build-dbginfo\PhrozenOrca_dep`，重用已建好的非 resin deps（deps 為第三方函式庫，`PHROZEN_ORCA_ENABLE_RESIN` 只影響應用程式碼；兩邊的 `DEP_DEBUG=OFF`、`PHROZEN_ORCA_INCLUDE_DEBUG_INFO=ON`、`RelWithDebInfo` 完全一致，可共用）。**修法 2（保險但耗時數十分鐘以上）**：執行 `build_resin_release_vs2022.bat deps debuginfo` 真的建一份 resin deps
- [x] 0.3 【建置檢查點】請使用者執行本文件「步驟 1」的 `cmake -S . -B build-resin-dbginfo ...` 設定指令，確認輸出結尾為 `-- Configuring done` 與 `-- Generating done`；若仍失敗，請將完整輸出回報，暫停所有後續工作
- [x] 0.4 【建置檢查點】請使用者確認 `build-resin-dbginfo/PhrozenOrca.sln` 已產生，且方案中包含 `libslic3r`、`libslic3r_gui`、`libslic3r_tests`、`PhrozenOrca` 四個專案
- [x] 0.5 【建置檢查點】請使用者執行基線建置 `cmake --build ... --target libslic3r_tests -- -m` 與基線測試 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"`，記錄未修改任何程式碼時的通過／失敗清單，作為後續回歸比對的基準。**實測基線（2026-08-26，build-resin-dbginfo）：libslic3r_tests 共 90 個測試，85 通過、5 個既有失敗。後續判準為「無新增失敗」，非「全數通過」。**
- [x] 0.6 在 `design.md` 補記本節的環境結論：`tests/sla_print` 已失效不納入範圍、新測試改放 `tests/libslic3r`、resin deps 以 junction 共用；並註明「復活 `tests/sla_print`」為未來獨立變更的候選

## 1. Phase 1：#6 taildir 正規化（前置依賴）

- [x] 1.1 於 `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` 的 `SupportTreeBuildsteps::connect_to_model_body()`（約 1035 行）將 `Vec3d taildir = endp - hitp;` 改為正規化後的單位向量，並加註來源與意圖註解（說明 `IndexedMesh::query_ray_hit()` 的單位向量前置條件在 Release 下不存在）
- [x] 1.2 確認正規化後 `dist` / `w` 的計算語意不變（`dist` 仍以 `(hitp - endp).norm()` 取實際長度，不得誤用正規化後的向量）
- [x] 1.3 【建置檢查點】請使用者建置 `libslic3r`，確認無編譯錯誤。**註：依使用者指示勾選；AI 未取得建置輸出回報，本項未經實測確認。**
- [x] 1.4 【建置檢查點】請使用者建置 `libslic3r_tests` 並執行 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"`，與 0.5 的基線比對（90 測試 / 5 既有失敗），確認無新增失敗。**註：依使用者指示勾選；AI 未取得測試輸出回報，本項未經實測確認。**
- [x] 1.5 錨點幾何影響評估（原訂 GUI 目視比對，經 code review 改為靜態論證）：`taildir` 僅成為 `Anchor::dir`；`m_anchors` 的唯一消費端是 `SupportTreeBuilder.cpp:166` 的 `get_mesh(anch, steps)`，其 `head_mesh_local()` 以 `Quaternion::FromTwoVectors()` 建立旋轉，該函式內部即正規化輸入，故方向長度不影響輸出網格；唯一受長度影響的 `Anchor::junction_point()` 從未被讀取，且 `Head::Head()` 不對 `dir` 正規化（故此結論非來自建構子）。**結論：本 Phase 對支撐網格逐點無變化，回歸風險為零。**已同步修正 `design.md` 的 D6 與風險表——來源變更宣稱的「#6 改變既有 anchor 幾何」在本 fork 不成立
- [x] 1.6 Review：命名沿用檔案既有 snake_case 慣例；註解已重寫，納入「今日為 no-op、至 Phase 3 才成為前置」此一關鍵事實；`dist` 與 `w` 仍以 `(hitp - endp).norm()` 計算，`taildir` 僅出現於 `add_anchor()` 呼叫處；`EPSILON` 退化保護維持既有行為、不引入 NaN。未觸碰任何防貫穿相關程式碼，可獨立 revert。已建立分支並提交獨立 commit

## 2. Phase 2：#1 支撐點朝下法線優先吸附

- [ ] 2.1 於 `src/libslic3r/SLA/SupportPointGenerator.cpp` 的 `move_on_mesh_surface()`（約 1304-1305 行）將命中面選擇改為三層決策：朝下面優先 → 兩者皆朝下取較近者 → 兩者皆非朝下回退至現行「取較近者」
- [ ] 2.2 朝下判定使用 `IndexedMesh::hit_result::normal()` 的 z 分量為負；加註「面法線不隨射線方向翻轉，故自內側向上命中上表面仍會被排除」的說明註解
- [ ] 2.3 確認 `allowed_move` 的計算（`src/libslic3r/SLAPrintSteps.cpp` 約 934-937 行）**維持現狀不動**，並於該處加註釋說明為何不改為 `layer_height`（對應 spec「投影位移上限維持現行取值」）
- [ ] 2.4 【建置檢查點】請使用者建置 `libslic3r`，確認無編譯錯誤
- [ ] 2.5 新增測試檔案 `tests/libslic3r/sla_thin_model_tests.cpp`（Catch2 標籤 `[ThinModel]`），並加入 `tests/libslic3r/CMakeLists.txt` 的 `add_executable()` 來源清單；撰寫前先讀 `tests/CLAUDE.md`
- [ ] 2.6 撰寫測試：0.2 mm 水平薄板，取樣層位於中面之上與之下兩種情形，斷言支撐點皆落在下表面 z=0.00（對應 spec「支撐點必須投影至朝下的承載面」前兩個 Scenario）
- [ ] 2.7 撰寫測試：兩個方向皆非朝下面時回退既有行為，斷言結果與「取較近者」一致（對應 spec 第四個 Scenario）
- [ ] 2.8 撰寫 A1 / A2 驗證測試：0.2 mm 薄板，`layer_height` 取 0.05 / 0.10 / 0.15，`support_object_elevation` 自 5.00 至 5.15 逐 0.01 掃描（迴圈內用 `DYNAMIC_SECTION`）；記錄每個相位的 `hit.distance()` 與 `allowed_move`，斷言 `hit.distance() <= allowed_move` 恆成立、且未進入 `squared_distance` 回退分支
- [ ] 2.9 撰寫測試：同一掃描下，所有產生有效切片層的相位皆產出相同數量的支撐點，且所有點 z = 0.00（對應 spec「支撐點產出必須與切片網格相位無關」）
- [ ] 2.10 撰寫測試：`m_model_height_levels.size() == 1` 的相位仍產出非零支撐點
- [ ] 2.11 【建置檢查點】請使用者重跑「步驟 1」的 cmake 設定（因為改了 `tests/libslic3r/CMakeLists.txt`），再建置 `libslic3r_tests`
- [ ] 2.12 【建置檢查點】請使用者執行 `libslic3r_tests.exe "[ThinModel]"`，確認全數通過；再執行 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"` 確認與 0.5 基線相比無新增失敗
- [ ] 2.13 若 A1 或 A2 任一驗證失敗，**停止實作**並回報，回到 `design.md` 重新決議（不得就地為 `squared_distance` 分支加補丁）
- [ ] 2.14 Review：統計常態模型回歸樣本中「兩者皆非朝下」回退分支的觸發次數，確認為可解釋的少數；提交獨立 commit

## 3. Phase 3：#5 切片端動態防貫穿夾限

- [ ] 3.1 於 `src/libslic3r/SLA/SupportPoint.hpp` 新增純函式，實作 spec 的最深點偏移與夾限式：`offset(r_pin, r_contact)` 與 `clamp_front_depth(configured_front, local_thickness, r_pin, r_contact)`；兩端（切片與 GUI）共用，**不得**修改既有 `point_head_penetration_mesh_mm()` 的語意
- [ ] 3.2 新增可用深度量測輔助函式（建議置於 `src/libslic3r/SLA/SupportTreeUtils.hpp`）：以 `hp + eps * dir_in` 為起點、`dir_in = -head.dir` 為方向呼叫 `query_ray_hit()`，回傳 `distance() + eps`；`eps` 取 1e-3 mm 量級並以具名常數定義，加註「起點必須踏入材料內部、不可退到外側」的理由註解
- [ ] 3.3 【建置檢查點】請使用者建置 `libslic3r`，確認新增的純函式與輔助函式可編譯
- [ ] 3.4 於 `tests/libslic3r/sla_thin_model_tests.cpp` 撰寫單元測試涵蓋 3.1 的三種 Contact Sphere 組態（無接觸球 / 接觸球有效 / 退化帶 `0 < r_contact <= r_pin`），斷言 `offset` 與夾限結果符合 spec 表格
- [ ] 3.5 提交點一（Default 樹主頭）：於 `SupportTreeBuildsteps.cpp` 的 `SupportTreeBuildsteps::filter()` 內 `filterfn` 接受區塊（約 746-753 行）施加夾限，寫回 `h.penetration_mm`，確認 `fullwidth()` / `junction_point()` 連帶更新
- [ ] 3.6 提交點二（Default 樹錨點）：於 `SupportTreeBuildsteps::connect_to_model_body()` 的 `m_builder.add_anchor()` 呼叫處（約 1044 行）施加夾限，並**連帶重算 `w`**（`dist = |hitp - endp| + 夾限後 penetration`，`w = dist - 2*r_pin - r_back`）
- [ ] 3.7 提交點三（Branching 樹主頭）：於 `src/libslic3r/SLA/SupportTreeUtils.hpp` 的 `optimize_pinhead_placement()` 接受區塊（約 328-333 行）施加夾限；該路徑 `r_contact = 0`、`offset = 0`，可直接夾限 `penetration_mm`
- [ ] 3.8 提交點四（Branching 樹錨點）：於 `src/libslic3r/SLA/BranchingTreeSLA.cpp` 的 `m_builder.add_anchor(*anchor)` 呼叫處（約 304 行）施加夾限。**必須在 297 行讀取 `junction_point()` 建立 `toj` 之後**，並加註「不可上移至 297 行之前」的警示註解
- [ ] 3.9 確認四處皆在角度搜尋（optimizer）完成之後施加，且量測射線**未**出現在任何 optimizer 目標函式內
- [ ] 3.10 實作 fail-safe：射線無命中時 `front_clamped = 0`；以計數器累計觸發點數，於支撐樹生成結束時輸出**恰好一行** `BOOST_LOG_TRIVIAL(warning)` 彙總日誌（含觸發數量），無觸發時不輸出
- [ ] 3.11 【建置檢查點】請使用者建置 `libslic3r`，確認四處提交點與 fail-safe 皆可編譯
- [ ] 3.12 撰寫幾何測試：0.2 mm 薄板 + `support_head_penetration = 0.3`，斷言有效 front depth 為 0.1，且支撐網格與模型網格的布林交集在上表面之上無任何幾何
- [ ] 3.13 撰寫幾何測試：同一模型分別以 Default 樹與 Branching/Organic 樹生成，兩者皆斷言承載面另一側無支撐幾何
- [ ] 3.14 撰寫幾何測試：啟用 Contact Sphere（`r_contact > r_pin`）與退化帶（`0 < r_contact <= r_pin`）兩種組態，皆斷言無穿透
- [ ] 3.15 撰寫測試：厚 10 mm 模型 + `support_head_penetration = 0.4`，斷言夾限完全失效、front depth 仍為 0.4、支撐網格與變更前逐點相同
- [ ] 3.16 撰寫測試：破面模型觸發 fail-safe，斷言 front depth 為 0 且輸出恰好一行含觸發數量的警告日誌；另斷言全數正常命中時不輸出該日誌
- [ ] 3.17 撰寫測試：Branching 樹錨點夾限前後，用於建立橋接端點的 junction 位置與橋接可行性檢查結果完全相同
- [ ] 3.18 【建置檢查點】請使用者建置 `libslic3r_tests` 並執行 `libslic3r_tests.exe "[ThinModel]"`，確認全數通過
- [ ] 3.19 【建置檢查點】請使用者執行 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"`，與 0.5 基線比對確認無新增失敗
- [ ] 3.20 Review：逐一核對四處提交點的時序是否符合 spec「四個夾限提交點的語意與時序」；提交獨立 commit

## 4. Phase 4：GUI 預覽自洽防貫穿

- [ ] 4.1 於 `src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp` 的 `on_get_requirements()`（約 88 行）加回 `CommonGizmosDataID::HollowedMesh`，並更新 `GLGizmoSlaSupports.cpp:1798` 與 `GLGizmoHollow.cpp:14` 的過時註解
- [ ] 4.2 【建置檢查點】請使用者建置 `libslic3r_gui`，並以中斷點或暫時日誌確認 `HollowedMesh::on_update()` 已會執行
- [ ] 4.3 於 `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` 新增：厚度量測用的 `MeshRaycaster` 成員、上次使用的 `const TriangleMesh*` 指標、以及與繪製點集等長的厚度快取（`std::vector<float>`，哨兵 `NaN`）
- [ ] 4.4 於 `GLGizmoSlaSupports.cpp` 實作厚度量測輔助函式：起點 `po->trafo() * sp.pos`、方向由 `trafo().linear().inverse().transpose() * n_raw` 正規化後取反向，呼叫 `m_thickness_raycaster->get_aabb_mesh().query_ray_hit()`，並共用 Phase 3 的 `clamp_front_depth()`
- [ ] 4.5 實作 `MeshRaycaster` 的重建判準：僅在 `HollowedMesh` 的網格指標改變時重建（仿 `GLGizmosCommon.cpp:321` 的 `m_old_meshes` 做法），**不得每幀重建 AABB**
- [ ] 4.6 實作厚度快取的惰性填充與失效：點集重載、量測網格改變、物件變換改變時整份清空；編輯模式下單點被拖動時僅失效該點；設定值改變**不**失效
- [ ] 4.7 於 `preview_sla_head_for_point()`（約 284 行）套用夾限：取得該點的可用深度後計算 `front_clamped`，再經 `point_head_penetration_mesh_mm()` 換算寫入回傳的 `Head::penetration_mm`
- [ ] 4.8 實作降級：`HollowedMesh` 不可用或射線無命中時，維持未夾限的設定深度（**與切片端 fail-safe 取 0 的政策刻意不同**），並加註理由註解
- [ ] 4.9 【建置檢查點】請使用者建置 `libslic3r_gui` 與 `PhrozenOrca` 並以 F5 啟動，於 Points 檢視目視確認 0.2 mm 薄板的預覽支撐頭不再刺穿模型
- [ ] 4.10 於 `GLGizmoSlaSupports.hpp` 的 `HeadGeomKey`（約 72-80 行）移除 `penetration` 欄位，並同步更新 `operator<` 的 `std::tie` 比較串
- [ ] 4.11 於 `GLGizmoSlaSupports.cpp` 的 `head_geom_key()`（約 656 行）移除 `key.penetration` 的填值
- [ ] 4.12 於 `render_points()` 建構 canonical 網格時，改以 `penetration_mm = 0` 的 `Head` 呼叫 `sla::head_mesh_body()`（約 833 行）
- [ ] 4.13 於 `render_points()` 的 `model_matrix`（約 851-853 行）尾端追加 `Geometry::translation_transform(head.penetration_mm * Vec3d::UnitZ())`，位置必須在擺放旋轉**之後**；確認 `view_normal_matrix` 的推導不需修改
- [ ] 4.14 確認 `update_point_raycasters_for_picking_transform()` **不需**新增夾限邏輯——三個碰撞體位置本就由 `head.penetration_mm` / `head.fullwidth()` 導出，僅需確保其取得的 `Head` 與 `render_points()` 讀同一份厚度快取
- [ ] 4.15 【建置檢查點】請使用者建置 `libslic3r_gui` 與 `PhrozenOrca` 並啟動，以 500 點以上的模型確認：畫面正確、旋轉視角流暢、`m_head_model_cache` 項目數不隨點數成長（可暫時加日誌確認）
- [ ] 4.16 【建置檢查點】請使用者於執行中的程式手動驗證 hover：夾限後的支撐頭，滑鼠移至可見頂端球會被標示，移至夾限前的舊位置不會被標示
- [ ] 4.17 A3 驗證（鏡像 / 縮放）：對同一薄板模型分別套用 (a) X 軸鏡像、(b) 非等比縮放 (1.0, 1.0, 3.0)、(c) 兩者併用，目視確認預覽支撐頭皆未穿透，並比對 GUI 與切片端於垂直承載面上量得的可用深度是否相等
- [ ] 4.18 若 A3 驗證失敗，**停止實作**並回報，依 `design.md` 的備案改以 `po->trafo()` 導出 GUI 的 `head.dir`，且該變更需獨立評估
- [ ] 4.19 驗證不污染持久化資料：於 Top 面板設定手動點 `head_penetration_mm = 0.4`、令其被夾限後，確認面板仍顯示 0.4；存檔重載後 3MF 中的值未變；進入 gizmo 繪製後專案未被標記為已變更、undo 堆疊未增加
- [ ] 4.20 【建置檢查點】請使用者執行 `ctest -C RelWithDebInfo --output-on-failure`（全專案），與 0.5 基線比對確認無新增失敗
- [ ] 4.21 Review：核對 `sla-support-preview-penetration` 與 `sla-support-points-preview-performance` 兩份 spec 的每一條 Scenario；提交獨立 commit

## 5. Phase 5：全域幾何回歸驗收與規格核對

- [ ] 5.1 薄板全相位掃描複驗：0.2 mm 薄板，`layer_height` 取 0.05 / 0.10 / 0.15，`support_object_elevation` 自 5.00 至 5.15 逐 0.01；斷言每個有效相位的支撐點數量相同、z 皆為 0.00，且無相位回報 `Automatic support points: 0`
- [ ] 5.2 常態模型逐點不變性複驗：挑選至少 3 顆厚度除以 `layer_height` >= 2 且局部可用深度 >= 2 x configured 的既有模型，比對變更前後的支撐點數量與座標逐點一致
- [ ] 5.3 常態模型支撐網格複驗：同一組模型，比對變更前後的支撐網格頂點與面完全相同（可用 3MF/STL 匯出後做二進位或幾何比對）
- [ ] 5.4 中間帶行為確認：局部可用深度 0.6 mm + `support_head_penetration = 0.4` 的模型，確認 front depth 為 0.3，並在驗收紀錄中明確標示此為**符合規格**而非回歸
- [ ] 5.5 端到端手動驗收：於 resin 建置（`build-resin-dbginfo`）以 `dish.stl` 等極薄模型完整走一遍「自動產生支撐點 → 套用 → 切片 → PRZ 輸出」，確認不再出現 `There are unprintable objects`
- [ ] 5.6 【建置檢查點】請使用者重跑「步驟 1」的 cmake 設定後做一次乾淨全建（`cmake --build ... --target ALL_BUILD -- -m`），確認無新增編譯警告
- [ ] 5.7 【建置檢查點】請使用者執行 `ctest -C RelWithDebInfo --output-on-failure`（全專案），確認與 0.5 基線相比無新增失敗
- [ ] 5.8 規格核對：逐條走過四份 spec 的所有 Requirement 與 Scenario，標記「已由自動測試涵蓋」或「已由手動驗收涵蓋」，未涵蓋者需補測試或補記為已知限制
- [ ] 5.9 更新 `design.md` 的「驗證假設」一節：將 A1 / A2 / A3 由「待驗證」改為實測結果，並附上實際數據
- [ ] 5.10 於四個修改點的程式碼加註來源與意圖註解（標明對應本變更與 upstream PrusaSlicer 2.9.6 的關係），供未來 rebase 時逐項核對
- [ ] 5.11 Review：確認 Phase 1 至 Phase 4 的 commit 彼此可獨立 revert，回退路徑與 `design.md` 的 Migration Plan 一致
