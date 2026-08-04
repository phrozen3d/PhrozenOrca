## Why

`phrozen-education-variant-branding`（已封存）已讓 resin/education 變體在建置時取得獨立的品牌識別（AppData 資料夾、執行檔名稱、安裝機碼、macOS bundle 等），但**目前沒有任何 CI/CD 能建出這個變體**：

- 既有的 `build_all.yml` → `build_check_cache.yml` → `build_deps.yml` → `build_orca.yml` 這條鏈完全寫死主線的建置腳本與路徑，macOS 端更是連一支 resin 專用的建置腳本都沒有（Windows 至少已有 `build_resin_release_vs2022.bat`）。
- education 版目前只能在開發者本機用 `build_resin_release_vs2022.bat` 手動建置 Windows 版；**macOS 版完全無法產出**，因為 macOS 的 codesign + notarize 憑證與 secrets 只存在於 GitHub Actions 環境。
- 這直接卡住了前一個 change 遺留的驗收項目（其 tasks 4.8／4.9：macOS 套件識別與最終回歸彙整），因為沒有 mac 產出物可驗。

同時，本次必須在極嚴格的約束下進行：主線 `phrozen-custom-dev` 是 1~2 年內都不打算納入 resin 功能的穩定分支，**不能在主線的共用 CI 檔案中塞入任何 resin 判斷**；而主線的 FDM 更新每月都會 merge 進 resin 支線，因此「主線 merge 下來不要衝突」是最高優先的維護性目標。

## What Changes

- **新增 `.github/workflows/build_education.yml`**：一個自帶完整邏輯、僅手動觸發（`workflow_dispatch`）的 workflow，一次建出 education 變體的 Windows 版與 macOS 版（含 codesign + notarize）。它**不呼叫**既有的三層 reusable workflow 鏈，而是自行實作 deps 快取、建置、簽章、打包與 artifact 上傳。
- **新增 `build_resin_release_macos.sh`**（僅在 resin 支線）：`build_release_macos.sh` 的變體副本，固定帶 `-DPHROZEN_ORCA_ENABLE_RESIN=ON`，並把「Fix macOS app package」與 `build_universal()` 中寫死的 `PhrozenOrca.app` / `Contents/MacOS/PhrozenOrca` 改為 `PhrozenOrca-Education`，建置目錄改用 `build-resin/` 與 `deps/build-resin/`。與 Windows 的 `build_resin_release_vs2022.bat` 同一套做法：**resin 專屬設定寫死在獨立腳本裡，共用的 `build_release_macos.sh` 一行都不動**。
- **新增 CI 漂移偵測機制**：在 resin 支線記錄主線**五個**共用檔案的校驗值 —— CI 三件（`build_orca.yml`、`build_deps.yml`、`build_check_cache.yml`）加建置腳本兩件（`build_release_vs2022.bat`、`build_release_macos.sh`，因 resin 的兩支獨立腳本是它們的複本）—— 由一個廉價的 parity check job 驗證；當主線的修正 merge 下來時，校驗值不符會讓 CI 大聲失敗，強制人工比對 education 這邊是否需要同步同樣的修正。
- **不修改任何共用檔案**（主線與 resin 支線皆是）：`build_all.yml`、`build_check_cache.yml`、`build_deps.yml`、`build_orca.yml`、`pack-win-release`／`pack-win-nightly` composite actions、`build_release_vs2022.bat`、`build_release_macos.sh` 全部一行都不動。resin 所需的一切都放在主線不會使用到的獨立新檔案裡。
- **不使用**既有的 `pack-win-release` composite action（共用檔案且 artifact 名稱寫死），改為在 `build_education.yml` 內聯打包步驟或另建 education 專用的獨立 composite action。

## Capabilities

### New Capabilities
- `resin-education-cicd`: 定義 resin/education 變體的 CI/CD 建置管線 —— 觸發方式（僅手動）、必須產出的成品（Windows NSIS 安裝檔／portable ZIP／PDB、macOS 已簽章公證的 DMG）、artifact 命名必須與主線可區分、以及「主線共用 CI 檔案零修改」與「主線建置行為完全不受影響」這兩個結構性約束。

### Modified Capabilities
（無 —— `openspec/specs/` 目前沒有既有 spec 涵蓋 CI/CD 管線；`resin-education-variant-branding` 涵蓋的是建置產物的識別字串，本 change 只是消費它的結果，不改變其任何需求。）

## Impact

- **新增檔案**（resin 支線）：`.github/workflows/build_education.yml`、`build_resin_release_macos.sh`、漂移偵測所需的校驗值紀錄檔、（可選）education 專用的 Windows 打包 composite action。
- **修改檔案**：無。共用檔案零修改是本 change 的結構性約束（見 design.md 決策 8）。
- **主線 `phrozen-custom-dev`**：僅新增 `.github/workflows/build_education.yml` 這一個獨立檔案（透過 PR），目的純粹是讓 Actions 頁面出現手動觸發按鈕；主線自己永遠不會觸發它，且主線的所有既有建置行為（`build_all`、`windows_nightly_dev`、`macos_weekly_dev`、`build_release`）完全不變。
- **依賴的 GitHub Secrets**（已存在於現有 macOS 簽章流程，本 change 沿用不新增）：`MAC_CERTIFICATE_P12`、`MAC_CERTIFICATE_PASSWORD`、`KEYCHAIN_PASSWORD`、`MAC_CERTIFICATE_NAME`、`APPLE_ID`、`TEAM_ID`、`APPLE_APP_PASSWORD`。
- **解除封鎖**：`phrozen-education-variant-branding` 遺留未完成的 tasks 4.8／4.9（macOS 套件識別驗收、主線回歸彙整）在本 change 完成後才具備驗收條件，其驗證步驟納入本 change 的 QA 清單。
- **CI 成本**：macOS runner 為 10 倍計費，單次完整建置含 deps（首次約 1 小時）、建置、公證等待（5~15 分鐘）。因此觸發方式限定為手動，並規劃「先 Windows、再 macOS 不簽章、最後才加簽章公證」的分階段實作順序以降低開發期成本。
