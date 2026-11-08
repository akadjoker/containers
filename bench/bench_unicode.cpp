#include <ct/unicode.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{
    namespace uc = ct::unicode;

    std::uint32_t rng_state = 12345;

    std::uint32_t rnd()
    {
        rng_state = rng_state * 1664525u + 1013904223u;
        return rng_state >> 8;
    }

    std::vector<char16_t> make_text(std::size_t n, int accent_every, bool decomposed)
    {
        std::vector<char16_t> out;
        out.reserve(n + 4);
        while (out.size() < n)
        {
            std::size_t len = 1 + rnd() % 9;
            for (std::size_t i = 0; i < len; ++i)
                out.push_back(static_cast<char16_t>('a' + rnd() % 26));
            if (accent_every && rnd() % accent_every == 0)
            {
                if (decomposed)
                {
                    out.push_back('e');
                    out.push_back(0x301);
                }
                else
                    out.push_back(0xE9);
            }
            out.push_back(' ');
        }
        return out;
    }

    void report(const char *name, double ms, std::size_t units)
    {
        std::printf("%-44s %9.2f ms  %8.1f M units/s\n", name, ms, units / ms / 1000.0);
    }
}

int main()
{
    const std::size_t N = 10u << 20;
    std::vector<char16_t> nfc = make_text(N, 20, false);
    std::vector<char16_t> mixed = make_text(N, 20, true);
    ct::Vector<char16_t> out;

    report("is_normalized NFC, 10M composed", bench::time_best_ms([&] { bench::sink += uc::is_normalized(uc::NFC, nfc.data(), nfc.size()); }, 5), nfc.size());
    report("is_normalized NFD, 10M with e+U+0301", bench::time_best_ms([&] { bench::sink += uc::is_normalized(uc::NFD, mixed.data(), mixed.size()); }, 5), mixed.size());
    report("is_normalized NFC, 10M with e+U+0301 (false)", bench::time_best_ms([&] { bench::sink += uc::is_normalized(uc::NFC, mixed.data(), mixed.size()); }, 5), mixed.size());
    report("normalize NFC, 10M already composed", bench::time_best_ms([&] { uc::normalize(uc::NFC, nfc.data(), nfc.size(), out); bench::sink += out.size(); }, 5), nfc.size());
    report("normalize NFC, 10M with e+U+0301", bench::time_best_ms([&] { uc::normalize(uc::NFC, mixed.data(), mixed.size(), out); bench::sink += out.size(); }, 5), mixed.size());
    report("normalize NFD, 10M composed accents", bench::time_best_ms([&] { uc::normalize(uc::NFD, nfc.data(), nfc.size(), out); bench::sink += out.size(); }, 5), nfc.size());
    report("normalize NFKD, 10M composed accents", bench::time_best_ms([&] { uc::normalize(uc::NFKD, nfc.data(), nfc.size(), out); bench::sink += out.size(); }, 5), nfc.size());

    std::vector<unsigned char> l1(N);
    for (std::size_t i = 0; i < N; ++i)
        l1[i] = static_cast<unsigned char>(rnd() % 8 == 0 ? 0xC0 + rnd() % 0x3F : 'a' + rnd() % 26);
    for (std::size_t i = 0; i < N; ++i)
        if (l1[i] == 0xF7 || l1[i] == 0xFF || l1[i] == 0xB5)
            l1[i] = 'x';
    ct::Vector<unsigned char> o8;
    ct::Vector<char16_t> o16;
    report("to_upper Latin-1, 10M", bench::time_best_ms([&] { bench::sink += uc::to_upper(l1.data(), l1.size(), o8, o16); }, 5), N);
    report("to_lower Latin-1, 10M", bench::time_best_ms([&] { uc::to_lower(l1.data(), l1.size(), o8); bench::sink += o8.size(); }, 5), N);

    std::vector<char16_t> greek(N);
    for (std::size_t i = 0; i < N; ++i)
        greek[i] = static_cast<char16_t>(rnd() % 6 == 0 ? ' ' : 0x3B1 + rnd() % 25);
    report("to_upper UTF-16 Greek, 10M", bench::time_best_ms([&] { uc::to_upper(greek.data(), greek.size(), out); bench::sink += out.size(); }, 5), N);
    report("to_lower UTF-16 Greek, 10M", bench::time_best_ms([&] { uc::to_lower(greek.data(), greek.size(), out); bench::sink += out.size(); }, 5), N);
    for (std::size_t i = 0; i < N; ++i)
        if (greek[i] >= 0x3B1)
            greek[i] -= 32;
    report("to_lower UTF-16 capital Greek, 10M", bench::time_best_ms([&] { uc::to_lower(greek.data(), greek.size(), out); bench::sink += out.size(); }, 5), N);

    std::vector<char16_t> cjk(N);
    for (std::size_t i = 0; i < N; ++i)
        cjk[i] = static_cast<char16_t>(0x4E00 + rnd() % 20000);
    report("to_upper UTF-16 CJK, 10M", bench::time_best_ms([&] { uc::to_upper(cjk.data(), cjk.size(), out); bench::sink += out.size(); }, 5), N);
    report("normalize NFC UTF-16 CJK, 10M", bench::time_best_ms([&] { uc::normalize(uc::NFC, cjk.data(), cjk.size(), out); bench::sink += out.size(); }, 5), N);
    return 0;
}
