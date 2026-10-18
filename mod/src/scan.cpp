// Memory scans for finding the game's data (a cheat-engine style float search, run inside the process):
//   scan start [from] [to]      every float of the committed guest pages in range, remembered
//   scan changed <lo> <hi>      kept: those that moved by lo..hi since the last step
//   scan same [eps]             kept: those that did not move
//   scan up / scan down         kept: those that grew / shrank
//   scan list [n]               the candidates: address=value
//   mats [from] [to]            4x4 float matrices with an orthonormal 3x3 and a w column 0,0,0,1: address and position
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "mod.h"

namespace mod::scan {

namespace {
std::mutex g_lock;
std::vector<uint32_t> g_at;
std::vector<float> g_value;

bool Finite(float f) { return std::isfinite(f) && std::fabs(f) < 1e7f; }
}  // namespace

std::string Run(const std::vector<std::string>& a) {
    std::lock_guard lock(g_lock);
    char line[64];
    std::string sub = a.size() > 1 ? a[1] : "";
    if (sub == "start") {
        uint32_t from = a.size() > 2 ? std::strtoul(a[2].c_str(), nullptr, 16) : 0x00590000;
        uint32_t to = a.size() > 3 ? std::strtoul(a[3].c_str(), nullptr, 16) : 0x08000000;
        g_at.clear(), g_value.clear();
        for (uint32_t page = from & ~0xFFFu; page < to; page += 0x1000) {
            if (!Mapped(page, 0x1000)) continue;
            for (uint32_t at = page; at < page + 0x1000; at += 4) {
                float f = RF(at);
                if (Finite(f) && f != 0.0f) g_at.push_back(at), g_value.push_back(f);
            }
        }
        return "candidates " + std::to_string(g_at.size());
    }
    if (sub == "changed" || sub == "same" || sub == "up" || sub == "down") {
        float lo = a.size() > 2 ? float(std::atof(a[2].c_str())) : 0.0f;
        float hi = a.size() > 3 ? float(std::atof(a[3].c_str())) : 1e9f;
        float eps = sub == "same" && a.size() > 2 ? lo : 1e-4f;
        size_t k = 0;
        uint32_t page = 1;  // the candidates are in address order: one mapping check a page
        bool mapped = false;
        for (size_t i = 0; i < g_at.size(); i++) {
            if ((g_at[i] & ~0xFFFu) != page) page = g_at[i] & ~0xFFFu, mapped = Mapped(page, 0x1000);
            if (!mapped) continue;
            float now = RF(g_at[i]);
            if (!Finite(now)) continue;
            float d = now - g_value[i];
            bool keep = sub == "changed" ? std::fabs(d) >= lo && std::fabs(d) <= hi
                        : sub == "same"  ? std::fabs(d) <= eps
                        : sub == "up"    ? d > 1e-4f
                                         : d < -1e-4f;
            if (keep) g_at[k] = g_at[i], g_value[k] = now, k++;
        }
        g_at.resize(k), g_value.resize(k);
        return "candidates " + std::to_string(k);
    }
    if (sub == "list") {
        size_t n = a.size() > 2 ? std::strtoul(a[2].c_str(), nullptr, 10) : 50;
        std::string out;
        for (size_t i = 0; i < g_at.size() && i < n; i++) {
            std::snprintf(line, sizeof line, "%08X=%.3f\n", g_at[i], double(RF(g_at[i])));
            out += line;
        }
        return out.empty() ? "none" : out;
    }
    return "error scan start|changed|same|up|down|list";
}

std::string Matrices(const std::vector<std::string>& a) {
    // right, up, forward rows (unit, orthogonal) then the position: packed (12 byte rows) or Vector4 rows (16 bytes,
    // the w of each row whatever it is), on 4 byte alignment
    uint32_t from = a.size() > 1 ? std::strtoul(a[1].c_str(), nullptr, 16) : 0x80000000;
    uint32_t to = a.size() > 2 ? std::strtoul(a[2].c_str(), nullptr, 16) : 0x84000000;
    std::string out;
    char line[128];
    int hits = 0;
    auto unit = [](const float* r) { return std::fabs(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] - 1.0f) < 0.002f; };
    auto dot = [](const float* a, const float* b) { return std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) < 0.002f; };
    for (uint32_t page = from & ~0xFFFu; page < to && hits < 800; page += 0x1000) {
        if (!Mapped(page, 0x2000)) continue;
        for (uint32_t at = page; at < page + 0x1000 && hits < 800; at += 4) {
            const float* m = reinterpret_cast<const float*>(HostPtr(at));
            for (int stride : {3, 4}) {
                const float *r = m, *u = m + stride, *f = m + 2 * stride, *p = m + 3 * stride;
                if (!unit(r) || !unit(u) || !unit(f) || !dot(r, u) || !dot(r, f) || !dot(u, f)) continue;
                if (!Finite(p[0]) || !Finite(p[1]) || !Finite(p[2]) || std::fabs(p[0]) + std::fabs(p[2]) < 1.0f) continue;
                if (r[0] == 1.0f && u[1] == 1.0f && f[2] == 1.0f) continue;  // identity rotations: statics, not cars
                std::snprintf(line, sizeof line, "%08X s%d pos=%.2f,%.2f,%.2f fwd=%.2f,%.2f,%.2f\n", at, stride * 4,
                              double(p[0]), double(p[1]), double(p[2]), double(f[0]), double(f[1]), double(f[2]));
                out += line;
                hits++;
                at += stride * 16 - 4;
                break;
            }
        }
    }
    return out.empty() ? "none" : out;
}

}  // namespace mod::scan
