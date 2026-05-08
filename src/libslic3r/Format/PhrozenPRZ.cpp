#include "PhrozenPRZ.hpp"

#include <cmath>
#include <ctime>
#include <functional>
#include <ostream>
#include <limits>
#include <sstream>
#include <iomanip>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/libslic3r.h>
#include <libslic3r/SLA/RasterToCvMat.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/task_arena.h>

namespace Slic3r {

// ---------------------------------------------------------------------------
// Helpers: read values from DynamicPrintConfig by key
// ---------------------------------------------------------------------------
static float cfg_f(const DynamicPrintConfig &cfg, const std::string &key, float def = 0.f)
{
    if (cfg.has(key))
        if (auto *opt = cfg.option(key))
            return static_cast<float>(opt->getFloat());
    return def;
}

static int cfg_i(const DynamicPrintConfig &cfg, const std::string &key, int def = 0)
{
    if (cfg.has(key))
        if (auto *opt = cfg.option(key))
            return opt->getInt();
    return def;
}

static bool cfg_bool(const DynamicPrintConfig &cfg, const std::string &key, bool def = false)
{
    if (cfg.has(key))
        if (auto *opt = cfg.option(key))
            return opt->getBool();
    return def;
}

static double cfg_double(const DynamicPrintConfig &cfg, const std::string &key, double def = 0.)
{
    if (cfg.has(key))
        if (auto *opt = cfg.option(key))
            return opt->getFloat();
    return def;
}

static std::string cfg_s(const DynamicPrintConfig &cfg, const std::string &key)
{
    if (cfg.has(key))
        if (auto *opt = cfg.option(key))
            return opt->serialize();
    return {};
}

// ---------------------------------------------------------------------------
// Helper: write a big-endian value of sizeof(T) bytes into fh / out
// ---------------------------------------------------------------------------
template<typename T>
static void write_be(std::string &fh, T val)
{
    const int N = sizeof(T);
    const char *c = reinterpret_cast<const char *>(&val);
    for (int i = N - 1; i >= 0; --i)
        fh += c[i];
}

template<typename T>
static void write_be(std::ostream &out, T val)
{
    const int N = sizeof(T);
    const char *c = reinterpret_cast<const char *>(&val);
    for (int i = N - 1; i >= 0; --i)
        out.put(c[i]);
}

// ---------------------------------------------------------------------------
// Calculate estimated print time in seconds from PRZ parameters
// ---------------------------------------------------------------------------
int calculate_prz_print_time(int                       total_layers,
                             const DynamicPrintConfig &cfg)
{
    // speed == 0 ??treat that motion segment as 0 s (avoid divide-by-zero)
    auto motion_s = [](float dist_mm, float speed_mm_min) -> float {
        if (speed_mm_min <= 0.f) return 0.f;
        return dist_mm / speed_mm_min * 60.f;
    };


    const int   bottom_count     = cfg_i(cfg, "bottom_layer_count");
    const int   transition_count = cfg_i(cfg, "transition_layer_count");

    const float bt   = cfg_f(cfg, "bottom_exposure_time");
    const float nt   = cfg_f(cfg, "exposure_time");
    const float lod  = cfg_f(cfg, "light_off_day");
    const float rtbl = cfg_f(cfg, "rest_time_before_lift");
    const float rtal = cfg_f(cfg, "rest_time_after_lift");
    const float rtr  = cfg_f(cfg, "rest_time_after_retract");

    // ---- Bottom layer motion parameters ----
    const float b_lh  = cfg_f(cfg, "bottom_lift_distance");
    const float b_lh2 = cfg_f(cfg, "bottom_lift_second_distance");
    const float b_dh2 = cfg_f(cfg, "bottom_retract_second_distance");
    float b_dh1 = b_lh + b_lh2 - b_dh2;
    if (b_dh1 <= 0.f) b_dh1 = b_lh + b_lh2;

    const float b_ls  = cfg_f(cfg, "bottom_lift_speed");
    const float b_ls2 = cfg_f(cfg, "bottom_lift_second_speed");
    const float b_ds  = cfg_f(cfg, "bottom_retract_speed");
    const float b_ds2 = cfg_f(cfg, "bottom_retract_second_speed");

    // ---- Normal/transition layer motion parameters ----
    const float n_lh  = cfg_f(cfg, "lifting_distance");
    const float n_lh2 = cfg_f(cfg, "lift_second_distance");
    const float n_dh2 = cfg_f(cfg, "retract_second_distance");
    float n_dh1 = n_lh + n_lh2 - n_dh2;
    if (n_dh1 <= 0.f) n_dh1 = n_lh + n_lh2;

    const float n_ls  = cfg_f(cfg, "lifting_speed");
    const float n_ls2 = cfg_f(cfg, "lift_second_speed");
    const float n_ds  = cfg_f(cfg, "retract_speed");
    const float n_ds2 = cfg_f(cfg, "retract_second_speed");

    // ---- Pre-compute motion time per segment ----
    const float t_b_motion = motion_s(b_lh,  b_ls)
                           + motion_s(b_lh2, b_ls2)
                           + motion_s(b_dh1, b_ds)
                           + motion_s(b_dh2, b_ds2);

    const float t_n_motion = motion_s(n_lh,  n_ls)
                           + motion_s(n_lh2, n_ls2)
                           + motion_s(n_dh1, n_ds)
                           + motion_s(n_dh2, n_ds2);

    // ---- Count effective layers per zone ----
    const int eff_bottom     = std::min(bottom_count, total_layers);
    const int eff_transition = std::min(transition_count,
                                        std::max(0, total_layers - eff_bottom));
    const int eff_normal     = std::max(0, total_layers - eff_bottom - eff_transition);

    // ---- Bottom zone ----
    float total_s = static_cast<float>(eff_bottom)
                  * (bt + lod + rtbl + t_b_motion + rtal + rtr);

    // ---- Transition zone (exposure interpolated, motion = normal) ----
    // Sum of interpolated exposure times (arithmetic series):
    //   Σ_{k=1}^{N_t} [ bt + (nt-bt)/(N_t+1)*k ] = N_t*(bt+nt)/2
    if (eff_transition > 0) {
        const float exp_sum = static_cast<float>(eff_transition) * (bt + nt) * 0.5f;
        total_s += exp_sum
                 + static_cast<float>(eff_transition) * (lod + rtbl + t_n_motion + rtal + rtr);
    }

    // ---- Normal zone ----
    total_s += static_cast<float>(eff_normal)
             * (nt + lod + rtbl + t_n_motion + rtal + rtr);

    return static_cast<int>(total_s);
}

int adjusted_prz_print_time_seconds(int total_layers, const DynamicPrintConfig &cfg)
{
    const int base = calculate_prz_print_time(total_layers, cfg);
    if (!cfg_bool(cfg, "print_time_compensation", false) || total_layers <= 0)
        return base;
    const double c   = cfg_double(cfg, "layer_print_time_compensation", 0.);
    const double adj = static_cast<double>(base) + c * static_cast<double>(total_layers);
    long long       r  = std::llround(adj);
    if (r < 0)
        r = 0;
    if (r > static_cast<long long>(std::numeric_limits<int>::max()))
        r = std::numeric_limits<int>::max();
    return static_cast<int>(r);
}

// ---------------------------------------------------------------------------
// PRZ Header  (mirrors Slicer::PrzHeader)
// ---------------------------------------------------------------------------
static void prz_header(std::string              &fh,
                       const SLAPrint           &print,
                       const DynamicPrintConfig  &cfg,
                       const ThumbnailData       *thumb)
{
    const SLAPrinterConfig &pcfg = print.printer_config();

    int layerContent_position_offset = 0;

    // Version "V3.0"
    {
        const int sz = 4;
        std::string ver = "V3.0";
        ver.resize(sz, '\0');
        fh += ver;
        layerContent_position_offset += sz;
    }
    // Tag
    {
        const char tag[8] = { 0x07, 0x00, 0x00, 0x00, 0x44, 0x4C, 0x50, 0x00 };
        for (int i = 0; i < 8; ++i) fh += tag[i];
        layerContent_position_offset += 8;
    }
    // Software (blank, 32 bytes)
    {
        fh.append(32, '\0');
        layerContent_position_offset += 32;
    }
    // Software version (blank, 24 bytes)
    {
        fh.append(24, '\0');
        layerContent_position_offset += 24;
    }
    // File time (24 bytes)
    {
        const int sz = 24;
        time_t now = time(nullptr);
        tm *ltm = localtime(&now);
        std::ostringstream ss;
        ss << (1900 + ltm->tm_year) << '-'
           << std::setfill('0') << std::setw(2) << (1 + ltm->tm_mon) << '-'
           << std::setfill('0') << std::setw(2) << ltm->tm_mday << ' '
           << std::setfill('0') << std::setw(2) << ltm->tm_hour << ':'
           << std::setfill('0') << std::setw(2) << ltm->tm_min  << ':'
           << std::setfill('0') << std::setw(2) << ltm->tm_sec;
        std::string t = ss.str();
        t.resize(sz, '\0');
        fh += t;
        layerContent_position_offset += sz;
    }
    // Printer name (printer_settings_id, 32 bytes)
    {
        const int sz = 32;
        std::string s = cfg_s(cfg, "printer_settings_id");
        s.resize(sz, '\0');
        fh += s;
        layerContent_position_offset += sz;
    }
    // Printer type (printer_model, 32 bytes)
    {
        const int sz = 32;
        std::string s = cfg_s(cfg, "printer_model");
        s.resize(sz, '\0');
        fh += s;
        layerContent_position_offset += sz;
    }
    // Profile name (sla_print_settings_id, 32 bytes)
    {
        const int sz = 32;
        std::string s = cfg_s(cfg, "sla_print_settings_id");
        s.resize(sz, '\0');
        fh += s;
        layerContent_position_offset += sz;
    }
    // Anti-aliasing level (2 bytes big-endian short)
    {
        short v = static_cast<short>(cfg_i(cfg, "anti_aliasing_level"));
        write_be(fh, v);
        layerContent_position_offset += 2;
    }
    // Grey level: gray_scale_level[0] mapped from [0,255] to [0,8]
    {
        int raw = 0;
        if (cfg.has("gray_scale_level"))
            if (auto *opt = cfg.option<ConfigOptionInts>("gray_scale_level"))
                if (!opt->values.empty())
                    raw = opt->values[0];
        raw = std::max(0, std::min(raw, 255));
        short v = static_cast<short>(std::lround(raw / 255.0 * 8.0));
        write_be(fh, v);
        layerContent_position_offset += 2;
    }
    // Blur level / image_blur_pixel enum (2 bytes)
    {
        short v = static_cast<short>(cfg_i(cfg, "image_blur_pixel"));
        write_be(fh, v);
        layerContent_position_offset += 2;
    }
    // Preview 116?116 (RGB565 big-endian, from rendered thumbnail or PreviewImage_116_116.png or zeros)
    {
        const int W = 116, H = 116;
        const int sz = W * H * 2;
        cv::Mat image;
        if (thumb && thumb->is_valid()) {
            cv::Mat rgba(thumb->height, thumb->width, CV_8UC4,
                         const_cast<unsigned char *>(thumb->pixels.data()));
            cv::cvtColor(rgba, image, cv::COLOR_RGBA2BGR);
            cv::flip(image, image, 0);
            if (image.cols != W || image.rows != H)
                cv::resize(image, image, cv::Size(W, H), 0, 0, cv::INTER_AREA);
        } else {
            std::string preview_path = cfg_s(cfg, "preview_image_path");
            if (!preview_path.empty())
                image = cv::imread(preview_path + "/PreviewImage_116_116.png");
        }
        if (!image.empty() && image.rows >= H && image.cols >= W) {
            std::vector<cv::Mat> bgr;
            cv::split(image, bgr);
            for (int i = 0; i < W * H; ++i) {
                int x = i % W, y = i / W;
                uchar b = bgr[0].at<uchar>(cv::Point(x, y));
                uchar g = bgr[1].at<uchar>(cv::Point(x, y));
                uchar r = bgr[2].at<uchar>(cv::Point(x, y));
                r >>= (8 - 5); g >>= (8 - 6); b >>= (8 - 5);
                int color = (r << (5 + 6)) + (g << 5) + b;
                fh += static_cast<char>((color >> 8) & 0xff);
                fh += static_cast<char>(color & 0xff);
            }
        } else {
            fh.append(sz, '\0');
        }
        layerContent_position_offset += sz;
    }
    // CR LF
    {
        fh += '\r'; fh += '\n';
        layerContent_position_offset += 2;
    }
    // Preview 290?290 (RGB565 big-endian, from rendered thumbnail or PreviewImage_290_290.png or zeros)
    {
        const int W = 290, H = 290;
        const int sz = W * H * 2;
        cv::Mat image;
        if (thumb && thumb->is_valid()) {
            cv::Mat rgba(thumb->height, thumb->width, CV_8UC4,
                         const_cast<unsigned char *>(thumb->pixels.data()));
            cv::cvtColor(rgba, image, cv::COLOR_RGBA2BGR);
            cv::flip(image, image, 0);
            if (image.cols != W || image.rows != H)
                cv::resize(image, image, cv::Size(W, H), 0, 0, cv::INTER_AREA);
        } else {
            std::string preview_path = cfg_s(cfg, "preview_image_path");
            if (!preview_path.empty())
                image = cv::imread(preview_path + "/PreviewImage_290_290.png");
        }
        if (!image.empty() && image.rows >= H && image.cols >= W) {
            std::vector<cv::Mat> bgr;
            cv::split(image, bgr);
            for (int i = 0; i < W * H; ++i) {
                int x = i % W, y = i / W;
                uchar b = bgr[0].at<uchar>(cv::Point(x, y));
                uchar g = bgr[1].at<uchar>(cv::Point(x, y));
                uchar r = bgr[2].at<uchar>(cv::Point(x, y));
                r >>= (8 - 5); g >>= (8 - 6); b >>= (8 - 5);
                int color = (r << (5 + 6)) + (g << 5) + b;
                fh += static_cast<char>((color >> 8) & 0xff);
                fh += static_cast<char>(color & 0xff);
            }
        } else {
            fh.append(sz, '\0');
        }
        layerContent_position_offset += sz;
    }
    // CR LF
    {
        fh += '\r'; fh += '\n';
        layerContent_position_offset += 2;
    }
    // Total layers
    {
        int total = static_cast<int>(print.print_layers().size());
        write_be(fh, total);
        layerContent_position_offset += 4;
    }
    // XResolution / YResolution
    {
        short xr = static_cast<short>(pcfg.display_pixels_y.getInt());
        short yr = static_cast<short>(pcfg.display_pixels_x.getInt());
        write_be(fh, xr);
        write_be(fh, yr);
        layerContent_position_offset += 4;
    }
    // Xmirror (1 byte): mirror_x=false ??1; true ??0
    { fh += static_cast<char>(pcfg.display_mirror_x.getBool() ? 0 : 1); layerContent_position_offset += 1; }
    // Ymirror (1 byte): mirror_y=false ??0; true ??1
    { fh += static_cast<char>(pcfg.display_mirror_y.getBool() ? 1 : 0); layerContent_position_offset += 1; }
    // PlatformXLength (4 bytes, float, mm)
    { float v = static_cast<float>(pcfg.display_height.getFloat());  write_be(fh, v); layerContent_position_offset += 4; }
    // PlatformYLength (4 bytes, float, mm)
    { float v = static_cast<float>(pcfg.display_width.getFloat()); write_be(fh, v); layerContent_position_offset += 4; }
    // PlatformZLength (4 bytes, float, mm)
    { float v = static_cast<float>(pcfg.printable_height.getFloat()); write_be(fh, v); layerContent_position_offset += 4; }
    // LayerThickness (4 bytes, float, mm)
    { float v = cfg_f(cfg, "layer_height"); write_be(fh, v); layerContent_position_offset += 4; }
    // ExposureTime (normal, 4 bytes)
    { float v = cfg_f(cfg, "exposure_time"); write_be(fh, v); layerContent_position_offset += 4; }
    // Exposure_delay_mode (1 byte): 1 = use static_time
    { fh += '\x01'; layerContent_position_offset += 1; }
    // TurnOffTime (light_off_day, 4 bytes)
    { float v = cfg_f(cfg, "light_off_day"); write_be(fh, v); layerContent_position_offset += 4; }
    // Bottom_Before_lift_static_time (rest_time_before_lift)
    { float v = cfg_f(cfg, "rest_time_before_lift"); write_be(fh, v); layerContent_position_offset += 4; }
    // Bottom_After_lift_static_time (rest_time_after_lift)
    { float v = cfg_f(cfg, "rest_time_after_lift"); write_be(fh, v); layerContent_position_offset += 4; }
    // Bottom_After_retract_static_time (rest_time_after_retract)
    { float v = cfg_f(cfg, "rest_time_after_retract"); write_be(fh, v); layerContent_position_offset += 4; }
    // Before_lift_static_time (rest_time_before_lift)
    { float v = cfg_f(cfg, "rest_time_before_lift"); write_be(fh, v); layerContent_position_offset += 4; }
    // After_lift_static_time (rest_time_after_lift)
    { float v = cfg_f(cfg, "rest_time_after_lift"); write_be(fh, v); layerContent_position_offset += 4; }
    // After_retract_static_time (rest_time_after_retract)
    { float v = cfg_f(cfg, "rest_time_after_retract"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomExposureTime
    { float v = cfg_f(cfg, "bottom_exposure_time"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomLayers
    { int v = cfg_i(cfg, "bottom_layer_count"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomLiftDist
    { float v = cfg_f(cfg, "bottom_lift_distance"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomLiftSpeed
    { float v = cfg_f(cfg, "bottom_lift_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // LiftDist (normal)
    { float v = cfg_f(cfg, "lifting_distance"); write_be(fh, v); layerContent_position_offset += 4; }
    // LiftSpeed (normal)
    { float v = cfg_f(cfg, "lifting_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomRetractDist = bottom_lift_distance + bottom_lift_second_distance - bottom_retract_second_distance
    {
        float lh  = cfg_f(cfg, "bottom_lift_distance");
        float lh2 = cfg_f(cfg, "bottom_lift_second_distance");
        float dh2 = cfg_f(cfg, "bottom_retract_second_distance");
        float v   = lh + lh2 - dh2;
        if (v <= 0.f) v = lh + lh2;
        write_be(fh, v); layerContent_position_offset += 4;
    }
    // BottomRetractSpeed
    { float v = cfg_f(cfg, "bottom_retract_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // RetractDist = lifting_distance + lift_second_distance - retract_second_distance
    {
        float lh  = cfg_f(cfg, "lifting_distance");
        float lh2 = cfg_f(cfg, "lift_second_distance");
        float dh2 = cfg_f(cfg, "retract_second_distance");
        float v   = lh + lh2 - dh2;
        if (v <= 0.f) v = lh + lh2;
        write_be(fh, v); layerContent_position_offset += 4;
    }
    // RetractSpeed
    { float v = cfg_f(cfg, "retract_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomLift_second_Dist
    { float v = cfg_f(cfg, "bottom_lift_second_distance"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomLift_second_Speed
    { float v = cfg_f(cfg, "bottom_lift_second_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // Lift_second_Dist
    { float v = cfg_f(cfg, "lift_second_distance"); write_be(fh, v); layerContent_position_offset += 4; }
    // Lift_second_Speed
    { float v = cfg_f(cfg, "lift_second_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomRetract_second_Dist
    { float v = cfg_f(cfg, "bottom_retract_second_distance"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomRetract_second_Speed
    { float v = cfg_f(cfg, "bottom_retract_second_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // Retract_second_Dist
    { float v = cfg_f(cfg, "retract_second_distance"); write_be(fh, v); layerContent_position_offset += 4; }
    // Retract_second_Speed
    { float v = cfg_f(cfg, "retract_second_speed"); write_be(fh, v); layerContent_position_offset += 4; }
    // BottomLightPwm (2 bytes)
    { short v = static_cast<short>(cfg_i(cfg, "bottom_light_pwm")); write_be(fh, v); layerContent_position_offset += 2; }
    // LightPwm (2 bytes)
    { short v = static_cast<short>(cfg_i(cfg, "light_pwm")); write_be(fh, v); layerContent_position_offset += 2; }
    // Advance_Mode (0 = normal, 1 byte)
    { fh += '\0'; layerContent_position_offset += 1; }
    // PrintTimes (estimated, seconds)
    { int v = adjusted_prz_print_time_seconds(static_cast<int>(print.print_layers().size()), cfg); write_be(fh, v); layerContent_position_offset += 4; }
    // TotalVolume / TotalWeight / TotalPrice (from print statistics)
    {
        const SLAPrintStatistics &stats = print.print_statistics();
        float volume = static_cast<float>(stats.objects_used_material + stats.support_used_material);
        float weight = static_cast<float>(stats.total_weight);
        float price  = static_cast<float>(stats.total_cost);
        write_be(fh, volume); layerContent_position_offset += 4;
        write_be(fh, weight); layerContent_position_offset += 4;
        write_be(fh, price);  layerContent_position_offset += 4;
    }
    // PriceUnit (8 bytes, zeros)
    { fh.append(8, '\0'); layerContent_position_offset += 8; }
    // LayerContent_position_offset (4 bytes, self-referential)
    {
        layerContent_position_offset += 4 + 3;
        write_be(fh, layerContent_position_offset);
    }
    // Grayscale_level: 1 = 8-bit
    { fh += '\x01'; }
    // Transition layers (2 bytes big-endian short)
    { short v = static_cast<short>(cfg_i(cfg, "transition_layer_count")); write_be(fh, v); }
}

// ---------------------------------------------------------------------------
// PRZ Layer Content  (mirrors Slicer::PrzLayerContent)
// ---------------------------------------------------------------------------
static void prz_layer_content(std::string              &fh,
                               const SLAPrint           &print,
                               const DynamicPrintConfig &cfg,
                               size_t                    layerId)
{
    const int bottom     = cfg_i(cfg, "bottom_layer_count");
    const int transition = cfg_i(cfg, "transition_layer_count") + bottom;
    const bool is_bottom = static_cast<int>(layerId) < bottom;

    // PauseFlag (short, 0)
    { short v = 0; write_be(fh, v); }

    // Layer print Z (mm), stored as big-endian float
    {
        coord_t lvl = print.print_layers()[layerId].level();
        float z = static_cast<float>(unscale<double>(lvl));
        write_be(fh, z); // PausePositionZ
        write_be(fh, z); // LayerPositionZ
    }

    // LayerExposureTime (with transition interpolation)
    {
        float bt = cfg_f(cfg, "bottom_exposure_time");
        float nt = cfg_f(cfg, "exposure_time");
        float expTime = nt;
        if (is_bottom) {
            expTime = bt;
        } else if (static_cast<int>(layerId) >= bottom &&
                   static_cast<int>(layerId) < transition) {
            int tcount = cfg_i(cfg, "transition_layer_count");
            expTime = bt + (nt - bt) / (1.f + tcount)
                          * static_cast<float>(layerId - bottom + 1);
        }
        write_be(fh, expTime);
    }

    // LayerOffTime (light_off_day)
    { float v = cfg_f(cfg, "light_off_day"); write_be(fh, v); }

    // Before_lift_static_time (0)
    { float v = 0.f; write_be(fh, v); }
    // After_lift_static_time (0)
    { float v = 0.f; write_be(fh, v); }
    // After_retract_static_time (rest_time_after_retract)
    { float v = cfg_f(cfg, "rest_time_after_retract"); write_be(fh, v); }

    // LiftDist
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_lift_distance")
                            : cfg_f(cfg, "lifting_distance");
        write_be(fh, v);
    }
    // LiftSpeed
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_lift_speed")
                            : cfg_f(cfg, "lifting_speed");
        write_be(fh, v);
    }
    // Lift_Second_Dist
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_lift_second_distance")
                            : cfg_f(cfg, "lift_second_distance");
        write_be(fh, v);
    }
    // Lift_Second_Speed
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_lift_second_speed")
                            : cfg_f(cfg, "lift_second_speed");
        write_be(fh, v);
    }
    // Retract_Dist = lift + lift2 - drop2
    {
        float lh, lh2, dh2;
        if (is_bottom) {
            lh  = cfg_f(cfg, "bottom_lift_distance");
            lh2 = cfg_f(cfg, "bottom_lift_second_distance");
            dh2 = cfg_f(cfg, "bottom_retract_second_distance");
        } else {
            lh  = cfg_f(cfg, "lifting_distance");
            lh2 = cfg_f(cfg, "lift_second_distance");
            dh2 = cfg_f(cfg, "retract_second_distance");
        }
        float v = lh + lh2 - dh2;
        if (v <= 0.f) v = lh + lh2;
        write_be(fh, v);
    }
    // Retract_Speed
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_retract_speed")
                            : cfg_f(cfg, "retract_speed");
        write_be(fh, v);
    }
    // Retract_Second_Dist
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_retract_second_distance")
                            : cfg_f(cfg, "retract_second_distance");
        write_be(fh, v);
    }
    // Retract_Second_Speed
    {
        float v = is_bottom ? cfg_f(cfg, "bottom_retract_second_speed")
                            : cfg_f(cfg, "retract_second_speed");
        write_be(fh, v);
    }
    // LightPwm (short)
    {
        short v = is_bottom ? static_cast<short>(cfg_i(cfg, "bottom_light_pwm"))
                            : static_cast<short>(cfg_i(cfg, "light_pwm"));
        write_be(fh, v);
    }

    // CR LF
    fh += '\r'; fh += '\n';
}

// ---------------------------------------------------------------------------
// Main entry point  (mirrors Slicer::getPRZString2)
// Batched on-demand rasterization: BATCH_SZ layers rasterized in parallel,
// RLE-encoded sequentially, streamed directly to out — bounded memory, no
// large intermediate buffer.
// ---------------------------------------------------------------------------
void generate_prz(std::ostream &out, const SLAPrint &print, const ThumbnailData *thumb,
                  std::function<bool(int)> progress)
{
    if (!print.raster_params().has_value())
        return;

    const SLARasterParams    &rp     = *print.raster_params();
    const DynamicPrintConfig &cfg    = print.full_print_config();
    const auto               &layers = print.print_layers();
    const size_t              N      = layers.size();

    if (N == 0)
        return;

    static constexpr uchar BLACK = 0x00;
    static constexpr uchar WHITE = 0xc0;
    static constexpr uchar GRAY  = 0x40;
    static constexpr uchar BYTE_NUMBER[4]      = { 0x00, 0x10, 0x20, 0x30 };
    static constexpr int   CONTINUOUS_BOUND[4] = { 1 << 4, 1 << 12, 1 << 20, 1 << 28 };
    static constexpr int   BOUND_0             = 0x0f;
    constexpr size_t       BATCH_SZ            = 8;

    // Write header (~1.2 KB) to a local buffer, then flush immediately
    {
        std::string hdr;
        hdr.reserve(2048);
        prz_header(hdr, print, cfg, thumb);
        out.write(hdr.data(), static_cast<std::streamsize>(hdr.size()));
    }

    // -----------------------------------------------------------------------
    // Producer-Consumer pipeline
    //
    // Producer thread: [A] collect ExPolygons + [B] TBB rasterize → queue.push
    // Consumer (this thread): queue.pop → [E] RLE encode + stream write + release
    //
    // Overlap: while Consumer encodes batch k, Producer rasterizes batch k+1.
    // QUEUE_DEPTH = 2 keeps peak memory ≤ 3 × BATCH_SZ × ~23 MB ≈ 552 MB.
    // -----------------------------------------------------------------------
    constexpr size_t QUEUE_DEPTH = 2;
    using Batch = std::vector<cv::Mat>;
    std::queue<Batch>       batch_queue;
    std::mutex              queue_mutex;
    std::condition_variable queue_cv_not_full;
    std::condition_variable queue_cv_not_empty;
    bool                    producer_done      = false;
    std::exception_ptr      producer_exception = nullptr;
    std::atomic<bool>       cancelled{false};

    auto producer_lambda = [&]() {
        try {
            for (size_t batch_start = 0; batch_start < N; batch_start += BATCH_SZ) {
                // Task 4.4: check cancellation before each batch
                if (cancelled.load())
                    break;

                const size_t batch_end = std::min(batch_start + BATCH_SZ, N);
                const size_t batch_n   = batch_end - batch_start;

                // [A] Collect ExPolygons and apply bed→display shift translation
                std::vector<ExPolygons> batch_polys(batch_n);
                for (size_t i = 0; i < batch_n; ++i) {
                    batch_polys[i] = layers[batch_start + i].transformed_slices();
                    for (ExPolygon &ep : batch_polys[i])
                        ep.translate(rp.shift);
                }

                // [B] TBB parallel rasterize inside a limited arena (Task 5.1):
                // reserve 2 logical cores for the UI event loop and the Consumer thread.
                std::vector<cv::Mat> batch_mats(batch_n);
                {
                    int max_tbb = std::max(1, tbb::this_task_arena::max_concurrency());
                    tbb::task_arena arena(max_tbb);
                    arena.execute([&] {
                        tbb::parallel_for(tbb::blocked_range<size_t>(0, batch_n),
                            [&](const tbb::blocked_range<size_t> &r) {
                                for (size_t i = r.begin(); i < r.end(); ++i) {
                                    batch_mats[i] = sla::expolygons_to_cvmat(
                                        batch_polys[i], rp.res, rp.pxdim, rp.trafo,
                                        rp.gamma, rp.aa_steps, rp.gray_lo, rp.gray_hi,
                                        rp.blur_pixel);
                                    sla::apply_picture_grayscale_lut(batch_mats[i],
                                                                     rp.picture_grayscale);
                                }
                            });
                    });
                }

                // Push batch to queue; block until Consumer drains a slot (Task 2.3)
                {
                    std::unique_lock<std::mutex> lk(queue_mutex);
                    queue_cv_not_full.wait(lk, [&] {
                        return batch_queue.size() < QUEUE_DEPTH || cancelled.load();
                    });
                    if (cancelled.load()) {
                        // Task 4.5: woken by Consumer cancel — release mats to avoid leak
                        for (auto &mat : batch_mats) mat.release();
                        break;
                    }
                    batch_queue.push(std::move(batch_mats));
                }
                queue_cv_not_empty.notify_one();
            }
        } catch (...) {
            // Task 4.1: capture exception; signal Consumer
            {
                std::lock_guard<std::mutex> lk(queue_mutex);
                producer_exception = std::current_exception();
                producer_done = true;
            }
            queue_cv_not_empty.notify_one();
            return;
        }
        // Task 2.4: normal completion
        {
            std::lock_guard<std::mutex> lk(queue_mutex);
            producer_done = true;
        }
        queue_cv_not_empty.notify_one();
    };

    // Task 2.5
    std::thread producer_thread(producer_lambda);

    // Release all cv::Mat instances still in the queue (cancel / exception paths)
    auto drain_queue = [&] {
        std::lock_guard<std::mutex> lk(queue_mutex);
        while (!batch_queue.empty()) {
            for (auto &mat : batch_queue.front()) mat.release();
            batch_queue.pop();
        }
    };

    // Task 4.2: accumulate producer exception for re-throw after join
    std::exception_ptr consumer_rethrow = nullptr;
    size_t batch_start_consumer = 0;
    try {
        while (true) {
            // Task 3.1: wait for a batch or producer completion
            Batch batch;
            {
                std::unique_lock<std::mutex> lk(queue_mutex);
                queue_cv_not_empty.wait(lk, [&] {
                    return !batch_queue.empty() || producer_done;
                });
                if (producer_exception) {
                    consumer_rethrow = producer_exception;
                    break;
                }
                if (batch_queue.empty()) // producer_done + empty queue → all done
                    break;
                batch = std::move(batch_queue.front());
                batch_queue.pop();
            }
            // Task 3.1: wake Producer now that a slot opened
            queue_cv_not_full.notify_one();

            const size_t batch_n   = batch.size();
            const size_t batch_end = batch_start_consumer + batch_n;

            // Task 3.2: sequential RLE encode + write + release per layer
            for (size_t i = 0; i < batch_n; ++i) {
                const size_t lid = batch_start_consumer + i;

                std::string lc;
                prz_layer_content(lc, print, cfg, lid);
                out.write(lc.data(), static_cast<std::streamsize>(lc.size()));

                const cv::Mat &img = batch[i];
                const int total    = img.rows * img.cols;
                const uchar *data  = img.data;

                int sum = 0;
                std::string przByte;
                przByte.reserve(static_cast<size_t>(total) / 2 + 8);
                przByte += static_cast<char>(0x55);

                auto flush_run = [&](uchar color, int count) {
                    const char *c_count = reinterpret_cast<const char *>(&count);

                    if (color == 0x00 || color == 0xff) {
                        uchar base = (color == 0x00) ? BLACK : WHITE;
                        for (int bid = 0; bid < 4; ++bid) {
                            if (count < CONTINUOUS_BOUND[bid]) {
                                uchar byte0 = base + BYTE_NUMBER[bid] + (count & BOUND_0);
                                count >>= 4;
                                sum += static_cast<int>(byte0);
                                przByte += static_cast<char>(byte0);
                                for (int k = bid; k >= 1; --k) {
                                    przByte += c_count[k - 1];
                                    sum += static_cast<int>(static_cast<uchar>(c_count[k - 1]));
                                }
                                break;
                            }
                        }
                    } else {
                        for (int bid = 0; bid < 4; ++bid) {
                            if (count < CONTINUOUS_BOUND[bid]) {
                                uchar byte0 = GRAY + BYTE_NUMBER[bid] + (count & BOUND_0);
                                count >>= 4;
                                sum += static_cast<int>(byte0);
                                przByte += static_cast<char>(byte0);
                                sum += static_cast<int>(color);
                                przByte += static_cast<char>(color);
                                for (int k = bid; k >= 1; --k) {
                                    przByte += c_count[k - 1];
                                    sum += static_cast<int>(static_cast<uchar>(c_count[k - 1]));
                                }
                                break;
                            }
                        }
                    }
                };

                if (total > 0) {
                    uchar cur   = data[0];
                    int   count = 1;
                    for (int j = 1; j < total; ++j) {
                        uchar px = data[j];
                        if (px == cur) {
                            ++count;
                        } else {
                            flush_run(cur, count);
                            cur   = px;
                            count = 1;
                        }
                    }
                    flush_run(cur, count);
                }

                uchar checksum = static_cast<uchar>((~sum) & 0xff);
                przByte += static_cast<char>(checksum);

                write_be(out, static_cast<int>(przByte.size()));
                out.write(przByte.data(), static_cast<std::streamsize>(przByte.size()));
                out.put('\r'); out.put('\n');

                if (lid == N - 1) {
                    const char tag[11] = { 0x00, 0x00, 0x00, 0x07,
                                           0x00, 0x00, 0x00,
                                           0x44, 0x4C, 0x50, 0x00 };
                    out.write(tag, 11);
                }

                batch[i].release();
            }

            batch_start_consumer = batch_end;

            // Task 3.3 / 3.4: progress + cancellation — only Consumer calls progress (SPSC)
            if (progress) {
                int pct = static_cast<int>((batch_end * 100) / N);
                if (!progress(pct)) {
                    cancelled.store(true);
                    queue_cv_not_full.notify_all(); // Task 4.5: wake Producer if blocked
                    break;
                }
            }
        }
    } catch (...) {
        cancelled.store(true);
        queue_cv_not_full.notify_all();
        drain_queue();
        producer_thread.join(); // Task 4.3
        throw;
    }

    drain_queue();             // no-op on normal finish; releases leftover mats on cancel
    producer_thread.join();    // Task 4.3: always join

    if (consumer_rethrow)
        std::rethrow_exception(consumer_rethrow); // Task 4.2: propagate producer exception
}

} // namespace Slic3r
