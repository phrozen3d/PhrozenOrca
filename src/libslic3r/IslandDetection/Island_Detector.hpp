#pragma once

#include <opencv2/core.hpp>
#include <vector>

namespace Slic3r {
namespace island {

struct Point2f {
  float x;
  float y;
};

struct BBox3D {
  float min_x, min_y, min_z;
  float max_x, max_y, max_z;
};

struct Island {
  int label;                        ///< Global ID: 0..N-1, sorted by z
  std::vector<Point2f> contour;     ///< World-space contour (mm)
  float z;                          ///< World-space Z (mm)
};

struct DetectionConfig {
  float display_width;              ///< Physical display width (mm), e.g. 68.04
  float display_height;             ///< Physical display height (mm), e.g. 120.96
  float layer_height;               ///< Layer height (mm), pre-multiplied by stride
  BBox3D model_bbox;                ///< Model bounding box in world space (after transforms)
  float offset_mm = 0.0f;           ///< Contour offset/expansion in mm (0 = no offset)
};

/// Detect islands across all provided layer images.
///
/// Compares consecutive layer pairs: layer_images[i-1] vs layer_images[i].
/// Layer 0 is assumed to sit on the build plate (no islands).
/// Input cv::Mat should be grayscale (CV_8UC1), binary or near-binary.
///
/// @param layer_images  Pre-filtered layers (index 0 = bottom). Caller handles stride.
/// @param config        Detection parameters
/// @return All detected islands, globally labeled 0..N-1, sorted by z ascending.
std::vector<Island> detect_islands(
    const std::vector<cv::Mat>& layer_images,
    const DetectionConfig& config);

}  // namespace island
}  // namespace Slic3r
