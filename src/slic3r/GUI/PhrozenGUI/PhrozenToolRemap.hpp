#ifndef slic3r_GUI_PhrozenToolRemap_hpp_
#define slic3r_GUI_PhrozenToolRemap_hpp_

// Phrozen Orca FDM —— G-code 工具號重映射（純函式、無 wxWidgets 相依，可單元測試）。
//
// 設計：單次前向掃描 + 不可變查表。
//   - 讀「來源」、寫「目的」,每個工具號 token 僅查一次不可變映射表，輸出到另一個緩衝/串流。
//   - 絕不使用「全域連續多次字串取代」,因此 T0<->T2 互換不會發生二次覆蓋污染。
//
// 重寫範圍（方案 Y）：
//   (a) 裸工具切換行：  ^\s*T<digits>\s*(; 註解)?$
//   (b) 帶工具號的溫度指令：M104 / M109 行內的 T<digits> 參數（不更動 S 溫度或其他參數）。
// 其餘行（座標、進給、純註解…）一律原樣輸出。
//
// 行尾風格（LF / CRLF）與「最後一行是否帶換行」皆完整保留。

#include <string>
#include <map>
#include <atomic>
#include <cctype>
#include <istream>
#include <ostream>
#include <boost/nowide/fstream.hpp>

namespace Slic3r { namespace GUI {

// 不可變查表：找不到的來源工具號維持原值（恆等）。
inline int phrozen_lookup_tool(const std::map<int, int>& remap, int src)
{
    auto it = remap.find(src);
    return (it == remap.end()) ? src : it->second;
}

inline bool phrozen_is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool phrozen_is_space(char c) { return c == ' ' || c == '\t'; }

// 在 M104/M109 的「程式碼部分」中，單次前向掃描替換獨立的 T<digits> 參數 token。
// 讀 in、寫 out（新緩衝），每個 token 只查一次 remap，避免二次覆蓋。
inline std::string phrozen_replace_m_tparams(const std::string& code, const std::map<int, int>& remap)
{
    std::string out;
    out.reserve(code.size() + 4);
    const size_t n = code.size();
    for (size_t i = 0; i < n;) {
        const char c = code[i];
        const bool boundary = (i == 0) || phrozen_is_space(code[i - 1]);
        if (c == 'T' && boundary && (i + 1 < n) && phrozen_is_digit(code[i + 1])) {
            size_t j = i + 1;
            while (j < n && phrozen_is_digit(code[j])) ++j;
            // T<digits> 後須為 token 邊界（行尾或空白）才算工具號參數。
            if (j == n || phrozen_is_space(code[j])) {
                const int src = std::stoi(code.substr(i + 1, j - (i + 1)));
                out.push_back('T');
                out += std::to_string(phrozen_lookup_tool(remap, src));
                i = j;
                continue;
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// 判斷程式碼部分（自 a 起）是否為 M104 或 M109 指令。
inline bool phrozen_is_m104_m109(const std::string& code, size_t a)
{
    if (code.size() - a < 4) return false;
    if (code.compare(a, 4, "M104") != 0 && code.compare(a, 4, "M109") != 0) return false;
    const size_t after = a + 4;
    // 後面必須是行尾或非數字（排除 M1041 之類）。
    return (after == code.size()) || !phrozen_is_digit(code[after]);
}

// 轉換單一邏輯行（不含行尾終止符）。回傳重寫後的內容。
inline std::string phrozen_remap_tool_line(const std::string& line, const std::map<int, int>& remap)
{
    // 切出註解（gcode 以第一個 ';' 起為註解），僅處理程式碼部分。
    const size_t semi    = line.find(';');
    const std::string code    = (semi == std::string::npos) ? line : line.substr(0, semi);
    const std::string comment = (semi == std::string::npos) ? std::string() : line.substr(semi);

    const size_t a = code.find_first_not_of(" \t");
    if (a == std::string::npos) return line; // 空行或純註解，原樣保留

    // (a) 裸工具切換行：T<digits> 後僅餘空白。
    if (code[a] == 'T' && (a + 1 < code.size()) && phrozen_is_digit(code[a + 1])) {
        size_t j = a + 1;
        while (j < code.size() && phrozen_is_digit(code[j])) ++j;
        bool only_space_after = true;
        for (size_t k = j; k < code.size(); ++k) {
            if (!phrozen_is_space(code[k])) { only_space_after = false; break; }
        }
        if (only_space_after) {
            const int src = std::stoi(code.substr(a + 1, j - (a + 1)));
            std::string out = code.substr(0, a) + "T" + std::to_string(phrozen_lookup_tool(remap, src)) + code.substr(j);
            return out + comment;
        }
    }

    // (b) M104 / M109 的 T 參數。
    if (phrozen_is_m104_m109(code, a)) {
        return phrozen_replace_m_tparams(code, remap) + comment;
    }

    // 其餘行不動。
    return line;
}

// 串流式重寫：逐行讀取並保留各行的行尾終止符（LF / CRLF）與最後一行是否帶換行。
// cancel 非空且為 true 時，於行邊界中止並回傳 false（呼叫端負責丟棄半套輸出）。
inline bool phrozen_remap_tool_gcode_stream(std::istream&               in,
                                            std::ostream&               out,
                                            const std::map<int, int>&   remap,
                                            const std::atomic<bool>*    cancel = nullptr)
{
    std::string line;
    for (;;) {
        const int ch = in.get();
        if (ch == EOF) {
            if (!line.empty()) out << phrozen_remap_tool_line(line, remap); // 最後一行無換行
            break;
        }
        if (ch == '\n') {
            std::string term = "\n";
            if (!line.empty() && line.back() == '\r') { line.pop_back(); term = "\r\n"; }
            out << phrozen_remap_tool_line(line, remap) << term;
            line.clear();
            if (cancel && cancel->load()) return false;
        } else {
            line.push_back(static_cast<char>(ch));
        }
    }
    return true;
}

struct PhrozenRemapFileResult
{
    bool        ok       = false;
    bool        canceled = false;
    std::string error;
};

// 檔案層：讀 src、寫 dst（二進位以保留行尾）。原子覆蓋／備份由上層（階段 3）處理。
inline PhrozenRemapFileResult phrozen_remap_tool_gcode_file(const std::string&        src_path,
                                                            const std::string&        dst_path,
                                                            const std::map<int, int>& remap,
                                                            const std::atomic<bool>*  cancel = nullptr)
{
    PhrozenRemapFileResult r;
    // 以 boost::nowide 開檔，支援 Windows 上的 UTF-8 路徑（窄字串會被 MSVC 當 ANSI）。
    boost::nowide::ifstream in(src_path.c_str(), std::ios::binary);
    if (!in) { r.error = "cannot open source gcode: " + src_path; return r; }
    boost::nowide::ofstream out(dst_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) { r.error = "cannot open dest gcode: " + dst_path; return r; }

    const bool completed = phrozen_remap_tool_gcode_stream(in, out, remap, cancel);
    out.flush();
    if (!completed) { r.canceled = true; return r; }
    if (!out.good()) { r.error = "write error on dest gcode: " + dst_path; return r; }
    r.ok = true;
    return r;
}

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PhrozenToolRemap_hpp_