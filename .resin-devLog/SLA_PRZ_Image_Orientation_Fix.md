# SLA PRZ 切片影像方向修正（旋轉後補償翻轉）

**建立日期**：2026-05-29
**範圍**：修正 web_slicer_core（DS-online）產出之 `.prz` 逐層曝光影像方向，使其逐機台與 Chitubox 一致
**對應 OpenSpec 變更**：`fix-prz-image-mirror-axis-swap`

---

## 一、問題現象

以 Chitubox 對「同一台機型」輸出的 `.prz` 為事實標竿，比對發現方向不一致：

| 機型 | Chitubox（目標） | 修正前 web_slicer_core | 差異 |
|---|---|---|---|
| Mega 8K S / 8K V2（`normal`） | 影像正立（與物件一致） | **純垂直鏡像**（字體上下顛倒、字序不變） | 多了一個垂直翻轉 |
| Mighty Revo 16K（`lcd_mirror`） | 影像 X 鏡像（左右反、字體不倒） | 完全正立 | 缺了應有的 X 鏡像 |

兩台的誤差落在**不同軸**（Mega 在 Y、Revo 在 X），但兩台設定上唯一差異只有 `display_mirror_x`——這是破案關鍵。

---

## 二、根因：ROTATE_90 把鏡像軸投影到錯誤的軸

每張切片影像經過兩段變換：

1. **光柵化（共用 `RasterBase::Trafo`）**：portrait ⇒ `flipXY`（轉置）→ `mirror_x`（`x=W-x`）→ `mirror_y`（`y=H-y`），產出**直向** `cv::Mat`。
2. **後置旋轉**：`cv::rotate(ROTATE_90_CLOCKWISE)` 轉成**橫向**緩衝，使每列像素數 = `display_pixels_x`，對齊 PRZ header 的 `XResolution`。

關鍵幾何事實：**90° 旋轉會把鏡像的兩個軸整體轉 90°**。因鏡像在旋轉「之前」套用，造成軸向投影錯位：

```
            應控制的最終軸          旋轉後實際控制的軸
config       ──────────         ────────────────────
mirror_x  →  左右 / 字序     →   ❌ 變成控制 上下 / 字體倒立
mirror_y  →  上下 / 字體     →   ❌ 變成控制 左右 / 字序（三台同值，看不出差異）
```

三台機型的 `display_mirror_y` 皆為 0，故 trafo `mirror_y=!0=true` 純粹是「raster 原點移到左上」的約定，非使用者鏡像；唯一隨機型變動的是 `mirror_x`。

### 座標推導（以「蔣劲君」字序＋字體上下為不對稱探針）

世界座標 X 向右、Y 向上。portrait 映射 `px=Y·sy+cx`、`py=X·sx+cy`。ROTATE_90_CW 方向定律：上→右、右→下、下→左、左→上。

- **Mega**（trafo `mirror_x=!1=false`）：字體「上」指向 +px（右）→ 旋轉後「右→下」⇒ 字體上下顛倒；字序仍左→右。= **純垂直鏡像**（重現現象 ✓）。
- **Revo**（trafo `mirror_x=!0=true`）：字體「上」因 mirror_x 改指 -px（左）→ 旋轉後「左→上」⇒ 字體正立；字序左→右。= **完全正立**（重現現象 ✓）。

兩個獨立觀察都被精確重現。

---

## 三、修正：旋轉後補償翻轉（單一條件 flip）

以 D4（二面體群）計算每台「目標 = 補償量 ∘ 現狀」：

| 機型 | trafo mirror_x | 現狀 | 目標 | 需補償 C |
|---|---|---|---|---|
| Mega | false | V（垂直翻轉） | I（正立） | **V（垂直翻轉）** |
| Revo | true | I（正立） | X（水平鏡像） | **H（水平翻轉）** |

化簡為單一條件式（在 `ROTATE_90_CLOCKWISE` **之後**施加一次）：

```
final_x_mirror == false → cv::flip(mat, mat, 0)  // 垂直翻轉，e.g. Mega(normal)
final_x_mirror == true  → cv::flip(mat, mat, 1)  // 水平翻轉，e.g. Revo(lcd_mirror)
```

驗證：Mega 現狀 V，補垂直 → V∘V = I ✓；Revo 現狀 I，補水平 → H = X ✓。
`cv::flip` 為元素重排，**不改變維度**（橫向 7680×4320 維持）與 `CV_8UC1` row-major 佈局，下游 RLE 掃描不受影響。

### 為何不選「先轉再鏡像」

「先轉再鏡像」需把使用者鏡像從共用 `Trafo` 抽離（影響 SL1／預覽等）、且直向基底旋轉後本身仍是 V，終究要兩個 flip。補償翻轉改動最小、不動全域 Trafo 語意，故採用。

---

## 四、實作落點

### 共用 helper（單一事實來源）

**新檔**：[PhrozenPRZOrient.hpp](../src/libslic3r/Format/PhrozenPRZOrient.hpp)

> 刻意獨立成輕量標頭，**只由兩個旋轉點所在的 .cpp include**，避免把 OpenCV 拉進被 GUI（Plater.cpp / ExportPRZJob.cpp）引用的 `PhrozenPRZ.hpp`。

```cpp
// 最終 X 鏡像狀態（與 PRZ header Xmirror byte 同源）
inline bool prz_final_x_mirror(const SLAPrinterConfig &pcfg) {
    switch (pcfg.display_mirror_mode.value) {
    case slammLCDMirror: return true;
    case slammNormal:    return false;
    case slammDLPNormal: return false;
    default:             return pcfg.display_mirror_x.getBool();
    }
}

// 在 ROTATE_90_CLOCKWISE 之後呼叫
inline void prz_orient_after_rotate(cv::Mat &mat, bool final_x_mirror) {
    cv::flip(mat, mat, final_x_mirror ? 1 : 0);
}
```

### 兩個旋轉點（必須同時修正，確保 cache-hit ≡ cache-miss）

| 位置 | 角色 |
|---|---|
| [SLAPrintSteps.cpp](../src/libslic3r/SLAPrintSteps.cpp)（rasterize step） | 主路徑：旋轉後 RLE 編碼**寫入 raster cache** |
| [PhrozenPRZ.cpp](../src/libslic3r/Format/PhrozenPRZ.cpp)（`generate_prz` cache-miss 分支） | cache-miss 備援，須與主路徑位元組一致 |

兩處皆於迴圈前算一次 `prz_final_x_mirror(...)`，旋轉後呼叫**同一個** `prz_orient_after_rotate(...)`。匯出時 cache-hit 直接串流 cache 預編碼位元組，故主路徑（SLAPrintSteps）才是必修點，僅改 PhrozenPRZ.cpp 不足。

### header 同源重構

[PhrozenPRZ.cpp](../src/libslic3r/Format/PhrozenPRZ.cpp) 的 Xmirror byte 寫入改呼叫 `prz_final_x_mirror(pcfg)`，與翻轉條件共用單一來源，避免雙事實來源漂移。

### 快取失效

[RasterCache.hpp](../src/libslic3r/SLA/RasterCache.hpp) `CACHE_VERSION` **4 → 5**。該常數參與 cache key 的 CRC32 雜湊，提升版本使舊版「錯誤方向」快取一律 cache-miss，更新後首次切片以修正後方向重建。

---

## 五、影響範圍與邊界

- **三台作用中 PRZ 機型**（Mega 8K S / 8K V2 / Mighty Revo 16K）皆 portrait、`display_mirror_y=0`，已實測圖檔正確。
- **不波及**：2D 預覽（[SLASlice2DCanvas.cpp](../src/slic3r/GUI/SLASlice2DCanvas.cpp)）自行 on-demand 光柵化、不讀 cache；未動 `RasterBase::Trafo`/`RasterToCvMat`/`AGGRaster`，SL1 等其他輸出格式不受影響。
- **參數零變動**：profile、`display_mirror_*`、`display_orientation`、解析度全未改；header Xmirror byte 輸出值不變（等價重構）。
- **前提**：補償公式假設 `display_mirror_y=0`。`prz_orient_after_rotate` 已預留以「最終 X 鏡像」為參數；若未來機型具真正 Y 鏡像，需擴充考慮最終 Y 鏡像。