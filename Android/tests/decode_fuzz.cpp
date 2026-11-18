// decode_fuzz.cpp — proves the PNG decoder always terminates.
//
// On the device a short asset read handed the decoder a truncated PNG while it
// was initialising on the GL thread; the result was a black screen with no
// crash and no log line. This hammers decodePng with every truncation and
// byte-flip of the shipped icons and reports the slowest call, so a regression
// (an unbounded loop) shows up here instead of on a phone.
//
//   g++ -std=c++17 -O2 -I <src> decode_fuzz.cpp <src>/png.cpp -o decode_fuzz
//   ./decode_fuzz <assets>/icons/*.png
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "png.h"
static bool readFile(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    if (std::fread(out.data(), 1, (size_t)n, f) != (size_t)n) { std::fclose(f); return false; }
    std::fclose(f); return true;
}
int main(int argc, char** argv) {
    int files = 0, runs = 0;
    double worst = 0; std::string worstName;
    for (int a = 1; a < argc; ++a) {
        std::vector<uint8_t> orig;
        if (!readFile(argv[a], orig) || orig.size() < 40) continue;
        ++files;
        for (size_t cut = 8; cut <= orig.size(); cut += 1 + orig.size() / 400) {
            std::vector<uint8_t> part(orig.begin(), orig.begin() + (long)cut);
            pdb::ImageRGBA img; std::string err;
            auto t0 = std::chrono::steady_clock::now();
            pdb::decodePng(part, img, err);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > worst) { worst = ms; worstName = std::string(argv[a]) + " cut=" + std::to_string(cut); }
            ++runs;
        }
        for (size_t i = 0; i < orig.size(); i += 1 + orig.size() / 400) {
            std::vector<uint8_t> bad = orig; bad[i] ^= 0x5A;
            pdb::ImageRGBA img; std::string err;
            auto t0 = std::chrono::steady_clock::now();
            pdb::decodePng(bad, img, err);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > worst) { worst = ms; worstName = std::string(argv[a]) + " flip=" + std::to_string(i); }
            ++runs;
        }
    }
    std::printf("%d files, %d decode calls, slowest %.3f ms (%s)\n", files, runs, worst, worstName.c_str());
    return 0;
}
