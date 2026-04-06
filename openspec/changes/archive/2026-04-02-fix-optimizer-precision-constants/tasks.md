## 1. 修改常數

- [x] 1.1 在 `src/libslic3r/SLA/SupportTree.hpp` 中，將 `optimizer_rel_score_diff` 從 `1e-6` 改為 `1e-10`
- [x] 1.2 在 `src/libslic3r/SLA/SupportTree.hpp` 中，將 `optimizer_max_iterations` 從 `1000` 改為 `2000`

## 2. 驗證

- [x] 2.1 編譯確認零錯誤（兩個常數皆為 constexpr，型別不變）
- [x] 2.2 開啟含有 SLA 支撐的模型，執行自動生成，確認程式不崩潰
- [x] 2.3 確認 FDM 切片不受影響（`SupportTreeConfig` 僅用於 SLA）
