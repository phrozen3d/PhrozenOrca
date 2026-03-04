# Step 1.4 執行結果: Rasterization 評估

**分析日期**: 2026-01-26
**執行日期**: 2026-02-08
**狀態**: ✅ 完成 (無需程式碼修改)

---

## 1. 決策記錄

### D1: Include 風格同步
**決策**: 不修改
- PrusaSlicer 使用較防禦性的 include 風格 (完整明確)
- PhrozenOrca 使用最小化 include (relies on transitive)
- 兩者功能等價，無需同步

### D2: Vec2i vs Vec2i32
**決策**: 保留 PhrozenOrca 的 `Vec2i32`
- `Vec2i32` 是明確的 32-bit 整數型別，比 `Vec2i` (平台相依) 更嚴格
- 功能上完全等價
- PhrozenOrca 的選擇更為嚴謹

### D3: Output Pipeline (m_archiver vs m_printer)
**決策**: 延後到 Step 1.3 (Archive 格式擴展) 一併處理
- PrusaSlicer 使用 `m_archiver` + `execution::SpinningMutex<ExecutionTBB>`
- PhrozenOrca 使用 `m_printer` + `sla::ccr::SpinningMutex`
- 此差異與 Archive 架構遷移 (`SLAArchive` → `SLAArchiveWriter`) 緊密相關
- 在 Step 1.3 統一處理更合理

### D4: AGG 函式庫版本
**決策**: 不修改
- 兩邊使用相同版本的 AGG (Anti-Grain Geometry) 函式庫
- 所有 AGG header 功能一致

---

## 2. 差異分析結果

### 核心發現

**光柵化演算法 100% 相同，差異僅在基礎設施層面。**

| 檔案 | PrusaSlicer | PhrozenOrca | 演算法差異 |
|------|:-----------:|:-----------:|:----------:|
| RasterBase.hpp | 129 行 | 118 行 | 無 |
| RasterBase.cpp | 94 行 | 86 行 | 無 |
| AGGRaster.hpp | 229 行 | 225 行 | 無 |
| RasterToPolygons.hpp | 20 行 | 15 行 | Vec2i vs Vec2i32 |
| RasterToPolygons.cpp | 102 行 | 91 行 | 無 |
| **合計** | **574 行** | **535 行** | **無功能差異** |

### 不需修改的功能 (兩邊完全相同)

| 功能 | 說明 |
|------|------|
| Marching Squares | 從光柵提取多邊形輪廓 |
| PNG 編碼 | 使用 miniz 的 DEFLATE 壓縮 |
| PPM 編碼 | P5 格式生成 |
| Gamma 校正 | agg::gamma_power 應用 |
| 座標轉換 | mirror_x/y, flipXY, center offset |
| Anti-aliasing | 閾值 128 的灰階抗鋸齒 |
| 像素讀取 | read_pixel() 函式 |

### 差異詳細

| 差異項 | PrusaSlicer | PhrozenOrca | 結論 |
|--------|-------------|-------------|------|
| Include 風格 | 完整明確 (defensive) | 最小化 (transitive) | 功能等價，不修改 |
| Vec2i vs Vec2i32 | `Vec2i` (平台相依) | `Vec2i32` (明確 32-bit) | 保留 PhrozenOrca |
| Output Pipeline 物件 | `m_archiver` | `m_printer` | 延後到 Step 1.3 |
| Mutex 型別 | `execution::SpinningMutex<ExecutionTBB>` | `sla::ccr::SpinningMutex` | 延後到 Step 1.3 |
| Copyright 標頭 | 有 AGPLv3 標頭 | 無 | 不修改 |

---

## 3. 實際修改內容

**無。** 本步驟經評估後結論為不需要任何程式碼修改。

---

## 4. 規則遵守確認

| 保護項目 | 狀態 |
|----------|------|
| Rasterization 現有演算法 | ✅ 未觸及 |
| PhrozenOrca 的 Vec2i32 選擇 | ✅ 保留 |
| m_printer 架構 | ✅ 保留 (延後到 Step 1.3) |
| sla::ccr 命名空間 | ✅ 保留 (延後到 Step 1.3) |

---

## 5. 影響範圍

### 直接影響
無。本步驟不修改任何檔案。

### 延後到 Step 1.3 的項目
| 項目 | 說明 |
|------|------|
| `m_printer` → `m_archiver` 遷移 | 取決於 Archive 架構決策 |
| `sla::ccr::SpinningMutex` → `execution::SpinningMutex` | 取決於 Archive 架構決策 |
| `rasterize()` 函式更新 | 取決於上述兩項決策 |

---

## 6. 變更統計

| 類別 | 數量 |
|------|:----:|
| 新增檔案 | 0 |
| 修改檔案 | 0 |
| 新增程式碼行數 | 0 |

---

## 7. 已完成的執行階段

| Phase | 說明 | 狀態 |
|-------|------|:----:|
| 分析 | 比對 PrusaSlicer 與 PhrozenOrca Rasterization 差異 | ✅ 完成 |
| 決策 | 確認不需修改，延後項歸入 Step 1.3 | ✅ 完成 |

---

## 8. 與其他步驟的關聯

```
Step 1.3 (Archive) ──► 決定 m_archiver vs m_printer 架構
                            │
                            ▼
Step 1.4 (Rasterization) ──► 據此調整 rasterize() 函式 (如需要)
```

如果 Step 1.3 決定遷移到 `SLAArchiveWriter` (m_archiver)，則需回頭更新 `rasterize()` 中的物件引用和 mutex 型別。此更新將在 Step 1.3 的結果中記錄。
