# 將 OrcaSlicer v2.3.1 同步到 PhrozenOrca - 更新策略

## 🚨 重要發現

經過實際嘗試 git merge，發現了以下關鍵問題：

### 衝突規模
- **衝突檔案數量**: 5,471 個檔案
- **PhrozenOrca 的客製化**: 從共同起點後有 264 個 commits
- **OrcaSlicer 的更新**: 從共同起點到 v2.3.1 有 157 個 commits
- **共同起點**: `c8a27715a` (OrcaSlicer v2.3.1-dev 早期)

### 為什麼衝突這麼多？

1. **大規模品牌重命名**
   - PhrozenOrca 將所有 "OrcaSlicer" 字串改為 "PhrozenOrca"
   - 影響了幾乎所有的原始碼、設定檔、翻譯檔案
   - OrcaSlicer 在同一時期也更新了這些檔案

2. **新增大量 Profile 檔案**
   - OrcaSlicer v2.3.1 新增了許多新的機台 profile（Wanhao, Z-Bolt, iQ 等）
   - PhrozenOrca 也有自己的 Phrozen Arco profile
   - 這些都是新檔案，但 git 認為是衝突

3. **翻譯檔案的雙向更新**
   - PhrozenOrca 更新了翻譯檔案（PhrozenOrca_*.po）
   - OrcaSlicer 也更新了翻譯檔案（OrcaSlicer_*.po）
   - 檔名不同但 git 認為是重命名衝突

---

## 🔄 修正後的策略建議

> [!CAUTION]
> 直接 git merge 在這種情況下**不可行**。需要採用更聰明的策略。

### 策略 A：選擇性 Cherry-pick 關鍵修復（推薦）

這是最實際且風險最低的方法。

**原理**:
- 不嘗試合併所有變更
- 只挑選對 PhrozenOrca 有價值的修復和改進
- 保持 PhrozenOrca 的客製化完整性

**步驟**:

#### 1. 識別關鍵類別的 commits

```bash
cd c:\Dev\PhrozenOrca
git checkout phrozen-custom-dev

# 建立工作分支
git checkout -b selective-merge-v2.3.1

# 列出所有 bug 修復
git log c8a27715a..v2.3.1 --grep="fix" --grep="Fix" --grep="FIX" --oneline --no-merges > fixes.txt

# 列出安全性修復
git log c8a27715a..v2.3.1 --grep="security" --grep="crash" --grep="CVE" --oneline --no-merges > security.txt

# 列出核心功能改進
git log c8a27715a..v2.3.1 --grep="refactor" --grep="improve" --oneline --no-merges > improvements.txt
```

#### 2. 手動審查並分類

建立一個 Excel 或文字檔，將 commits 分為：
- **必須合併**: 安全性修復、嚴重 bug 修復
- **建議合併**: 功能改進、效能優化
- **可選合併**: 新功能、UI 改進
- **不合併**: Profile 更新（與 Phrozen 無關的機台）、品牌相關

#### 3. 逐一 Cherry-pick

```bash
# 針對每個選定的 commit
git cherry-pick <commit-hash>

# 如果有衝突
# 1. 檢查衝突內容
git status

# 2. 手動解決衝突（優先保留 PhrozenOrca 的品牌化）
# 3. 標記為已解決
git add <resolved-files>

# 4. 繼續
git cherry-pick --continue
```

#### 4. 重點關注的修復類別

根據 OrcaSlicer v2.3.1 的 changelog，以下是值得關注的：

**高優先級**:
- 切片引擎的 bug 修復
- G-code 生成的修復
- 記憶體洩漏修復
- 崩潰修復

**中優先級**:
- UI 改進（需要檢查是否與 Phrozen 品牌衝突）
- 效能優化
- 新的切片選項

**低優先級**:
- 新機台 profile（除非 Phrozen 也需要）
- 文件更新
- 翻譯更新

---

### 策略 B：建立新的 Fork 並重新套用客製化

如果 PhrozenOrca 的客製化相對集中且有清楚的文件，可以考慮這個方法。

**原理**:
- 從 OrcaSlicer v2.3.1 建立新的分支
- 重新套用 PhrozenOrca 的客製化
- 這樣可以獲得最新的 OrcaSlicer 功能

**步驟**:

#### 1. 分析 PhrozenOrca 的客製化

```bash
# 列出所有 PhrozenOrca 特有的修改
git log c8a27715a..phrozen-custom-dev --oneline > phrozen_customizations.txt
```

#### 2. 分類客製化

將 264 個 PhrozenOrca commits 分類：
- 品牌重命名
- Phrozen Arco 機台支援
- UI 客製化（顏色、佈局）
- 功能隱藏/移除
- Bug 修復

#### 3. 建立新分支並重新套用

```bash
# 從 OrcaSlicer v2.3.1 建立新分支
git checkout -b phrozen-v2.3.1-base v2.3.1

# 逐一套用 PhrozenOrca 的客製化
# 可以使用 cherry-pick 或手動修改
```

**優點**:
- 獲得完整的 OrcaSlicer v2.3.1 功能
- 避免複雜的衝突解決

**缺點**:
- 工作量大
- 需要重新測試所有客製化
- 可能遺漏某些客製化

---

### 策略 C：使用 Patch 檔案

這是一個中間方案。

**步驟**:

#### 1. 生成 PhrozenOrca 的客製化 patch

```bash
# 生成從共同起點到當前的 patch
git diff c8a27715a..phrozen-custom-dev > phrozen_customizations.patch
```

#### 2. 切換到 OrcaSlicer v2.3.1

```bash
git checkout -b phrozen-v2.3.1-patched v2.3.1
```

#### 3. 套用 patch

```bash
git apply --reject phrozen_customizations.patch
```

#### 4. 手動解決被拒絕的部分

Git 會生成 `.rej` 檔案，包含無法自動套用的部分。

---

## 📊 策略比較

| 策略 | 工作量 | 風險 | 完整性 | 推薦度 |
|------|--------|------|--------|--------|
| A: 選擇性 Cherry-pick | 中 | 低 | 中（只包含選定的修復） | ⭐⭐⭐⭐⭐ |
| B: 重新套用客製化 | 高 | 中 | 高（完整的 v2.3.1） | ⭐⭐⭐ |
| C: Patch 檔案 | 高 | 高 | 高 | ⭐⭐ |
| 原方案: 直接 Merge | 極高 | 極高 | 高 | ⭐ (不推薦) |

---

## 🎯 建議的執行計畫（策略 A）

### 階段一：準備與分析（1 天）

1. **提取關鍵 commits 列表**
   ```bash
   # 已完成 - 在 c:\Dev\OrcaSlicer
   git log --oneline c8a27715a..v2.3.1 --no-merges > all_commits.txt
   ```

2. **分類 commits**
   - 使用關鍵字過濾
   - 手動審查每個 commit 的內容
   - 建立優先級列表

3. **識別必須合併的修復**
   - 安全性問題
   - 崩潰修復
   - 資料損壞修復

### 階段二：執行選擇性合併（3-5 天）

1. **第一批：安全性和崩潰修復**
   - 逐一 cherry-pick
   - 仔細測試每個修復

2. **第二批：核心功能改進**
   - 切片引擎改進
   - G-code 生成改進
   - 效能優化

3. **第三批：UI 和其他改進**
   - 需要特別注意品牌化
   - 可能需要調整以符合 Phrozen 風格

### 階段三：測試（2-3 天）

1. **編譯測試**
2. **功能測試**
3. **回歸測試**

### 階段四：文件與發布（1 天）

1. **記錄合併的 commits**
2. **更新版本號**（建議：v1.2.0，表示整合了 OrcaSlicer v2.3.1 的部分功能）
3. **撰寫 release notes**

---

## 🔍 需要特別關注的 OrcaSlicer v2.3.1 修復

根據快速審查，以下是一些值得關注的修復：

### 高優先級

1. **崩潰修復**
   ```
   737948be1f [Profiles] Fix bed_exclude_area excluding the whole bed on Anycubic Kobra 3
   f27381533c Fix a crash issue when importing a 3MF file saved from version 2.3.1-alpha
   ```

2. **G-code 處理改進**
   ```
   b483dff617 Enhance GCode handling for Z-axis movements
   ```

3. **UI 修復**
   ```
   d55f016568 Fix grid lines origin for multiple plates
   cd9081b16d [QOL] Remember slider position for single layer mode in preview
   ```

### 中優先級

1. **效能改進**
   ```
   15037283e7 Disable smooth sprial in input_shaping calibrations
   ```

2. **Profile 修復**
   ```
   504b89af03 [PROFILE] fix for Ender 3 V3 KE
   e5243be866 fix a regression bug that wrong printer model for Prusa MK3S and MINI
   ```

---

## 💡 建議的下一步

### 選項 1：執行策略 A（選擇性 Cherry-pick）

我可以協助您：
1. 生成分類後的 commits 列表
2. 識別最關鍵的修復
3. 逐步執行 cherry-pick
4. 解決衝突

**預估時間**: 5-7 天
**風險**: 低
**結果**: PhrozenOrca v1.2.0，包含 OrcaSlicer v2.3.1 的關鍵修復

### 選項 2：執行策略 B（重新套用客製化）

我可以協助您：
1. 分析 PhrozenOrca 的所有客製化
2. 建立客製化清單
3. 從 v2.3.1 重新開始
4. 逐步套用客製化

**預估時間**: 10-14 天
**風險**: 中
**結果**: PhrozenOrca v2.0.0，基於 OrcaSlicer v2.3.1

### 選項 3：維持現狀，只合併特定修復

如果 PhrozenOrca 目前運作良好，可以考慮：
1. 只合併已知的 bug 修復
2. 不追求完整的版本同步
3. 建立長期的選擇性合併流程

**預估時間**: 2-3 天
**風險**: 最低
**結果**: PhrozenOrca v1.1.1，包含特定修復

---

## ❓ 需要您的決定

請告訴我：

1. **您偏好哪個策略？**
   - A: 選擇性 Cherry-pick
   - B: 重新套用客製化
   - C: 只合併特定修復

2. **您最關心的是什麼？**
   - 獲得最新功能
   - 修復已知 bug
   - 保持穩定性
   - 減少工作量

3. **時程限制？**
   - 有截止日期嗎？
   - 可以投入多少時間？

4. **測試資源？**
   - 有完整的測試環境嗎？
   - 可以進行充分的測試嗎？

根據您的回答，我會調整計畫並開始執行。
