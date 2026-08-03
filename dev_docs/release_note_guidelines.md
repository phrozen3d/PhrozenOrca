# PhrozenOrca Release Note 撰寫規範

這份文件定義 PhrozenOrca 在 GitHub Release 上發佈 Release Note 時應遵循的格式與寫作規則，依據過去 v1.1.0 / v1.1.1 / v1.2.0 的實際發佈內容歸納而成。撰寫新版本 Release Note（包含請 AI 代寫）時，請依此規範產出。

---

## 1. 檔案結構總覽

一份 Release Note 由上到下依序為：

1. 標題與發佈日期
2. 內容分類區塊（依需要挑選，順序固定，見第 2 節）
3. 版本同步說明（如有 merge 上游 OrcaSlicer）
4. 注意事項 Notice（如有）
5. GitHub 自動產生的 `What's Changed` / `Full Changelog`（不用手動寫，由 GitHub Release 草稿自動附加）

區塊與區塊之間用 `---` 分隔。

---

## 2. 標題與 Metadata

```markdown
# PhrozenOrca v{X.Y.Z} Release Notes

**Release Date:** {Month YYYY}
```

- 版本號與日期皆置於最上方，日期格式固定為「英文月份全名 + 西元年」，例如 `March 2026`。
- 版本號需與 git tag / `version.inc` 一致。

---

## 3. 內容分類區塊（依出現順序）

只挑有內容的區塊放進去，**不要保留空區塊**。各區塊固定使用以下 emoji + 標題，不要自創或更換：

| 順序 | 標題 | 使用情境 |
|---|---|---|
| 1 | `## ✨ New Features` | 全新功能，使用者過去完全沒有的能力 |
| 2 | `## 📋 Major Features` | 大改版時取代「New Features」，用於整理大版號（如 v1.x.0）的主功能總覽，可包含多層子項目 |
| 3 | `## 🔧 Improvements` | 既有功能的優化、行為調整、效能/體驗提升（非 bug） |
| 4 | `## 🐛 Bug Fixes` | 修正錯誤行為 |
| 5 | `## 🌐 Translations & Localization` | 新增/更新多語言字串 |
| 6 | `## 🎨 UI/UX Updates` | 純視覺、素材、樣式調整，不影響功能邏輯 |
| 7 | `## 🔄 OrcaSlicer v{X.Y.Z} Sync` | 同步上游 OrcaSlicer 版本時使用，說明這次合併了哪些上游內容 |
| 8 | `## 📝 Workflow Changes` | 使用流程被改變，內含 `### Breaking Changes` 與 `### Deprecations` 子區塊 |
| 9 | `## 💡 Notice` | 需要使用者特別注意的事項（如系統權限提示） |

> 注意：emoji 必須用真正的 Unicode emoji 字元（✨ 🔧 🐛 🌐 🎨 🔄 📋 📝 💡），不要用文字描述或編碼錯誤的亂碼字元替代。發佈前務必確認檔案以 UTF-8 編碼儲存，避免重蹈過去 sample 檔案 emoji 變成亂碼（如 `â¨`、`ð§`）的問題。

### 3.1 子分類（### 階層）

每個大區塊內，依「功能模組 – 子功能」分小節，例如：

```markdown
### Monitor – Webcam Controls

### Monitor – Usability

### Slicing

### Stability
```

- 模組名稱用專有名詞（Monitor、Slicing、Calibration、Stability、UI 等），用 en dash `–` 連接子功能，不是 hyphen `-`。
- 若該分類下只有一個模組的內容，仍建議保留 `###` 小標題，方便未來擴充。

---

## 4. 條目（bullet）寫法

每一條都用同一個格式：

```markdown
- **{粗體的一句話標題，講動作 + 對象}**: {1–3 句說明，講清楚「使用者會遇到的症狀／情境」與「修正後的行為」}
```

寫作原則：

1. **受眾是終端使用者**（印表機操作者），不是工程師。優先描述「使用者會看到/感覺到什麼改變」，技術名詞（如 `T0`、`SAVE_CONFIG`、Moonraker、Klipper）只在使用者會直接接觸到（例如 G-code、韌體訊息）時才提及。
2. **粗體標題要能單獨成立**：就算使用者只掃過粗體部分，也要能看懂改了什麼。例如 `**Fixed webcam switch button pressed state**`，不要寫成 `**Fix bug #123**`。
3. **每條只講一件事**。多個相關修正分開列條目，不要塞進同一條。
4. **避免內部代號**：不要出現 issue 編號、分支名稱、PR 號、commit hash；如果需要追溯，留在 commit message 或 PR 描述，不要寫進 Release Note。
5. Bug Fixes 條目盡量交代「修正前的錯誤行為」與「修正後的正確行為」兩者，讓使用者能判斷自己是否受影響過。
6. Improvements / New Features 條目交代「使用者可以怎麼用」與「帶來的好處」。

---

## 5. `OrcaSlicer Sync` 區塊寫法

不用條列格式，用一段簡短說明文字即可：

```markdown
## 🔄 OrcaSlicer v{X.Y.Z} Sync

This release incorporates a partial/full merge of upstream OrcaSlicer v{X.Y.Z} updates, including {簡述合併內容的類別，例如 slicing algorithm improvements, new infill patterns, UI fixes, stability enhancements}.

{若有特別值得標註的單一行為改變，可用一段話補充說明，例如修正了某個具體的列印行為}
```

---

## 6. `Workflow Changes` 區塊寫法（僅大版號使用）

用於使用者操作流程被改變的大版本，需明確分兩個子區塊：

```markdown
## 📝 Workflow Changes

### Breaking Changes

- **{改變的流程名稱}**: {改變前 vs 改變後的行為差異}
<img width="{px}" height="{px}" alt="{說明文字}" src="{GitHub 附件圖片連結}" />

### Deprecations

- {被棄用的舊流程/功能，一句話即可}
```

- Breaking Changes 條目可附截圖（GitHub Release 編輯器貼圖會自動產生 `user-attachments` 連結），用於對比新舊操作介面時建議附上。
- Deprecations 條目通常不需要截圖，列出名稱即可。

---

## 7. `Notice` 區塊寫法

用於需要使用者主動配合的提示（例如系統權限對話框），格式：

```markdown
## 💡 Notice

**{平台或情境前綴，例如 macOS Users}**: {要使用者做的具體動作，講清楚會看到的系統提示文字原文，以及該選哪個選項}

{若需要，補一段說明為什麼需要這個權限/動作}
```

---

## 8. 結尾：GitHub 自動產生區塊

不需要手動撰寫，使用 GitHub Release 介面的「Generate release notes」功能會自動附加在最後：

```markdown
## What's Changed
* {PR 標題} by @{作者} in {PR 連結}

**Full Changelog**: https://github.com/phrozen3d/PhrozenOrca/compare/v{上一版}...v{這一版}
```

撰寫 Release Note 主體時，把這段留給 GitHub 自動生成，不要手刻。

---

## 9. 完整骨架範本

```markdown
# PhrozenOrca v{X.Y.Z} Release Notes

**Release Date:** {Month YYYY}

---

## ✨ New Features

### {Module} – {Sub-feature}

- **{標題}**: {說明}

---

## 🔧 Improvements

### {Module}

- **{標題}**: {說明}

---

## 🐛 Bug Fixes

### {Module}

- **{標題}**: {說明}

---

## 🎨 UI/UX Updates

- **{標題}**: {說明}

---

## 🔄 OrcaSlicer v{X.Y.Z} Sync

This release incorporates a {partial/full} merge of upstream OrcaSlicer v{X.Y.Z} updates, including {...}.

---

## 💡 Notice

**{Platform} Users**: {動作說明}

{補充說明}
```

---

## 10. Checklist（發佈前自我檢查）

- [ ] 標題版號與 `version.inc` / tag 一致
- [ ] 只保留有內容的區塊，空區塊不留
- [ ] 區塊順序符合第 2 節表格
- [ ] emoji 是正確的 Unicode 字元，檔案存成 UTF-8
- [ ] 每條 bullet 都是「使用者視角」寫法，沒有內部代號/PR 號/issue 號
- [ ] Breaking Changes 區塊有附上對應截圖（如適用）
- [ ] `What's Changed` / `Full Changelog` 交給 GitHub 自動產生，沒有手刻
