## ADDED Requirements

### Requirement: 翻譯 catalog domain 名稱不受 Education 尾綴規則影響

系統查找與載入翻譯字典（`.mo`）時所使用的 `wxTranslations` catalog domain 名稱 SHALL 維持固定值，不隨「Education 尾綴命名規則」變化，即不得直接使用套用了 `-Education`/`-education` 尾綴的 `SLIC3R_APP_KEY` 作為翻譯 domain。翻譯 `.mo` 檔案在所有建置變體間共用同一份，其查找 domain 名稱 SHALL 與翻譯檔案實際編譯輸出的檔名（不含副檔名）保持一致。

#### Scenario: Education/Resin build 下語言清單完整

- **WHEN** 使用 `build_resin_release_vs2022.bat` 建置（`PHROZEN_ORCA_ENABLE_RESIN=ON`）後開啟 Preference 對話框的語言選項
- **THEN** 語言清單顯示所有已編譯的翻譯語言（不只 English），與主線 FDM build 顯示的語言清單一致

#### Scenario: Education/Resin build 下切換語言可正確翻譯介面

- **WHEN** 在 Education/Resin build 中，使用者於 Preference 選擇 English 以外的語言
- **THEN** 介面文字正確切換為該語言的翻譯內容，不因建置變體不同而載入失敗或維持英文

#### Scenario: 主線 FDM build 行為不變

- **WHEN** 使用 `build_release_vs2022.bat` 建置（`PHROZEN_ORCA_ENABLE_RESIN` 維持預設 `OFF`）
- **THEN** 翻譯查找 domain 名稱與此規則引入前完全相同，語言清單與翻譯載入行為不受影響
