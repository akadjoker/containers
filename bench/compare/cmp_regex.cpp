// ct::Regex contra std::regex e RE2 (Ronda 3): as mesmas expressoes e o mesmo texto de bench/bench_regex.cpp (1,5 MB de log),
// tempo de CPU do processo, o minimo de 5 repeticoes. Esta pasta e a excecao declarada ao "sem std".
// uso: cmp_regex   (so compila se houver <re2/re2.h>; ver tools/bench_compare.sh regex)
#include <ct/regex.hpp>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <regex>
#include <string>

#if __has_include(<re2/re2.h>)
#include <re2/re2.h>
#define CMP_RE2 1
#endif

static double cpu_ms()
{
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1e3 + static_cast<double>(ts.tv_nsec) / 1e6;
}

template <class F> static double best_ms(F &&f, int reps = 5)
{
    double best = 1e300;
    for (int r = 0; r < reps; ++r)
    {
        const double t0 = cpu_ms();
        f();
        const double t1 = cpu_ms();
        if (t1 - t0 < best)
            best = t1 - t0;
    }
    return best;
}

static volatile std::uint64_t g_sink = 0;

static std::string make_log(int lines)
{
    std::string out;
    const char *hosts[] = {"alpha", "beta", "gamma", "delta"};
    for (int i = 0; i < lines; ++i)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "2026-09-12 10:%02d:%02d host=%s ip=192.168.%d.%d user=user%d@site%d.com latency=%d.%03d ms status=%d\n",
                      (i / 60) % 60, i % 60, hosts[i & 3], (i * 7) % 256, (i * 13) % 256, i % 100, i % 7, (i * 37) % 900,
                      (i * 91) % 1000, (i % 11 == 0) ? 500 : 200);
        out += buf;
    }
    return out;
}

static void row(const char *name, double ct_ms, double std_ms, double re2_ms)
{
    std::printf("%-40s %10.3f %10.3f %10.3f   ct/std %.2f  ct/re2 %.2f\n", name, ct_ms, std_ms, re2_ms, ct_ms / std_ms,
                re2_ms > 0 ? ct_ms / re2_ms : 0.0);
}

int main()
{
    const std::string log = make_log(20000);
    std::printf("texto de log: %zu bytes; ms de CPU, minimo de 5 (ct, std::regex, RE2)\n", log.size());
    struct Pat
    {
        const char *name;
        const char *pat;
        bool icase;
    };
    const Pat pats[] = {
        {"literal 'status=500'", "status=500", false},
        {"literal 'needle' (absent)", "needle", false},
        {"ip [0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+", "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+", false},
        {"email [\\w.]+@[\\w.]+\\.com", "[\\w.]+@[\\w.]+\\.com", false},
        {"float latency=(\\d+)\\.(\\d+)", "latency=(\\d+)\\.(\\d+)", false},
        {"alternation of 4 hosts", "host=(alpha|beta|gamma|delta)", false},
        {"line start \\n2026-\\d\\d-\\d\\d", "\\n2026-\\d\\d-\\d\\d", false},
        {"lazy user=(.+?)@", "user=(.+?)@", false},
        {"class run [a-z]+=\\d+", "[a-z]+=\\d+", false},
        {"icase HOST=ALPHA", "HOST=ALPHA", true},
    };
    std::printf("\n== search (primeira ocorrencia)\n");
    for (const Pat &p : pats)
    {
        ct::Regex cre = ct::Regex::compile(p.pat, p.icase ? ct::Regex::IgnoreCase : 0u);
        auto sf = std::regex::ECMAScript | std::regex::optimize;
        if (p.icase)
            sf |= std::regex::icase;
        std::regex sre(p.pat, sf);
        double r2 = 0;
#ifdef CMP_RE2
        RE2::Options o;
        o.set_case_sensitive(!p.icase);
        RE2 re2(p.pat, o);
        r2 = best_ms([&] { g_sink = g_sink + (RE2::PartialMatch(log, re2) ? 1 : 0); });
#endif
        const double c = best_ms([&] {
            ct::Match m;
            g_sink = g_sink + (cre.search(ct::StringView(log.data(), log.size()), &m) ? m.start() + m.end() : 0);
        });
        const double s = best_ms([&] { g_sink = g_sink + (std::regex_search(log.data(), log.data() + log.size(), sre) ? 1 : 0); });
        row(p.name, c, s, r2);
    }
    std::printf("\n== todas as ocorrencias (finditer)\n");
    for (const Pat &p : pats)
    {
        ct::Regex cre = ct::Regex::compile(p.pat, p.icase ? ct::Regex::IgnoreCase : 0u);
        auto sf = std::regex::ECMAScript | std::regex::optimize;
        if (p.icase)
            sf |= std::regex::icase;
        std::regex sre(p.pat, sf);
        double r2 = 0;
#ifdef CMP_RE2
        RE2::Options o;
        o.set_case_sensitive(!p.icase);
        RE2 re2(p.pat, o);
        r2 = best_ms([&] {
            std::uint64_t acc = 0;
            re2::StringPiece in(log);
            re2::StringPiece m;
            size_t pos = 0;
            while (pos <= log.size() && re2.Match(in, pos, log.size(), RE2::UNANCHORED, &m, 1))
            {
                acc += m.size() + 1;
                pos = (m.size() ? static_cast<size_t>(m.data() - log.data()) + m.size() : static_cast<size_t>(m.data() - log.data()) + 1);
            }
            g_sink = g_sink + acc;
        });
#endif
        const double c = best_ms([&] {
            std::uint64_t acc = 0;
            ct::Vector<ct::Match> ms = cre.finditer(ct::StringView(log.data(), log.size()));
            for (std::size_t i = 0; i < ms.size(); ++i)
                acc += ms[i].end() - ms[i].start();
            g_sink = g_sink + acc + ms.size();
        });
        const double s = best_ms([&] {
            std::uint64_t acc = 0;
            for (std::cregex_iterator it(log.data(), log.data() + log.size(), sre), end; it != end; ++it)
                acc += static_cast<std::uint64_t>((*it)[0].length()) + 1;
            g_sink = g_sink + acc;
        });
        row(p.name, c, s, r2);
    }
    std::printf("\n== substituir\n");
    {
        ct::Regex c1 = ct::Regex::compile("\\s+");
        std::regex s1("\\s+", std::regex::optimize);
        double r2 = 0;
#ifdef CMP_RE2
        RE2 e1("\\s+");
        r2 = best_ms([&] { std::string t = log; RE2::GlobalReplace(&t, e1, " "); g_sink = g_sink + t.size(); });
#endif
        row("\\s+ -> ' '", best_ms([&] { g_sink = g_sink + c1.sub(ct::StringView(log.data(), log.size()), " ").size(); }),
            best_ms([&] { g_sink = g_sink + std::regex_replace(log, s1, " ").size(); }), r2);
        ct::Regex c2 = ct::Regex::compile("(\\w+)@(\\w+)");
        std::regex s2("(\\w+)@(\\w+)", std::regex::optimize);
        r2 = 0;
#ifdef CMP_RE2
        RE2 e2("(\\w+)@(\\w+)");
        r2 = best_ms([&] { std::string t = log; RE2::GlobalReplace(&t, e2, "\\2:\\1"); g_sink = g_sink + t.size(); });
#endif
        row("(\\w+)@(\\w+) -> \\2:\\1", best_ms([&] { g_sink = g_sink + c2.sub(ct::StringView(log.data(), log.size()), "\\2:\\1").size(); }),
            best_ms([&] { g_sink = g_sink + std::regex_replace(log, s2, "$2:$1").size(); }), r2);
    }
    std::printf("\n== fullmatch de 100000 textos curtos\n");
    {
        std::string items[1000];
        for (int i = 0; i < 1000; ++i)
        {
            char buf[64];
            if (i & 1)
                std::snprintf(buf, sizeof(buf), "user%d@site%d.com", i, i % 9);
            else
                std::snprintf(buf, sizeof(buf), "not an email %d", i);
            items[i] = buf;
        }
        ct::Regex cre = ct::Regex::compile("[a-z0-9]+@[a-z0-9]+\\.(com|org)");
        std::regex sre("[a-z0-9]+@[a-z0-9]+\\.(com|org)", std::regex::optimize);
        double r2 = 0;
#ifdef CMP_RE2
        RE2 re2("[a-z0-9]+@[a-z0-9]+\\.(com|org)");
        r2 = best_ms([&] { for (int r = 0; r < 100; ++r) for (int i = 0; i < 1000; ++i) g_sink = g_sink + (RE2::FullMatch(items[i], re2) ? 1 : 0); });
#endif
        row("email fullmatch x100000",
            best_ms([&] { for (int r = 0; r < 100; ++r) for (int i = 0; i < 1000; ++i) g_sink = g_sink + (cre.fullmatch(ct::StringView(items[i].data(), items[i].size())) ? 1 : 0); }),
            best_ms([&] { for (int r = 0; r < 100; ++r) for (int i = 0; i < 1000; ++i) g_sink = g_sink + (std::regex_match(items[i], sre) ? 1 : 0); }), r2);
    }
    std::printf("\n== patologica (x+x+)+y em 30 x, sem correspondencia (x1000; std::regex so com 18 x)\n");
    {
        std::string xs(30, 'x'), s18(18, 'x');
        ct::Regex cre = ct::Regex::compile("(x+x+)+y");
        std::regex sre("(x+x+)+y", std::regex::optimize);
        double r2 = 0;
#ifdef CMP_RE2
        RE2 re2("(x+x+)+y");
        r2 = best_ms([&] { for (int r = 0; r < 1000; ++r) g_sink = g_sink + (RE2::PartialMatch(xs, re2) ? 1 : 0); }, 3);
#endif
        row("(x+x+)+y", best_ms([&] { for (int r = 0; r < 1000; ++r) { ct::Match m; g_sink = g_sink + (cre.search(ct::StringView(xs.data(), xs.size()), &m) ? 1 : 0); } }, 3),
            best_ms([&] { for (int r = 0; r < 1000; ++r) g_sink = g_sink + (std::regex_search(s18, sre) ? 1 : 0); }, 3), r2);
    }
    std::printf("\n== compilar x2000\n");
    {
        double r2 = 0;
#ifdef CMP_RE2
        r2 = best_ms([&] { for (int r = 0; r < 2000; ++r) { RE2 re("[\\w.]+@[\\w.]+\\.(com|org|net)"); g_sink = g_sink + re.ok(); } }, 3);
#endif
        row("compile email regex",
            best_ms([&] { for (int r = 0; r < 2000; ++r) { ct::Regex re = ct::Regex::compile("[\\w.]+@[\\w.]+\\.(com|org|net)"); g_sink = g_sink + re.valid(); } }, 3),
            best_ms([&] { for (int r = 0; r < 2000; ++r) { std::regex re("[\\w.]+@[\\w.]+\\.(com|org|net)"); g_sink = g_sink + 1; } }, 3), r2);
    }
    return 0;
}
