// Fuzz de mutacao dos parsers: cada entrada e uma amostra valida estragada (bytes trocados, apagados, repetidos,
// inseridos de um conjunto de caracteres estruturais, cortada) e passada a Json, Xml, Ini, Soup (HTML e seletores),
// HttpParser (pedidos e respostas, partidos em bocados), Regex, JsRegex e Unicode. Nao ha resposta certa a comparar:
// o teste e nao rebentar (ASan, UBSan), nao ficar preso (alarme de 20 s por entrada) e que o que um parser aceita
// volte a dar o mesmo depois de escrito (Json e Ini). Sem biblioteca standard.
//
//   ct_parsefuzz all|json|xml|ini|soup|http|regex|jsregex|unicode [entradas por parser (3000)] [semente (1)]
//
// A semente e fixa por omissao: a mesma corrida repete-se igual e uma falha imprime a entrada que a causou.

#include <ct/json.hpp>
#include <ct/xml.hpp>
#include <ct/ini.hpp>
#include <ct/soup.hpp>
#include <ct/http.hpp>
#include <ct/regex.hpp>
#include <ct/jsregex.hpp>
#include <ct/unicode.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static unsigned long long rs = 88172645463325252ull;
static unsigned rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 16); }
static unsigned below(unsigned n) { return n ? rnd() % n : 0; }

static const char *g_what = ""; static unsigned long long g_iter = 0;
static ct::String g_input;
static void on_alarm(int) {
    fprintf(stderr, "TIMEOUT in %s iter=%llu input(%zu)=[", g_what, g_iter, g_input.size());
    fwrite(g_input.data(), 1, g_input.size() < 400 ? g_input.size() : 400, stderr);
    fprintf(stderr, "]\n"); _exit(3);
}
static const char *punct[] = {"{","}","[","]","<",">","/","\"","'","\\",":",",","=","&",";","#","(",")","*","+","?","|","^","$","-","\r\n","\n","\0","%","@","!","\xff","\xc3","\xe2\x82","\xf0\x9f\x98","\\u","\\ud800","e","E",".","0","9","\x80"};
static ct::String mutate(const ct::String &seed)
{
    ct::String s = seed;
    unsigned n = 1 + below(4);
    for (unsigned k = 0; k < n; ++k) {
        unsigned op = below(8);
        size_t sz = s.size();
        switch (op) {
        case 0: if (sz) s[below((unsigned)sz)] = (char)rnd(); break;
        case 1: { const char *p = punct[below(sizeof(punct)/sizeof(punct[0]))]; size_t l = strlen(p); if (!l) l = 1; s.insert(below((unsigned)sz + 1), p, l); break; }
        case 2: if (sz) { size_t a = below((unsigned)sz); size_t l = 1 + below(8); s.erase(a, l); } break;
        case 3: if (sz) { size_t a = below((unsigned)sz); size_t l = 1 + below(16); if (l > sz - a) l = sz - a; ct::String t(s.data() + a, l); s.insert(below((unsigned)sz + 1), t); } break;
        case 4: if (sz) s.erase(below((unsigned)sz), ct::String::npos); break;
        case 5: if (sz) { size_t a = below((unsigned)sz); size_t l = 1 + below(30); if (l > sz - a) l = sz - a; ct::String t(s.data() + a, l); size_t reps = 2 + below(30); for (size_t r = 0; r < reps; ++r) s.insert(a, t); } break;
        case 6: s.insert(below((unsigned)sz + 1), (size_t)(1 + below(40)), "[{(<\"'"[below(6)]); break;
        case 7: if (sz > 2) { size_t a = below((unsigned)sz); s[a] = s[below((unsigned)sz)]; } break;
        }
    }
    return s;
}

static const char *json_seeds[] = {
 "{\"a\":[1,2,3.5e10,-0,\"x\\n\\u00e9\\ud83d\\ude00\"],\"b\":{\"c\":null,\"d\":true,\"e\":false},\"n\":-9223372036854775808,\"u\":18446744073709551615,\"f\":1.7976931348623157e308,\"s\":4.9e-324}",
 "[[[[1,2,[3,[4,[5]]]]]]]", "  \"\\u0000\\u001f\\\\\\/\"  ", "[1e999]", "{\"k\":\"v\",\"k\":\"w\"}"};
static const char *xml_seeds[] = {
 "<?xml version=\"1.0\"?><!DOCTYPE a><a x=\"1\" y='2'><b>text &amp; &lt; &#65; &#x1F600;<![CDATA[ <raw> ]]></b><!-- c --><c/><d>tail</d>mixed<e a=\"&quot;\"/></a>",
 "<a><b><c><d/></c></b></a>", "<r xmlns:x=\"u\"><x:a/>&unknown;</r>"};
static const char *ini_seeds[] = {
 "; comment\n[sec]\nkey=value\nq=\"quoted \"\" value\nwith newline\"\n# c\n[other]\na = b\n\n=novalue\nnoeq\n",
 "[a]\r\nk=v\r\n[a]\r\nk=w\r\n"};
static const char *html_seeds[] = {
 "<!DOCTYPE html><html><head><title>t &amp; t</title><script>if(a<b){}</script></head><body class=\"a b\" id=x><div><p>one<p>two<ul><li>a<li>b</ul><table><tr><td>1<td>2</table><a href='u?x=1&y=2'>l</a><br><img src=a.png><!-- c --><style>p>a{}</style></div>&nbsp;&copy;&#x41;&bogus;</body></html>",
 "<div><span>unclosed<b><i>x</b></i></div></span>"};
static const char *sel_seeds[] = {
 "div > p:nth-child(2n+1)", "a[href^=\"u\"]:not(.x), li:is(.a,#b) ~ li", "ul li:nth-last-of-type(-n+3):has(> a)", ":root :scope + p[class~=a i]"};
static const char *http_req_seeds[] = {
 "POST /a/b%20c?x=1&y=%41 HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n\r\nhello",
 "POST /u HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
 "GET / HTTP/1.0\r\nA: b\r\n c\r\n\r\n"};
static const char *http_resp_seeds[] = {
 "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",
 "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;ext=1\r\nabc\r\n0\r\nTrailer: x\r\n\r\n",
 "HTTP/1.0 404 Not Found\r\n\r\nbody until close"};
static const char *re_seeds[] = {
 "(?P<n>a+)(b|c)*?\\1[^a-z]{2,5}(?=x)(?<!y)\\w\\d\\s\\b$", "(?i)^(a|b)*?c{3}(?:x|y)+\\Z", "[\\d\\-a-f]+(?(1)a|b)", "(?x) a b # c\n (d)"};
static const char *jsre_seeds[] = {
 "(?<year>\\d{4})-(?<m>\\d\\d)\\k<year>(?=a)(?<!b)[\\p{L}--[a-z]]\\u{1F600}\\q{abc}", "^(a+)+$", "[\\w&&\\d]|\\P{Lu}\\1(?:x)*?", "(?i:a)(?-i:B)\\cA\\x41\\u0041[^\\]]"};

template <size_t N> static ct::String pick(const char *(&arr)[N]) { return ct::String(arr[below((unsigned)N)]); }

static void walk_json(const ct::Json &j, int depth) {
    if (depth > 300) return;
    ct::String d = j.dump(depth % 2 ? 2 : -1);
    ct::Json back = ct::Json::parse(d);
    if (!(back == j) ) {
        // NaN can't occur, so a dumped doc must reparse equal
        fprintf(stderr, "JSON roundtrip mismatch: %s\n", d.c_str()); _exit(4);
    }
}

static int run_one(const char *which, unsigned long long iters)
{
    g_what = which;
    for (g_iter = 0; g_iter < iters; ++g_iter) {
        alarm(20);
        if (!strcmp(which, "json")) {
            g_input = mutate(pick(json_seeds));
            ct::Json::Error e; ct::Json j = ct::Json::parse(g_input, &e);
            if (!e) walk_json(j, 0);
        } else if (!strcmp(which, "xml")) {
            g_input = mutate(pick(xml_seeds));
            ct::Xml::Error e; ct::Xml x = ct::Xml::parse(g_input, &e);
            if (!e) { ct::String d = x.dump(); ct::Xml::Error e2; ct::Xml y = ct::Xml::parse(d, &e2); (void)y; ct::String d2 = x.dump(2); (void)d2; }
        } else if (!strcmp(which, "ini")) {
            g_input = mutate(pick(ini_seeds));
            ct::Ini ini = ct::Ini::parse(g_input);
            ct::String d = ct::Ini::parse(ini.dump()).dump();
            if (d != ini.dump()) { fprintf(stderr, "INI dump not stable\n[%s]\n[%s]\n", ini.dump().c_str(), d.c_str()); _exit(4); }
        } else if (!strcmp(which, "soup")) {
            g_input = mutate(pick(html_seeds));
            ct::Soup s = ct::Soup::parse(ct::StringView(g_input));
            ct::String h = s.root().html(); (void)h; ct::String t = s.getTextTrimmed(); (void)t;
            ct::String sel = mutate(pick(sel_seeds));
            ct::Vector<const ct::Node *> r = s.select(ct::StringView(sel)); (void)r;
            g_input.append("\n--sel: "); g_input.append(sel);
            ct::String dec = ct::decodeEntities(ct::StringView(g_input)); (void)dec;
        } else if (!strcmp(which, "http")) {
            bool req = below(2);
            g_input = mutate(req ? pick(http_req_seeds) : pick(http_resp_seeds));
            ct::HttpParser p(1 << 16, 1 << 14);
            ct::HttpRequest rq; ct::HttpResponse rp;
            size_t off = 0;
            while (off < g_input.size()) {
                size_t k = 1 + below(12); if (k > g_input.size() - off) k = g_input.size() - off;
                ct::HttpParser::State st = req ? p.feed(g_input.data() + off, k, rq) : p.feed(g_input.data() + off, k, rp);
                off += k;
                if (st != ct::HttpParser::NeedMore) { if (st == ct::HttpParser::Done && below(2)) { p.reset(); } else break; }
            }
            if (!req) p.finish(rp);
        } else if (!strcmp(which, "regex")) {
            g_input = mutate(pick(re_seeds));
            if (g_input.size() > 80) continue;   // as repeticoes inseridas dao padroes exponenciais (como no Python re)
            ct::Regex::Error e; ct::Regex re = ct::Regex::compile(ct::StringView(g_input), below(8) & 7, &e);
            if (re.valid()) { const char *txt = "aaab bcxx  abc 2020-01-02 aAbB\n\xc3\xa9x"; ct::Match m; re.search(txt, &m); ct::Vector<ct::String> f = re.findall(txt); ct::String r = re.sub(txt, "<\\g<0>>"); (void)r; }
        } else if (!strcmp(which, "jsregex")) {
            g_input = mutate(pick(jsre_seeds));
            if (g_input.size() > 80) continue;
            ct::JsRegex::Error e; unsigned fl = below(64);
            ct::JsRegex re = ct::JsRegex::compile(ct::StringView(g_input), fl, &e);
            if (re.valid()) {
                ct::JsRegex::Limits lim; lim.max_steps = 200000; lim.max_stack = 1 << 20; re.set_limits(lim);
                ct::Vector<char16_t> t; const char *txt = "aaab 2020-2020 x\xc3\xa9 abc"; for (const char *c = txt; *c; ++c) t.push_back((char16_t)(unsigned char)*c); t.push_back(0xD83D); t.push_back(0xDE00);
                ct::JsCaptures caps; for (size_t st = 0; st <= t.size(); st += 3) re.exec(t.data(), t.size(), st, below(2), caps);
            }
        } else if (!strcmp(which, "unicode")) {
            ct::Vector<char16_t> t; unsigned n = below(24); for (unsigned i = 0; i < n; ++i) { unsigned r = rnd(); t.push_back((char16_t)(r & 3 ? (r >> 8) & 0xFFFF : 0xD800 + ((r >> 8) & 0x7FF))); }
            g_input.clear();
            ct::Vector<char16_t> o; for (int f = 0; f < 4; ++f) { ct::unicode::normalize((ct::unicode::Form)f, t.data(), t.size(), o); ct::unicode::to_upper(t.data(), t.size(), o); ct::unicode::to_lower(t.data(), t.size(), o); }
        }
    }
    printf("%-8s %llu entradas ok\n", which, iters);
    return 0;
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    unsigned long long iters = argc > 2 ? strtoull(argv[2], 0, 10) : 3000;
    rs ^= (argc > 3 ? strtoull(argv[3], 0, 10) : 1) * 0x9E3779B97F4A7C15ull; rnd(); rnd();
    signal(SIGALRM, on_alarm);
    static const char *all[] = {"json", "xml", "ini", "soup", "http", "regex", "jsregex", "unicode"};
    if (!strcmp(which, "all")) {
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
            if (run_one(all[i], iters)) return 1;
        return 0;
    }
    return run_one(which, iters);
}
