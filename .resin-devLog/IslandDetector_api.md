# Island Detector – Input / Output 說明與 C++ 使用範例

本文件說明 `island_detector` C++ library 的 **所有 input 參數**（包含詳細用途說明），以及 **input → output 的實際 C++ 使用範例**。

---

## API

```cpp
std::vector<island::Island>
island::detect_islands(
    const std::vector<cv::Mat>& layer_images,
    const island::DetectionConfig& config
);
```

---

## Input 參數

### layer_images
- 型別：`std::vector<cv::Mat>`
- 單通道灰階 (`CV_8UC1`)
- index 0 為最底層

### DetectionConfig
```cpp
struct DetectionConfig {
    float display_width;
    float display_height;
    float layer_height;
    BBox3D model_bbox;
    float offset_mm = 0.0f;
};
```

- display_width：LCD 實體寬度 (mm)
- display_height：LCD 實體高度 (mm)
- layer_height：層高 (mm)
- model_bbox：模型世界座標 bounding box
- offset_mm：輪廓外擴 (mm)

---

## Output

```cpp
struct Island {
    int label;
    std::vector<Point2f> contour;
    float z;
};
```

- label：全域 island ID
- contour：世界座標輪廓 (mm)
- z：世界座標 Z (mm)

---

## C++ 使用範例

```cpp
#include <island_detector.h>
#include <opencv2/imgcodecs.hpp>
#include <iostream>

int main() {
    std::vector<cv::Mat> layers;
    for (int i = 0; i < 100; ++i) {
        auto img = cv::imread("layers/layer_" + std::to_string(i) + ".png", cv::IMREAD_GRAYSCALE);
        if (img.empty()) break;
        layers.push_back(img);
    }

    island::DetectionConfig config;
    config.display_width = 68.04f;
    config.display_height = 120.96f;
    config.layer_height = 0.05f;
    config.model_bbox = {-10,-15,0, 10,15,30};
    config.offset_mm = 0.5f;

    auto islands = island::detect_islands(layers, config);

    for (const auto& isl : islands) {
        std::cout << "Island #" << isl.label << " Z=" << isl.z << "mm
";
    }
}
```
