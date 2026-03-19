#include "Island_Detector.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
//#include <clipper2/clipper.h>
//#include "clipper.hpp"
#include <libslic3r/ClipperUtils.hpp>
#include <clipper2/clipper.h>

namespace Slic3r {
namespace island {

/// Binarize image: threshold at 127 → 0 or 255.
static cv::Mat binarize(const cv::Mat& img) {
  cv::Mat bin;
  if (img.channels() > 1) {
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, bin, 127, 255, cv::THRESH_BINARY);
  }
  else {
    cv::threshold(img, bin, 127, 255, cv::THRESH_BINARY);
  }
  return bin;
}

/// Convert pixel contour to world-space coordinates.
/// Matches the rotated texture plane mapping (flipY=false, rotation.z=π/2):
///   sceneX = display_h/2 - (py / imgH) * display_h + centerX
///   sceneY = (px / imgW) * display_w - display_w/2 + centerY
static std::vector<Point2f> contour_to_world(
    const std::vector<cv::Point>& contour_px,
    int img_w, int img_h,
    float display_w, float display_h,
    float center_x, float center_y) {

  std::vector<Point2f> world;
  world.reserve(contour_px.size());

  for (const auto& pt : contour_px) {
    float px = static_cast<float>(pt.x);
    float py = static_cast<float>(pt.y);
    float scene_x = display_h / 2.0f - (py / img_h) * display_h + center_x;
    float scene_y = (px / img_w) * display_w - display_w / 2.0f + center_y;
    world.push_back({scene_x, scene_y});
  }

  return world;
}

/// Offset a world-space contour outward by `offset_mm` using Clipper2.
static std::vector<Point2f> offset_contour(const std::vector<Point2f>& pts, double offset_mm) {
  if (offset_mm <= 0.0 || pts.size() < 3) return pts;

  //ClipperLib::Path path;
  //path.reserve(pts.size());
  //for (const auto& p : pts) {
  //  path.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));
  //}
  //
  //ClipperLib::Paths solution = ClipperLib::InflatePaths(
  //    {path}, offset_mm, ClipperLib::JoinType::Round, ClipperLib::EndType::Polygon);
  Clipper2Lib::PathD path;
  path.reserve(pts.size());
  for (const auto& p : pts) {
    path.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));
  }

  Clipper2Lib::PathsD solution = Clipper2Lib::InflatePaths(
      {path}, offset_mm, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon);


  if (solution.empty()) return pts;

  // Pick the largest result path
  const auto& best = *std::max_element(solution.begin(), solution.end(),
      [](const auto& a, const auto& b) { return a.size() < b.size(); });

  std::vector<Point2f> result;
  result.reserve(best.size());
  for (const auto& p : best) {
    result.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
  }
  return result;
}

std::vector<Island> detect_islands(
    const std::vector<cv::Mat>& layer_images,
    const DetectionConfig& config) {

  std::vector<Island> all_islands;

  if (layer_images.size() < 2) {
    return all_islands;
  }

  const float base_z = config.model_bbox.min_z;
  const float center_x = (config.model_bbox.min_x + config.model_bbox.max_x) / 2.0f;
  const float center_y = (config.model_bbox.min_y + config.model_bbox.max_y) / 2.0f;

  // Process consecutive pairs: compare layer i (current) against layer i-1 (below)
  for (size_t i = 1; i < layer_images.size(); ++i) {
    cv::Mat img_below = binarize(layer_images[i - 1]);
    cv::Mat img_current = binarize(layer_images[i]);

    const int img_w = img_current.cols;
    const int img_h = img_current.rows;
    const float z = base_z + static_cast<float>(i) * config.layer_height;

    // Connected components on current layer
    cv::Mat labels, stats, centroids;
    int num_labels = cv::connectedComponentsWithStats(
        img_current, labels, stats, centroids, 8, CV_32S);

    // Check each component (skip label 0 = background)
    for (int lbl = 1; lbl < num_labels; ++lbl) {
      int left   = stats.at<int>(lbl, cv::CC_STAT_LEFT);
      int top    = stats.at<int>(lbl, cv::CC_STAT_TOP);
      int width  = stats.at<int>(lbl, cv::CC_STAT_WIDTH);
      int height = stats.at<int>(lbl, cv::CC_STAT_HEIGHT);

      // Check overlap with layer below within bounding box
      bool has_overlap = false;
      for (int y = top; y < top + height && !has_overlap; ++y) {
        for (int x = left; x < left + width && !has_overlap; ++x) {
          if (labels.at<int>(y, x) == lbl && img_below.at<uint8_t>(y, x) > 0) {
            has_overlap = true;
          }
        }
      }

      if (has_overlap) continue;

      // --- Island found: extract contour ---

      // Build binary mask for this component
      cv::Mat mask = cv::Mat::zeros(img_h, img_w, CV_8UC1);
      for (int y = top; y < top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
          if (labels.at<int>(y, x) == lbl) {
            mask.at<uint8_t>(y, x) = 255;
          }
        }
      }

      // Find contours
      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

      if (contours.empty()) continue;

      // Pick the largest contour by point count
      size_t best_idx = 0;
      for (size_t ci = 1; ci < contours.size(); ++ci) {
        if (contours[ci].size() > contours[best_idx].size()) {
          best_idx = ci;
        }
      }

      // Use contour if >= 3 points, otherwise fallback to bounding box rectangle
      std::vector<cv::Point> contour_px;
      if (contours[best_idx].size() >= 3) {
        contour_px = contours[best_idx];
      }
      else {
        contour_px = {
          {left, top},
          {left + width, top},
          {left + width, top + height},
          {left, top + height},
        };
      }

      // Convert pixel contour → world-space (mm)
      std::vector<Point2f> contour_pts = contour_to_world(
          contour_px, img_w, img_h,
          config.display_width, config.display_height,
          center_x, center_y);

      // Apply Clipper2 offset in mm space
      if (config.offset_mm > 0.0f) {
        contour_pts = offset_contour(contour_pts, static_cast<double>(config.offset_mm));
      }

      Island island;
      island.label = -1;  // assigned below after sorting
      island.contour = std::move(contour_pts);
      island.z = z;

      all_islands.push_back(std::move(island));
    }
  }

  // Sort by z ascending, then assign global labels 0..N-1
  std::sort(all_islands.begin(), all_islands.end(),
            [](const Island& a, const Island& b) { return a.z < b.z; });

  for (int i = 0; i < static_cast<int>(all_islands.size()); ++i) {
    all_islands[i].label = i;
  }

  return all_islands;
}

}  // namespace island
}  //namespace Slic3r
