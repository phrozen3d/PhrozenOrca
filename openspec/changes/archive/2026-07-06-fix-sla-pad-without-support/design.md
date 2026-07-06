## Context

SLA 切片管線依序執行：`slaposObjectSlice → slaposSupportTree → slaposPad → slaposSliceSupports → slapsMergeSlicesAndEval`。

**上游 "builtin pad" 行為**（Phrozen profile 下的問題組合：`pad_enable=1`、`generate_support=0`）：

1. `get_elevation()`（`SLAPrint.cpp`）：當 `generate_support=false` 但 `pad_enable=true` 時，仍將 pad 高度加入 elevation，回傳非 0 值。
2. `object_slice()` 以此非 0 elevation 計算 `minZ = mesh_bottom - elevation`，導致 `m_slice_index` 包含模型底部以下的「pad 預留層」，這些層的 `m_po` 尚未被設定（`nullptr`）。
3. `generate_pad()` 採樣物件底部輪廓（`pad_blueprint`）並在物件正下方建立 pad（builtin pad 路徑）。
4. `slice_supports()` 切片 pad 幾何，呼叫 `set_support_slice_idx()` 將 `m_po` 設入預留層，使所有記錄 valid。
5. `initialize_printer_input()` 通過 `is_valid()` 檢查，正常產出切片。

**問題**：Phrozen 的預期行為是：無支撐 → 無 pad。需要在保留上游邏輯完整性的前提下加入可切換的行為差異。

## Goals / Non-Goals

**Goals:**
- 以單一開關（`kPadRequiresSupport`）控制「pad 須以支撐為前提」的 Phrozen 行為，預設 `true`。
- 開關為 `true` 時：`generate_support=false` → 無 pad、無 elevation、無支撐切片，切片結果正確。
- 開關為 `false` 時：完整還原上游行為（builtin pad 路徑可正常運作）。

**Non-Goals:**
- 不修改 `pad_enable` 的 UI 語意與 profile JSON。
- 不修改 zero-elevation（`embed_object`）模式本身的行為。
- 不新增 runtime config 設定項或 UI 控件。

## Decisions

### 決策：以 `inline constexpr bool kPadRequiresSupport` 統一控制三個修改點

**位置**：`src/libslic3r/SLAPrint.hpp`（`SliceOrigin` enum 之後，`namespace sla` 之前），兩個 `.cpp` 均已 include 此 header。

```cpp
// Phrozen product policy: pad is only generated alongside supports.
// Set to false to restore upstream behaviour.
inline constexpr bool kPadRequiresSupport = true;
```

開關為單一真相來源，關閉時三個受控點同步還原。

---

### 修改點一：`generate_pad()`（`SLAPrintSteps.cpp`）

在 `pad_enable` 區塊最前端加入 guard：

```cpp
if (kPadRequiresSupport && !po.m_config.generate_support.getBool()) {
    if (po.m_supportdata && po.m_supportdata->support_tree_ptr)
        po.m_supportdata->support_tree_ptr->remove_pad();
    throw_if_canceled();
    report_status(-1, L("Visualizing supports"), SlicingStatus::RELOAD_SCENE);
    return;
}
```

guard 之後，`pad_blueprint` 的觸發條件維持上游原始條件 `!generate_support || embed_object`：

```cpp
if (!po.m_config.generate_support.getBool() || pcfg.embed_object) {
    sla::pad_blueprint(...);
}
```

**為何不把條件改成只剩 `embed_object`**：當 `kPadRequiresSupport=false` 時，guard 被跳過，`generate_support=false` 的情況需要 `pad_blueprint` 填充 `bp`；若只留 `embed_object`，`bp` 為空，`create_pad(empty_bp)` 生成無效 pad，切片崩潰。維持原始條件確保兩個開關值都正確。

---

### 修改點二：`get_elevation()`（`SLAPrint.cpp`）

```cpp
if (kPadRequiresSupport && !m_config.generate_support.getBool()) return 0.;
```

**必要性**：若 `get_elevation()` 在 `generate_support=false` 時仍回傳 pad 高度，`m_slice_index` 會包含 pad 預留層。`generate_pad()` guard 跳過了 pad 生成，`slice_supports()` 無支撐/pad 幾何可切，預留層的 `m_po` 永遠為 `nullptr`，`initialize_printer_input()` 拋出「unprintable objects」警告且切片中斷。

---

### 修改點三：`slice_supports()` guard（`SLAPrintSteps.cpp`）

```cpp
const bool need_support_slices = po.m_config.generate_support.getBool() ||
                                 (!kPadRequiresSupport && po.m_config.pad_enable.getBool());
if (!need_support_slices)
    return;
```

- `kPadRequiresSupport=true`：`need_support_slices = generate_support`（pad 不再獨立產生切片）。
- `kPadRequiresSupport=false`：`need_support_slices = generate_support || pad_enable`（還原上游行為）。

## Risks / Trade-offs

- **上游 merge 衝突**：三個修改點均與上游 PrusaSlicer/OrcaSlicer 不同，merge 時需注意保留 `kPadRequiresSupport` guard。
- **embed_object 情境**：`kPadRequiresSupport=true` 時，`embed_object=true` + `generate_support=false` 的組合同樣被 guard 攔截，不生成 pad。如未來需要此組合支援，在 guard 加 `&& !pcfg.embed_object` 例外即可。
