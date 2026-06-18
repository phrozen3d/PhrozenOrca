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

    // In an SLA gizmo session (Hollow / Drill / SLA Support), set_hide_full_scene(true)
    // marks every model object GLVolume as is_active=false, which would make
    // _update_prepare_scene_max_z() see no printable volume and fall back to 50mm.
    // Use the selected object's bbox Z range cached by enter_gizmo_slider_mode()
    // instead, so the slider reflects the object actually being edited.
    double min_z = 0.0;
    double max_z = 50.0;
    if (m_sla_oc_clip_slider_session) {
        min_z = m_gizmo_obj_z_min;
        max_z = m_gizmo_obj_z_max;
        if (max_z <= min_z) {
            // Only happens if the bbox passed to enter_gizmo_slider_mode() was invalid.
            min_z = 0.0;
            max_z = 50.0;
        }
    } else {
        _update_prepare_scene_max_z();
        min_z = m_prepare_scene_min_z;
        max_z = m_prepare_scene_max_z;
    }

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

    // In a gizmo session the zs vector was built from the selected object's bbox,
    // so the "full top" reference is the object's max Z, not the whole scene's.
    // Bottom reference stays at 0 because the slider is still allowed to clip
    // below the object (the gizmo only constrains how Z values are interpreted).
    const double effective_max_z = m_sla_oc_clip_slider_session
                                       ? m_gizmo_obj_z_max
                                       : m_prepare_scene_max_z;

    auto set_planes_and_state = [this, effective_max_z](bool full_low, bool full_high, double z_low_mm, double z_high_mm) {
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
        m_prepare_clip_z_high   = full_high ? effective_max_z : z_high_mm;
    };

    double z_low_mm  = 0.0;
    double z_high_mm = effective_max_z;
    bool   full_low  = true;
    bool   full_high = true;

    if (low_pos == high_pos) {
        const double z_bot = (low_pos == 0) ? 0.0 : zs[low_pos - 1];
        const double z_top = zs[low_pos];
        full_low  = (low_pos == 0);
        full_high = (z_top >= effective_max_z - 1e-6);
        z_low_mm  = full_low ? 0.0 : z_bot;
        z_high_mm = full_high ? effective_max_z : z_top;
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
                    // Align ObjectClipper's m_clp with the visual Z-range plane so the
                    // cap mesh, contour and raycaster cut at the same world Z as the
                    // shader's z_range clip. set_position_by_ratio() internally builds
                    // the plane from (mo->max_z - mo->min_z) and the instance offset
                    // assuming a model centered at z=0; SLA hollow / drill / support
                    // meshes are typically bed-aligned (model.min_z = 0) and Support
                    // adds a non-zero z_shift (elevation lift), so the implicit-plane
                    // formula lands at a world Z that doesn't match m_clipping_planes[1]
                    // (which is world-Z based). The mismatch leaves the cap mesh at the
                    // wrong cross-section and renders a thin un-clipped slab between
                    // the two planes, most visible in Support mode where z_shift > 0.
                    // set_range_and_pos() sets m_clp directly with the world-Z offset
                    // already used by the visual clip.
                    const double z_high_eff = full_high
                        ? m_gizmo_obj_z_max
                        : std::clamp(z_high_mm, m_gizmo_obj_z_min, m_gizmo_obj_z_max);
                    const double z_span     = m_gizmo_obj_z_max - m_gizmo_obj_z_min;
                    m_gizmo_clip_ratio      = (z_span > 1e-6)
                        ? std::clamp((m_gizmo_obj_z_max - z_high_eff) / z_span, 0.0, 1.0)
                        : 0.0;
                    // Normal (+Z) so GLGizmosManager::get_clipping_plane() flips it
                    // to (-Z, z_high_eff), matching the visual clipping plane set
                    // above (set_clipping_plane(1, ClippingPlane(-Z, z_high_mm))).
                    oc->set_range_and_pos(Vec3d(0., 0., 1.), z_high_eff, m_gizmo_clip_ratio);
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
