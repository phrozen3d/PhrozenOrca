# Step 3.3: BranchingTreeSLA 移植 — 完成報告

**完成日期**: 2026-02-26
**狀態**: ✅ 完成（編譯 + 執行驗證通過）

---

## 任務目標

將 PrusaSlicer 的有機分枝支撐演算法（BranchingTreeSLA）移植到 PhrozenOrca，
啟用 `SupportTreeType::Branching` 策略。

---

## 新增/修改檔案

| 檔案 | 操作 | 說明 |
|------|------|------|
| `SLA/BranchingTreeSLA.hpp` | 新增 | 直接複製自 PrusaSlicer（24 行） |
| `SLA/BranchingTreeSLA.cpp` | 新增 | 移植自 PrusaSlicer（~280 行，含 PhrozenOrca 適配） |
| `SLA/SupportTreeUtils.hpp` | 新增 | PhrozenOrca 適配版（~430 行，從 PrusaSlicer SupportTreeUtils.hpp 移植） |
| `SLA/SupportTree.cpp` | 修改 | 啟用 Branching case，呼叫 `create_branching_tree()` |
| `SLA/SupportTree.hpp` | 修改 | 新增 `safety_distance(r)` 方法 |
| `SLA/SupportTreeBuilder.hpp` | 修改 | 新增 `DiffBridge(Junction, Junction)` 建構子 |
| `Optimize/NLoptOptimizer.hpp` | 修改 | 新增 `AlgNLoptMLSL_Subplx` alias |
| `CMakeLists.txt` | 修改 | 新增 3 個 source file 項目 |

---

## 主要適配（PhrozenOrca vs PrusaSlicer 差異）

| 差異點 | PrusaSlicer | PhrozenOrca 處理 |
|--------|-------------|-----------------|
| Mesh 類型 | `AABBMesh` | `IndexedMesh`（相同 ray-cast API） |
| NaNd 常數 | `Optimizer.hpp::NaNd` | `std::nan("")` |
| AlgNLoptMLSL_Subplx | 已有 | 新增 `using` alias |
| `add_pillar()` | 4 個參數（有 taper） | 3 個參數（無 taper） |
| `set_loc_criteria()` | 已有 | 移除（不在 PhrozenOrca NLoptAlgComb） |
| `ground_level(sm)` | 含 pad_cfg/zoffset | 簡化為 `emesh.ground_level() - cfg.object_elevation_mm` |
| `get_normal()` | `MeshNormals.hpp` | Inline via `IndexedMesh::normal_by_face_id` |
| `DiffBridge(Junction,Junction)` | 已有 | 新增到 `SupportTreeBuilder.hpp` |
| `safety_distance(r)` | 已有 | 新增到 `SupportTree.hpp` |

---

## SupportTree.cpp 策略選擇

```cpp
case SupportTreeType::Branching:
    create_branching_tree(*builder, sm);
    break;
case SupportTreeType::Organic:
    BOOST_LOG_TRIVIAL(warning)
        << "[SLA] Organic support tree not yet implemented, using Branching.";
    create_branching_tree(*builder, sm);
    break;
case SupportTreeType::Default:
default:
    SupportTreeBuildsteps::execute(*builder, sm);
    break;
```

---

## 驗證結果

- ✅ 編譯通過（無 error，無 warning）
- ✅ 執行正常（SLA 支撐生成功能正常）
- ✅ FDM 不受影響（Default 策略走 SupportTreeBuildsteps 路徑不變）
