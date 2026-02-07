# 將 OrcaSlicer v2.3.1 同步到 PhrozenOrca - 任務清單

## 階段一：準備工作
- [x] 確認當前分支狀態
- [x] 建立備份分支
- [x] 建立工作分支
- [x] 添加 OrcaSlicer remote
- [x] 抓取 OrcaSlicer 資料

## 階段二：分析與策略調整
- [x] 嘗試 git merge
- [x] 分析衝突規模（5,471 個檔案）
- [x] 中止不可行的 merge
- [x] 重新評估策略
- [x] 更新實施計畫
- [ ] 等待使用者選擇策略

## 後續階段（待策略確認）
### 策略 A：選擇性 Cherry-pick
- [ ] 提取並分類 commits
- [ ] 識別關鍵修復
- [ ] 執行 cherry-pick
- [ ] 解決衝突
- [ ] 測試驗證

### 策略 B：重新套用客製化
- [ ] 分析 PhrozenOrca 客製化
- [ ] 從 v2.3.1 建立新分支
- [ ] 重新套用客製化
- [ ] 測試驗證

### 策略 C：只合併特定修復
- [ ] 識別必要的 bug 修復
- [ ] Cherry-pick 特定修復
- [ ] 測試驗證

