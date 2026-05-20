// SLA Prepare: layer slider sync (separate TU so MSVC always links these symbols).
#include "libslic3r/libslic3r.h"
#include "GLCanvas3D.hpp"
#include "SlaPrepareLayers.hpp"
#include "GUI_App.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "IMSlider.hpp"

#include <algorithm>

namespace Slic3r {
namespace GUI {

void GLCanvas3D::update_sla_prepare_layers_slider()
{
    if (m_canvas_type != ECanvasType::CanvasView3D) {
        return;
    }
    if (current_printer_technology() != ptSLA) {
        IMSlider* sl = m_gcode_viewer.get_layers_slider();
        if (sl != nullptr) {
            sl->set_sla_prepare_mode(false);
            sl->set_menu_enable(true);
        }
        return;
    }

    m_gcode_viewer.init(wxGetApp().get_mode(), wxGetApp().preset_bundle);

    _update_prepare_scene_max_z();
    const double min_z = m_prepare_scene_min_z;
    const double max_z = m_prepare_scene_max_z;

    PresetBundle* pb = wxGetApp().preset_bundle;
    double        layer_h = pb->sla_prints.get_edited_preset().config.opt_float("layer_height");
    double        init_h  = pb->sla_materials.get_edited_preset().config.opt_float("initial_layer_height");

    std::vector<double> zs = build_sla_estimated_layer_top_zs(min_z, max_z, init_h, layer_h);
    if (zs.empty())
        return;

    const bool size_changed   = (zs.size() != m_sla_prepare_layers_z.size());
    const bool values_changed = (zs != m_sla_prepare_layers_z);
    m_sla_prepare_layers_z    = std::move(zs);

    IMSlider* sl = m_gcode_viewer.get_layers_slider();
    sl->set_sla_prepare_mode(true);
    sl->set_menu_enable(false);
    sl->SetDrawMode(DrawMode::dmSlaPrint);

    CustomGCode::Info empty_ticks;
    empty_ticks.mode = CustomGCode::SingleExtruder;
    sl->SetTicksValues(empty_ticks);

    sl->SetSliderValues(m_sla_prepare_layers_z);
    sl->set_sla_prepare_height_base(min_z);
    const int maxp = int(m_sla_prepare_layers_z.size()) - 1;
    sl->SetMaxValue(maxp);
    if (size_changed || values_changed || sl->GetHigherValue() > maxp || sl->GetLowerValue() > maxp || sl->GetLowerValue() < 0) {
        if (size_changed || sl->GetHigherValue() > maxp || sl->GetLowerValue() > maxp || sl->GetLowerValue() < 0) {
            sl->SetSelectionSpan(0, maxp);
            sl->SetLowerValue(0);
            sl->SetHigherValue(maxp);
        } else {
            sl->SetHigherValue(std::min(sl->GetHigherValue(), maxp));
            sl->SetLowerValue(std::min(sl->GetLowerValue(), maxp));
        }
    }

    sl->set_on_change_callback([this]() { _apply_sla_prepare_clip_from_layers_slider(); });

    sl->Show();
    _apply_sla_prepare_clip_from_layers_slider();

    set_as_dirty();
}

void GLCanvas3D::_apply_sla_prepare_clip_from_layers_slider()
{
    if (m_sla_prepare_layers_z.empty())
        return;
    IMSlider* slider = m_gcode_viewer.get_layers_slider();
    if (slider == nullptr)
        return;

    int       low_pos  = slider->GetLowerValue();
    int       high_pos = slider->GetHigherValue();
    const int max_pos  = int(m_sla_prepare_layers_z.size()) - 1;
    low_pos  = std::clamp(low_pos, 0, max_pos);
    high_pos = std::clamp(high_pos, 0, max_pos);

    const std::vector<double>& zs = m_sla_prepare_layers_z;

    auto set_planes_and_state = [this](bool full_low, bool full_high, double z_low_mm, double z_high_mm) {
        if (full_low)
            set_clipping_plane(0, ClippingPlane::ClipsNothing());
        else
            set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -z_low_mm));

        if (full_high)
            set_clipping_plane(1, ClippingPlane::ClipsNothing());
        else
            set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), z_high_mm));

        const bool full_range   = full_low && full_high;
        m_use_clipping_planes   = !full_range;
        m_prepare_clip_z_low    = full_low ? 0.0 : z_low_mm;
        m_prepare_clip_z_high   = full_high ? m_prepare_scene_max_z : z_high_mm;
    };

    double z_low_mm  = 0.0;
    double z_high_mm = m_prepare_scene_max_z;
    bool   full_low  = true;
    bool   full_high = true;

    if (low_pos == high_pos) {
        const double z_bot = (low_pos == 0) ? 0.0 : zs[low_pos - 1];
        const double z_top = zs[low_pos];
        full_low  = (low_pos == 0);
        full_high = (z_top >= m_prepare_scene_max_z - 1e-6);
        z_low_mm  = full_low ? 0.0 : z_bot;
        z_high_mm = full_high ? m_prepare_scene_max_z : z_top;
        set_planes_and_state(full_low, full_high, z_low_mm, z_high_mm);
    } else {
        z_low_mm  = zs[low_pos];
        z_high_mm = zs[high_pos];
        full_low  = (low_pos == 0);
        full_high = (high_pos == max_pos);
        set_planes_and_state(full_low, full_high, z_low_mm, z_high_mm);
    }

    if (!m_syncing_clipper) {
        m_syncing_clipper = true;
        auto* pool = m_gizmos.get_common_gizmos_data();
        if (pool) {
            auto* oc = pool->object_clipper();
            if (oc) {
                if (m_sla_oc_clip_slider_session) {
                    // SLA Support / Hollow / Drill 等：Gizmo 網格用 set_position_by_ratio（物件 Z 比例），
                    // 與 GLGizmoSlaBase::render_volumes 的 get_position() 語意一致。
                    const double z_high_eff = full_high
                        ? m_gizmo_obj_z_max
                        : std::clamp(z_high_mm, m_gizmo_obj_z_min, m_gizmo_obj_z_max);
                    const double z_span     = m_gizmo_obj_z_max - m_gizmo_obj_z_min;
                    m_gizmo_clip_ratio      = (z_span > 1e-6)
                        ? std::clamp((m_gizmo_obj_z_max - z_high_eff) / z_span, 0.0, 1.0)
                        : 0.0;
                    oc->set_position_by_ratio(m_gizmo_clip_ratio, true, true);
                } else if (m_use_clipping_planes) {
                    const double z_high_eff  = m_prepare_clip_z_high;
                    const double rough_ratio = (m_prepare_scene_max_z > 0.0)
                        ? std::clamp(1.0 - z_high_eff / m_prepare_scene_max_z, 0.0, 1.0)
                        : 0.0;
                    oc->set_range_and_pos(Vec3d(0., 0., -1.), z_high_eff, rough_ratio);
                }
            }
        }
        m_syncing_clipper = false;
    }

    set_as_dirty();
}

} // namespace GUI
} // namespace Slic3r
