#include "PhrozenPRZ.hpp"

#include <ctime>
#include <sstream>
#include <iomanip>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/libslic3r.h>

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

static std::string cfg_s(const DynamicPrintConfig &cfg, const std::string &key)
{
    if (cfg.has(key))
        if (auto *opt = cfg.option(key))
            return opt->serialize();
    return {};
}

// ---------------------------------------------------------------------------
// Helper: write a big-endian value of sizeof(T) bytes into fh
// ---------------------------------------------------------------------------
template<typename T>
static void write_be(std::string &fh, T val)
{
    const int N = sizeof(T);
    const char *c = reinterpret_cast<const char *>(&val);
    for (int i = N - 1; i >= 0; --i)
        fh += c[i];
}

// ---------------------------------------------------------------------------
// Calculate estimated print time in seconds from PRZ parameters
// ---------------------------------------------------------------------------
static int calculate_prz_print_time(const SLAPrint          &print,
                                    const DynamicPrintConfig &cfg)
{
    // speed == 0 → treat that motion segment as 0 s (avoid divide-by-zero)
    auto motion_s = [](float dist_mm, float speed_mm_min) -> float {
        if (speed_mm_min <= 0.f) return 0.f;
        return dist_mm / speed_mm_min * 60.f;
    };

    const int   total_layers     = static_cast<int>(print.layer_images().size());
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

// ---------------------------------------------------------------------------
// PRZ Header  (mirrors Slicer::PrzHeader)
// ---------------------------------------------------------------------------
static void prz_header(std::string             &fh,
                       const SLAPrint          &print,
                       const DynamicPrintConfig &cfg)
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
    // Preview 116×116 (RGB565 big-endian, from PreviewImage_116_116.png or zeros)
    {
        const int W = 116, H = 116;
        const int sz = W * H * 2;
        std::string preview_path = cfg_s(cfg, "preview_image_path");
        cv::Mat image;
        if (!preview_path.empty())
            image = cv::imread(preview_path + "/PreviewImage_116_116.png");
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
    // Preview 290×290 (RGB565 big-endian, from PreviewImage_290_290.png or zeros)
    {
        const int W = 290, H = 290;
        const int sz = W * H * 2;
        std::string preview_path = cfg_s(cfg, "preview_image_path");
        cv::Mat image;
        if (!preview_path.empty())
            image = cv::imread(preview_path + "/PreviewImage_290_290.png");
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
        int total = static_cast<int>(print.layer_images().size());
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
    // Xmirror (1 byte): mirror_x=false → 1; true → 0
    { fh += static_cast<char>(pcfg.display_mirror_x.getBool() ? 0 : 1); layerContent_position_offset += 1; }
    // Ymirror (1 byte): mirror_y=false → 0; true → 1
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
    { int v = calculate_prz_print_time(print, cfg); write_be(fh, v); layerContent_position_offset += 4; }
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
// encode_pixels replaced by direct cv::Mat pixel scan — no intermediate vector
// ---------------------------------------------------------------------------
std::string generate_prz(const SLAPrint &print)
{
    const auto &layer_images = print.layer_images();
    if (layer_images.empty())
        return {};

    const DynamicPrintConfig &cfg = print.full_print_config();

    static constexpr uchar BLACK = 0x00;
    static constexpr uchar WHITE = 0xc0;
    static constexpr uchar GRAY  = 0x40;
    static constexpr uchar BYTE_NUMBER[4]      = { 0x00, 0x10, 0x20, 0x30 };
    static constexpr int   CONTINUOUS_BOUND[4] = { 1 << 4, 1 << 12, 1 << 20, 1 << 28 };
    static constexpr int   BOUND_0             = 0x0f;

    std::string out;
    out.reserve(64 * 1024 * 1024);

    prz_header(out, print, cfg);

    const size_t layerSize = layer_images.size();

    for (size_t lid = 0; lid < layerSize; ++lid) {
        prz_layer_content(out, print, cfg, lid);

        const cv::Mat &img = layer_images[lid]; // CV_8UC1, row-major
        const int total    = img.rows * img.cols;
        const uchar *data  = img.data;

        int sum = 0;
        std::string przByte;
        przByte.reserve(static_cast<size_t>(total) / 2 + 8);

        przByte += static_cast<char>(0x55); // layer head

        // Write one RLE run (color, count) directly into przByte
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
                        for (int i = bid; i >= 1; --i) {
                            przByte += c_count[i - 1];
                            sum += static_cast<int>(static_cast<uchar>(c_count[i - 1]));
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
                        for (int i = bid; i >= 1; --i) {
                            przByte += c_count[i - 1];
                            sum += static_cast<int>(static_cast<uchar>(c_count[i - 1]));
                        }
                        break;
                    }
                }
            }
        };

        // Scan pixels directly — no intermediate encode_pixels vector
        if (total > 0) {
            uchar cur   = data[0];
            int   count = 1;
            for (int i = 1; i < total; ++i) {
                uchar px = data[i];
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

        // Checksum byte
        uchar checksum = static_cast<uchar>((~sum) & 0xff);
        przByte += static_cast<char>(checksum);

        // Layer data size prefix (4 bytes big-endian int)
        {
            int sz = static_cast<int>(przByte.size());
            write_be(out, sz);
        }
        out += przByte;

        // CR LF after layer pixel data
        out += '\r'; out += '\n';

        // DLP end tag on last layer
        if (lid == layerSize - 1) {
            const char tag[11] = { 0x00, 0x00, 0x00, 0x07,
                                   0x00, 0x00, 0x00,
                                   0x44, 0x4C, 0x50, 0x00 };
            for (int i = 0; i < 11; ++i)
                out += tag[i];
        }
    }

    return out;
}

} // namespace Slic3r
