#ifndef slic3r_GUI_PhrozenToolRemapApply_hpp_
#define slic3r_GUI_PhrozenToolRemapApply_hpp_

// Phrozen Orca FDM —— G-code 工具號重映射的「檔案編排層」。
//
// 設計（修正後）：永不修改原檔。
//   - 原檔（plate 的 tmp gcode）剛切片完仍被 OrcaSlicer 預覽持有（memory-mapped）。
//     在 Windows 上對其 rename-replace 會 ERROR_ACCESS_DENIED(5)，原地覆寫亦不可行。
//   - 因此：讀原檔（映射允許 share-read）→ 寫一份全新「旁路檔」<gcode>.remapped.gcode，
//     原檔全程位元不變。上傳由上層重導到旁路檔（send_gcode_legacy override）。
//   - 冪等免費：每次都讀「未變動的原檔」、重生旁路檔，連續重選不疊加。
//   - 取消／失敗：刪除旁路檔，原檔不動。
//
// 注意：演算法本身在 PhrozenToolRemap.hpp（純函式）。本檔僅做路徑組裝與失敗清理。

#include "PhrozenToolRemap.hpp"

#include <string>
#include <map>
#include <atomic>
#include <boost/nowide/cstdio.hpp> // boost::nowide::remove（UTF-8 安全）

namespace Slic3r { namespace GUI {

// 旁路輸出檔路徑（與原檔同目錄；原檔永不被覆蓋）。
inline std::string phrozen_remapped_path(const std::string& gcode_path) { return gcode_path + ".remapped.gcode"; }

struct PhrozenApplyResult
{
    bool        ok       = false;
    bool        canceled = false;
    std::string error;
};

// 讀「原檔」(src，只讀、全程不變) → 寫「旁路檔」(dst)。
//   - 成功：dst 為重映射後的 G-code。
//   - 取消：刪除 dst，回 canceled；原檔不動。
//   - 失敗：刪除 dst，回 error；原檔不動。
inline PhrozenApplyResult phrozen_write_remapped_gcode(const std::string&        src_gcode_path,
                                                       const std::string&        dst_remapped_path,
                                                       const std::map<int, int>& remap,
                                                       const std::atomic<bool>*  cancel = nullptr)
{
    PhrozenApplyResult r;

    const PhrozenRemapFileResult fr = phrozen_remap_tool_gcode_file(src_gcode_path, dst_remapped_path, remap, cancel);
    if (fr.canceled) {
        boost::nowide::remove(dst_remapped_path.c_str());
        r.canceled = true;
        return r;
    }
    if (!fr.ok) {
        boost::nowide::remove(dst_remapped_path.c_str());
        r.error = fr.error;
        return r;
    }
    r.ok = true;
    return r;
}

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PhrozenToolRemapApply_hpp_