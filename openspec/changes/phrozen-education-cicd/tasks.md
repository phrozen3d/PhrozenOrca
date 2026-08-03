## 1. 階段 1-A：Windows job（先做，成本最低）

> **階段 1-A 已完成並驗收通過（2026-07-31）**。Windows 建置全數過關，安裝檔產出正確，第二次執行確認快取命中並跳過 deps 建置。
>
> 過程中修掉三個實作缺陷，記錄於此供後續 macOS 階段參考：
> 1. **`runs-on` 必須釘 `windows-2022`** —— 原本照 resin 支線上較舊的 `build_all.yml` 寫成 `windows-latest`，但主線早在 `8359f943aa` 就已改釘，因為 `windows-latest` 已不再提供 VS 2022，CMake 的 `Visual Studio 17 2022` generator 會找不到任何實例。**這是漂移偵測要防的情境的真實案例**（此次屬「一開始就沒跟上」）。
> 2. **`actions/cache@v4` 需設 `ACTIONS_ALLOW_USE_UNSECURE_NODE_VERSION`** —— 同樣來自主線 `d4c40f1f6d`。
> 3. **快取鍵不可內嵌 `hashFiles()`** —— `key` 在還原與儲存（Post）兩個時機都會被求值，deps 建成後 `deps/build-resin/` 有數萬個檔案，Post 步驟求值必定超過 `hashFiles()` 的 120 秒上限，導致建置成功卻在最後一步失敗且快取存不進去。已改為建置前先算好存成 step output。
>
> 另新增一個 `Verify deps were actually built` 步驟：`build_resin_release_vs2022.bat` 的 deps 分支以無條件 `exit /b 0` 結尾，cmake 失敗時步驟仍回報成功，會讓空的 deps 被存進快取。
>
> **通則**：凡是主線靠「拆成多個 job」自然迴避掉的問題（快取鍵計算、runner 標籤集中管理），本 change 的自帶邏輯架構都必須顯式處理。

- [x] 1.1 在 `phrozen-education-variant` 分支建立 `.github/workflows/build_education.yml` 骨架：
  - `on:` 同時包含 `workflow_dispatch:`（正式用）與開發期專用的 `push:` 區塊（`branches: ['phrozen-education-variant']`，`paths:` 限定 `.github/workflows/build_education.yml`、`build_resin_release_macos.sh`、`build_resin_release_vs2022.bat`）
  - 註：`paths` 原列的是 `build_release_macos.sh`，第 2 組改採獨立腳本後同步改為 `build_resin_release_macos.sh` —— 主線腳本的變動**不應**觸發 education 建置
  - **不宣告任何 `inputs:`**（見 design.md 決策 3）
  - 檔案開頭加註解說明：resin/education 專用、主線永不自動觸發、存在目的是讓 Actions 出現手動按鈕、請勿刪除
  - 此階段先只放 Windows job，macOS job 整段註解掉或尚未加入（避免每次迭代消耗 10 倍計費）
- [x] 1.2 Windows job：checkout（`lfs: 'true'`）＋ deps 快取設定
  - 快取鍵**必須含 education 識別**，不可與主線的 `${os}-cache-phrozenorca_deps-build-${hashFiles('deps/**')}` 相同
  - 快取路徑必須是 `deps/build-resin/PhrozenOrca_dep`（`build_resin_release_vs2022.bat` 實際使用的目錄），不是主線的 `deps/build`
- [x] 1.3 Windows job：環境準備 —— `lukka/get-cmake@latest`（`cmakeVersion: "~3.28.0"`）、`microsoft/setup-msbuild@v2`、`choco install strawberryperl`
- [x] 1.4 Windows job：deps 建置（僅在快取未命中時執行）—— `build_resin_release_vs2022.bat deps` 與 `pack`
- [x] 1.5 Windows job：slicer 建置 —— `build_resin_release_vs2022.bat slicer`，並設定 `WindowsSdkDir` 與 `WindowsSDKVersion` env（參考 `build_orca.yml` 的 `Build slicer Win` 步驟）
- [x] 1.6 Windows job：版本字串擷取 —— 從 `version.inc` 讀 `Phrozen_VERSION`（乾淨的 base 版號，合併主線後為 `1.2.1`），**並在 CI 端補上 `-Education` 尾綴**後才用於命名（見 design.md 決策 9）
- [x] 1.7 Windows job：打包 —— `choco install nsis` 後執行 `cpack -G NSIS`、產生 portable ZIP、以 7z 打包 PDB。**不得**使用既有的 `pack-win-release` composite action（見 design.md 決策 7），改為內聯步驟或另建 education 專用的獨立 composite action
- [x] 1.8 Windows job：上傳 artifact，名稱需帶 education 識別，與主線 artifact 名稱不重複

## 2. 階段 1-B：macOS 的 resin 專用建置腳本

> **原規劃已作廢，改採獨立腳本（使用者要求，2026-08-03）**。
>
> 原本第 2 組的規劃是在共用的 `build_release_macos.sh` 上加 `-r` 旗標。該版本一度實作完成，但**違反本專案最高優先的維護性約束**：「主線的共用檔案不得夾帶任何 resin 判斷或條件；單獨給 resin 支線用、主線不會使用到的獨立檔案則可接受」。
>
> `build_release_macos.sh` 正是共用檔案。一旦在 resin 支線上改它，主線每次更新該腳本都會在 merge 進 resin 支線時衝突 —— 而主線的 FDM 更新是常態、resin 回主線是一兩年後的事，維護成本完全不對稱。
>
> 改為新增 `build_resin_release_macos.sh`，與 Windows 的 `build_resin_release_vs2022.bat` 同一套做法：**resin 專屬設定全部寫死在獨立腳本裡，不靠旗標傳參**。共用的 `build_release_macos.sh` 在 resin 支線上保持與主線逐位元組相同。

- [x] 2.1 新增 `build_resin_release_macos.sh`（`build_release_macos.sh` 的變體副本），檔頭註解說明其為 resin 專用、為何獨立成檔、以及與主線腳本的漂移須人工比對
- [x] 2.2 腳本頂端集中宣告三個 resin 專屬常數：`APP_NAME="PhrozenOrca-Education"`、`BUILD_DIR_NAME="build-resin"`、`DEPS_BUILD_DIR_NAME="build-resin"`
  - `APP_NAME` 必須與 CMake 的 `SLIC3R_APP_KEY` 一致，否則 `.app` 名稱與 `Info.plist` 的 `CFBundleExecutable` 不符，app 無法啟動
- [x] 2.3 `build_slicer()` 的 cmake 參數列固定加上 `-DPHROZEN_ORCA_ENABLE_RESIN=ON`，並將 `-DCMAKE_INSTALL_PREFIX` 改為 `"$PWD/$APP_NAME"`
  - install prefix 與「Fix macOS app package」的 staging 目錄是同一個路徑，兩變體共用會讓 DMG 內同時出現兩個 `.app`
  - 已確認 `BIN_RESOURCES_DIR` 取自 `CMAKE_CURRENT_BINARY_DIR` 而非 install prefix，故此改動無其他副作用
- [x] 2.4 「Fix macOS app package」段落與 `build_universal()` 的所有 `.app` 路徑改用 `$APP_NAME`
- [x] 2.5 `build_universal()` 的 **`BINARY_PATH="Contents/MacOS/$APP_NAME"`** —— 這是 `lipo` 的輸入檔路徑，最容易漏掉的一處，未同步會直接找不到檔案而失敗
- [x] 2.6 所有建置目錄與主線分開：`build-resin/<arch>`、`deps/build-resin/<arch>`；`pack_deps()` 的 tar 檔名改為 `PhrozenOrca-Education_dep_mac_*`
  - 與 Windows 的 `build_resin_release_vs2022.bat`（`build-resin` ／ `deps/build-resin`）同一套命名慣例
  - deps 本身其實與變體無關（已確認 `deps/` 底下完全沒有引用 `PHROZEN_ORCA_ENABLE_RESIN`），分開純粹是為了本機同時建兩個變體時互不覆蓋、以及與 Windows 保持一致的心智模型。代價是本機開發時 deps 會建兩份；CI 端因為快取鍵本來就分開，無額外成本
- [x] 2.7 確認主線 `build_release_macos.sh` 未被修改（`git diff` 對該檔無任何輸出）
- [x] 2.8 確認新腳本的 git 檔案模式為 `100755`（可執行）—— 若為 `100644`，CI 執行 `./build_resin_release_macos.sh` 會 permission denied
- [x] 2.9 已用 `bash -n` 驗證語法
  - **既有缺陷（沿用主線，未修）**：optstring `":dpa:snt:xbc:h"` 不含 `1`，因此 CI 傳的 `-1`（單執行緒建置）一直被忽略、`CMAKE_BUILD_PARALLEL_LEVEL` 從未生效。刻意保持與主線相同的行為，以免 education 與主線在建置平行度上出現無謂的差異

## 3. 階段 1-C：macOS job（先不簽章）

> **與 design.md 決策 10 的偏離（已與使用者確認）**：原訂「先產出未簽章 DMG，確認無誤後才加簽章公證」，實際改為 **1-C 與 1-D 一次做完**。
>
> 理由三點：
> 1. **未簽章的 DMG 幾乎無法驗收**。macOS 14 對從瀏覽器下載、未簽章的 `.app` 會直接以「已損毀」為由拒絕開啟，測試者必須手動 `xattr -dr com.apple.quarantine` 才能啟動 —— 而 9.3.7（使用者資料夾隔離）與 9.3.8（兩版本並存）都必須真的啟動軟體才能驗。等於未簽章這一輪只能驗到檔名層級的項目。
> 2. **成本期望值不會比較低**。分兩階段是保證跑兩次 macOS job（10 倍計費）；一次做完則是「順利就一次、簽章有問題才第二次」。
> 3. **失敗範圍仍可控**。簽章相關步驟全部排在建置之後，且刻意拆成「匯入憑證／簽章＋產 DMG／公證＋staple」三個獨立步驟，失敗時一眼可知卡在哪一段；`Upload artifacts mac` 加上 `if: always()`，公證失敗時已產生的 DMG 仍會上傳，不會連成品一起消失。

- [x] 3.1 macOS job：checkout ＋ deps 快取（education 專屬快取鍵；macOS 的 deps 路徑依 `build_resin_release_macos.sh` 實際使用的位置設定）
  - 快取路徑為 `deps/build-resin`（resin 腳本實際使用的目錄，主線用 `deps/build`），快取鍵帶 education 識別：`macos-14-cache-phrozenorca_deps-education-build-<hash>`
  - 同樣套用階段 1-A 學到的教訓：快取鍵先算好存成 step output，不可內嵌在 `key:` 裡（deps 建完後 `deps/**` 會有數萬個檔案，Post 步驟求值必定超過 `hashFiles()` 的 120 秒上限）
- [x] 3.2 macOS job：環境準備 —— `lukka/get-cmake@latest`、`brew install automake texinfo libtool`、`brew uninstall --ignore-dependencies zstd`（建置後再裝回）、釋放磁碟空間步驟（參考 `build_orca.yml` 的 `Free disk space`）
- [x] 3.3 macOS job：deps 建置（僅快取未命中時）—— `./build_resin_release_macos.sh -dx -a universal -t 10.15 -1`
  - 另補上主線 `build_deps.yml` 的中間產物清理迴圈（只保留 `PhrozenOrca_dep`），否則快取會膨脹到數十 GB
  - 另補 `Verify deps were actually built` 步驟（比照 Windows）：壞快取在 10 倍計費的 macOS 上代價太高
- [x] 3.4 macOS job：slicer 建置 —— `./build_resin_release_macos.sh -s -n -x -a universal -t 10.15 -1`
  - 旗標與主線的 `Build slicer mac` 完全一致；resin 專屬設定全部寫死在腳本內，**不靠 workflow 傳參**
  - `run_gettext.sh` **不需**在 workflow 另外呼叫 —— 腳本的 `build_slicer()` 內部已經會執行
- [x] 3.5 macOS job：先走**不簽章**路徑產生 DMG（等同 `build_orca.yml` 的 `Create DMG without notary`），DMG 檔名須帶 education 識別與版本尾綴
  - 依上方偏離說明，改為直接走簽章路徑；DMG 檔名為 `PhrozenOrca-Education_Mac_universal_V<版本>-Education.dmg`，volume 名稱為 `PhrozenOrca-Education`
- [x] 3.6 macOS job：上傳 DMG artifact（education 命名）

## 4. 階段 1-D：加入簽章與公證（最貴，最後做）

> **階段 1-C＋1-D 已完成並驗收通過（2026-08-03）**。push 上 `phrozen-education-variant` 後，`build_education.yml` 首次同時執行 Windows 與 macOS 兩個 job，兩者皆全綠、無任何步驟失敗，成功產出 Windows 安裝檔與 macOS DMG。macOS job 的 `Import signing certificate`、`Sign app and create DMG`、`Notarize and staple` 三個步驟全部通過，證實 7 個簽章／公證用 secrets 皆存在且可用（見 4.4）。
>
> **尚未完成的部分**：CI 全綠只證明建置管線本身正確，**不等於**已完成 9.3 節列出的終端使用者體感驗證（bundle identifier、簽章內容、Gatekeeper 通過與否、使用者資料夾隔離、雙版本並存）——這些項目需要在真正的 Mac 上手動執行終端機指令才能確認，CI log 看不出來。9.3 節維持未勾選，待實際在 Mac 上驗證後才更新。

- [x] 4.1 macOS job：加入 codesign 步驟 —— 匯入 `MAC_CERTIFICATE_P12`、建立並解鎖 keychain、以 `MAC_CERTIFICATE_NAME` 簽署 `.app`，使用 `scripts/disable_validation.entitlements`、`--options runtime --timestamp`
- [x] 4.2 macOS job：建立 `Applications` 符號連結後以 `hdiutil create` 產生 DMG，並對 DMG 本身簽章
- [x] 4.3 macOS job：以 `xcrun notarytool store-credentials` 與 `submit --wait` 完成公證（使用 `APPLE_ID`、`TEAM_ID`、`APPLE_APP_PASSWORD`），再以 `xcrun stapler staple` 附加公證票據
- [x] 4.4 確認所需 secrets 皆可取得：`MAC_CERTIFICATE_P12`、`MAC_CERTIFICATE_PASSWORD`、`KEYCHAIN_PASSWORD`、`MAC_CERTIFICATE_NAME`、`APPLE_ID`、`TEAM_ID`、`APPLE_APP_PASSWORD`
  - **Pass（2026-08-03 實測）**：macOS job 首次執行即全綠，`Import signing certificate`／`Notarize and staple` 皆通過，證實 7 個 secrets 全部存在且內容正確可用，不需額外設定

## 5. 漂移偵測機制

> **已完成（2026-08-03）**。建立時機特別乾淨：實作前先合併了主線進 `phrozen-resin-dev` 再進 `phrozen-education-variant`（見本 change 較早的合併紀錄），此刻五個受監控檔案在本分支與主線的 git blob hash **完全一致**，符合 design.md 47 行「基準值應以主線最新版本為準」的要求，不是拿 resin 支線當下（可能落後）的版本當基準。

- [x] 5.1 建立校驗值紀錄檔 `.github/education-ci-parity.lock`，記錄主線共用檔案的校驗值，並附註「上次比對時主線的 commit」（`LAST_REVIEWED_MAINLINE_COMMIT=dd9e7860b4...`，即目前 `origin/phrozen-custom-dev` 的 HEAD）以利日後人工 diff。監控清單為 **五個**檔案：
  - `.github/workflows/build_orca.yml`、`build_deps.yml`、`build_check_cache.yml` —— `build_education.yml` 自帶邏輯的來源
  - **`build_release_vs2022.bat`、`build_release_macos.sh`** —— `build_resin_release_vs2022.bat` 與 `build_resin_release_macos.sh` 是這兩支的變體副本
  - 補上後兩者的理由：階段 1-B 改採「獨立腳本」後，resin 的建置腳本變成主線腳本的**複本**而非其變體呼叫。複本的好處是主線 merge 下來永不衝突，代價是主線修了 bug（例如新增必要的 cmake 參數、修 SDK 路徑）時 resin 這邊**完全不會有任何訊號**。這正是漂移偵測存在的意義，因此監控範圍必須涵蓋它們，否則等於用「無聲的錯誤」換掉了「吵鬧的衝突」
  - **實作細節（校驗值選擇 git blob SHA，而非對簽出後檔案內容算 sha256）**：`git rev-parse HEAD:<path>` 取自 git 物件庫，不受 `parity_check`（`ubuntu-latest`）與後續建置 job（`windows-2022`／`macos-14`）checkout 時 CRLF/LF 轉換影響，三種 runner 算出來的值保證一致。若改用對簽出後檔案內容算 sha256，同一個 commit 在不同 OS 的 runner 上可能因 `core.autocrlf` 造成的行尾轉換而算出不同結果，產生假陽性的漂移警報——這點在 `.gitattributes` 設有 `* text=auto` 的本專案尤其重要。
- [x] 5.2 在 `build_education.yml` 中加入 `parity_check` job（`runs-on: ubuntu-latest`，全 workflow 中唯一非 Windows/macOS 的 job，因為這個檢查不需要任何建置工具鏈，選最便宜最快的 runner）：僅 checkout ＋ 解析 lock 檔逐行比對 blob SHA，不執行任何編譯／deps／簽章
  - 解析邏輯：以 `grep -e $'\t'` 篩出含 tab 的資料行（跳過註解行與 `LAST_REVIEWED_MAINLINE_COMMIT=` 這行中繼資料），逐行 `git rev-parse "HEAD:$path"` 與紀錄值比對
  - 已在本機模擬驗證兩種情境：① 現況全部相符 → 通過；② 刻意竄改一個 hash → 正確指名該檔案並回報 blob 差異
- [x] 5.3 將 Windows 與 macOS 兩個建置 job 都加上 `needs: parity_check`，確保**偵測失敗時昂貴的建置不會啟動**（`needs` 的預設行為：前置 job 失敗，後續 job 自動變成 `skipped`，不需額外的 `if` 判斷）
- [x] 5.4 失敗訊息需明確指出：哪個檔案變了、應該去比對什麼、以及確認後如何更新紀錄檔恢復綠燈（讓不熟悉此機制的人也能處理）
  - 錯誤訊息包含：檔案路徑、本分支目前的 blob SHA、lock 檔記錄的 blob SHA、以及「比對主線後決定是否同步、更新該行、更新 `LAST_REVIEWED_MAINLINE_COMMIT`」的具體指引
- [x] 5.5 在 lock 檔開頭以大段註解寫明格式、五個檔案監控範圍的理由、blob SHA 選型理由、以及完整的六步更新流程

## 6. 階段 2：讓手動按鈕出現（動主線，僅一個獨立檔案）

> **順序很重要**：必須先完成本組（讓 `Run workflow` 按鈕出現），才能執行第 7 組移除開發期觸發。否則會出現「push 不再觸發、按鈕也還沒出現」的空窗期，屆時完全無法觸發此 workflow。
>
> 實測確認（2026-07-31）：workflow 檔案只推到 `phrozen-education-variant` 時，Actions 左側欄**會**出現 `Build Education`（因為開發期 push 觸發已產生執行紀錄），但右上角**沒有** `Run workflow` 按鈕 —— 兩者是不同機制，前者看「有無執行紀錄」，後者看「檔案是否在預設分支」。
>
> **更正（2026-08-04）**：本組原本規劃的合入目標寫的是 `phrozen-custom-dev`，經 GitHub API 查證，**此 repo 實際的預設分支是 `release`**，`phrozen-custom-dev` 不是。下列各項的合入目標已改為 `release`。詳見 design.md 決策 2 的更正說明。
>
> **連帶發現並已與使用者確認接受**：`release` 的 `build_all.yml` 之 `pull_request` 觸發條件包含 `branches: [main, release]` 且 `paths` 含 `.github/workflows/build_*.yml`，`build_education.yml` 符合此 glob，故**對 `release` 開 PR 會自動觸發一次完整主線 `build_all`**（Windows + macOS + 簽章公證）。已確認無法在不修改共用檔案 `build_all.yml` 的前提下迴避，屬一次性代價（public repo 無金錢成本，且 `release`／`phrozen-custom-dev` 目前 deps 雜湊相同，大機率命中既有快取）。此結果同時可當作 9.5.2「主線既有建置行為不變」的一次免費回歸驗證。

- [x] 6.1 開 PR 將 `build_education.yml` **單獨**合入 `release`（不夾帶任何其他檔案）
  - 分支 `ci/add-build-education-workflow` 已從 `origin/release` HEAD（`c53bb94c2f`）建立，僅新增這一個檔案，內容與 `phrozen-education-variant` 上的版本逐位元組相同，已 push 至 origin
  - PR 尚未實際建立（環境無 `gh` CLI），已準備好可直接點擊的 compare 連結與預先寫好的標題/說明，交由使用者於 GitHub 網頁完成建立動作
- [ ] 6.2 PR 描述須寫明三件事：①這是 resin/education 專用；②主線永遠不會自動觸發它；③存在目的是讓 Actions 頁面出現手動觸發按鈕，**請勿刪除**
- [x] 6.3 ~~確認此 PR 未觸發完整建置~~ → **結論已反轉**：此 PR **會**觸發一次完整 `build_all` 執行（見本組開頭更正說明），非「不會觸發」。已確認這是結構性、無法迴避的一次性代價，且已取得使用者同意接受
- [ ] 6.4 合入後確認 `Run workflow` 按鈕已出現，且從 UI 選 ref = `phrozen-education-variant` 可正常觸發
- [ ] 6.5 合入後確認：主線不再修改此檔案，之後由 resin 支線自由演進

## 7. 階段 1 收尾：移除開發期觸發（須在第 6 組完成後才做）

- [ ] 7.1 移除 `on:` 中開發期專用的整個 `push:` 區塊，只保留 `workflow_dispatch:`
- [ ] 7.2 完整跑一次驗證：確認移除開發期觸發後，一般 push 不會再觸發建置，且仍可從 UI 手動觸發

## 8. 階段 3：回歸 resin 主維護分支

- [ ] 8.1 merge `phrozen-education-variant` → `phrozen-resin-dev`
- [ ] 8.2 確認合併後 `.github/` 中的既有共用 CI 檔案（`build_orca.yml`、`build_deps.yml`、`build_check_cache.yml`、`build_all.yml`）與主線內容仍完全相同、未產生衝突
- [ ] 8.3 從 Actions UI 選 ref = `phrozen-resin-dev` 觸發一次，確認可正常建置

## 9. QA 手動驗收步驟（提交測試人員驗收用）

> 以下步驟涵蓋三個階段。標註「階段 N」者請在對應階段完成後執行。所有 workflow 執行記錄可在 GitHub repo 的 **Actions** 頁籤查看。

### 9.1 觸發機制驗收

- [x] 9.1.1 **（階段 1）開發期分支觸發生效**：對 `phrozen-education-variant` 分支推送一個只修改 `.github/workflows/build_education.yml` 的 commit。
  - 開啟 GitHub repo → **Actions** 頁籤 → 左側選擇 `build_education`
  - 預期：出現一筆新的執行記錄，觸發來源顯示為 push
  - **Pass**（2026-07-31 實測，多次推送皆正確觸發）
  - 附帶確認：此時 Actions 左側欄**會**出現 `Build Education` 項目，但右上角**沒有** `Run workflow` 按鈕 —— 證實左側欄看「是否曾有執行紀錄」、按鈕看「檔案是否在預設分支」，兩者機制不同，階段 2 無法省略

- [ ] 9.1.2 **（階段 1）paths 過濾生效**：對 `phrozen-education-variant` 分支推送一個**只修改 C++ 原始碼**（例如 `src/slic3r/GUI/` 底下任一檔案）的 commit。
  - 開啟 **Actions** → `build_education`
  - 預期：**不會**出現新的執行記錄（避免改程式碼時誤觸發昂貴建置）

- [ ] 9.1.3 **（階段 1 收尾後）開發期觸發已移除**：再次推送一個修改 `.github/workflows/build_education.yml` 的 commit。
  - 預期：**不再**自動觸發任何執行

- [ ] 9.1.4 **（階段 2）手動按鈕出現**：`build_education.yml` 合入 `release`（本 repo 實際的預設分支，見第 6 組開頭更正）後，開啟 **Actions** → 左側選擇 `build_education`。
  - 預期：右上角出現 **Run workflow** 按鈕（合入前不會出現）

- [ ] 9.1.5 **（階段 2）跨 ref 觸發正確**：點 **Run workflow** → 分支下拉選單選擇 `phrozen-education-variant` → 執行。
  - 預期：執行成功，且產出物為 education 變體（依 9.2／9.3 驗證）

- [ ] 9.1.6 **（階段 2）誤選主線分支會安全失敗**：點 **Run workflow** → 分支選擇 `release` 或 `phrozen-custom-dev`（兩者皆缺少 `build_resin_release_vs2022.bat` / `build_resin_release_macos.sh`）→ 執行。
  - 預期：**快速失敗**，且 **artifact 區塊沒有任何產出物**
  - 重點確認：不可產出任何「看似 education 實為主線」的成品

- [ ] 9.1.7 **（階段 3）從 resin-dev 觸發正常**：merge 回 `phrozen-resin-dev` 後，**Run workflow** 選擇 `phrozen-resin-dev` → 執行。
  - 預期：執行成功，產出物與 9.1.5 一致

### 9.2 Windows 產出物驗收

- [x] 9.2.1 **artifact 命名可區分**：在成功執行的頁面下方 **Artifacts** 區塊檢視清單。
  - 預期：各 artifact 名稱皆帶有 education 識別，與主線建置（`build_all` 的執行）的 artifact 名稱不重複
  - **Pass**（2026-07-31 實測）

- [x] 9.2.2 **安裝檔檔名**：下載 Windows 安裝檔 artifact 並解壓。
  - 預期：安裝檔檔名帶有 education 識別與版本尾綴（例如 `PhrozenOrca-Education_Windows_Installer_V1.2.1-Education.exe`），與主線同版本安裝檔可明確區分
  - **Pass**（2026-07-31 實測）。此檔名完全由 CMake 的 `CPACK_PACKAGE_FILE_NAME` 決定，能對上即同時證明 `PHROZEN_ORCA_ENABLE_RESIN=ON` 有傳進 CMake、`SLIC3R_APP_NAME` 被正確覆寫、`Phrozen_VERSION` 的尾綴由 CMake 動態附加成功

- [x] 9.2.3 **安裝後的識別隔離**：執行該安裝檔完成安裝。
  - 檔案總管網址列輸入 `%APPDATA%` → 預期出現 `PhrozenOrca-Education` 資料夾（不是 `PhrozenOrca`）
  - 安裝目錄底下的執行檔 → 預期為 `phrozen-orca-education.exe`
  - Windows「應用程式與功能」→ 預期出現獨立的 `PhrozenOrca-Education` 項目
  - 開啟軟體 →「說明」→「關於」→ 預期版本號顯示 `1.2.1-Education`
  - **Pass**（2026-07-31 實測：安裝後各處顯示皆帶有 education 尾綴）

- [x] 9.2.4 **portable ZIP 與 PDB**：確認 artifact 中同時有 portable ZIP 與 PDB 壓縮檔，且皆可正常解壓
  - **Pass**（2026-07-31 實測，`Pack portable ZIP` 與 `Pack PDB` 步驟皆通過並成功上傳）

- [x] 9.2.5 **deps 快取行為**：開啟該次執行的 Windows job log，搜尋 cache 相關步驟。
  - 預期：快取鍵含 education 識別；還原路徑為 `deps/build-resin/PhrozenOrca_dep`
  - 重點確認：**沒有**出現「宣稱快取命中，但後續建置卻找不到 deps」的矛盾情形（這是快取鍵誤用主線的典型症狀）
  - 第二次觸發時應顯示快取命中，且 deps 建置步驟被跳過
  - **Pass**（2026-07-31 實測：第二次執行確實命中快取並跳過 deps 建置）

### 9.3 macOS 產出物驗收

> **CI 管線本身已驗證成功（2026-08-03）**：macOS job 首次執行即全綠（建置、簽章、公證、staple 全部通過），已解除 `phrozen-education-variant-branding` 遺留的「無 mac 產出物可驗」的卡點。以下各項仍需在**真正的 Mac 硬體**上手動執行終端機指令才能確認，CI 日誌本身看不出使用者體感結果，故維持未勾選。
>
> **取得產出物**：GitHub repo → **Actions** → 左側 `build_education` → 點選該次執行 → 下方 **Artifacts** 區塊 → 下載 `PhrozenOrca-Education_Mac_universal_V<版本>-Education`（下載下來是 zip，解開後才是 `.dmg`）。
>
> **先看 job 有沒有全綠**：macOS job 的最後三個步驟依序是 `Import signing certificate`、`Sign app and create DMG`、`Notarize and staple`。
> - 若 `Import signing certificate` 紅燈 → secrets 缺失或內容有誤（對應 task 4.4），此時**不會有 DMG**
> - 若 `Notarize and staple` 紅燈但前面綠燈 → DMG 仍會上傳（`if: always()`），但它**只有簽章、沒有公證**，9.3.6 會失敗、9.3.5 應該仍會過。這個區分很有用：代表憑證沒問題，是 Apple ID／team-id／app 專用密碼那組 secret 的問題

- [ ] 9.3.1 **DMG 檔名**：下載 macOS DMG artifact。
  - 預期：檔名帶有 education 識別與版本尾綴，與主線 DMG 可明確區分

- [ ] 9.3.2 **`.app` 名稱**：在 Mac 上掛載該 DMG。
  - 預期：其中的應用程式套件名稱為 `PhrozenOrca-Education.app`（**不是** `PhrozenOrca.app`）

- [ ] 9.3.3 **Bundle Identifier**：把 `.app` 拖到「應用程式」資料夾後，開啟「終端機」執行：
  ```
  mdls -name kMDItemCFBundleIdentifier /Applications/PhrozenOrca-Education.app
  ```
  - 預期：回傳 `com.phrozen3d.phrozen-orca-education`
  - 若主線版也已安裝，對其執行同樣指令 → 預期回傳 `com.phrozen3d.phrozen-orca`，兩者不同

- [ ] 9.3.4 **內部 binary 名稱**：終端機執行：
  ```
  ls /Applications/PhrozenOrca-Education.app/Contents/MacOS/
  ```
  - 預期：看到 `PhrozenOrca-Education`（與 `Info.plist` 的 `CFBundleExecutable` 一致）

- [ ] 9.3.5 **簽章驗證**：終端機執行：
  ```
  codesign -dv --verbose=4 /Applications/PhrozenOrca-Education.app
  ```
  - 預期：顯示有效的 Developer ID 簽章、`flags` 含 `runtime`（hardened runtime 已啟用）、有 timestamp

- [ ] 9.3.6 **公證驗證（Gatekeeper）**：終端機執行：
  ```
  spctl -a -vv /Applications/PhrozenOrca-Education.app
  ```
  - 預期：結果為 `accepted`、`source=Notarized Developer ID`
  - 補充驗證：在一台**未曾安裝過此 App 的 Mac** 上下載 DMG 並開啟，預期**不會**跳出「無法驗證開發者」的 Gatekeeper 阻擋訊息

- [ ] 9.3.7 **使用者資料夾隔離**（同時解決 `phrozen-education-variant-branding` 遺留的驗收項目）：啟動 education 版一次後，Finder 按 `Cmd+Shift+G` 輸入 `~/Library/Application Support/`。
  - 預期：出現 `PhrozenOrca-Education` 資料夾
  - 若主線版也安裝過 → 預期 `PhrozenOrca` 與 `PhrozenOrca-Education` 兩個資料夾並存、內容互不干擾
  - **重點確認**：此項用於驗證 macOS 上的使用者資料夾是否確實依 `SLIC3R_APP_KEY` 命名（見 design.md Open Questions）；若實際資料夾名稱不符預期，須記錄實際值並回報

- [ ] 9.3.8 **兩個版本可並存**：確認 `PhrozenOrca.app` 與 `PhrozenOrca-Education.app` 可同時存在於「應用程式」資料夾，安裝其中一個不會覆蓋另一個；刪除其中一個時另一個的檔案與資料夾都不受影響

### 9.4 漂移偵測機制驗收

> **已在 GitHub Actions 上完整實測通過（2026-08-03～2026-08-04）**。三次連續觸發：① 正常狀態 → 綠燈；② 刻意破壞 `build_orca.yml` → 紅燈且準確指名；③ `git revert` 破壞用 commit → 恢復綠燈。
>
> 本機模擬（複製 lock 檔竄改 `build_release_macos.sh` 那行的 hash，重跑同一段 bash 解析邏輯）僅在實際 CI 驗證前用來檢查解析邏輯本身有沒有寫錯，不能取代下列這輪真正的 GitHub Actions 實測——這輪已經做過了。

- [x] 9.4.1 **正常情況通過**：在未變更主線共用 CI 檔案的情況下觸發 education 建置。
  - 開啟該次執行 → 預期 parity check job **通過（綠燈）**，且建置 job 正常接續執行
  - **Pass（2026-08-03 實測）**：push 第 5 組（新增 parity_check job）後首次觸發，parity check 通過，Windows／macOS 兩個建置 job 正常接續執行

- [x] 9.4.2 **偵測到變更時失敗**：在 resin 支線上刻意修改 `.github/workflows/build_orca.yml` 任意一行（測試用，事後還原）後觸發 education 建置。
  - 預期：parity check job **失敗（紅燈）**，且失敗訊息明確指出是 `build_orca.yml` 發生變更
  - **重點確認**：Windows 與 macOS 兩個建置 job 皆顯示為 **skipped**，未被啟動（避免浪費昂貴的 macOS 額度）
  - **Pass（2026-08-04 實測，commit `017db157cd`）**：parity check 紅燈，錯誤訊息準確指出 `.github/workflows/build_orca.yml` blob 不符；Windows／macOS 兩個 job 皆顯示 `skipped`，確認未被啟動

- [x] 9.4.3 **更新校驗值後恢復**：依失敗訊息的指示更新 `.github/education-ci-parity.lock` 後再次觸發。
  - 預期：parity check 通過，建置正常進行
  - **Pass（2026-08-04 實測，commit `2fcfda421a`），但走的是另一條等價路徑**：本次驗證是用 `git revert` 撤銷 9.4.2 的破壞用 commit，讓五個檔案的 blob hash 恢復成與 lock 檔記錄值一致，而非「依錯誤訊息更新 lock 檔的 hash」這條原描述的路徑——兩者本質相同（讓「本分支當下的 blob hash」與「lock 檔記錄值」重新一致），只是這次的落差來源是測試用的刻意修改（用 revert 復原），不是真正的主線同步（用更新 lock 檔記錄值來承認新狀態）。真正走「主線改了東西、同步後更新 lock 檔」這條路徑，會在下次實際合併主線進來時自然發生，屆時再驗一次
  - parity check 通過，Windows／macOS 兩個建置 job 恢復正常接續執行

- [x] 9.4.4 **成本確認**：檢視 parity check job 的執行時間。
  - 預期：僅數十秒等級（只做 checkout 與雜湊計算），未執行任何編譯、deps 或簽章步驟
  - **Pass**：三次執行中 parity check job 皆為秒等級完成，符合預期

### 9.5 主線不受影響的回歸驗收

- [ ] 9.5.1 **主線共用 CI 檔案未被修改**：在 `phrozen-custom-dev` 上比對本 change 前後的 `.github/workflows/build_all.yml`、`build_check_cache.yml`、`build_deps.yml`、`build_orca.yml`、`.github/actions/pack-win-release/action.yml`、`.github/actions/pack-win-nightly/action.yml`、`build_release_vs2022.bat`、`build_release_macos.sh`。
  - 預期：以上檔案內容**完全未被修改**；主線上唯一的新增是 `.github/workflows/build_education.yml` 這一個獨立檔案
  - **同一份清單也要在 resin 支線上檢查**：第 2 組改採獨立腳本後，`build_release_macos.sh` 在 **resin 支線上也應與主線逐位元組相同**。在 `phrozen-education-variant` 上執行 `git diff phrozen-custom-dev -- build_release_macos.sh build_release_vs2022.bat .github/workflows/build_all.yml .github/workflows/build_check_cache.yml .github/workflows/build_deps.yml .github/workflows/build_orca.yml`，預期**無任何輸出**（若有輸出，代表 resin 支線落後主線幾個 commit，須先 merge 主線再確認；這正是漂移偵測要防的情境）

- [ ] 9.5.2 **主線既有建置行為不變**：在主線觸發一次既有的完整建置（`build_all`）。
  - 預期：執行成功，產出物名稱與內容與本 change 實施前完全相同，**不含任何 `-Education`／`-education` 字樣**

- [ ] 9.5.3 **主線排程建置不受影響**：確認 `windows_nightly_dev`（每日）與 `macos_weekly_dev`（每週五）的排程執行仍正常，且建置的仍是 `phrozen-custom-dev` 的內容

- [ ] 9.5.4 **主線不會自動觸發 education 建置**：對 `phrozen-custom-dev` 推送任意變更。
  - 預期：`build_education` **不會**出現任何自動執行記錄

- [ ] 9.5.5 **（階段 3 後）merge 無衝突**：檢視 `phrozen-education-variant` → `phrozen-resin-dev` 的合併結果。
  - 預期：`.github/` 中的既有共用 CI 檔案未產生任何衝突，且合併後其內容與主線仍完全相同
