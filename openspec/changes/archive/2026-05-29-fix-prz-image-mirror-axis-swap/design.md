## Context

`.prz` 逐層曝光影像由以下兩段變換產生（見 proposal.md 的根因分析）：

1. **光柵化（共用 Trafo）**：`expolygons_to_cvmat(..., rp.trafo, ...)` 依 `RasterBase::Trafo` 套用 `flipXY`（portrait 轉置）、`mirror_x`（`x=W-x`）、`mirror_y`（`y=H-y`），產出**直向（portrait）** `cv::Mat`（rows=`display_pixels_x`、cols=`display_pixels_y`）。
2. **後置旋轉**：`cv::rotate(ROTATE_90_CLOCKWISE)` 把直向 Mat 轉成**橫向（landscape）**緩衝（rows=`display_pixels_y`、cols=`display_pixels_x`），使每列像素數 = `display_pixels_x`，對齊 PRZ header 的 `XResolution`。

**關鍵架構事實（本次設計的核心約束來源）——旋轉出現在兩個地方：**

| 位置 | 角色 | 是否主路徑 |
|---|---|---|
| [SLAPrintSteps.cpp:1560](src/libslic3r/SLAPrintSteps.cpp#L1560) | 切片時平行光柵化 → R₉₀cw → RLE 編碼 **寫入 raster cache** | **是（主路徑）** |
| [PhrozenPRZ.cpp:809](src/libslic3r/Format/PhrozenPRZ.cpp#L809) | 匯出時 **cache-miss 備援**，須產生與上者**位元組一致**的串流 | 否（fallback） |

匯出 `.prz` 時，cache-hit 路徑（[PhrozenPRZ.cpp](src/libslic3r/Format/PhrozenPRZ.cpp) line ~723）**直接串流 cache 內已旋轉、已 RLE 編碼的位元組**，完全不經過 PhrozenPRZ:809。因此 cache 的內容由 SLAPrintSteps:1560 決定。

**約束彙整：**
- C1：最終緩衝維持橫向 `display_pixels_x × display_pixels_y`（Mega 7680×4320）——旋轉的「轉置維度」成分不可移除。
- C2：不更動全域 `RasterBase::Trafo` 的鏡像語意（SL1 / AnycubicSLA / 預覽等共用）。
- C3：cache-hit 與 cache-miss 兩路徑必須維持位元組一致 → **修正必須同時、等效地施加於兩個旋轉點**，不能只改 PhrozenPRZ.cpp。
- C4：不更動任何 profile 參數與 header 位元組推導。

## Goals / Non-Goals

**Goals:**
- 讓 `.prz` 逐層影像方向逐機台對齊 Chitubox：Mega 8K S/V2 = 正立；Revo 16K = X 鏡像（左右反、字體不倒立）。
- 修正限定在「後置旋轉之後的像素層級」，不改動共用 Trafo 的鏡像邏輯（C2）。
- 兩旋轉點以**共用機制**施加同一修正，確保 cache-hit ≡ cache-miss（C3）。

**Non-Goals:**
- 不改變輸出影像維度／解析度（C1）。
- 不改 `display_mirror_*`、`display_orientation`、header Xmirror/Ymirror 位元組（C4）。
- 不處理「真正的 Y 鏡像」（`display_mirror_y=1`）機型——目前三台 `display_mirror_y` 皆為 0，本次以此前提設計，超出者列為 Open Question。
- 不改其他輸出格式或 UI。

## Decisions

### Decision 1 — 採「後置補償性翻轉」而非「先轉再鏡像」

以 D4（二面體群）分析最終影像相對「正立物件」的狀態。三台 `display_mirror_y=0`，故 trafo `mirror_y=!0=true` 為**固定的 raster 原點（左上）約定**，非使用者鏡像；唯一隨機型變動的是 trafo `mirror_x`：

| 機型 | trafo mirror_x | 現狀最終影像 | 目標最終影像 | 需補償 C = target∘current⁻¹ |
|---|---|---|---|---|
| Mega 8K S/V2 | false (`!1`) | V（垂直翻轉） | I（正立） | **V** |
| Revo 16K | true (`!0`) | I（正立） | X（水平鏡像） | **X (= H)** |

補償量 C 隨 `mirror_x` 不同（V vs H），化簡為單一條件式：

```
C(mirror_x) =  flip 垂直 (cv::flip code 0)   if trafo.mirror_x == false   (無 X 鏡像機型，如 Mega)
               flip 水平 (cv::flip code 1)   if trafo.mirror_x == true    (有 X 鏡像機型，如 Revo)
```

驗證：Mega 現狀 V，補垂直翻轉 → V∘V = I ✓；Revo 現狀 I，補水平翻轉 → H = X ✓。

**做法：在兩個旋轉點的 `cv::rotate(...ROTATE_90_CLOCKWISE)` 之後，依該機型「最終 X 鏡像狀態」施加一次 `cv::flip`**（垂直或水平）。`cv::flip` 不改變維度（C1 滿足），CV_8UC1 row-major 佈局不變（RLE 掃描不受影響）。

**Choice over alternative（先轉再鏡像）：** 「先轉再鏡像」需把使用者鏡像從 Trafo 抽離、改於旋轉後施加；但 (a) 直向基底（轉置+原點翻轉）旋轉後本身即為 V，仍須額外固定垂直翻轉才能正立，最終一樣是「兩個 flip」；(b) 需傳入「鏡像中性化」的 Trafo 副本給光柵化器，動到光柵化輸入，較接近觸及共用語意（違反 C2 精神）且改動面更大。補償性翻轉改動最小、最貼近「局部修正後置旋轉偏移」的目標，故採用。

### Decision 2 — 翻轉條件以「與 header 同一來源」的最終 X 鏡像值為準

翻轉的條件值應與 header Xmirror 位元組同源，避免雙重事實來源。header 端由 `display_mirror_mode` 推導（`lcd_mirror→1`、`normal/dlp_normal→0`，fallback `display_mirror_x`）。對現有三台，該值與 trafo `mirror_x`（portrait 取反後）一致：

| 機型 | mirror_mode | header X byte | trafo mirror_x | 一致 |
|---|---|---|---|---|
| Mega | normal | 0 | false | ✓ |
| Revo | lcd_mirror | 1 | true | ✓ |

實作時以單一 helper 計算「最終 X 鏡像 bool」並同時供 header 與翻轉條件使用，保證兩者不漂移。

### Decision 3 — 以共用機制施加於兩個旋轉點（滿足 C3）

於 [SLAPrintSteps.cpp:1560](src/libslic3r/SLAPrintSteps.cpp#L1560)（主路徑，寫 cache）與 [PhrozenPRZ.cpp:809](src/libslic3r/Format/PhrozenPRZ.cpp#L809)（cache-miss 備援）旋轉後，呼叫同一段補償翻轉邏輯（建議抽成共用 inline helper，例如 `prz_orient_after_rotate(mat, final_x_mirror)`），確保兩路徑產生位元組一致的影像。**僅改 PhrozenPRZ.cpp 不足以修好主路徑（cache-hit）**，這是本設計相對使用者初始假設的重要修正。

## Risks / Trade-offs

- **[Risk] 既有 raster cache 內容已是「錯誤方向」的編碼位元組** → 修改 SLAPrintSteps:1560 後，舊 cache 仍會餵出舊方向。Mitigation：須使 cache 失效／重新切片（確認 cache key 是否涵蓋此邏輯版本；若否，於 tasks 納入「清快取或升版 cache key」）。此為下一步 specs/tasks 待確認項。
- **[Risk] 「單一條件翻轉」看似魔法、不自證** → Mitigation：在程式碼註解引用本 design 的 D4 推導表，並由驗收測試（同 STL 對齊 Chitubox）守住。
- **[Risk] 前提「display_mirror_y=0」** → 若未來機型有真正 Y 鏡像，本條件式需擴充為同時考慮最終 Y 鏡像。Mitigation：helper 以「最終 X/Y 鏡像」雙參數設計，Y 目前恆 false，預留擴充。
- **[Trade-off] 補償翻轉是「修正症狀的座標代數」而非「重構為正確的鏡像-旋轉次序」** → 接受：在 C2（不動全域 Trafo）與最小改動前提下，這是最務實解；若日後要根治可另開重構變更。

## Open Questions

- raster cache 的失效策略：cache key 是否需升版以丟棄舊方向位元組？（影響 tasks）
- 是否需要對 SL1 / 預覽等其他消費 Trafo 的路徑做回歸確認，確保本次「僅後置翻轉」未波及它們（預期不波及，因翻轉只在 PRZ 旋轉點）。
- 驗收基準檔：取得 Chitubox 對三台機型同 STL 的 `.prz`，逐層影像作為 pixel-diff 黃金標準。