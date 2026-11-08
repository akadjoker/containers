#include <ct/jsregex.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{

    std::uint32_t rng_state = 12345;

    std::uint32_t rnd()
    {
        rng_state = rng_state * 1664525u + 1013904223u;
        return rng_state >> 8;
    }

    std::string make_words(std::size_t bytes)
    {
        std::string out;
        while (out.size() < bytes)
        {
            std::size_t len = 1 + rnd() % 9;
            for (std::size_t i = 0; i < len; ++i)
                out += static_cast<char>('a' + rnd() % 26);
            std::size_t sp = 1 + rnd() % 3;
            for (std::size_t i = 0; i < sp; ++i)
                out += (rnd() % 8 == 0) ? '\n' : ' ';
        }
        return out;
    }

    std::string make_numbers(std::size_t bytes)
    {
        std::string out;
        while (out.size() < bytes)
        {
            out += "item" + std::to_string(rnd() % 1000) + ": ";
            out += std::to_string(rnd() % 100000) + "-" + std::to_string(rnd() % 100000) + " ok; ";
        }
        return out;
    }

    template <typename Unit>
    std::uint64_t count_matches(const ct::JsRegex &re, const std::vector<Unit> &text)
    {
        ct::JsCaptures caps;
        std::size_t pos = 0;
        std::uint64_t n = 0;
        while (pos <= text.size() && re.exec(text.data(), text.size(), pos, false, caps) == ct::JsRegex::Matched)
        {
            ++n;
            pos = caps.end() == caps.start() ? caps.end() + 1 : caps.end();
        }
        return n;
    }

    template <typename F>
    void report(const char *name, F &&f, int reps = 3)
    {
        std::uint64_t result = 0;
        double ms = bench::time_best_ms([&] { result = f(); }, reps);
        std::printf("%-44s %10.3f ms   (%llu)\n", name, ms, static_cast<unsigned long long>(result));
        bench::sink += result;
    }

} // namespace

int main()
{
    std::string words = make_words(10u << 20);
    std::vector<unsigned char> w8(words.begin(), words.end());
    std::vector<char16_t> w16(words.begin(), words.end());
    std::string nums = make_numbers(10u << 20);
    std::vector<unsigned char> n8(nums.begin(), nums.end());

    ct::JsRegex space = ct::JsRegex::compile(ct::StringView("\\s+"), ct::JsRegex::Global);
    ct::JsRegex pair = ct::JsRegex::compile(ct::StringView("(\\d+)-(\\d+)"), ct::JsRegex::Global);
    ct::JsRegex word = ct::JsRegex::compile(ct::StringView("\\b[a-z]+\\b"), ct::JsRegex::Global | ct::JsRegex::IgnoreCase);
    ct::JsRegex email = ct::JsRegex::compile(ct::StringView("^[\\w.+-]+@[\\w-]+\\.[\\w.-]+$"), 0);

    report("split /\\s+/ over 10 MB (latin1)", [&] { return count_matches(space, w8); });
    report("split /\\s+/ over 10 MB (utf16)", [&] { return count_matches(space, w16); });
    report("/(\\d+)-(\\d+)/g over 10 MB (latin1)", [&] { return count_matches(pair, n8); });
    report("/\\b[a-z]+\\b/gi over 10 MB (latin1)", [&] { return count_matches(word, w8); });

    std::vector<std::string> emails;
    emails.reserve(1000000);
    for (int i = 0; i < 1000000; ++i)
    {
        std::string e = "user" + std::to_string(rnd() % 100000) + (i % 7 == 0 ? ".x+tag" : "") + "@site" +
                        std::to_string(i % 50) + (i % 13 == 0 ? "" : ".example.com");
        emails.push_back(e);
    }
    report("email regex over 1 M strings (latin1)", [&] {
        std::uint64_t n = 0;
        ct::JsCaptures caps;
        for (const std::string &e : emails)
            n += email.exec(reinterpret_cast<const unsigned char *>(e.data()), e.size(), 0, false, caps) == ct::JsRegex::Matched;
        return n;
    });

    ct::JsRegex cat = ct::JsRegex::compile(ct::StringView("(?:a+)+b"), 0);
    std::vector<unsigned char> as(5000, 'a');
    report("(?:a+)+b over 5 k 'a' (memo)", [&] { return count_matches(cat, as); });
    return 0;
}
