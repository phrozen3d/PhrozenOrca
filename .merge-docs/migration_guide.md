# 搬移 Merge 進度到另一台電腦 - 完整指南

## 📋 需要搬移的內容

### 1. Git 分支和 Commits
- `merge-orca-v2.3.1` 分支（包含所有 cherry-picked commits）
- `backup-before-merge` 分支（備份分支）
- OrcaSlicer remote 設定

### 2. 規劃和記錄文件
- `implementation_plan.md` - 實施計畫
- `task.md` - 任務清單
- `cherry_pick_analysis.md` - Commit 分析
- `cherry_pick_log.md` - Cherry-pick 記錄
- `walkthrough.md` - 進度報告
- `migration_guide.md` - 本文件

---

## 🚀 搬移步驟

### 步驟一：在當前電腦推送分支到遠端

```powershell
# 進入 PhrozenOrca 目錄
cd c:\Dev\PhrozenOrca

# 推送工作分支到遠端
git push origin merge-orca-v2.3.1

# 推送備份分支到遠端
git push origin backup-before-merge

# 確認推送成功
git branch -r
```

### 步驟二：備份規劃文件

有兩種方法：

#### 方法 A：複製到 PhrozenOrca 專案中（推薦）

```powershell
# 在 PhrozenOrca 專案中建立文件資料夾
mkdir c:\Dev\PhrozenOrca\.merge-docs

# 複製所有規劃文件
Copy-Item "C:\Users\User\.gemini\antigravity\brain\8bb0583a-fbe4-4f74-a164-5aaed389163f\*.md" `
    -Destination "c:\Dev\PhrozenOrca\.merge-docs\"

# 將文件加入 git（但不推送，加入 .gitignore）
cd c:\Dev\PhrozenOrca
git add .merge-docs/
git commit -m "Add merge documentation"
git push origin merge-orca-v2.3.1
```

#### 方法 B：手動複製（備用方案）

```powershell
# 複製到 USB 或雲端硬碟
$sourceDir = "C:\Users\User\.gemini\antigravity\brain\8bb0583a-fbe4-4f74-a164-5aaed389163f"
$destDir = "D:\PhrozenOrca-Merge-Docs"  # 改成您的目標路徑

# 建立目標資料夾
New-Item -ItemType Directory -Force -Path $destDir

# 複製所有 .md 文件
Copy-Item "$sourceDir\*.md" -Destination $destDir
```

---

## 💻 在新電腦上恢復

### 步驟一：Clone 專案（如果還沒有）

```powershell
# 在新電腦上 clone PhrozenOrca
cd c:\Dev
git clone https://github.com/phrozen3d/PhrozenOrca.git
cd PhrozenOrca
```

### 步驟二：拉取分支

```powershell
# 拉取所有遠端分支
git fetch origin

# 切換到工作分支
git checkout merge-orca-v2.3.1

# 確認分支狀態
git log --oneline -10
```

### 步驟三：設定 OrcaSlicer Remote

```powershell
# 添加 OrcaSlicer remote
git remote add orcaslicer https://github.com/OrcaSlicer/OrcaSlicer.git

# 抓取 OrcaSlicer 資料（如果需要繼續 cherry-pick）
git fetch orcaslicer --tags

# 確認 remote 設定
git remote -v
```

### 步驟四：恢復規劃文件

#### 如果使用方法 A（推薦）

文件已經在專案中，直接可用：

```powershell
# 查看文件
ls c:\Dev\PhrozenOrca\.merge-docs\
```

#### 如果使用方法 B

```powershell
# 從 USB 或雲端硬碟複製回來
$sourceDir = "D:\PhrozenOrca-Merge-Docs"  # 您的來源路徑
$destDir = "C:\Users\User\.gemini\antigravity\brain\8bb0583a-fbe4-4f74-a164-5aaed389163f"

# 建立目標資料夾
New-Item -ItemType Directory -Force -Path $destDir

# 複製文件
Copy-Item "$sourceDir\*.md" -Destination $destDir
```

---

## ✅ 驗證搬移成功

在新電腦上執行以下檢查：

### 1. 檢查 Git 分支

```powershell
cd c:\Dev\PhrozenOrca

# 確認當前分支
git branch

# 應該看到：
# * merge-orca-v2.3.1
#   backup-before-merge
#   phrozen-custom-dev
#   ...

# 檢查最近的 commits
git log --oneline -10

# 應該看到：
# 5f74b54f52 Fix grid lines origin for multiple plates
# 49232549d1 Fix logic for precise_outer_wall condition
# 6d3f070058 Enhance GCode handling for Z-axis movements
# 6b00c65315 Fix crash when opening AMS humidity popup
# 06c06ddcd9 Fix a crash issue when importing 3MF file
```

### 2. 檢查 Remote 設定

```powershell
git remote -v

# 應該看到：
# origin        https://github.com/phrozen3d/PhrozenOrca.git (fetch)
# origin        https://github.com/phrozen3d/PhrozenOrca.git (push)
# orcaslicer    https://github.com/OrcaSlicer/OrcaSlicer.git (fetch)
# orcaslicer    https://github.com/OrcaSlicer/OrcaSlicer.git (push)
```

### 3. 檢查規劃文件

```powershell
# 如果使用方法 A
ls c:\Dev\PhrozenOrca\.merge-docs\

# 如果使用方法 B
ls C:\Users\User\.gemini\antigravity\brain\8bb0583a-fbe4-4f74-a164-5aaed389163f\

# 應該看到：
# implementation_plan.md
# task.md
# cherry_pick_analysis.md
# cherry_pick_log.md
# walkthrough.md
# migration_guide.md
```

---

## 📝 繼續工作

在新電腦上，您可以：

### 1. 查看當前進度

```powershell
# 查看 cherry-pick 記錄
cat c:\Dev\PhrozenOrca\.merge-docs\cherry_pick_log.md

# 查看任務清單
cat c:\Dev\PhrozenOrca\.merge-docs\task.md
```

### 2. 繼續 Cherry-pick

```powershell
# 確保在正確的分支
git checkout merge-orca-v2.3.1

# 繼續 cherry-pick 其他 commits
# 參考 cherry_pick_analysis.md 中的建議
```

### 3. 測試和驗證

```powershell
# 編譯測試
# 根據您的建置流程執行相應命令
```

---

## 🔧 故障排除

### 問題 1：推送分支失敗

```powershell
# 如果遠端沒有這個分支，需要設定 upstream
git push -u origin merge-orca-v2.3.1
```

### 問題 2：找不到 OrcaSlicer remote

```powershell
# 重新添加
git remote add orcaslicer https://github.com/OrcaSlicer/OrcaSlicer.git
git fetch orcaslicer --tags
```

### 問題 3：文件路徑不同

如果新電腦的使用者名稱不同，brain 資料夾路徑會不同：

```powershell
# 找到新電腦的 brain 路徑
$newPath = "C:\Users\<新使用者名稱>\.gemini\antigravity\brain\8bb0583a-fbe4-4f74-a164-5aaed389163f"

# 建立資料夾並複製文件
New-Item -ItemType Directory -Force -Path $newPath
Copy-Item "c:\Dev\PhrozenOrca\.merge-docs\*.md" -Destination $newPath
```

---

## 📌 重要提醒

1. ✅ **推送前確認**: 確保所有重要的 commits 都已經 committed
2. ✅ **備份文件**: 建議同時使用方法 A 和 B 備份文件
3. ✅ **測試拉取**: 在推送後，可以在當前電腦的另一個資料夾測試拉取
4. ✅ **記錄狀態**: 記下當前的 commit hash，方便在新電腦上驗證

---

## 🎯 快速命令摘要

### 當前電腦（推送）

```powershell
cd c:\Dev\PhrozenOrca
git push origin merge-orca-v2.3.1
git push origin backup-before-merge
mkdir .merge-docs
Copy-Item "C:\Users\User\.gemini\antigravity\brain\8bb0583a-fbe4-4f74-a164-5aaed389163f\*.md" -Destination ".merge-docs\"
git add .merge-docs/
git commit -m "Add merge documentation"
git push origin merge-orca-v2.3.1
```

### 新電腦（拉取）

```powershell
cd c:\Dev
git clone https://github.com/phrozen3d/PhrozenOrca.git
cd PhrozenOrca
git fetch origin
git checkout merge-orca-v2.3.1
git remote add orcaslicer https://github.com/OrcaSlicer/OrcaSlicer.git
git fetch orcaslicer --tags
ls .merge-docs\
```

---

完成以上步驟後，您就可以在新電腦上無縫繼續 merge 工作了！
