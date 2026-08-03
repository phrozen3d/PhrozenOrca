// Phrozen Orca FDM —— 旁路檔重映射（不覆蓋原檔）單元測試（自帶極簡 harness）。
// 編譯：cl /utf-8 /EHsc /std:c++17 /I<boost-1_84> phrozen_tool_remap_apply_tests.cpp
// 覆蓋 specs/gcode-tool-remap-rewrite 之「寫入旁路檔、原檔位元不變、冪等、取消刪旁路檔」場景。

#include "../../src/slic3r/GUI/PhrozenGUI/PhrozenToolRemapApply.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <filesystem>
#include <atomic>

namespace fs = std::filesystem;
using namespace Slic3r::GUI;

static int g_failures = 0;
static int g_checks   = 0;

static void check(bool cond, const std::string& name)
{
    ++g_checks;
    if (!cond) { ++g_failures; std::cerr << "[FAIL] " << name << "\n"; }
    else       { std::cout << "[ ok ] " << name << "\n"; }
}

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

static void write_file(const std::string& path, const std::string& content)
{
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o.write(content.data(), (std::streamsize) content.size());
}

static std::string read_file(const std::string& path)
{
    std::ifstream i(path, std::ios::binary);
    std::ostringstream ss; ss << i.rdbuf();
    return ss.str();
}

int main()
{
    fs::path dir = fs::temp_directory_path() / "phrozen_apply_test";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);

    const std::string gcode    = (dir / "plate.gcode").string();
    const std::string remapped = phrozen_remapped_path(gcode);

    // 原檔內容：含裸工具與預熱指令，CRLF。
    const std::string pristine =
        "M104 S210 T0\r\n"
        "T0\r\n"
        "G1 X1 Y2\r\n"
        "M104 S215 T2\r\n"
        "T2\r\n";
    write_file(gcode, pristine);

    check_eq(phrozen_remapped_path(gcode), gcode + ".remapped.gcode", "remapped path = <gcode>.remapped.gcode");

    // --- 場景 1：寫旁路檔 {0:2,2:0}，原檔位元不變 ---
    {
        const std::map<int, int> swap02 = { {0, 2}, {2, 0} };
        PhrozenApplyResult r = phrozen_write_remapped_gcode(gcode, remapped, swap02, nullptr);
        check(r.ok && !r.canceled, "write#1 swap02 ok");
        check_eq(read_file(gcode), pristine, "write#1 ORIGINAL unchanged (byte-for-byte)");

        const std::string expect1 =
            "M104 S210 T2\r\n"
            "T2\r\n"
            "G1 X1 Y2\r\n"
            "M104 S215 T0\r\n"
            "T0\r\n";
        check_eq(read_file(remapped), expect1, "write#1 sidecar swapped (T0<->T2, preheat incl, CRLF kept)");
    }

    // --- 場景 2：以不同映射 {0:3} 再寫一次，皆從「原檔」算起，不疊加 ---
    {
        const std::map<int, int> m0to3 = { {0, 3} };
        PhrozenApplyResult r = phrozen_write_remapped_gcode(gcode, remapped, m0to3, nullptr);
        check(r.ok && !r.canceled, "write#2 {0:3} ok");
        check_eq(read_file(gcode), pristine, "write#2 ORIGINAL still unchanged");

        const std::string expect2 =
            "M104 S210 T3\r\n"
            "T3\r\n"
            "G1 X1 Y2\r\n"
            "M104 S215 T2\r\n"
            "T2\r\n";
        check_eq(read_file(remapped), expect2, "write#2 sidecar computed from ORIGINAL, NOT stacked on write#1");
    }

    // --- 場景 3：取消 → 旁路檔被刪、原檔不變 ---
    {
        // 先確保有一個既有 sidecar
        write_file(remapped, "STALE");
        const std::string before_gcode = read_file(gcode);
        std::atomic<bool> cancel{ true };
        const std::map<int, int> m = { {0, 1} };
        PhrozenApplyResult r = phrozen_write_remapped_gcode(gcode, remapped, m, &cancel);
        check(r.canceled && !r.ok, "write#3 reports canceled");
        check(!fs::exists(remapped), "write#3 sidecar removed on cancel");
        check_eq(read_file(gcode), before_gcode, "write#3 ORIGINAL unchanged after cancel");
    }

    // --- 場景 4：來源不存在 → 失敗、不留旁路檔 ---
    {
        const std::string missing = (dir / "nope.gcode").string();
        const std::string missing_out = phrozen_remapped_path(missing);
        const std::map<int, int> m = { {0, 1} };
        PhrozenApplyResult r = phrozen_write_remapped_gcode(missing, missing_out, m, nullptr);
        check(!r.ok && !r.canceled && !r.error.empty(), "write#4 missing source -> error");
        check(!fs::exists(missing_out), "write#4 no sidecar left on error");
    }

    fs::remove_all(dir, ec);

    std::cout << "\n=== " << (g_checks - g_failures) << "/" << g_checks << " checks passed ===\n";
    if (g_failures != 0) { std::cerr << g_failures << " CHECK(S) FAILED\n"; return 1; }
    std::cout << "ALL PASSED\n";
    return 0;
}