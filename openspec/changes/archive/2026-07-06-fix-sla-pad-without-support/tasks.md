## 1. 加入 kPadRequiresSupport 開關

- [x] 1.1 在 `src/libslic3r/SLAPrint.hpp`（`SliceOrigin` enum 之後）加入 `inline constexpr bool kPadRequiresSupport = true;`，作為三個修改點的單一控制開關

## 2. 修改 generate_pad()

- [x] 2.1 在 `src/libslic3r/SLAPrintSteps.cpp` `generate_pad()` 的 `pad_enable` 區塊最前端，加入 `if (kPadRequiresSupport && !generate_support)` guard：呼叫 `remove_pad()`、`throw_if_canceled()`、`report_status(..., RELOAD_SCENE)` 後 return
- [x] 2.2 確認 `pad_blueprint` 的觸發條件維持上游原始值 `!generate_support || embed_object`（guard 為 true 時此條件對 `kPadRequiresSupport=true` 等價於只剩 `embed_object`，但對 `kPadRequiresSupport=false` 還原正確行為）

## 3. 修改 get_elevation()

- [x] 3.1 在 `src/libslic3r/SLAPrint.cpp` `get_elevation()` 加入 `if (kPadRequiresSupport && !generate_support) return 0.`，避免 pad 高度被計入 elevation 而造成 slice index 包含無效預留層

## 4. 修改 slice_supports() guard

- [x] 4.1 在 `src/libslic3r/SLAPrintSteps.cpp` `slice_supports()` 將 guard 改為：`need_support_slices = generate_support || (!kPadRequiresSupport && pad_enable)`，確保 `kPadRequiresSupport=true` 時無支撐即跳過，`kPadRequiresSupport=false` 時還原上游 `generate_support || pad_enable` 邏輯

## 5. 手動驗證

- [x] 5.1 `kPadRequiresSupport=true`，`generate_support=false`、`pad_enable=true` → 切片正常，物件底部無 pad，無「unprintable objects」警告
- [x] 5.2 `kPadRequiresSupport=true`，`generate_support=true`、`pad_enable=true` → pad 正常出現於支撐樹底部
- [x] 5.3 `kPadRequiresSupport=false`，`generate_support=false`、`pad_enable=true` → 上游 builtin pad 行為正確還原，切片無錯誤
- [x] 5.4 `kPadRequiresSupport=false`，`generate_support=true`、`pad_enable=true` → pad 正常出現，行為與上游一致
