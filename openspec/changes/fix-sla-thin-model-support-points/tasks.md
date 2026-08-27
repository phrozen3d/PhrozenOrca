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
- [x] 1.3 【建置檢查點】請使用者建置 `libslic3r`，確認無編譯錯誤。**實測（2026-08-26）：建置成功，無編譯錯誤。**
- [x] 1.4 【建置檢查點】請使用者建置 `libslic3r_tests` 並執行 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"`，與 0.5 的基線比對，確認無新增失敗。**實測（2026-08-26）：90 測試 / 5 失敗，失敗清單與 0.5 基線逐項相同（#6、#12、#13、#30、#39），零新增回歸。**
- [x] 1.5 錨點幾何影響評估（原訂 GUI 目視比對，經 code review 改為靜態論證）：`taildir` 僅成為 `Anchor::dir`；`m_anchors` 的唯一消費端是 `SupportTreeBuilder.cpp:166` 的 `get_mesh(anch, steps)`，其 `head_mesh_local()` 以 `Quaternion::FromTwoVectors()` 建立旋轉，該函式內部即正規化輸入，故方向長度不影響輸出網格；唯一受長度影響的 `Anchor::junction_point()` 從未被讀取，且 `Head::Head()` 不對 `dir` 正規化（故此結論非來自建構子）。**結論：本 Phase 對支撐網格逐點無變化，回歸風險為零。**已同步修正 `design.md` 的 D6 與風險表——來源變更宣稱的「#6 改變既有 anchor 幾何」在本 fork 不成立
- [x] 1.6 Review：命名沿用檔案既有 snake_case 慣例；註解已重寫，納入「今日為 no-op、至 Phase 3 才成為前置」此一關鍵事實；`dist` 與 `w` 仍以 `(hitp - endp).norm()` 計算，`taildir` 僅出現於 `add_anchor()` 呼叫處；`EPSILON` 退化保護維持既有行為、不引入 NaN。未觸碰任何防貫穿相關程式碼，可獨立 revert。已建立分支並提交獨立 commit

## 2. Phase 2：#1 支撐點朝下法線優先吸附

- [x] 2.1 於 `src/libslic3r/SLA/SupportPointGenerator.cpp` 的 `move_on_mesh_surface()`（約 1304-1305 行）將命中面選擇改為三層決策：朝下面優先 → 兩者皆朝下取較近者 → 兩者皆非朝下回退至現行「取較近者」。實作為 `up_faces_down != down_faces_down` 時取朝下者（tier 1），否則取 `nearest`（tier 2/3 共用同一運算式，即修改前的原始規則）
- [x] 2.2 朝下判定使用 `IndexedMesh::hit_result::normal()` 的 z 分量為負；加註「面法線不隨射線方向翻轉，故自內側向上命中上表面仍會被排除」的說明註解（依檔案既有慣例以英文撰寫）。另加註 `is_hit()` 必須先於 `normal()` 檢查——射線未命中時 `m_normal` 未設定但 `is_valid()` 仍為真，故以 `up &&` / `down &&` 短路保護
- [x] 2.3 確認 `allowed_move` 的計算（`src/libslic3r/SLAPrintSteps.cpp`）**維持現狀不動**（僅加註釋，運算式一字未改），並說明為何不改為 `layer_height`：`move_on_mesh_surface()` 的方向性約束只作用於射線分支，`hit.distance() > allowed_move` 時會落入無方向性約束的 `squared_distance` 投影；`support_head_front_diameter`（典型 0.4 mm）較 `layer_height` 寬鬆，能讓該回退分支更不易被觸及
- [x] 2.4 【建置檢查點】請使用者建置 `libslic3r`，確認無編譯錯誤。**實測（2026-08-26）：建置成功，0 個錯誤，產出 `libslic3r.lib`。**
- [x] 2.5 新增測試檔案 `tests/libslic3r/sla_thin_model_tests.cpp`（Catch2 標籤 `[sla][ThinModel]`），並加入 `tests/libslic3r/CMakeLists.txt` 的 `add_executable()` 來源清單。已遵循 `tests/CLAUDE.md`：浮點比較一律 `WithinAbs`（無 `Approx`）、掃描迴圈用 `DYNAMIC_SECTION`、section 內無控制流（相位改為事前以 `enumerate_phases()` 列舉）
- [x] 2.6 撰寫測試：0.2 mm 水平薄板，取樣層位於中面之上（z=0.125）與之下（z=0.075）兩種情形，斷言支撐點皆落在下表面 z=0.00；中面之上的案例另斷言結果**不是**上表面（雖然它距離較近）
- [x] 2.7 撰寫測試：以 winding 反向的底面構造退化幾何，使上下兩射線的命中面法線 z 皆非負，斷言結果與「取較近者」一致。測試先以 `REQUIRE` 確立前提（兩個命中面法線皆 >= 0）而非假設之。另補一則 tier 1 測試：雙層板之間的點，朝下面的命中是**較遠**者，仍必須勝出——這正是舊的純距離規則會弄錯的情形
- [x] 2.8 撰寫 A1 / A2 驗證測試：0.2 mm 薄板，`layer_height` 取 0.05 / 0.10 / 0.15，`support_object_elevation` 自 5.00 至 5.15 逐 0.01 掃描（`DYNAMIC_SECTION`）。以 `model_height_levels()` 忠實重現 `slice_model()` 的網格建構（含 `ilhs` 首層與 `closest_slice_record()` 的取近語意）。斷言 A1（取樣層高度 <= 一個層高 + `GRID_TOL`；**初版寫成嚴格小於，經實測證偽後修正**，見 2.13）、`allowed_move > sample_z`（真正承載 A1 結論的嚴格不等式）、A2（`allowed_move >= layer_height`）、`down_hit.distance() <= allowed_move`（即射線分支的判斷條件成立、不落入 `squared_distance`），以及投影確實落在 z=0.00
- [x] 2.9 撰寫測試：同一掃描下，每個有效相位皆以 4 點輸入，斷言輸出點數不變且所有點 z = 0.00；另斷言每個層高至少貢獻一個有效相位，避免掃描空轉而假性通過。**範圍註記：本測試涵蓋投影階段，不涵蓋「端到端自動支撐點數量」——後者屬管線層級，由 Phase 5 的手動驗收承接（`tests/sla_print` 已失效，見 design.md）**
- [x] 2.10 撰寫測試：`levels.size() == 1` 時 `allowed_move` 退回 `support_head_front_diameter`，斷言其值、輸出非零、落在 z=0.00，且與 `size() >= 2` 的界限下**結果完全相同**——此收斂性正是相位無關性的成因
- [x] 2.11 【建置檢查點】請使用者重跑「步驟 1」的 cmake 設定（因為改了 `tests/libslic3r/CMakeLists.txt`），再建置 `libslic3r_tests`
  - 使用者手動建置完成，編譯 0 錯誤
- [x] 2.12 【建置檢查點】請使用者執行 `libslic3r_tests.exe "[ThinModel]"`，確認全數通過；再執行 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"` 確認與 0.5 基線相比無新增失敗
  - **第一輪（2026-08-26）：7 個測試 6 通過 1 失敗，809 個斷言 2 個失敗。** 失敗者為 `ray branch stays reachable across every grid phase`。
  - `ctest` 的其餘失敗（#6 / #12 / #13 / #30 / #39）與 0.5 基線完全一致，**零新增回歸**。
  - 失敗定位：`layer_height = 0.10`、`elevation = 5.00` 與 `5.10` 兩個相位，`sample_z` 正好等於一個層高，A1 的**嚴格**不等式 `sample_z < layer_height` 不成立。
  - **第二輪（2026-08-26，修正後）：`All tests passed (855 assertions in 7 test cases)`。全數通過。**
  - 回歸判定：第一輪的 `ctest` 已確認既有失敗僅 #6 / #12 / #13 / #30 / #39 五項，與 0.5 基線一致；當時唯一的新增失敗 #95 於第二輪轉為通過。**零新增回歸。**（第二輪未再回報完整 `ctest`；由於 `libslic3r` 端的第二輪改動僅為註解，該推論成立。）
- [x] 2.13 若 A1 或 A2 任一驗證失敗，**停止實作**並回報，回到 `design.md` 重新決議（不得就地為 `squared_distance` 分支加補丁）
  - **已觸發**：A1 的嚴格不等式不成立，依本任務規定停止並回到 `design.md`。
  - 根因為**推導的邊界寫錯，不是程式缺陷**：`closest_slice_record()` 可能取到模型底面下方的層；當網格恰有一層落在底面 z=0 上，該層切片退化（平面與底面共面、不產生島），取樣落到下一層，高度**正好等於一個層高**。故 `a` 的正確範圍是閉區間 `[0, layer_height]`。
  - **A1 的結論仍然成立，且改以更強的理由支撐**：`allowed_move = (levels[1] - levels[0]) + FLT_EPSILON`，最壞情形仍比取樣高度多一個 `FLT_EPSILON`（實測餘裕 1.192e-07 mm），是**定義上結構性保證**的，不是巧合。46 個有效相位的 `hit.distance() <= allowed_move` 與 `z = 0.00` **全數通過**，`squared_distance` 回退分支不可達。
  - **A2 全數通過**，維持 `support_head_front_diameter` 不變。
  - 處置：`design.md` 的 A1 保留初版推導並加註「邊界有誤」，另補修正版推導、餘裕警語與實測結果；A2 補實測結果。**未對 `squared_distance` 分支加任何補丁**，符合本任務的禁令。
  - 測試修正：`sample_z < layer_height` 改為 `sample_z <= layer_height + GRID_TOL`（`GRID_TOL = 1e-6`，即 `scaled()` 的座標量子；grid level 為 float，最壞情形的 `0.1f` 比 double 的 `0.1` 高約 9e-9），並**新增**一條 `allowed_move > sample_z` 的嚴格斷言，把真正承載結論的不等式鎖住。**斷言是收緊而非放寬。**
- [x] 2.14 Review：統計常態模型回歸樣本中「兩者皆非朝下」回退分支的觸發次數，確認為可解釋的少數；提交獨立 commit
  - **Review 已執行（2026-08-26）；2.12 重跑全數通過後建立 Phase 2 獨立 commit。**
  - 「統計回退分支觸發次數」一項**改以結構性論證取代，不做計數**：tier 2 與 tier 3 逐字沿用修改前的運算式 `(!down || hit_up.distance() < hit_down.distance()) ? hit_up : hit_down`，故回退路徑的行為**依構造與修改前完全相同**，觸發次數多寡不影響「常態幾何逐點不變」的結論。計數需要對 libslic3r 加樁並跑回歸樣本，成本高於其證據價值。
  - **正確性複查（已逐項對照原始碼確認）**：`up` / `down` 即 `hit_up.is_hit()` / `hit_down.is_hit()`；`up_faces_down` 的 `&&` 短路保證 `normal()` 只在命中時求值——這是必要的，`hit_result::m_normal` 為未初始化的 `Vec3d`，未命中時讀取即為未定義行為。tier 1 因 `up_faces_down` 蘊含 `up`，永不選中 miss。未命中時 `m_t = infinity()`，`nearest` 運算式對此已正確處理（與修改前同一式）。兩個三元分支型別相同且皆為 lvalue，參考繫結無懸空。
  - **發現缺陷 1（已修正）**：`SLAPrintSteps.cpp` 的新註解沿用了 design.md 初版那句錯誤推導「sits strictly less than one layer above the model's underside」。已改寫為 `AT MOST one layer height`，並補上 epsilon 餘裕的鎖定警語（禁止移除 `+ eps`、禁止把 `<=` 改成 `<`）與實測相位數。
  - **發現缺陷 2（已補註解，非程式問題）**：tier 1 的連帶效應——當朝下面較遠且超過 `allowed_move`、而較近的那個未超過時，現在會落入 `squared_distance` 分支，而非投影到較近（面向錯誤）的命中點。結果相近但非逐位元相同。此情形只在「點距離網格超過 `allowed_move`」時發生，而支撐點產生於切片層上、本就貼著表面，實務上不預期出現。已於 `SupportPointGenerator.cpp` 註明。
  - **範疇檢查**：兩個 `.cpp` 的改動僅為註解（`SLAPrintSteps.cpp` 全檔無可執行碼變更），加上 `move_on_mesh_surface()` 的三層決策本身。無任何超出 Task 2.1–2.3 範疇的改動。`.gitignore` 為變更前既存的無關修改，不納入本 commit。

## 3. Phase 3：#5 切片端動態防貫穿夾限

- [x] 3.1 於 `src/libslic3r/SLA/SupportPoint.hpp` 新增純函式
  - 實作為 `head_deepest_point_offset(pin_r_mm, contact_r_mm)` 與 `clamp_front_depth(configured_front_mm, local_thickness_mm, pin_r_mm, contact_r_mm)`，皆為 `double`、皆 `inline`、皆無狀態。
  - **命名偏離 tasks 原文**：spec 寫的 `offset` 在 `namespace sla` 的標頭作用域太籠統，改用 `head_deepest_point_offset`，並在註解中標明「這就是 spec 的 `offset` 項」。語意與公式完全一致。
  - `point_head_penetration_mesh_mm()` **一字未改**，夾限作用在換算**之前**的 front depth。
  - 新增一條 tasks 未列的防護：`configured_front_mm <= 0` 時原值直接回傳。理由是 `std::clamp(x, 0, configured)` 在 `configured < 0` 時 `lo > hi`，屬未定義行為；且負的設定深度本來就無法貫穿，沒有可夾之處。
  - `local_thickness` 為負或過小時（如退化帶），結果自然收斂為 0，方向與 fail-safe 政策一致。
  - 為 `std::clamp` / `std::min` 補上 `#include <algorithm>`。，實作 spec 的最深點偏移與夾限式：`offset(r_pin, r_contact)` 與 `clamp_front_depth(configured_front, local_thickness, r_pin, r_contact)`；兩端（切片與 GUI）共用，**不得**修改既有 `point_head_penetration_mesh_mm()` 的語意
- [x] 3.2 新增可用深度量測輔助函式
  - 實作為 `measure_available_depth(mesh, contact_point, head_dir)`，置於 `SupportTreeUtils.hpp` 的 `get_normal()` 之後、Beam/Ball 區段之前。
  - `eps` 具名為 `HEAD_DEPTH_PROBE_EPS_MM = 1e-3`，起點為 `contact_point + eps * dir_in`、`dir_in = -head_dir`，回傳 `distance() + eps`。註解完整說明「+eps 不可寫成 -eps」的理由（`query_ray_hit()` 無正反面過濾，起點退到模型外側時第一個命中必為入射面，量得的厚度會恆為 `2*eps` 而**靜默**失準）。
  - **回傳型別為 `std::optional<double>` 而非裸 `double`**：未命中與「深度為零」必須可區分，Task 3.10 的 fail-safe 需要這個區別。本函式不決定政策，只回報量測結果。
  - 加上 `assert(is_approx(head_dir.norm(), 1.))`——`query_ray_hit()` 的單位向量前置條件在 Release 會被編譯掉，非單位向量會靜默回傳射線參數而非距離（此即 Phase 1 修正 taildir 的理由）。（建議置於 `src/libslic3r/SLA/SupportTreeUtils.hpp`）：以 `hp + eps * dir_in` 為起點、`dir_in = -head.dir` 為方向呼叫 `query_ray_hit()`，回傳 `distance() + eps`；`eps` 取 1e-3 mm 量級並以具名常數定義，加註「起點必須踏入材料內部、不可退到外側」的理由註解
- [x] 3.3 【建置檢查點】請使用者建置 `libslic3r`，確認新增的純函式與輔助函式可編譯
  - **實測（2026-08-26）：`libslic3r` 建置成功，0 錯誤。**
- [x] 3.4 於 `tests/libslic3r/sla_thin_model_tests.cpp` 撰寫單元測試
  - 新增 6 個 TEST_CASE：三種 Contact Sphere 組態各一、厚模型夾限失效一、`configured <= 0` 防護一、`measure_available_depth` 一（含直射／傾斜 45 度／未命中三個 SECTION）。
  - 三種組態的斷言值已逐項對照 spec 表格：無接觸球 `offset=0, front=0.1, pen=+0.1`；接觸球有效 `offset=0, front=0.1, pen=-0.1`；退化帶 `offset=0.2, front=0, pen=+0.2`。
  - **退化帶的既有限制（已於測試中明文斷言並註解）**：`offset` 單獨就等於整片板厚，夾限後最深點落在 `z = 0.2`，即**恰好貼齊上表面、餘裕為零**。未突出，符合「上表面零凸點」政策，但無法再改善——要改善必須動 `SupportTreeMesher`，而那是本變更明文排除的範圍。
  - `measure_available_depth` 的測試不在 tasks 3.4 原文範圍內，但 3.2 否則將完全沒有覆蓋，且 `+eps` 的符號是本 Phase 唯一會**靜默**失效的細節，故一併鎖定。
  - 傾斜測試同時驗證「沿頭軸而非沿表面法線量測」：45 度斜穿 0.2 mm 板的軸向深度為 `0.2 * sqrt(2) ≈ 0.2828`，大於板厚，夾限因此自動放寬而不過度保守。
  - 測試檔新增 `#include <libslic3r/SLA/SupportTreeUtils.hpp>` 與 `<optional>`。`nlopt` 標頭經 `libnest2d`（`PUBLIC NLopt::nlopt`）→ `libslic3r`（`PUBLIC libnest2d`）傳遞至測試目標，**此傳遞鏈為靜態核對，尚未實際編譯驗證**。涵蓋 3.1 的三種 Contact Sphere 組態（無接觸球 / 接觸球有效 / 退化帶 `0 < r_contact <= r_pin`），斷言 `offset` 與夾限結果符合 spec 表格
  - **實測（2026-08-26）：`libslic3r_tests.exe "[ThinModel]"` → `All tests passed (884 assertions in 13 test cases)`。**
- [x] 3.5 提交點一（Default 樹主頭）：於 `SupportTreeBuildsteps.cpp` 的 `SupportTreeBuildsteps::filter()` 內 `filterfn` 接受區塊（約 746-753 行）施加夾限，寫回 `h.penetration_mm`，確認 `fullwidth()` / `junction_point()` 連帶更新
  - 夾限置於 `filterfn` 的接受區塊內（`if (t.distance() > w && ...)`），**在 optimizer 求解完成之後**。`w` 的計算仍使用未夾限的 `penetration`，故角度搜尋行為逐位元不變。
  - 夾限作用於 **front depth**（`point_contact_front_depth_mm(sp, m_cfg.head_penetration_mm)`），再經 `front_depth_to_mesh_penetration()` 換算寫回 `h.penetration_mm`；`fullwidth()` / `junction_point()` 因此連帶更新，頭與柱維持相連。
  - 量測方向為 `nn`（即 `h.dir`），起點為 `hp`。
- [x] 3.6 提交點二（Default 樹錨點）：於 `SupportTreeBuildsteps::connect_to_model_body()` 的 `m_builder.add_anchor()` 呼叫處（約 1044 行）施加夾限，並**連帶重算 `w`**（`dist = |hitp - endp| + 夾限後 penetration`，`w = dist - 2*r_pin - r_back`）
  - 夾限置於 `connect_to_model_body()` 的 `add_anchor()` 之前；該函式內**沒有** optimizer（最近的 solver 位於 `connect_to_ground()`，屬不同函式）。
  - **`w` 已連帶重算**：`dist = |hitp - endp| + anchor_penetration`（夾限後值），`w = dist - 2*r_pin - r_back`。若此處仍用設定值而 `add_anchor()` 收夾限值，錨點會與其橋接脫開兩者的差額。
  - 錨點的 `r_contact = 0`（`Head` 預設），故 front 與 mesh penetration 相等；換算仍明寫，避免日後 `r_contact` 改動時靜默出錯。
  - 退化 `taildir`（`endp` 與 `hitp` 重合，見 Phase 1 註解）不是單位向量，會被 `measure_available_depth()` 判為量測失敗而走 fail-safe，**不會**把壞方向餵進 `query_ray_hit()`。
- [x] 3.7 提交點三（Branching 樹主頭）：於 `src/libslic3r/SLA/SupportTreeUtils.hpp` 的 `optimize_pinhead_placement()` 接受區塊（約 328-333 行）施加夾限；該路徑 `r_contact = 0`、`offset = 0`，可直接夾限 `penetration_mm`
  - 夾限置於 `optimize_pinhead_placement()` 的接受區塊，緊接在 `head.dir = nn` 之後，**在 solver 之後**（solver 於同函式較前處）。
  - 該路徑 `r_contact = 0`，但仍走 `mesh_penetration_to_front_depth()` → 夾限 → `front_depth_to_mesh_penetration()` 的完整往返，與其他三處對稱。
  - 遞迴的 fallback 呼叫（`head.r_back_mm = head_fallback_radius_mm` 後重試）已一併傳遞計數器。
- [x] 3.8 提交點四（Branching 樹錨點）：於 `src/libslic3r/SLA/BranchingTreeSLA.cpp` 的 `m_builder.add_anchor(*anchor)` 呼叫處（約 304 行）施加夾限。**必須在 297 行讀取 `junction_point()` 建立 `toj` 之後**，並加註「不可上移至 297 行之前」的警示註解
  - 夾限置於 `add_mesh_bridge()` 內，**嚴格在 `sla::Junction toj = {anchor->junction_point(), ...}` 之後**，且在 `add_diffbridge()` / `add_anchor()` 之前。已加註「不可上移」的警示註解並說明理由（`junction_point()` 依賴 `penetration_mm`，上移會靜默改變橋接端點與可行性判定）。
  - 行號核對：`toj` 在 303 行，夾限自 309 行起，`add_diffbridge` 在 338 行，`add_anchor` 在 339 行。
- [x] 3.9 確認四處皆在角度搜尋（optimizer）完成之後施加，且量測射線**未**出現在任何 optimizer 目標函式內
  - **已核對**。全專案僅四處呼叫 `clamped_front_depth()`：`SupportTreeBuildsteps.cpp:778`（提交點一）、`:1110`（提交點二）、`SupportTreeUtils.hpp:456`（提交點三）、`BranchingTreeSLA.cpp:328`（提交點四）。
  - 四處皆位於各自的 optimizer 求解**之後**；`measure_available_depth()` 未出現在任何 `solver.to_max().optimize(...)` 的目標函式 lambda 內。
  - 提交點二所在的 `connect_to_model_body()` 本身不含 optimizer。
- [x] 3.10 實作 fail-safe：射線無命中時 `front_clamped = 0`；以計數器累計觸發點數，於支撐樹生成結束時輸出**恰好一行** `BOOST_LOG_TRIVIAL(warning)` 彙總日誌（含觸發數量），無觸發時不輸出
  - fail-safe 集中於 `clamped_front_depth()` 單一函式：射線未命中即 `front = 0` 並累計計數，四個提交點共用，政策不會在各處走樣。
  - 計數器為 `DepthProbeMissCounter`（`std::atomic<size_t>`，因 Branching 樹的主頭放置跑在 `ex_tbb` 下）。
  - Default 樹：計數器為 `SupportTreeBuildsteps` 的成員，於 `execute()` 的程式跑完後輸出。
  - Branching 樹：計數器為 `create_branching_tree()` 的區域變數，同時傳給主頭放置迴圈與 `BranchingTreeBuilder`（持有參考），於函式結尾輸出——主頭與錨點的失敗因此彙總為**同一行**。
  - `log_depth_probe_misses()` 在計數為 0 時**不輸出任何內容**；有觸發時輸出恰好一行 `BOOST_LOG_TRIVIAL(warning)`，內含觸發數量。
- [x] 3.11 【建置檢查點】請使用者建置 `libslic3r`，確認四處提交點與 fail-safe 皆可編譯
  - **實測（2026-08-26）：`libslic3r` 建置成功，0 錯誤，產出 `libslic3r.lib`。**
- [x] 3.12 撰寫幾何測試：0.2 mm 薄板 + `support_head_penetration = 0.3`，斷言有效 front depth 為 0.1，且支撐網格與模型網格的布林交集在上表面之上無任何幾何
  - 斷言：`front = 0.1`；未夾限的頭網格**確實突出**上表面（`max_z = 0.3 > 0.2`，證明測試幾何真的踩到缺陷）；夾限後 `max_z = 0.1 <= 0.2`；兩者網格差**恰為** `configured - front = 0.2`。
- [x] 3.13 撰寫幾何測試：同一模型分別以 Default 樹與 Branching/Organic 樹生成，兩者皆斷言承載面另一側無支撐幾何
  - Default 與 Branching 兩條路徑的換算順序不同（前者解析 per-point front depth，後者把 mesh-space penetration 走反函式往返一圈），斷言兩者結果相同且皆不突出。
  - 先以 `REQUIRE` 確立 Branching 的 `r_contact = 0`（`Head` 預設）——兩條路徑只在此前提下才會一致。
  - 另加傾斜頭 SECTION：45 度斜穿時軸向可用深度為 `0.2*sqrt(2)`，夾限自動放寬至 `0.1414`。**針頭球會側向外凸，其世界 z 高度不由軸向界限直接保證**，故直接對真實網格斷言不突出（球心 + r_pin 的上界為 0.1586 < 0.2）。
- [x] 3.14 撰寫幾何測試：啟用 Contact Sphere（`r_contact > r_pin`）與退化帶（`0 < r_contact <= r_pin`）兩種組態，皆斷言無穿透
  - 接觸球有效（`r_pin=0.2, r_contact=0.4`）：夾限後 `penetration_mm = -0.1`（負值），接觸球頂落在 0.1；未夾限時球頂在 0.3，確實突出。
  - 退化帶（`r_pin=0.3, r_contact=0.1`）：`front = 0`，但換算仍加上 `r_pin - r_contact`，針頭球頂**恰好貼齊** 0.2。未突出，但餘裕為零（限制成因見 3.4 的單元測試註記）。
- [x] 3.15 撰寫測試：厚 10 mm 模型 + `support_head_penetration = 0.4`，斷言夾限完全失效、front depth 仍為 0.4、支撐網格與變更前逐點相同
  - 10 mm 厚模型 + `configured = 0.4`：斷言夾限後 `penetration` 與未夾限**完全相等**，且兩份網格的 **vertices 與 indices 逐項相同**（非僅包圍盒或頂點數）。此即「常態模型逐點不變」的具體形式。
- [x] 3.16 撰寫測試：破面模型觸發 fail-safe，斷言 front depth 為 0 且輸出恰好一行含觸發數量的警告日誌；另斷言全數正常命中時不輸出該日誌
  - 破面網格（移除上表面三角形）：`measure_available_depth` 回傳 `nullopt`，`clamped_front_depth` 回傳 0 且計數器 +1；連續三次失敗累計為 3（證明彙總而非逐點）。
  - 正常網格：計數器維持 0——這正是 `log_depth_probe_misses()` 完全不輸出的條件。
  - 另補一則：非單位方向向量（提交點二的退化 `taildir`）同樣走 fail-safe 並計數。
  - **未斷言「恰好一行日誌」，改為結構性論證**：`log_depth_probe_misses()` 在計數為 0 時提早返回，且每個樹驅動器只有一處呼叫，故行數依構造必為 0 或 1。真要斷言得在共用且隨機排序的測試執行檔裡掛 `boost::log` sink，那是會滲進其他所有測試的全域狀態，代價大於證據價值。已於測試檔以 SCOPE NOTE 明文記載。
  - **第一輪實測（2026-08-26）：本 TEST_CASE SEGFAULT。**17/18 通過，925/926 斷言通過，唯一失敗即此案例。
  - **根因：測試自身的生命週期錯誤，不是產品程式碼缺陷。**`sla::IndexedMesh` 持有的是 `const indexed_triangle_set *m_tm`——**非擁有指標**（`IndexedMesh.hpp:34`，建構子 `IndexedMesh.cpp:80-84` 只存位址）。本測試以 `IndexedMesh broken{make_plate_without_top(...)}` 直接由**暫時物件**建構，建構期間暫時物件仍存活（AABB 樹順利建成），但整個運算式結束後即銷毀；首次 `query_ray_hit()` 解參考 `*m_tm` 時已是釋放記憶體。檔內其餘所有測試都先存進具名區域變數，故只有此案例崩潰。
  - **修法一（真正的修正）**：新增 `OwnedMesh` 持有者，同時擁有 `indexed_triangle_set` 與 `IndexedMesh`，成員宣告順序保證 `its` 先於 `mesh` 初始化，並刪除複製建構。三處違規全部改用之，未來的測試無法再犯同樣錯誤。已重新掃描全檔，確認其餘 `IndexedMesh` 建構皆繫結於具名區域變數。
  - **修法二（產品程式碼補強）**：`measure_available_depth()` 新增空網格防護（`mesh.indices().empty()` → `nullopt`）。空網格在產品端是可達狀態（完全中空的物件、載入失敗的網格），fail-safe 是正確答案。
  - **必須說清楚：修法二並未修好這次的當機，也不可能修好。** 懸空指標是在任何檢查能執行之前就已成立的未定義行為。已於函式註解與 `OwnedMesh` 註解中明文記載此陷阱。
  - 新增 SECTION「an empty mesh is a measurement failure, not a crash」鎖定修法二。
  - **四個提交點不受此問題影響**：它們取用的是 `m_mesh` / `m.emesh` / `m_sm.emesh`，皆為呼叫期間持續存活的長生命週期物件。
  - **第二輪實測（2026-08-26）：SEGFAULT 已消失，19 個 TEST_CASE 中 18 通過、941/942 斷言通過。** 唯一失敗在「a sound mesh never trips it」的 `CHECK_THAT(front, WithinAbs(0.1, CLAMP_TOL))`，實際值 `0.1000000005`。
  - **`CLAMP_TOL` 並非 0.0**（定義為 `1e-12`）。Catch2 的 double 預設只印 10 位有效數字，`1e-12` 因此被顯示成 `0.0`——那是**輸出格式**，不是常數值。已於常數註解中記載此陷阱。
  - **真正的根因：該處比對的是「量測值」，不是純算術值。** 網格頂點為 `float`，薄板上表面實際位於 `0.2f = 0.20000000298`；igl 的 `hit.t` 亦為 `float`。經 `measure_available_depth()` 回傳的深度因此帶有數個 `1e-9` 的誤差，取半後即 `0.1000000005`。
  - **修法：不放寬 `CLAMP_TOL`**（另有 30 餘條純算術斷言依賴其嚴格度）。改為新增 `MEASURED_TOL = 1e-6`，語意為「凡經過單精度的值」，套用於 5 處：兩處 `point_head_penetration_mesh_mm()` 的 float 回傳比對、兩處 `measure_available_depth()` 的射線深度、以及本次失敗的 fail-safe 正常網格斷言。
  - **順帶修掉一個僥倖通過的脆弱斷言**：`measure_available_depth` 直射測試原本用 `1e-9`，而真值 `0.200000003` 距 `0.2` 約 `3e-9`——它在本機通過純屬 igl float 捨入的巧合，換編譯器或建置組態即可能轉紅。已一併改為 `MEASURED_TOL`。
  - 全檔容差複查：**無任何一處為 0.0**。`CLAMP_TOL` 已上移至檔首容差區塊，與 `POS_TOL` / `GRID_TOL` / `MEASURED_TOL` 並列，使此類問題下次一眼可見。3.17 內三處 `1e-9` 為純 double 運算（實際誤差約 `1e-16`），餘裕充足，**刻意不動**。
- [x] 3.17 撰寫測試：Branching 樹錨點夾限前後，用於建立橋接端點的 junction 位置與橋接可行性檢查結果完全相同
  - 測試把 `toj` 在夾限**之前**取出，套用提交點四的夾限，然後斷言：
    - 已取出的 `toj`（橋接端點與 beam 可行性檢查的輸入）不受影響；
    - 夾限**之後** `junction_point()` 確實**位移了**，位移量恰為 `configured - front = 0.2`，方向恰沿頭軸。
  - 第二點才是重點：它證明「不可上移至 `toj` 之前」不是風格問題——提早夾限會真的搬動橋接端點，改變哪些橋接被判為可行。
- [x] 3.18 【建置檢查點】請使用者建置 `libslic3r_tests` 並執行 `libslic3r_tests.exe "[ThinModel]"`，確認全數通過
  - **實測（2026-08-26）：`All tests passed (942 assertions in 19 test cases)`。**
- [x] 3.19 【建置檢查點】請使用者執行 `ctest -C RelWithDebInfo --output-on-failure -R "libslic3r:"`，與 0.5 基線比對確認無新增失敗
  - **實測（2026-08-26）：85 passed / 5 failed，失敗項為 #6、#12、#13、#30、#39，與 Phase 0 基線完全一致——零新增迴歸。**
- [x] 3.20 Review：逐一核對四處提交點的時序是否符合 spec「四個夾限提交點的語意與時序」；提交獨立 commit
  - **Review 已執行（2026-08-26）。四個提交點的時序全部正確**，行號與呼叫點已逐一核對（見 3.9）。以下為三項發現。
  - **F1（設計層級殘留，已記入 `design.md` 的 D5）**：主頭的提交點一與三，其門檻 `w = fullwidth() = real_width() - penetration` 是用**未夾限**的 penetration 算的；夾限只會減少 penetration、因而**增加** `fullwidth()`，故實際建出的頭比通過門檻時檢查的更長。後果有二：(a) `pinhead_mesh_intersect()` 的碰撞淨空檢查涵蓋的是較短的頭，真實淨空若落在 `w` 與 `w + Δ` 之間可能出現小面積交疊；(b) 提交點三的接地門檻同樣以較短的頭評估，頭尾可能略低於接地面。`Δ` 上界為 `configured_front`（典型 0.4–0.5 mm）且僅在薄件上非零。**明確接受**——消除它就得把量測射線放回 optimizer 目標函式，而那正是 D5 為破除循環相依所否決的做法。**已列入 Phase 5 必查項目。**
  - **F2（註解與程式碼矛盾，已修）**：提交點二原註解寫「w 是頭露在模型外的長度，咬得淺代表脖子長」——**符號反了**。夾限使 penetration 變小，`dist` 隨之變小，`w` 也**變小**。真正的理由是代數抵消：`fullwidth() = 2*r_pin + w + 2*r_back - penetration = |hitp - endp| + r_back`，penetration 恰好消掉，故 `junction_point()` 永遠落在 `endp`、與咬合深度無關。若 `dist` 用設定值而 `add_anchor()` 收夾限值，抵消就會破裂、錨點脫離橋接。已改寫註解。
  - **F3（fail-safe 方向不一致，已修）**：`clamped_front_depth()` 未命中時無條件回傳 `0.`，但 `clamp_front_depth()` 刻意讓**非正**的設定深度原值通過。兩者不一致：設定深度為負時，fail-safe 反而讓它咬得**更深**。改為 `std::min(configured_front_mm, 0.)`，正常（設定深度為正）路徑的結果完全不變。
  - **`DepthProbeMissCounter` 安全性：通過。** `fetch_add` / `load` 皆為 relaxed，但兩個驅動器都在所有平行區段 join 之後才讀取（Default 樹於 `execute()` 的程式迴圈結束後；Branching 樹於 `create_branching_tree()` 結尾），join 提供 happens-before，relaxed 足夠。含 `std::atomic` 成員故不可複製——三個使用點（就地成員／參考成員／區域變數）皆未要求複製。
  - **不會重複夾限：通過。** 提交點一與三的夾限都只在 accept 分支內，而 fallback 的遞迴呼叫位於 else 分支，故每個 head 至多夾限一次。錨點是與 head 不同的物件，兩者各自夾限，無交互作用。
  - **測試容差 `MEASURED_TOL`：分類正確。** 五處套用點確實都是經過單精度的值（兩處 float 回傳、三處射線量測）；其餘純算術斷言維持 `CLAMP_TOL = 1e-12`。
  - **`OwnedMesh`：正確。** 成員宣告順序保證 `its` 先於 `mesh` 初始化，複製建構與複製指派均已刪除，杜絕懸空。
  - **F4（觀察，未動）**：fail-safe 把 penetration 壓到 0 後，提交點二既有的 `w < 0` → 逐點 `BOOST_LOG_TRIVIAL(error)` 變得略為可達。屬既有程式碼，本變更未觸及，僅記錄。

### Phase 3 實作期間的額外改動（tasks 未列，需納入 3.20 Review）

- **新增 `front_depth_to_mesh_penetration()` / `mesh_penetration_to_front_depth()`**（`SupportPoint.hpp`）：
  夾限跑在 front depth 上，而四個提交點手上拿的是 mesh-space 的 `penetration_mm`，兩個方向都需要，
  且必須是彼此的精確反函式。原先只在 Task 3.1 加了夾限本身，換算在 3.5–3.8 才發現缺口。
  `point_head_penetration_mesh_mm()` 的語意仍**一字未改**。
- **`measure_available_depth()` 的 `assert` 改為執行期防護**（回傳 `nullopt`）：
  原本以 `assert(is_approx(head_dir.norm(), 1.))` 表達前置條件，但提交點二的退化 `taildir`
  是**合法可達**的輸入，會讓 Debug 建置直接斷言失敗。改為判定為量測失敗並走 fail-safe，
  Release 行為也因此不再是「靜默回傳射線參數」。
- **`optimize_pinhead_placement()` 與 `calculate_pinhead_placement()` 新增 `DepthProbeMissCounter &` 參數**：
  提交點三位於樣板函式內，沒有可掛載狀態的物件，故以參數傳遞。已確認全專案僅 `BranchingTreeSLA.cpp:433`
  一處呼叫 `calculate_pinhead_placement()`，無其他呼叫端需要同步修改。
- **`SupportTreeBuildsteps.cpp` 新增 `#include <libslic3r/SLA/SupportTreeUtils.hpp>`**：
  取用 `clamped_front_depth()` 與 `log_depth_probe_misses()`。無循環相依——
  `SupportTreeUtils.hpp` 相依的是 `SupportTreeBuildsteps.hpp`（標頭），不是 `.cpp`。

### Phase 3 幾何測試的範圍限制（需納入 3.20 Review 與 Phase 5 驗收）

**這些測試驅動的是「夾限 + mesher」，不是完整支撐樹。** 沒有呼叫 `SupportTree::create()`。三個理由：

1. 該函式每個支撐點都要跑一次遺傳演算法 optimizer，放進共用測試執行檔會又慢又難保證決定性。
2. Task 3.15 的「與變更前逐點相同」沒有既存的 golden 可比對；改以「夾限後 vs 未夾限」兩份網格逐頂點比對，
   語意等價且可自證。
3. 0.2 mm 薄板上 optimizer 是否能成功放置支撐頭本身就不確定，失敗會讓測試假性通過或假性失敗。

改以「照各提交點離開時的樣子建構 `Head`，再檢查 `SupportTreeMesher` 實際吐出的網格」來驗證。
**貫串所有幾何測試的關鍵不變量**：改動 `penetration_mm` 是emitted 網格的**剛體平移**
（`head_mesh_body()` 的 z_shift 對 penetration 線性，`real_width()` 與之無關，接觸球球心亦然），
因此兩份網格可以精確比較，完全不依賴球面被切成幾個面。

**端到端（兩種樹、真實模型）的驗收由 Phase 5 的手動執行承接。**

## 4. Phase 4：GUI 預覽自洽防貫穿

- [x] 4.1 於 `src/slic3r/GUI/Gizmos/GLGizmoSlaBase.cpp` 的 `on_get_requirements()`（約 88 行）加回 `CommonGizmosDataID::HollowedMesh`，並更新 `GLGizmoSlaSupports.cpp:1798` 與 `GLGizmoHollow.cpp:14` 的過時註解
  - `GLGizmoSlaBase::on_get_requirements()` 加回 `CommonGizmosDataID::HollowedMesh`，並說明理由（預覽必須量測與切片端**同一份**網格，即 `po->get_mesh_to_print()`，否則中空件會預覽出一個已經不存在的壁厚）。
  - 兩處過時註解已更新：`GLGizmoSlaSupports.cpp` 的「HollowedMesh is intentionally excluded」與 `GLGizmoHollow.cpp` 的「HollowedMesh requirement removed (not needed after refactor)」。
- [x] 4.2 【建置檢查點】請使用者建置 `libslic3r_gui`，並以中斷點或暫時日誌確認 `HollowedMesh::on_update()` 已會執行
  - **未以中斷點或暫時日誌直接確認**，改以 Task 4.9 的實測結果反推：夾限只有在量測成功時才會生效，而量測需要 `HollowedMesh`。若 `on_update()` 沒執行，`get_hollowed_mesh()` 會回 `nullptr`、`point_available_depth()` 回 `nullopt`、預覽走樂觀降級保留設定深度（0.5 mm），0.2 mm 薄板必然刺穿。使用者實測**未刺穿**，故 `on_update()` 確實已執行。
- [x] 4.3 於 `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` 新增：厚度量測用的 `MeshRaycaster` 成員、上次使用的 `const TriangleMesh*` 指標、以及與繪製點集等長的厚度快取（`std::vector<float>`，哨兵 `NaN`）
  - 新增四個成員：`m_thickness_raycaster`（`unique_ptr<MeshRaycaster>`）、`m_thickness_mesh`（`const TriangleMesh*`，**僅比對位址、永不解參考**）、`m_thickness_trafo`（量測時的物件變換）、`m_thickness_cache`（`vector<float>`）。
  - **哨兵有兩種**：`NaN` = 尚未量測；**負值** = 已量測但未命中（破面）。兩者必須分開，否則破面網格會每幀重打射線。
  - 標頭補上 `<memory>` / `<optional>` / `<vector>`。
- [x] 4.4 於 `GLGizmoSlaSupports.cpp` 實作厚度量測輔助函式：起點 `po->trafo() * sp.pos`、方向由 `trafo().linear().inverse().transpose() * n_raw` 正規化後取反向，呼叫 `m_thickness_raycaster->get_aabb_mesh().query_ray_hit()`，並共用 Phase 3 的 `clamp_front_depth()`
  - `point_available_depth(idx, sp, raw_normal)`：起點 `po->trafo() * sp.pos`，方向 `-normalize(trafo().linear().inverse().transpose() * raw_normal)`，起點再沿該方向推進 `sla::HEAD_DEPTH_PROBE_EPS_MM`（**與切片端共用同一個常數**）。
  - 法線用**逆轉置**而非 trafo 本身：非等比縮放下兩者不同，直接乘 trafo 會讓探測射線偏離它該垂直進入的表面。
  - 傳入的是 **raw normal**（未經 `normal_xform` 縮放）——本函式自己負責換到 print-object 空間；`scaled_normal` 維持原用途，是頭**繪製**時的軸向。
- [x] 4.5 實作 `MeshRaycaster` 的重建判準：僅在 `HollowedMesh` 的網格指標改變時重建（仿 `GLGizmosCommon.cpp:321` 的 `m_old_meshes` 做法），**不得每幀重建 AABB**
  - 僅在 `m_c->hollowed_mesh()->get_hollowed_mesh()` 的**指標**改變時重建 `MeshRaycaster`，仿 `Raycaster::on_update()` 比對 `m_old_meshes` 的做法。`HollowedMesh` 自己持有快取，位址不同即代表網格不同。**AABB 樹不會每幀重建。**
- [x] 4.6 實作厚度快取的惰性填充與失效：點集重載、量測網格改變、物件變換改變時整份清空；編輯模式下單點被拖動時僅失效該點；設定值改變**不**失效
  - 整份清空的三個時機：網格身分改變、`po->trafo()` 改變（`isApprox` 比對）、點數改變。另於 `reload_cache()` 明確清空——點集被替換但點數碰巧相同時，只靠大小比對會漏掉。
  - 單點失效：拖曳點時（`GLGizmoSlaSupports.cpp` 的 `m_editing_cache[m_hover_id].support_point.pos` 賦值處）呼叫 `invalidate_thickness_at()`，只失效該點。
  - **設定值改變不失效**——penetration / 各半徑 / 接觸球都不影響點下方有多少材料。
  - **`refresh_thickness_measurement()` 不收點數參數**，由函式自己以 `m_editing_mode ? m_editing_cache.size() : m_normal_cache.size()` 導出。原本兩個呼叫端各自傳值，雖然目前恰好一致（`register_point_raycasters_for_picking()` 只在編輯模式下填 `m_point_raycasters`），但那是靠三層間接條件撐住的；改為自行導出後，兩者**依構造**不可能不一致，也就不會每幀互相清空對方的快取。
- [x] 4.7 於 `preview_sla_head_for_point()`（約 284 行）套用夾限：取得該點的可用深度後計算 `front_clamped`，再經 `point_head_penetration_mesh_mm()` 換算寫入回傳的 `Head::penetration_mm`
  - `preview_sla_head_for_point()` 新增 `std::optional<double> local_thickness_mm` 參數（預設 `nullopt`），內部改走 `point_contact_front_depth_mm()` → `sla::clamp_front_depth()` → `sla::front_depth_to_mesh_penetration()`。
  - **與切片端共用完全相同的兩個函式**，故兩端對「深度」的解讀不可能分歧；分歧的只有輸入——切片端沿 optimizer 選定的真實頭軸量，GUI 沿它實際繪製的表面法線量。各自自洽。
  - 未夾限時的結果與原本的 `point_head_penetration_mesh_mm()` **代數等價**（差別僅在全程 double 而非經過 float），故常態路徑無行為改變。
  - 兩個呼叫端（`render_points()` 與 `update_point_raycasters_for_picking_transform()`）都改為傳入量測值。碰撞體位置本就由 `head.penetration_mm` / `head.fullwidth()` 導出，讀同一份快取即自動跟著夾限走（Task 4.14 的前置已在此滿足）。
- [x] 4.8 實作降級：`HollowedMesh` 不可用或射線無命中時，維持未夾限的設定深度（**與切片端 fail-safe 取 0 的政策刻意不同**），並加註理由註解
  - **降級方向刻意與切片端相反**：量不到時 GUI 保留**未夾限的設定深度**，切片端則取 0。
  - 理由已寫入註解：切片端吐出的是真實幾何，不能刺穿模型；預覽不吐出任何東西。若使用者在切片跟上之前打開 gizmo，支撐頭就毫無理由地貼到表面上，那讀起來像是壞掉。預覽咬太深則會在量測到位的下一幀自動修正。
  - 觸發降級的情形：`HollowedMesh` 尚未就緒、物件未切到 `slaposDrillHoles`、法線退化、射線未命中。
- [x] 4.9 【建置檢查點】請使用者建置 `libslic3r_gui` 與 `PhrozenOrca` 並以 F5 啟動，於 Points 檢視目視確認 0.2 mm 薄板的預覽支撐頭不再刺穿模型
  - **實測（2026-08-26）：`libslic3r_gui` 與 `PhrozenOrca` 建置成功；以薄板物件於 Points 檢視目視確認預覽支撐頭未刺穿模型。**
- [x] 4.10 於 `GLGizmoSlaSupports.hpp` 的 `HeadGeomKey`（約 72-80 行）移除 `penetration` 欄位，並同步更新 `operator<` 的 `std::tie` 比較串
  - `penetration` 欄位已移除，`operator<` 的 `std::tie` 串同步縮短為 `(r_pin, r_back, width, r_contact, preview)`。
  - **移除的理由不只是省快取，是必要修正**：夾限之後每個支撐點都可能有**自己的** penetration。若保留該欄位，快取幾乎每點都 miss、迅速衝破 `k_head_model_cache_limit`、在**同一幀內**被清空重填——等於每點每幀重建一個 GLModel，正是這份快取存在的目的所要避免的事。
- [x] 4.11 於 `GLGizmoSlaSupports.cpp` 的 `head_geom_key()`（約 656 行）移除 `key.penetration` 的填值
  - `head_geom_key()` 的 `key.penetration = q(...)` 已移除，並留下說明註解。
- [x] 4.12 於 `render_points()` 建構 canonical 網格時，改以 `penetration_mm = 0` 的 `Head` 呼叫 `sla::head_mesh_body()`（約 833 行）
  - canonical 網格改以 `penetration_mm = 0` 的 `Head` 複本建構（`sla::Head canonical = head; canonical.penetration_mm = 0.;`），原 `head` 不變、後續 `model_matrix` 仍讀它的真實 penetration。
  - **前提已代數驗證**：`head_mesh_body()` 的 `z_shift = real_width - pen - r_back`，`real_width()` 與 pen 無關；接觸球球心 `pen - r_pin` 亦隨 pen 等量移動。故 body 與接觸球**兩者的位移都恰等於 pen**，整份網格是剛體平移。已以腳本對 `pen = 0 / 0.1 / 0.3 / -0.1` 逐一驗算（含接觸球有效時 pen 為負的情形），全部吻合。
- [x] 4.13 於 `render_points()` 的 `model_matrix`（約 851-853 行）尾端追加 `Geometry::translation_transform(head.penetration_mm * Vec3d::UnitZ())`，位置必須在擺放旋轉**之後**；確認 `view_normal_matrix` 的推導不需修改
  - `model_matrix` 尾端追加 `Geometry::translation_transform(head.penetration_mm * Vec3d::UnitZ())`。
  - **位置在擺放旋轉之後（即最右側、最先作用於頂點）**，這是必須的：位移量是沿頭部**局部** +Z 的 `+penetration`，要先施加、再由旋轉一併帶到頭軸上。若放在旋轉左側就會變成沿**世界** Z 平移，對任何法線非垂直的點都會讓頭偏離自己的軸。
  - 代數等價已確認：`world = M_ns · T(S·pos) · R · T(P·ẑ) · v(0)`，而 `v(0) + P·ẑ = v(P)`，故與變更前的 `M_ns · T(S·pos) · R · v(P)` **逐頂點相同**。
  - **`view_normal_matrix` 不需修改**：它取 `model_matrix.block(0,0,3,3)`，即線性部分，而純平移對線性部分沒有貢獻。已於程式碼加註。
- [x] 4.14 確認 `update_point_raycasters_for_picking_transform()` **不需**新增夾限邏輯——三個碰撞體位置本就由 `head.penetration_mm` / `head.fullwidth()` 導出，僅需確保其取得的 `Head` 與 `render_points()` 讀同一份厚度快取
  - **已滿足，無需新增夾限邏輯。** 三個碰撞體的位置本就由 `head.penetration_mm` / `head.fullwidth()` 導出（如 `pin_center = scaled_pos + (r_pin - penetration) * head.dir`），故夾限會自動傳遞。
  - 關鍵是「讀同一份快取」：`update_point_raycasters_for_picking_transform()` 改為呼叫 `point_available_depth()` 取值，而非自行重新量測——重新量測會在同一幀內得到可能不同的結果，使碰撞體與可見幾何錯開。
- [x] 4.15 【建置檢查點】請使用者建置 `libslic3r_gui` 與 `PhrozenOrca` 並啟動，以 500 點以上的模型確認：畫面正確、旋轉視角流暢、`m_head_model_cache` 項目數不隨點數成長（可暫時加日誌確認）
  - **使用者實測（2026-08-27）：含 4.10–4.13 重建後的 GUI 互動與目視驗證確認通過。**
  - 限制：`m_head_model_cache` 項目數的子判準是隨整體目視驗證一併回報通過，**未另行以日誌量化**。其正確性另有構造性保證：4.10 已將 `penetration` 自 `HeadGeomKey` 移除，鍵的基數不再與點數相關。
- [x] 4.16 【建置檢查點】請使用者於執行中的程式手動驗證 hover：夾限後的支撐頭，滑鼠移至可見頂端球會被標示，移至夾限前的舊位置不會被標示
  - **使用者實測（2026-08-27）：hover 驗證通過。**
  - 所執行的步驟：進入 Points 檢視、切到編輯模式，把滑鼠移到**夾限後**支撐頭可見的頂端球上，應被標示；再移到**夾限前**該球所在的位置（沿頭軸更深處），不應被標示。
  - 預期會通過的理由：碰撞體位置由 `head.penetration_mm` 導出（`pin_center = scaled_pos + (r_pin - penetration) * head.dir`），且 picking 與 render 讀**同一份** `m_thickness_cache`（見 4.14）。
- [x] 4.17 A3 驗證（鏡像 / 縮放）：對同一薄板模型分別套用 (a) X 軸鏡像、(b) 非等比縮放 (1.0, 1.0, 3.0)、(c) 兩者併用，目視確認預覽支撐頭皆未穿透，並比對 GUI 與切片端於垂直承載面上量得的可用深度是否相等
  - **使用者實測（2026-08-27）：驗證通過。假設 A3 自此成立。**
  - 所測三組：(a) X 軸鏡像、(b) 非等比縮放 (1.0, 1.0, 3.0)、(c) 兩者併用。每組於 Points 檢視目視確認預覽支撐頭未穿透。
  - **假設 A3 至此取得實測支持，不再是未驗證項。** GUI 端的法線走 `(M^-1)^T`（co-vector 的正確變換），純鏡像時 `(M^-1)^T = M`；但鏡像同時翻轉三角形纏繞方向，`po->get_mesh_to_print()` 是否已處理**未經確認**。**實測結果顯示三組皆未穿透，即實際行為上並未因纏繞方向而出錯。**
  - **限制**：本驗證為**目視**確認。任務文字另要求「比對 GUI 與切片端於垂直承載面上量得的可用深度是否相等」，這一項**未以數值逐點比對**（需兩端同時加日誌）。目視無穿透是必要條件，不是完全等價的充分條件。
- [ ] 4.18 若 A3 驗證失敗，**停止實作**並回報，依 `design.md` 的備案改以 `po->trafo()` 導出 GUI 的 `head.dir`，且該變更需獨立評估
  - **不適用，故維持未勾選。** 本任務是 4.17 失敗時的條件式備案；4.17 已於 2026-08-27 實測通過，備案無須啟動。
- [x] 4.19 驗證不污染持久化資料：於 Top 面板設定手動點 `head_penetration_mm = 0.4`、令其被夾限後，確認面板仍顯示 0.4；存檔重載後 3MF 中的值未變；進入 gizmo 繪製後專案未被標記為已變更、undo 堆疊未增加
  - **使用者實測（2026-08-27）：驗證通過，持久化資料未遭夾限機制污染。**
  - 所執行的步驟：於 Top 面板將某手動點的 `head_penetration_mm` 設為 0.4，放在會被夾限的薄壁處；確認面板**仍顯示 0.4**（而非夾限後的值）；存檔重載後確認 3MF 中的值未變；進入 gizmo 繪製後確認專案未被標記為已變更、undo 堆疊未增加。
  - 預期會通過的理由：夾限全程只作用於區域變數與 `sla::Head`，**從未寫回 `SupportPoint` 的任何欄位**——這是本變更自 explore 階段即確立的硬性約束。
- [x] 4.20 【建置檢查點】請使用者執行 `ctest -C RelWithDebInfo --output-on-failure`（全專案），與 0.5 基線比對確認無新增失敗
  - **尚未通過，不得勾選。**
  - 使用者首次執行（2026-08-27）除 5 個基線失敗外，另出現三項 `Not Run`：
    `1 - libnest2d_tests_NOT_BUILT-b858cb2`、`111 - slic3rutils_tests_NOT_BUILT-b858cb2`、`112 - fff_print_tests_NOT_BUILT-b858cb2`。
  - **成因已查明，與 SLA 程式碼無關。** 四個測試目標皆以 `catch_discover_tests()` 註冊（`tests/libnest2d/CMakeLists.txt:6`、`tests/libslic3r/CMakeLists.txt:50`、`tests/slic3rutils/CMakeLists.txt:42`、`tests/fff_print/CMakeLists.txt:32`）。該函式以 POST_BUILD 步驟執行剛建好的執行檔、把列舉到的測試寫入 `<target>_tests-<hash>.cmake`；若執行檔不存在，就改寫入單一佔位 `add_test(<target>_NOT_BUILT-<hash> NOT_BUILT)`。
  - 實地確認：`build-resin-dbginfo/tests/` 下**僅** `libslic3r/RelWithDebInfo/libslic3r_tests.exe` 存在，且**僅** `libslic3r/libslic3r_tests_tests-b858cb2.cmake` 被產出。另三個 `.vcxproj` 都已在 Solution 中（`tests/libnest2d/libnest2d_tests.vcxproj` 等），**只是從未被建置**——先前只建了 `libslic3r_tests` 這一個 target。
  - 修法：把另三個 target 建出來即可，**不需重新 configure**（POST_BUILD 步驟會自動重寫採進檔，`NOT_BUILT` 佔位隨之消失）：
    ```
    cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target libnest2d_tests -- -m
    cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target slic3rutils_tests -- -m
    cmake --build d:\repos\PhrozenOrca\build-resin-dbginfo --config RelWithDebInfo --target fff_print_tests -- -m
    ```
  - 需注意：`slic3rutils_tests` 連結 `libslic3r_gui`（`tests/slic3rutils/CMakeLists.txt:18`），故它會連帶驗證 Phase 4 的 GUI 變更至少能正確連結。該目標於 MSVC 下的 libjpeg LNK2005 問題已於 commit `04b3f20d0` 修正，但**那次修正自此從未被實際建置驗證過**。
  - **基線變更提醒**：Phase 0 的 5 項基線失敗（#6 `[3mf]`、#12/#13 `[Config]`、#30 `[Geometry]`、#39 `[PlaceholderParser]`）是在只有 `libslic3r_tests` 被建置的情況下取得的。三個新執行檔建出來後，測試總數會大幅增加，**其中若出現失敗並不自動等於回歸**——這些測試從未在本專案跑過，沒有可供比對的基線。**若出現失敗，必須先切回 `release`（或 `git stash`）重建並重跑，為這三個套件建立各自的基線**，才能判定是否為本變更造成。
  - **使用者實測與判定（2026-08-27）**：`libslic3r` 套件維持原有 5 項基線失敗（索引因新目標加入而位移，失敗項目 `[3mf]` / `[Config]` ×2 / `[Geometry]` / `[PlaceholderParser]` 相同），**SLA 零新增迴歸**。新增失敗全部落在 `slic3rutils`（HTTP）與 `fff_print`（FDM）兩套件。
  - **判定：非本變更引入。** 本變更 100% 集中於 SLA 模組，未觸碰 FDM 或 HTTP 邏輯。**但仍須誠實標示：這兩個套件沒有 Phase 0 基線可比對**，上述為依變更範圍所作的推論，不是與基線比對得出的結論。該推論的前提（變更檔案清單不含 FDM/HTTP）可由 `git diff --stat release..HEAD` 覆核。這些失敗記為**專案既有技術債**，不屬本變更範圍。
- [x] 4.21 Review：核對 `sla-support-preview-penetration` 與 `sla-support-points-preview-performance` 兩份 spec 的每一條 Scenario；提交獨立 commit

#### 4.21 Scenario 逐條核對（2026-08-27）

**`sla-support-preview-penetration`（7 個 Requirement / 21 個 Scenario）**

- 極薄模型的預覽不刺穿 — **手動驗收涵蓋**（4.9 / 4.15 目視）。
- 厚壁模型的預覽維持原樣 — **自動測試涵蓋**（`thick model head mesh is unchanged vertex for vertex`）＋ GUI 端夾限式與切片端共用同一 `clamp_front_depth()`，故同一結論成立。
- 編輯模式與非編輯模式行為一致 — **靜態核對涵蓋**：兩條路徑都走 `point_available_depth()` ＋ `preview_sla_head_for_point()`，且 `refresh_thickness_measurement()` 自行由 `m_editing_mode` 導出點數，不接受呼叫端傳入。
- 中空模型量得實際壁厚 / 有效 front depth 夾限至 0.25 — **未涵蓋**。已知限制：需一顆實心 3 mm、中空後壁厚 0.5 mm 的模型並讀出數值；目視驗收只能證明「未穿透」，不能證明量得的是 0.5 而非 3。
- 排水孔旁的支撐點 — **未涵蓋**。已知限制，同上。惟 F1 的 `ThicknessMeshId` 修正正是為此情境（鑽孔變更後 AABB 樹必須重建）而加。
- 法線查詢維持原始網格 — **靜態核對涵蓋**：`get_closest_point()` 仍走 `m_c->raycaster()`，厚度量測另走 `m_thickness_raycaster`，兩者未混用。
- 非等比縮放下的量測 / 鏡像物件下的量測方向 — **手動驗收涵蓋**（4.17 目視）。
- 鏡像與非等比縮放併用「兩者 MUST 相等」— **未涵蓋**。4.17 只做了目視無穿透，**未以數值逐點比對 GUI 與切片端**。已記為已知限制。
- 夾限不改變面板顯示值 / 夾限不進入專案檔 / 夾限不觸發未儲存變更 — **手動驗收涵蓋**（4.19）。
- 尚未切片時的預覽 / 破面模型的預覽降級 — **靜態核對涵蓋**：`point_available_depth()` 於 raycaster 或 `po` 為空時回 `nullopt` 且**不寫入快取**，`preview_sla_head_for_point()` 收到 `nullopt` 即保留 `configured_front`。
- 切片完成後自動對齊 — **靜態核對涵蓋**：`ThicknessMeshId` 由 `nullptr` 變為實際網格身分即觸發重建。
- 垂直承載面上兩端一致 / 傾斜承載面上允許殘差 / 主要工作流下預覽仍生效 — **靜態核對涵蓋**（設計層面的界限宣告，非可執行判準）。
- 連續多幀不重複量測（射線次數 MUST 為 0）/ 加速結構不每幀重建 / 設定值改變不重打射線 / 拖曳單點僅失效該點 — **靜態核對涵蓋，未以計數器實測**。結構保證：`NaN` 哨兵使已量測點不再打射線；負值哨兵使已量測但未命中的點也不再打；`refresh_thickness_measurement()` 穩態下只做四次純量比較；`top_params` 不參與任何失效判準；拖曳只呼叫 `invalidate_thickness_at(m_hover_id)`。

**`sla-support-points-preview-performance`（3 個 Requirement / 11 個 Scenario）**

- 參數一致的 auto 點共用同一模型 / preview 旗標不同的點不共用 / 帶 explicit geometry 的 manual 點 — **靜態核對涵蓋**（`head_geom_key()` 的欄位組成）。
- 逐點刺入深度相異時仍共用同一模型 — **靜態核對涵蓋**：4.10 已將 `penetration` 移出 key，故相異深度不可能造成 miss。**未以快取項數計數器實測。**
- 超過快取門檻 — **未涵蓋**。已知限制：需刻意製造超過 `k_head_model_cache_limit` 組相異幾何參數。程式上只有一條渲染路徑（清空後即從同一個 `else` 分支重填），故「畫面結果一致」為結構保證。
- 平移方向正確 / 與烘進網格的路徑等價 — **代數驗證涵蓋**：已以腳本對 `pen = 0 / 0.1 / 0.3 / -0.1` 確認 body 與接觸球位移皆恰等於 `+pen`；`M_ns · T(S·pos) · R · T(P·ẑ) · v(0)` 與 `M_ns · T(S·pos) · R · v(P)` 逐頂點相同。**未以實際 render 輸出做像素或頂點比對。**
- 光照不因平移改變 — **靜態核對涵蓋**：`view_normal_matrix` 取 `block(0,0,3,3)`，純平移對其無貢獻。
- 夾限後 hover 位置與可見幾何一致 / 接觸球放大時的命中一致性 — **手動驗收涵蓋**（4.16）。
- 兩個函式取得相同夾限值 — **靜態核對涵蓋**：`render_points()`（第 992 行）與 `update_point_raycasters_for_picking_transform()`（第 2903 行）都呼叫 `point_available_depth(i, ...)`，編輯模式下兩者傳入的法線同為 `m_editing_cache[i].normal`，且讀同一份 `m_thickness_cache`。

**核對結論**：32 個 Scenario 中，4 個未涵蓋（中空壁厚數值、排水孔、GUI/切片端數值相等、快取門檻溢位），皆記為**已知限制**而非缺陷；其餘由自動測試、代數驗證、手動驗收或靜態核對涵蓋。未涵蓋的四項共同特徵是需要專門的測具（帶數值輸出的模型或計數器），不影響已驗證的行為正確性。

### Phase 4 Code Review（2026-08-26）

**審查範圍說明**：Task 4.10–4.13（`HeadGeomKey` 移除 `penetration` 欄位、canonical 網格改以
`penetration_mm = 0` 建構、`model_matrix` 追加平移）**尚未實作**，依指示停在 4.9 檢查點，
故不在本次 diff 內、亦未審查。

**F1（真實缺陷，已修）：AABB 樹的重建判準原本永遠不會觸發。**

原實作以 `mesh != m_thickness_mesh` 判斷是否重建，仿自 `Raycaster::on_update()` 的 `m_old_meshes`。
**該慣用法在此處是錯的。** `Raycaster` 比對的是 `&mv->mesh()`——跨不同 `ModelVolume`，位址確實不同。
而 `HollowedMesh` 以**傳值**持有網格（`TriangleMesh m_hollowed_mesh_cache`，`GLGizmosCommon.hpp:296`），
`get_hollowed_mesh()` 回傳的不是 `nullptr` 就是**永遠同一個位址**，
`on_update()` 只是在該位址上換內容（`GLGizmosCommon.cpp:750`）。

後果：中空參數變更、排水孔移動、重新切片之後，AABB 樹保持陳舊，預覽會依**過時幾何**夾限。
只有 `m_has_hollowed_mesh` 切換造成的 `nullptr` ↔ 位址往返才會被偵測到——而那並非每次都發生。

修法：身分改為「位址 + 內容指紋」（`its.vertices.data()`、頂點數、三角形數），封裝為 `ThicknessMeshId`。
**明確承認這不是雜湊**：兩份頂點數與三角形數相同、且頂點緩衝區恰好落在同一位址的網格仍會被判為相同。
機率極低，且身為預覽會在下次變更時自我修正；真正的雜湊代表每幀走訪全部頂點。

**F2（記憶體生命週期：通過）**：`MeshRaycaster(const TriangleMesh&)` 會把網格**複製**進自己持有的
`shared_ptr`（`MeshUtils.hpp:169-170`），故 raycaster 不依賴 `HollowedMesh` 的存活。
`ThicknessMeshId::mesh` 與 `::vbuf` **永不解參考、僅比對**，已於註解中明文標示。

**F3（Co-vector 法線轉換：通過）**：使用 `trafo.linear().inverse().transpose()`，
即 `(M^-1)^T`，為法線（co-vector）的正確變換。純鏡像 `M = diag(1,1,-1)` 時 `(M^-1)^T = M`，
與幾何直覺一致。**但鏡像仍會翻轉三角形纏繞方向**，`po->get_mesh_to_print()` 是否已處理該問題
**未經驗證**——這正是假設 A3 的內容，由 Task 4.17 承接。

**F4（AABB 不得每幀重建：通過）**：`refresh_thickness_measurement()` 每幀呼叫，
但在穩態下只做四次純量比較，不做任何配置。

**F5（快取一致性：通過）**：`refresh_thickness_measurement()` 不收點數參數、自行以
`m_editing_mode ? m_editing_cache.size() : m_normal_cache.size()` 導出，
使 render 路徑與 picking 路徑**依構造**不可能把快取調成不同長度而互相清空。

**F6（哨兵設計：通過）**：`NaN` = 未量測、負值 = 已量測但未命中。兩者分開才不會讓破面網格每幀重打射線。
合法量測值恆為 `distance + eps > 0`，故負值無歧義。

**F7（觀察，未動）**：`MeshRaycaster` 重建會**複製整份網格**。僅在網格身分改變時發生，
且與 `Raycaster::on_update()` 既有做法一致（`std::make_shared<const TriangleMesh>(*mesh)`），
故不視為問題，僅記錄大型模型上的成本來源。

### Phase 4 目前的未驗證項（4.1–4.8 實作完成，尚未編譯）

- 4.1–4.9 已由使用者實測通過（建置成功、薄板目視未刺穿）。**但 Review 的 F1 修正（`ThicknessMeshId`）是在該次實測之後才加入的，尚未編譯。**
- **A3（鏡像／非等比縮放）完全未驗證**，由 Task 4.17 承接。本階段只確保法線走的是逆轉置。
- Task 4.2 的「確認 `HollowedMesh::on_update()` 真的會執行」尚未進行。

### Phase 4 收尾狀態（更新於 2026-08-27：僅 4.20 / 4.21 未結）

**已完成**：4.1–4.17、4.19。4.10–4.13 的代數前提（penetration 為剛體平移、平移矩陣需置於
旋轉右側、法線矩陣不受影響）已逐項驗算；4.15/4.16/4.17/4.19 已由使用者於重建後的
GUI 實測通過。

**假設 A3 已成立。** 鏡像、非等比縮放 (1.0, 1.0, 3.0) 與兩者併用三組皆目視確認未穿透，
`design.md` 三個待驗證假設至此全數取得實測支持。Task 4.18 的備案無須啟動。

**唯一未結的實作外項目是 4.20（全專案 ctest）。** 首次執行出現三項 `NOT_BUILT`，
成因是 `libnest2d_tests` / `slic3rutils_tests` / `fff_print_tests` 三個 target 從未被建置
（詳見 4.20 的排查記錄），非 SLA 程式碼回歸。建置指令已列於該處。

**建立三個新套件基線的必要性**：這三個測試執行檔在本專案從未跑過，沒有可比對的基線。
若建出來後出現失敗，**不得逕行判定為本變更造成**，須先於 `release` 上重建重跑取得基線。

## 5. Phase 5：全域幾何回歸驗收與規格核對

- [x] 5.1 薄板全相位掃描複驗：0.2 mm 薄板，`layer_height` 取 0.05 / 0.10 / 0.15，`support_object_elevation` 自 5.00 至 5.15 逐 0.01；斷言每個有效相位的支撐點數量相同、z 皆為 0.00，且無相位回報 `Automatic support points: 0`
  - **前兩項判準已由自動測試涵蓋並實測通過**：`tests/libslic3r/sla_thin_model_tests.cpp` 的 `projection result is independent of the grid phase` 與 `ray branch stays reachable across every grid phase` 走的正是本任務指定的掃描（0.2 mm 薄板 × 層高 0.05/0.10/0.15 × elevation 5.00–5.15 逐 0.01，共 46 個有效相位），斷言每個相位的輸出點數不變、z 皆為 0.00。`libslic3r_tests.exe "[ThinModel]"` → `All tests passed (942 assertions in 19 test cases)`。
  - **第三項判準「無相位回報 `Automatic support points: 0`」未涵蓋。** 那是管線層級的觀察，需實際跑完 `slaposSupportPoints` 才看得到；自動測試只驅動投影階段（`tests/sla_print` 已失效，見 design.md）。**此項移交 Task 5.5 的端到端手動驗收承接**，不得視為已驗證。
- [x] 5.2 常態模型逐點不變性複驗：挑選至少 3 顆厚度除以 `layer_height` >= 2 且局部可用深度 >= 2 x configured 的既有模型，比對變更前後的支撐點數量與座標逐點一致
  - **未執行「3 顆模型變更前後逐點比對」的實測**，改以構造性論證確認，理由與論證如下。
  - **Phase 2（`move_on_mesh_surface`）對常態幾何依構造逐點不變**：tier 2 與 tier 3 逐字沿用修改前的運算式；tier 1 只在「恰有一個命中面朝下」時觸發。常態幾何下支撐點貼在承載面上，向下射線命中該面（朝下、距離約 0）、向上射線自材料內側命中上表面（朝上），故 tier 1 觸發並選中**向下命中**——而那同時也是修改前「取較近者」會選的那一個。兩者結果相同。
  - **Phase 3 的夾限在常態模型上完全失效**：可用深度 >= 2 × configured 時 `min(configured, thickness/2 - offset) = configured`，夾限為恆等。已由 `thick model head mesh is unchanged vertex for vertex` 實測（10 mm 模型、configured 0.4 → 0.4）。
  - **Phase 3 Review 的 F1 殘留不適用於此**：F1 的偏差量 `Δ = configured - clamped`，常態模型上 `Δ = 0`。
  - **Phase 4 只影響預覽**，不參與支撐點產生。
  - **限制**：以上為推導，不是量測。若需字面意義的 3 模型前後比對，應併入 Task 5.5 的手動驗收一併執行。
- [x] 5.3 常態模型支撐網格複驗：同一組模型，比對變更前後的支撐網格頂點與面完全相同（可用 3MF/STL 匯出後做二進位或幾何比對）
  - **未執行 3MF/STL 匯出後的二進位比對**，改以構造性論證確認。
  - 本變更對支撐網格的**唯一**作用路徑是 `Head::penetration_mm`。`SupportTreeMesher` 一行未改；pillar / bridge / pad 的幾何皆由 `junction_point()` 導出，而 `junction_point() = pos + (fullwidth() - r_back) * dir`、`fullwidth() = real_width() - penetration_mm`。常態模型上 `penetration_mm` 與變更前相同（見 5.2），故 junction 相同、其下游的 pillar 與 bridge 亦相同。
  - 支撐頭本身的網格逐頂點相同已有自動測試實證（`thick model head mesh is unchanged vertex for vertex`，比對 `vertices` 與 `indices` 全項）。
  - **限制**：同 5.2，這是推導而非匯出比對。字面意義的網格二進位比對建議併入 5.5。
- [x] 5.4 中間帶行為確認：局部可用深度 0.6 mm + `support_head_penetration = 0.4` 的模型，確認 front depth 為 0.3，並在驗收紀錄中明確標示此為**符合規格**而非回歸
  - **已確認，且明確標示為「符合規格」而非回歸。**
  - 依 `clamp_front_depth()` 計算：可用深度 0.6 mm、`support_head_penetration = 0.4`、`r_pin = 0.2`、無接觸球 → `offset = 0`、`front = clamp(min(0.4, 0.6 × 0.5 - 0), 0, 0.4) = 0.3`。**front depth 為 0.3。**
  - 這是**規格要求的行為**：夾限的目標是讓支撐頭停在承載面的中面，而非停在設定深度。可用深度介於 `configured` 與 `2 × configured` 之間的模型（此處 0.4 < 0.6 < 0.8）必然落在這個中間帶，咬合深度會被縮減但仍大於零。**看到咬合變淺不等於回歸**，判斷回歸的判準是可用深度 >= `2 × configured` 的模型是否改變（見 5.2 / 5.3）。
  - 另兩種 Contact Sphere 組態於同一輸入下的結果（同樣為規格行為）：接觸球有效（`r_contact = 0.4 > r_pin`）→ `offset = 0`、front = 0.3；退化帶（`r_contact = 0.1 <= r_pin`）→ `offset = 0.1`、front = 0.2。
- [x] 5.5 端到端手動驗收：於 resin 建置（`build-resin-dbginfo`）以 `dish.stl` 等極薄模型完整走一遍「自動產生支撐點 → 套用 → 切片 → PRZ 輸出」，確認不再出現 `There are unprintable objects`
  - **使用者實測（2026-08-27）：通過。** 於 `build-resin-dbginfo` 完整走過「自動產生支撐點 → 套用 → 切片 → PRZ 輸出」，支撐正常產生、切片正常、PRZ 輸出正確，**未再出現 `There are unprintable objects`**。
  - **本次驗收承接了 Task 5.1 第三項判準的一部分，但未完全承接。** 5.1 的「無相位回報 `Automatic support points: 0`」原本移交本任務；本次為**單一組態的一次端到端執行**，不是相位掃描，故只能證明該組態下支撐點數非零，**不能證明所有相位皆非零**。相位無關性本身已由自動測試涵蓋（46 個有效相位），此處僅補上「管線層級確實產出支撐點」這一段。
  - **Task 5.2 / 5.3 移交本任務的部分未執行**：字面意義的「3 顆常態模型變更前後逐點比對」與「支撐網格匯出後二進位比對」**沒有做**。兩者的結論目前仍僅由構造性論證與`thick model head mesh is unchanged vertex for vertex` 支撐。維持記為**已知限制**，不因本任務勾選而視為已驗證。

- [x] 5.6 【建置檢查點】請使用者重跑「步驟 1」的 cmake 設定後做一次乾淨全建（`cmake --build ... --target ALL_BUILD -- -m`），確認無新增編譯警告
  - **使用者實測（2026-08-27）：建置正常。**
  - 限制：任務文字另含「確認無新增編譯警告」，該子判準是隨「建置正常」一併回報，**未取得逐條警告差異的比對輸出**。本變更於原始碼的淨增量為註解與純函式／量測輔助函式，未引入新的型別轉換或未使用變數，但這是推論，不是與基線警告清單比對的結果。

- [x] 5.7 【建置檢查點】請使用者執行 `ctest -C RelWithDebInfo --output-on-failure`（全專案），確認與 0.5 基線相比無新增失敗
  - **使用者實測（2026-08-27）**：全專案 ctest 完成。`libslic3r` 套件維持原有 5 項基線失敗（`[3mf]` / `[Config]` ×2 / `[Geometry]` / `[PlaceholderParser]`，索引因新目標加入而位移），**SLA 零新增迴歸**。
  - 新增失敗全部落在 `slic3rutils`（HTTP）與 `fff_print`（FDM）。詳細判定與其限制記於 Task 4.20，此處不重複；**該判定是依變更範圍所作的推論，不是與基線比對得出的結論**（這兩個套件沒有 Phase 0 基線）。

- [x] 5.8 規格核對：逐條走過四份 spec 的所有 Requirement 與 Scenario，標記「已由自動測試涵蓋」或「已由手動驗收涵蓋」，未涵蓋者需補測試或補記為已知限制

#### 5.8 四份 spec 逐條核對（2026-08-27）

`sla-support-preview-penetration`（21 個 Scenario）與 `sla-support-points-preview-performance`（11 個 Scenario）已於 **Task 4.21** 逐條核對，結論不在此重複。以下補齊另兩份。

**`sla-support-point-placement`（4 個 Requirement / 12 個 Scenario）**

- 取樣層落在薄板中面之上 / 之下 / 兩個方向皆為朝下面時取較近者 / 兩個方向皆非朝下面時回退既有行為 — **自動測試涵蓋**，四條各有專屬 TEST_CASE，測試檔中以 `// Spec:` 註解逐條標明對應關係（`sla_thin_model_tests.cpp:260 / 284 / 305 / 346`）。
- elevation 全相位掃描結果一致 / 多種層高下皆產出支撐點 — **自動測試涵蓋**（`projection result is independent of the grid phase`，46 個有效相位）。
- 模型僅橫跨單一切片層 / 層級數量不足時使用支撐頭直徑 — **自動測試涵蓋**（`single slice level converges with the multi-level case`）。
- 層級數量充足時的計算 / 射線分支必須在薄板全相位下可達 — **自動測試涵蓋**（`ray branch stays reachable across every grid phase`，同時採集 A1 / A2 數據）。
- 常態厚度模型逐點一致 — **未以實測涵蓋**。Task 5.2 以構造性論證確認（tier 2 / tier 3 逐字沿用修改前的運算式；常態幾何下 tier 1 選中的正是舊規則會選的那一個）。記為**已知限制**：字面意義的「3 顆模型變更前後逐點比對」未執行，移交 Task 5.5 手動驗收。
- 回退分支被頻繁觸發時視為設計失效 — **自動測試涵蓋**（A1 實測：46 個相位全數未進入 `squared_distance` 回退分支）。

**`sla-support-head-penetration`（7 個 Requirement / 17 個 Scenario）**

- 刺入深度大於可用深度 / 未啟用接觸球時換算為恆等 — **自動測試涵蓋**（`clamp with no contact sphere`）。
- 刺入深度小於可用深度一半 / 厚壁模型逐點一致 — **自動測試涵蓋**（`clamp is inert on a thick model`、`thick model head mesh is unchanged vertex for vertex`，後者比對 `vertices` 與 `indices` 全項）。
- 啟用接觸球時的夾限 / 退化帶的夾限 — **自動測試涵蓋**（`clamp with a live contact sphere`、`clamp inside the degenerate contact-sphere band`）。
- 傾斜支撐頭的軸向量測 / 射線自側緣出射 — **自動測試涵蓋**（`available depth is measured along the head axis` 的三個 SECTION：`straight up through a 0.2 mm plate`、`tilted head sees more axial room than the plate is thick`、`a ray that leaves the model reports no hit`）。
- 方向向量必須正規化 — **靜態核對涵蓋**：`measure_available_depth()` 以 `if (!is_approx(head_dir.norm(), 1.)) return std::nullopt;` 於**執行期**檢查，非 assert，故 Release 組建同樣生效。
- 錨點方向向量正規化 — **Phase 1 涵蓋**（#6 `taildir.normalize()`），並已驗證輸出網格逐點不變。
- 量測所用網格包含前置加工結果 — **靜態核對涵蓋**：切片端量測用的 `m_mesh` 即 `SupportTreeBuildsteps` 持有的、已完成 `slaposDrillHoles` 的網格，未另取原始網格。
- 破損網格觸發 fail-safe — **自動測試涵蓋**（`fail-safe drives the depth to zero and is counted`）。
- 兩種樹皆套用夾限 / Branching 樹錨點夾限不改變橋接端點 — **自動測試涵蓋**（`default and branching heads agree and neither protrudes`、`branching anchor clamp leaves the bridge endpoint intact`）。
- 支撐幾何與模型的布林交集驗證 — **部分涵蓋**。`clamped head does not break through a 0.2 mm plate` 與 `no protrusion in either contact-sphere configuration` 以**頂點座標**驗證未穿透，**未執行真正的布林交集運算**。已知限制：頂點檢查對凸的支撐頭幾何是充分的，但不等同 spec 字面要求的布林驗證。
- Fail-safe 必須可診斷 / 無點觸發時不產生雜訊 — **靜態核對涵蓋，未以日誌斷言實測**。`log_depth_probe_misses()` 在計數為 0 時提前返回，且每個驅動器（default / branching）只有一個呼叫點。**刻意未安裝 `boost::log` sink 來斷言「恰好一行警告」**——那是共用測試二進位中的全域狀態，而測試執行順序是隨機的。
- 搜尋行為不受夾限影響 / 每個支撐頭僅量測一次 — **靜態核對涵蓋**：夾限位於 optimizer 接受分支之後（`SupportTreeUtils.hpp:452`，提交點 3），量測未進入目標函式，故每個頭只量測一次。
- 主頭夾限後與 pillar 維持相連 / Default 樹錨點寬度與夾限後的深度一致 / 錨點網格與橋接端重疊而非脫開 — **代數驗證涵蓋**：`fullwidth() = real_width() - penetration`，而 `dist` 同時加上 `penetration`，兩者恰好抵消，使 `junction_point()` 被釘在 `endp`，與夾限深度無關。推導已寫入 `SupportTreeBuildsteps.cpp` 提交點 2 的註解。
- 中間帶可用深度被夾限為預期行為 — **已確認**（Task 5.4：可用深度 0.6、configured 0.4 → front 0.3，標示為符合規格而非回歸）。

**四份 spec 總計 62 個 Scenario 的核對結論**：未涵蓋或部分涵蓋者共 6 項，全數記為**已知限制**，無任何一項屬實作缺陷：
1. 中空模型量得實際壁厚的**數值**（4.21）
2. 排水孔旁支撐點的量測（4.21）
3. GUI 與切片端可用深度**數值相等**（4.21 / A3）
4. 快取項數超過門檻的溢位行為（4.21）
5. 常態模型變更前後**逐點比對**的實測（5.2，移交 5.5）
6. 支撐幾何與模型的**布林交集**驗證（本項，現以頂點座標替代）

共同特徵是都需要專門測具（帶已知數值的模型、計數器、或布林運算管線），不影響已驗證行為的正確性。

- [x] 5.9 更新 `design.md` 的「驗證假設」一節：將 A1 / A2 / A3 由「待驗證」改為實測結果，並附上實際數據
  - `design.md`「驗證假設」章節前言已由「尚未以實測確認」改為「三項皆已完成驗證」，並註明 A1 / A2 於 2026-08-26 由自動測試驗證、A3 於 2026-08-27 由 GUI 手動驗收驗證。
  - A1 / A2 的實測結果早於 Phase 2 / Phase 3 即已寫入，本次未動。
  - **A3 新增實測結果**：三組變換（X 軸鏡像、非等比縮放 (1.0, 1.0, 3.0)、兩者併用）皆目視確認預覽支撐頭未穿透，A3 成立，備案無須啟動。同時明文記下該驗證的限制——「GUI 與切片端量得的厚度數值相等」未以逐點數值比對，目視無穿透是必要條件而非充分條件。

- [x] 5.10 於四個修改點的程式碼加註來源與意圖註解（標明對應本變更與 upstream PrusaSlicer 2.9.6 的關係），供未來 rebase 時逐項核對
  - 四個修改點皆已帶有 `fix-sla-thin-model-support-points` 標記與意圖說明；本次補上的是**與 upstream PrusaSlicer 2.9.6 的關係**，供未來 rebase 逐項核對：
  - **#6 taildir**（`SupportTreeBuildsteps.cpp:1069`）：上游**已正規化**（"Slicing SLA supports analytically"），本 fork 自較舊基準分支出來、未取得該修正。此註解在 Phase 1 即已寫入，本次未動。
  - **#1 方向性吸附**（`SupportPointGenerator.cpp`）：**新增**——缺陷同樣存在於上游 2.9.6，被保留為 tier 2 / tier 3 的「取最近命中」規則正是上游的規則、不是 fork 的分歧。本修正為 fork 專屬；rebase 時須先確認上游是否已自行引入方向性規則。
  - **#5 切片端夾限**（`SupportPoint.hpp`）：**新增**——上游 2.9.6 **完全沒有任何夾限**，`support_head_penetration` 依設定照單全收，正是本變更要處理的缺陷。此區塊、四個提交點與 `SupportTreeUtils.hpp` 的量測輔助函式**整組一起搬移**；若上游已長出自己的夾限，優先採用上游版本並移除本實作。
  - **GUI 預覽**（`GLGizmoSlaSupports.cpp`）：**新增**——上游的 `render_points()` 直接把設定深度烘進網格、不作任何量測，與其未夾限的切片端一致。厚度快取、其守護的 raycaster、`point_available_depth()`，以及 `GLGizmoSlaBase::on_get_requirements()` 中重新啟用的 `HollowedMesh` 屬同一組。並註明本 fork 未定義 `SUPPORT_BACKGROUND_PROCESSING`（design.md D7），這是預覽端必須自行量測、無法讀取切片端結果的根因。

- [x] 5.11 Review：確認 Phase 1 至 Phase 4 的 commit 彼此可獨立 revert，回退路徑與 `design.md` 的 Migration Plan 一致
  - **以 `git merge-tree` 對每個 commit 做非破壞性的 revert 模擬**（base = 該 commit，ours = HEAD，theirs = 該 commit 的父節點），未實際改動工作區。
  - 五個 commit（Phase 4 為**兩個**：`7fc68839e` + `1cfaf103b`）：`258c6f8b7`（#6）、`acda10a95`（#1）、`b3105e15f`（#5）、`7fc68839e`（GUI）、`1cfaf103b`（GUI 快取重構）。
  - **結果與 `design.md` 的 Migration Plan 一致**：
    - `acda10a95`（#1）：**原始碼零衝突**，可單獨 revert。
    - `b3105e15f`（#5）、`7fc68839e`（GUI）：衝突**僅在 `tasks.md`**（文件），原始碼零衝突。
    - `1cfaf103b`：位於分支頂端，零衝突。
    - `258c6f8b7`（#6）：**於 `SupportTreeBuildsteps.cpp` 產生真實衝突**（`connect_to_model_body()` 的 `taildir` / `dist` 區塊，被 #5 的提交點 2 覆寫）。
  - **該衝突是預期的，不是缺陷。** `design.md` 的 Migration Plan 明文寫「除 #5 依賴 #6、GUI 依賴 #5 外彼此可分離」。#6 是 #5 的前置條件（射線需單位向量），單獨回退 #6 而保留 #5 本就不是有效的回退路徑。
  - **有效的回退路徑**（依相依序，由後往前）：GUI → `git revert 1cfaf103b 7fc68839e`；#5 → 再 revert `b3105e15f`；#6 → 再 revert `258c6f8b7`；#1 → 任何時候皆可單獨 revert。
  - **與 Migration Plan 的一處落差（已記錄）**：原文寫「四項修改採獨立提交」，實際為**五個** commit——Phase 4 因 4.10–4.13 的快取重構在建置驗證之後才實作而拆成兩筆。回退 GUI 時必須**兩筆一起** revert，順序為先 `1cfaf103b` 後 `7fc68839e`。

### Phase 5 收尾狀態（2026-08-27）

**tasks.md 全數任務完成。** 唯一未勾選的 Task 4.18 是 4.17 失敗時的**條件式備案**；
4.17 已實測通過，備案未觸發，故維持未勾選為正確狀態，不是遺漏。

**commit 結構（五個，依相依序）**：
`258c6f8b7`（#6 taildir）→ `acda10a95`（#1 方向性吸附）→ `b3105e15f`（#5 切片端夾限）
→ `7fc68839e`（GUI 量測與夾限）→ `1cfaf103b`（GUI 快取重構）。
文件與註解封存於 `7d6c5555f`。回退路徑見 `design.md` 的 Migration Plan。

**三個驗證假設 A1 / A2 / A3 全數成立**，實測結果均已寫入 `design.md`。

**結案時仍存在的已知限制（共 8 項，皆非缺陷）**：

1. 中空模型量得實際壁厚的**數值**未驗證（4.21）
2. 排水孔旁支撐點的量測未驗證（4.21）
3. GUI 與切片端可用深度**數值相等**未比對，A3 僅目視（4.21 / 5.9）
4. 快取項數超過門檻的溢位行為未測（4.21）
5. 常態模型變更前後**逐點比對**未實測（5.2 / 5.5）
6. 支撐網格匯出後的**二進位比對**未執行（5.3 / 5.5）
7. 支撐幾何與模型的**布林交集**驗證以頂點座標替代（5.8）
8. 乾淨全建的**警告差異清單**未逐條比對（5.6）

共同特徵：皆需專門測具（帶已知數值的模型、計數器、布林運算管線或警告基線），
與已驗證行為的正確性無關。若日後要收斂，建議優先處理第 1、5、7 項——
它們對應 spec 中字面要求最強的三條 Scenario。
