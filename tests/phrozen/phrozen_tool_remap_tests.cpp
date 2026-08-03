// Phrozen Orca FDM —— G-code 工具號重映射 單元測試（自帶極簡 harness，無 Catch2 相依）。
// 建置並執行：tests\phrozen\build_and_run_tests.bat（於 repo 根目錄執行）
// 覆蓋 specs/gcode-tool-remap-rewrite 的各場景。

#include "../../src/slic3r/GUI/PhrozenGUI/PhrozenToolRemap.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <map>

using Slic3r::GUI::phrozen_remap_tool_line;
using Slic3r::GUI::phrozen_remap_tool_gcode_stream;

static int g_failures = 0;
static int g_checks   = 0;

static void check_eq(const std::string& got, const std::string& want, const std::string& name)
{
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::cerr << "[FAIL] " << name << "\n   expected: [" << want << "]\n   got     : [" << got << "]\n";
    } else {
        std::cout << "[ ok ] " << name << "\n";
    }
}

static std::string line_remap(const std::string& in, const std::map<int, int>& m)
{
    return phrozen_remap_tool_line(in, m);
}

int main()
{
    const std::map<int, int> swap02 = { {0, 2}, {2, 0} }; // T0<->T2 互換
    const std::map<int, int> m0to2  = { {0, 2} };
    const std::map<int, int> m1to3  = { {1, 3} };
    const std::map<int, int> m0to3  = { {0, 3} };
    const std::map<int, int> ident  = { {0, 0}, {1, 1}, {2, 2}, {3, 3} };

    // 1. 裸工具切換行：T0 -> T2
    check_eq(line_remap("T0", m0to2), "T2", "bare T0 -> T2");

    // 2. 裸工具切換行保留前置空白與行尾註解
    check_eq(line_remap("T0 ; change tool", m0to3), "T3 ; change tool", "bare T0 with trailing comment -> T3");
    check_eq(line_remap("   T1", m1to3), "   T3", "bare T1 with leading spaces preserved");

    // 3. T0<->T2 互換：分別計算，無二次覆蓋
    check_eq(line_remap("T0", swap02), "T2", "swap: T0 -> T2");
    check_eq(line_remap("T2", swap02), "T0", "swap: T2 -> T0");

    // 4. 帶工具號的預熱／溫度指令（不動 S）
    check_eq(line_remap("M104 S210 T0", m0to2), "M104 S210 T2", "M104 S210 T0 -> M104 S210 T2");
    check_eq(line_remap("M109 T1 S240", m1to3), "M109 T3 S240", "M109 T1 S240 (param order) -> M109 T3 S240");

    // 5. 不帶工具號的溫度指令不變
    check_eq(line_remap("M104 S210", m0to2), "M104 S210", "M104 S210 (no T) unchanged");
    check_eq(line_remap("M109 S60", m0to2),  "M109 S60",  "M109 S60 (no T) unchanged");

    // 6. 不誤傷移動指令與其他內容
    check_eq(line_remap("G1 X10 Y20 E0.5", swap02), "G1 X10 Y20 E0.5", "G1 move line unchanged");

    // 7. 純註解（含字母 T0）不被誤改
    check_eq(line_remap("; Tool change for part T0", swap02), "; Tool change for part T0", "pure comment unchanged");

    // 8. 不誤傷其他 M 指令中的 T（例如 M204 不是溫度指令，本規則不處理）
    check_eq(line_remap("M204 P500 T0", swap02), "M204 P500 T0", "M204 line not treated as temperature command");

    // 9. 多位數工具號
    check_eq(line_remap("T10", { {10, 3} }), "T3", "multi-digit T10 -> T3");

    // 10. 對映射表中不存在的工具號維持原值
    check_eq(line_remap("T3", m0to2), "T3", "unmapped T3 stays T3");

    // 11. 串流：保留 CRLF 與 LF 混用，且最後一行無換行也保留
    {
        const std::string input =
            "M104 S210 T0\r\n"   // CRLF
            "T0\n"               // LF
            "G1 X1 Y2\r\n"
            "T2";                // 最後一行無換行
        const std::string expect =
            "M104 S210 T2\r\n"
            "T2\n"
            "G1 X1 Y2\r\n"
            "T0";
        std::istringstream is(input);
        std::ostringstream os;
        const bool done = phrozen_remap_tool_gcode_stream(is, os, swap02, nullptr);
        check_eq(os.str(), expect, "stream: CRLF/LF preserved, swap, no trailing newline");
        check_eq(done ? "1" : "0", "1", "stream completed (not canceled)");
    }

    // 12. 串流：恆等映射不改變內容
    {
        const std::string input = "T0\nM104 S210 T1\nG28\n";
        std::istringstream is(input);
        std::ostringstream os;
        phrozen_remap_tool_gcode_stream(is, os, ident, nullptr);
        check_eq(os.str(), input, "stream: identity map leaves content unchanged");
    }

    std::cout << "\n=== " << (g_checks - g_failures) << "/" << g_checks << " checks passed ===\n";
    if (g_failures != 0) {
        std::cerr << g_failures << " CHECK(S) FAILED\n";
        return 1;
    }
    std::cout << "ALL PASSED\n";
    return 0;
}