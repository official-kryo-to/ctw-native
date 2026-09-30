// Headless check for the game's MP3s: header info, full length, and loudness of the first 10 seconds.
// Exit code 0 only if every file decodes and none of them is silent.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#define MINIMP3_IMPLEMENTATION
#include <minimp3_ex.h>

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data";
    int bad = 0, n = 0;
    double total = 0;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != ".mp3") continue;
        ++n;
        mp3dec_ex_t d;
        if (mp3dec_ex_open(&d, e.path().string().c_str(), MP3D_SEEK_TO_SAMPLE) != 0) {
            printf("FAIL open  %s\n", e.path().filename().string().c_str());
            ++bad;
            continue;
        }
        double secs = (double)d.samples / d.info.channels / d.info.hz;
        size_t want = (size_t)d.info.hz * d.info.channels * 10;
        std::vector<mp3d_sample_t> buf(want);
        size_t got = mp3dec_ex_read(&d, buf.data(), want);
        double sum = 0;
        for (size_t i = 0; i < got; ++i) sum += (double)buf[i] * buf[i];
        double rmsDb = got ? 20 * log10(sqrt(sum / got) / 32768.0 + 1e-12) : -999;
        bool expectSilent = e.path().stem() == "silence";   // the game ships an intentionally silent track
        bool silent = rmsDb < -60 && !expectSilent;
        if (silent || got == 0) ++bad;
        total += secs;
        printf("%-4s %-30s %5d Hz %d ch %7.1f s  rms %6.1f dB\n", silent ? "SIL" : "ok",
               e.path().filename().string().c_str(), d.info.hz, d.info.channels, secs, rmsDb);
        mp3dec_ex_close(&d);
    }
    printf("%d files, %.1f minutes, %d problems\n", n, total / 60, bad);
    return bad ? 1 : 0;
}
