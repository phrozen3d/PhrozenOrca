# Phase 2 機種 LCD 尺寸對照表

| 機種名稱 | LCD 對角 | 物理寬度 (mm) | 物理高度 (mm) | 像素 X | 像素 Y | 來源 |
|----------|----------|--------------|--------------|--------|--------|------|
| Phrozen Sonic Mega 8K S  | 15.1" | 330.24 | 185.76 | 7680 | 4320 | Phrozen 官方規格 / Chitubox PRZ header |
| Phrozen Sonic Mega 8K V2 | 15.1" | 330.24 | 185.76 | 7680 | 4320 | Phrozen 官方規格 / Chitubox PRZ header |
| Phrozen Sonic Mighty Revo 16K | 9.1" | 211.68 | 118.37 | 15120 | 6230 | Phrozen 官方規格 / Chitubox PRZ header |

## 備註

- Mega 8K S / V2：兩款機種共用相同 LCD 模組（7680×4320, 15.1"），物理尺寸相同
- Revo 16K：採 9.1" 橫向 16K LCD，像素 15120×6230
- 數值來源：Chitubox 0.0.15 切出的 PRZ header（xLength / yLength 欄位）+ Phrozen 官方規格頁
- `display_pixels_x/y` 為 profile orientation=portrait 下的值（寬軸為 X）

## pixel pitch 驗算

| 機種 | X pitch (mm/pixel) | Y pitch (mm/pixel) |
|------|--------------------|-------------------|
| Mega 8K S / V2 | 330.24 / 7680 ≈ **0.04300** | 185.76 / 4320 ≈ **0.04300** |
| Revo 16K | 211.68 / 15120 ≈ **0.01400** | 118.37 / 6230 ≈ **0.01900** |
