## Context

### 既有 CI 架構

目前的建置管線是三層巢狀的 reusable workflow：

```
build_all.yml / build_release.yml / windows_nightly_dev.yml / macos_weekly_dev.yml   （觸發點）
        │
        ▼
build_check_cache.yml   （決定 deps 快取鍵與路徑，判斷是否命中）
        │
        ▼
build_deps.yml          （未命中則建 deps；呼叫 build_release_vs2022.bat deps / build_release_macos.sh -d）
        │
        ▼
build_orca.yml          （建 slicer；Windows 交給 pack-win-* composite action 打包，
                          macOS 內嵌 codesign / notarize / hdiutil 產 DMG）
```

`build_all.yml` 是唯一同時涵蓋 Windows 與 macOS **且帶 `sign-and-notarize: true`** 的入口，因此是本 change 的參考藍本。

### 分支拓撲與資料流

```
phrozen-custom-dev（預設分支，主線 FDM）
        │
        │  FDM 功能更新 merge 下來（頻繁，每月）
        ▼
phrozen-resin-dev（未來的 resin/education 主要維護分支）
        ▲
        │  本 change 完成並驗收後 merge 回去
        │
phrozen-education-variant（目前的開發分支）
```

短期內（1~2 年）不會有 resin → 主線的合併。

### 關鍵實測數據（驅動本設計的證據）

- **resin 支線從未修改過 `.github/` 下的任何檔案**。目前兩分支的 `.github/` 內容不同，純粹是因為 resin 支線尚未合併主線較新的 commit（落後 4 個 CI 修正），而非 resin 自行改動所致。這個「resin 不碰共用 CI 檔案」的性質正是本設計要保住的 —— 只要維持它，主線的 CI 修正 merge 下來就永遠是乾淨套用。
- `build_orca.yml` 在主線歷史上被改過 **71 次**，且近期仍在密集修正（`windows-latest` 已不再提供 VS 2022 而改釘 `windows-2022`、`actions/cache@v4` 需停留 Node 20、macOS deps cache 路徑錯誤導致快取從未建立、notarize 流程修正）。
- `build_release_macos.sh` 目前兩分支完全相同（resin 支線亦未改動過）。
- 這代表：任何讓 resin 支線去修改這些檔案的做法，都會把「目前零成本」的月度 merge 變成「反覆手動解衝突」。

> **實地驗證（2026-07-31）**：階段 1-A 首次執行時，Windows 建置因 CMake 找不到 Visual Studio 而失敗。根因是 `build_education.yml` 照著 resin 支線上**較舊**的 `build_all.yml` 寫成 `windows-latest`，而主線早在 `8359f943aa` 就已改釘 `windows-2022`。同一批主線修正還包含 `actions/cache@v4` 必須設定 `ACTIONS_ALLOW_USE_UNSECURE_NODE_VERSION`。
> 這正是決策 5（漂移偵測）要防的情境的真實案例，且證實了它的必要性 —— 差別只在這次是「一開始就沒跟上」而非「事後漂移」。設計漂移偵測時，其基準值應以**主線最新版本**為準，而非 resin 支線當下的（可能落後的）版本。

## Goals / Non-Goals

**Goals:**
- 能以純手動方式，一次建出 education 變體的 Windows 版（NSIS 安裝檔、portable ZIP、PDB）與 macOS 版（已 codesign + notarize + staple 的 DMG）。
- 主線 `phrozen-custom-dev` 的**共用 CI 檔案零修改**，主線既有建置行為（`build_all`、nightly、weekly、release）完全不受影響。
- 主線 FDM 更新 merge 進 resin 支線時，`.github/` 與 `build_release_vs2022.bat` 等共用檔案**不產生衝突**。
- 在「不共用邏輯」的前提下，仍能讓主線的 CI 修正**不容易被遺漏**（透過自動漂移偵測，而非依賴人的記性）。
- 能在 `phrozen-education-variant` 分支上完成完整驗證後，才動主線與 `phrozen-resin-dev`。
- 產出物命名與主線可明確區分，避免人為誤用。

**Non-Goals:**
- 不建置主線（FDM）變體 —— 主線由 `phrozen-custom-dev` 上既有的管線負責，本 workflow 只建 education。
- 不參數化既有的三層 reusable workflow 鏈（不加 `variant` input）。
- 不改變 `phrozen-education-variant-branding` 已定義的任何識別字串規則，本 change 只是消費其結果。
- 不處理 Windows 端的程式碼簽章（主線目前也是手動簽章，見 `build_release.yml` 標題註記的 disabled 原因），本 change 不改變此現狀。
- 不建立自動發布到 GitHub Releases 的流程（產出物停在 workflow artifact 層級）。
- 不處理 Linux 變體。

## Decisions

### 1. `build_education.yml` 自帶完整邏輯，不呼叫既有 reusable workflow 鏈

**決策**：寫一個獨立、自帶完整邏輯的 workflow，自行實作 deps 快取、建置、簽章、打包、上傳，完全不 `uses: ./.github/workflows/build_check_cache.yml`。

**替代方案考慮**：在三層鏈加上 `variant` input（預設 `main`），由 `build_education.yml` 傳 `variant: education` —— **放棄**。這個方案在「單一份共用邏輯」上較優，但它會讓 resin 支線的 `build_orca.yml`／`build_deps.yml`／`build_check_cache.yml` 與主線結構永久不同。依上述實測數據，這三個檔案目前零分歧且是主線最常改的檔案（`build_orca.yml` 71 次），參數化等於把每月一次的乾淨 merge 變成反覆解衝突。由於 resin 1~2 年不回主線，「共用邏輯」的收益期很短，不值得換取這個長期成本。

**代價與補償**：自帶邏輯會產生邏輯重複，主線修了 CI 不會自動惠及 education。這個代價由決策 5（漂移偵測）補償。

### 2. 觸發方式：`workflow_dispatch`（正式）＋ 開發期暫用分支觸發

**背景限制**：GitHub 規定 `workflow_dispatch` 的 workflow 檔案必須存在於**預設分支**，Actions 頁面才會出現「Run workflow」按鈕。本 repo 預設分支為 `phrozen-custom-dev`（`origin/main` 不存在，只有 `origin/release`）。但「按鈕是否出現」看預設分支，「實際執行什麼」看使用者選的 ref；且 local reusable workflow（`uses: ./…`）從呼叫者所在的同一個 commit 解析。因此按鈕一旦出現，即可選擇任意分支執行該分支的版本。

> **實測確認（2026-07-31）**：workflow 檔案只推到 `phrozen-education-variant`（未合入預設分支）時，Actions 左側欄**會**出現 `Build Education` 項目，但右上角**沒有** `Run workflow` 按鈕。兩者是不同機制：
> - 左側欄列出項目 ← 該 workflow **曾產生過任何執行紀錄**（本例是開發期 push 觸發造成的）
> - `Run workflow` 按鈕 ← workflow 檔案**存在於預設分支**
>
> 這確認了階段 2 無法省略。同時也帶出一個順序要求：**必須先完成階段 2（按鈕出現），才能移除開發期的 push 觸發**，否則會出現兩種觸發方式都失效的空窗期。

**決策**：正式觸發用 `workflow_dispatch`；開發期額外加上分支觸發，讓階段 1 完全不需要碰主線：

```yaml
on:
  workflow_dispatch:
  push:                                   # 開發期專用，定稿前整段移除
    branches: ['phrozen-education-variant']
    paths:
      - '.github/workflows/build_education.yml'
      - 'build_resin_release_macos.sh'
      - 'build_resin_release_vs2022.bat'
```

**替代方案考慮**：用 tag 觸發（`push: tags: ['edu-test-*']`）—— 同樣能在不碰主線的情況下觸發，但 CI 除錯常需十幾二十次迭代，會累積大量無用的 tag，事後還要清理。分支觸發零 tag 累積、push 即測試，且 `paths` 過濾可避免改 C++ 時誤觸發昂貴建置。此外分支名寫死 `phrozen-education-variant`，將來 merge 進 `phrozen-resin-dev` 後該觸發自動失效，不會意外啟動。

### 3. 不定義 `inputs:`

**決策**：`build_education.yml` 不宣告任何 `workflow_dispatch` inputs。

**理由**：跨 ref 觸發時，UI 輸入欄位的定義來源歷史上存在歧義行為。此 workflow 本來就只建 education 單一變體，沒有需要參數化的東西，不宣告 inputs 即完全迴避這類問題。

### 4. 三階段落地流程

```
階段 1（完全不碰主線）
  在 phrozen-education-variant 開發，用分支觸發反覆迭代直到通過
        │
        ▼
階段 2（動主線，僅一個獨立檔案）
  開 PR 把 build_education.yml 單獨放進 phrozen-custom-dev → 按鈕出現
  之後改用 UI 選 ref 觸發，確認跨 ref 機制正常
        │
        ▼
階段 3（回歸 resin 主維護分支）
  merge phrozen-education-variant → phrozen-resin-dev
```

**PR 到主線的做法**：PR 只包含 `build_education.yml` 一個檔案，不夾帶其他東西；PR 描述與檔案開頭註解都須寫明「這是 resin/education 專用、主線永遠不會自動觸發、存在目的是讓 Actions 出現手動按鈕、請勿刪除」，避免日後被當成無用檔案清掉。合入後主線就不再碰這個檔案 —— git 只在雙方都改動同一區域時才衝突，主線不動即永遠不會衝突。

**成本確認**：`build_all.yml` 的 PR 觸發條件是 `branches: [main, release]`，`phrozen-custom-dev` 不在清單內，故此 PR **不會**觸發完整建置，只有 shellcheck 之類的輕量檢查會跑。

### 5. 漂移偵測機制

**決策**：在 resin 支線記錄主線五個共用檔案的校驗值 —— CI 三件（`build_orca.yml`、`build_deps.yml`、`build_check_cache.yml`，`build_education.yml` 自帶邏輯的來源）加上建置腳本兩件（`build_release_vs2022.bat`、`build_release_macos.sh`，resin 兩支獨立腳本的複本來源；此二者為決策 8 推翻後追加，見該節）—— 由 `build_education.yml` 中一個**最前置、最廉價**的 parity check job 驗證。校驗值不符時直接讓 workflow 失敗並輸出明確訊息。

**監控範圍由三件擴充為五件的理由**：本決策原本只監控 CI 三件。決策 8 於 2026-08-03 推翻後，resin 的建置腳本改為主線腳本的**複本**（`build_resin_release_vs2022.bat`、`build_resin_release_macos.sh`），主線對這兩支腳本的修正不再會以 merge 衝突的形式現身，必須由本機制承接 —— 否則等於用「無聲的錯誤」換掉了「吵鬧的衝突」。

**運作方式**：

```
主線修了 build_orca.yml（例如修 macOS deps cache 路徑）
        │
        ▼
merge 進 resin 支線 → .github/ 乾淨套用，不衝突（設計使然）
        │
        ▼
下次觸發 build_education → parity check job 偵測到校驗值不符 → 大聲失敗
        │
        ▼
人工比對主線這次的修正，決定 build_education.yml 是否需要同步
        │
        ▼
同步（或確認不需要）後，更新記錄的校驗值 → 恢復綠燈
```

**設計要點**：
- 校驗值紀錄檔放在 resin 支線的固定位置（例如 `.github/education-ci-parity.lock`），內容為檔名與其雜湊值的對應，並附上「上次比對時主線的 commit」以利人工 diff。
- parity check 必須是獨立且極廉價的 job（純 checkout + 計算雜湊），且必須排在建置 job **之前**並作為其前置依賴，避免在昂貴的 macOS 建置跑完之後才失敗。
- 失敗訊息需明確指出哪個檔案變了、以及該去比對什麼，讓不熟悉此機制的人也能處理。

**替代方案考慮**：純靠文件提醒「merge 後記得檢查 CI」—— 放棄，這正是使用者明確要避免的「容易遺漏」情境；設計上就不該依賴記性。

### 6. deps 快取：education 使用獨立的快取鍵與路徑

**決策**：education 使用與主線**不同的快取鍵**，Windows 的快取路徑對應 `build_resin_release_vs2022.bat` 實際使用的 `deps/build-resin/PhrozenOrca_dep`。

**理由（重要陷阱）**：主線的快取鍵格式為 `${os}-cache-phrozenorca_deps-build-${hashFiles('deps/**')}`，路徑 Windows 為 `deps/build/PhrozenOrca_dep`。而 resin 的建置腳本用的是 `deps/build-resin/PhrozenOrca_dep`。**若沿用相同的快取鍵但指定不同路徑**，會命中主線（含預設分支 fallback）建立的快取，但還原出來的檔案結構與 resin 腳本期待的位置不符，造成難以診斷的失敗。因此快取鍵必須帶有 education 識別。

**代價**：education 首次建置（以及每次 `deps/**` 雜湊變動後）需自行完整建 deps（約 1 小時）。使用者已確認不需與主線共用快取，接受此成本。

**自帶邏輯衍生的陷阱（2026-07-31 實地踩到）**：`actions/cache` 的 `key` 在「還原」與「儲存（Post）」兩個時機都會被重新求值，而 `hashFiles()` 有 120 秒上限。deps 建置完成後 `deps/build-resin/` 會有數萬個檔案，Post 步驟再對 `deps/**` 求雜湊必定逾時，導致整個 job 在最後一步失敗且**快取存不進去**（建置本身已成功，白跑一小時）。

主線不會遇到這個問題，因為它把 `hashFiles` 放在獨立的 `check_cache` job 裡算（該 job 不建置任何東西，`deps/` 始終乾淨），下游 job 收到的 `inputs.cache-key` 只是純字串。本設計把邏輯自帶進單一 job 時若直接把 `hashFiles()` 內嵌進 `key`，就會複製出這個 bug。

**對策**：在建置開始前先用一個獨立步驟算出完整快取鍵並寫入 step output，`key` 之後只引用該字串。這是「自帶完整邏輯」這個架構決策必須額外付出的注意成本 —— 凡是主線靠「分成多個 job」自然迴避掉的問題，自帶邏輯都需要顯式處理。

### 7. Windows 打包不使用既有 composite action

**決策**：不使用 `.github/actions/pack-win-release`，改為在 `build_education.yml` 內聯打包步驟，或另建 education 專用的獨立 composite action。

**理由**：`pack-win-release/action.yml` 是共用檔案，且 artifact 名稱寫死 `PhrozenOrca_Windows_...`。若為 education 加參數，就違反「主線共用檔案零修改」的約束；若只在 resin 支線改它，又會製造新的月度衝突源。內聯或另建獨立 action 兩者都能完全迴避。

**附註**：該 action 內的 `PhrozenOrca*.exe` glob 剛好也能匹配到 `PhrozenOrca-Education_Windows_Installer_*.exe`，但這不改變上述結論。

### 8. macOS 改用獨立的 `build_resin_release_macos.sh`（已推翻原決策，2026-08-03）

**決策**：新增 `build_resin_release_macos.sh`，作為 `build_release_macos.sh` 的變體副本，resin 專屬設定全部寫死其中；共用的 `build_release_macos.sh` 在 resin 支線上保持與主線逐位元組相同。與 Windows 的 `build_resin_release_vs2022.bat` 同一套做法。

**原決策與推翻理由**：本節原本的決策是「在 resin 支線上修改 `build_release_macos.sh`，新增 `-r` 旗標與 `APP_NAME` 變數，採純新增式」，並明確放棄複本方案，理由是「複製約 250 行 macOS 建置邏輯的漂移風險，遠大於少量分歧帶來的 merge 成本」。該版本一度實作完成後由使用者推翻。

推翻的理由是原決策**誤判了約束的優先序**：本 change 從 proposal 起就把「主線共用檔案零修改」列為結構性約束（見 spec 的「主線共用檔案零修改」與「主線更新合併至 resin 支線時不產生 CI 檔案衝突」兩條 Requirement），而 `build_release_macos.sh` 正是共用檔案。原決策把它當成「可以接受少量分歧的一般檔案」來權衡，等於在 CI 檔案上嚴守的原則，到了建置腳本上就自己破例。

成本結構也支持推翻：主線的 FDM 更新每月 merge 進 resin 支線是常態，resin 回主線是一兩年後的事。共用檔案的分歧會讓**每一次**主線 merge 都付出衝突解決成本；複本的漂移則只在主線真的改動該腳本時才需要人工比對一次。兩者不對稱。

**必須涵蓋的寫死處**（複本中已全部處理，仍列出以便日後與主線比對）：
- 「Fix macOS app package」段落的 `PhrozenOrca.app`
- `build_universal()` 的 `$PROJECT_BUILD_DIR/PhrozenOrca/PhrozenOrca.app` 與 `cp -R .../PhrozenOrca.app`
- 特別是 **`BINARY_PATH="Contents/MacOS/PhrozenOrca"`** —— 這是 `lipo` 的輸入檔路徑，未同步會直接找不到檔案而失敗
- `-DCMAKE_INSTALL_PREFIX="$PWD/PhrozenOrca"` —— 此 install prefix 與 staging 目錄是同一路徑，兩變體共用會讓 DMG 內同時出現兩個 `.app`。已確認 `BIN_RESOURCES_DIR` 取自 `CMAKE_CURRENT_BINARY_DIR` 而非 install prefix，故改動無其他副作用

**原決策提出的漂移風險仍然成立，須由機制承接**：複本的代價是主線修了 bug 時 resin 這邊**完全不會有任何訊號** —— 等於用「無聲的錯誤」換掉了「吵鬧的衝突」。因此決策 5 的漂移偵測監控清單必須從三個 CI 檔案擴充為五個，納入 `build_release_vs2022.bat` 與 `build_release_macos.sh`。**沒有這項擴充，本決策不成立。**

**建置目錄一併分開**：複本使用 `build-resin/<arch>` 與 `deps/build-resin/<arch>`，與 Windows 的 bat 一致。deps 本身其實與變體無關（`deps/` 底下完全沒有引用 `PHROZEN_ORCA_ENABLE_RESIN`），分開純粹是為了本機同時建兩變體時互不覆蓋、以及維持一致的心智模型；CI 端因快取鍵本來就分開，無額外成本。

**有利條件**：因為 `phrozen-education-variant-branding` 的 follow-up fix（commit `d29c9830c3`）讓 `.app` 名稱改由 `SLIC3R_APP_KEY` 決定，主線的 `.app` 維持 `PhrozenOrca.app` 原狀，故主線腳本完全不需要任何改動。

### 9. 版本字串需在 CI 端補上尾綴

**決策**：CI 讀取 `version.inc` 的 `Phrozen_VERSION` 後，需自行附加 `-Education` 才用於 artifact／DMG 命名。

**理由**：`phrozen-education-variant-branding` 已把 `version.inc` 改為乾淨的 base 版本號（合併主線後為 `1.2.1`），`-Education` 尾綴改由 CMake 在 `PHROZEN_ORCA_ENABLE_RESIN=ON` 時動態附加。CI 的版本擷取步驟（現有做法見 `build_orca.yml` 的 `Get the version and date` 步驟）直接讀檔，讀到的是不含尾綴的值，故需在 CI 端補上，否則產出物檔名無法與主線區分。

### 10. 分階段實作順序（把貴的留到最後）

macOS runner 為 10 倍計費，單次完整建置含 deps（首次約 1 小時）、建置、以及公證等待（5~15 分鐘）。實作順序刻意設計為：

1. **Windows job 先跑通** —— 便宜，先確認 deps 快取鍵/路徑正確、`cpack -G NSIS` 產出、artifact 命名。
2. **macOS job 但先不簽章** —— 走等同於 `build_orca.yml` 中「Create DMG without notary」的路徑，先確認 `build_resin_release_macos.sh` 的 `.app` 名稱、`build_universal()` 的 lipo 都正確。
   **此步驟已於實作時與步驟 3 合併執行**（未簽章的 DMG 在 macOS 14 上會被 Gatekeeper 以「已損毀」擋下，需啟動軟體才能驗的 QA 項目根本跑不了；且分階段是保證跑兩次 10 倍計費的 macOS job）。理由詳見 tasks.md 第 3 組開頭。
3. **最後才加 codesign + notarize** —— 最貴且最容易卡在憑證匯入、keychain、Apple ID 認證等問題。

階段 1 開發 Windows 時，可先把整個 macOS job 註解掉以避免每次都消耗 10 倍額度。

## Risks / Trade-offs

- **[風險] 邏輯重複導致 education CI 悄悄過期**：主線修了 CI（例如近期修的 macOS deps cache 路徑錯誤、windows runner 判斷），`.github/` merge 下來完全不衝突，沒有任何提示。→ Mitigation：決策 5 的漂移偵測機制，把「主線改了 CI」變成一個必須處理的紅燈。此為本設計最重要的補償措施，不可省略。

- **[風險] macOS 公證失敗且回饋週期長**：公證需等待 5~15 分鐘，且失敗原因（憑證、keychain、entitlements、未簽章的嵌套程式碼）不易從單次 log 判斷。→ Mitigation：決策 10 的分階段順序，把簽章公證留到最後單獨處理；先確保未簽章的 DMG 能正確產出，縮小問題範圍。

- **[風險] deps 快取鍵設計錯誤導致誤命中主線快取**：見決策 6，這是最容易踩且最難診斷的陷阱（症狀是檔案出現在錯誤路徑，而非明確的快取未命中）。→ Mitigation：tasks 中明列快取鍵必須含 education 識別，並在 QA 步驟中要求實際檢查 workflow log 的 cache 命中訊息與還原路徑。

- **[Trade-off，已接受] education 首次建置需完整建 deps（約 1 小時）**：因不共用主線快取所致。使用者已確認接受，且僅在 `deps/**` 雜湊變動時才會重複發生。

- **[Trade-off，已接受] 主線多一個它自己不會用到的 workflow 檔案**：這是 `workflow_dispatch` 必須在預設分支的硬性限制所致。使用者已確認「獨立檔案可接受，共用檔案塞 resin 條件不可接受」。風險是日後被當成廢檔刪除，由決策 4 的 PR 描述與檔案註解緩解。

- **[風險] 從主線 ref 誤觸發**：若有人在 UI 選 ref 為 `phrozen-custom-dev` 觸發，該分支沒有 `build_resin_release_vs2022.bat`，也沒有 `PHROZEN_ORCA_ENABLE_RESIN` 這個 CMake option。→ 評估為**可接受**：會快速失敗且訊息明確，不可能悄悄產出「看似 education 實為主線」的錯誤成品。QA 步驟應實際驗證此行為。

## Migration Plan

1. 階段 1：在 `phrozen-education-variant` 上依決策 10 的順序開發與驗證（Windows → macOS 不簽章 → 加簽章公證），使用分支觸發迭代。
2. 階段 1 結束前移除 `on: push:` 開發期觸發區塊，只留 `workflow_dispatch`。
3. 階段 2：開 PR 將 `build_education.yml` 單獨合入 `phrozen-custom-dev`；合入後用 UI 選 ref = `phrozen-education-variant` 觸發，確認跨 ref 機制與產出物皆正確。
4. 階段 3：merge `phrozen-education-variant` → `phrozen-resin-dev`，並確認從 UI 選 ref = `phrozen-resin-dev` 也能正確建置。
5. 回填 `phrozen-education-variant-branding` 遺留的 macOS 驗收項目（該 change 已封存，驗收結果記錄於本 change 的 QA 清單）。
6. Rollback：本 change 不影響任何線上服務或主線建置；若需回退，移除新增的 workflow 檔案即可，主線與 resin 支線的既有建置流程都不受影響。

## Open Questions

- **`CFBundleShortVersionString` / `CFBundleVersion` 的異常值是否影響公證**：`Info.plist.in` 中兩者分別展開為 `"PhrozenOrca-Education "`（結尾空格）與**空字串**，因為 `SLIC3R_BUILD_ID` 在整個專案從未被任何 CMake 檔案 `set()` 過。這是主線既有現象（主線為 `"PhrozenOrca "` 與空字串），非 education 引入。評估上，notarytool 只驗簽章有效性、hardened runtime、timestamp 與未簽章的嵌套程式碼，**不驗版本字串格式**（驗格式的是 App Store 審核），且主線帶著相同的值走同一條簽章流程，故推論不構成風險 —— 但這是推論，需在階段 3 實際跑過 notarize 才能確認。若確實受阻，需另開 change 處理 `SLIC3R_BUILD_ID` 從未設定這個更上游的問題。
- **macOS 使用者資料夾命名的實機確認**：`~/Library/Application Support/` 底下的資料夾是否確實依 `SetAppName(SLIC3R_APP_KEY)` 決定（程式碼註解如此描述，但未在 mac 實機確認 wxWidgets 是否在某些情況改用 bundle identifier）。這是 `phrozen-education-variant-branding` 遺留的待驗證項目，本 change 產出 mac 成品後應一併確認。
- **公證與新 bundle ID**：公證不需要事先在 App Store Connect 註冊 bundle ID，`com.phrozen3d.phrozen-orca-education` 理論上可直接簽章公證；階段 3 實測確認。
- **漂移偵測的校驗值更新流程是否需要工具輔助**：初期以人工更新 `.lock` 檔即可；若日後發現更新頻繁且易錯，再考慮提供一個更新用的小腳本。
