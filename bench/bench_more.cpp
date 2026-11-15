// Caminhos quentes que o bench_hot nao cobre, so com a ct (sem biblioteca standard): Json (parse, escrita, copia),
// Vector insert/erase no meio, String compare/hash/append de String, HashMap em varios fatores de carga e com chaves
// de varios tipos, Variant, TextReader/TextWriter, MemoryStream e BinaryReader/BinaryWriter. Cada linha e o melhor de varias
// repeticoes, em milissegundos; serve para comparar antes e depois de mexer num cabecalho (compilar contra duas arvores
// de include com -I e comparar as colunas). O argumento escolhe um grupo (json, vector, string, hashmap, variant, text,
// stream).
//
//   c++ -std=c++14 -O3 -march=native -I include bench/bench_more.cpp -o bench_more -lpthread

#include <ct/binary.hpp>
#include <ct/hashmap.hpp>
#include <ct/json.hpp>
#include <ct/slotmap.hpp>
#include <ct/stream.hpp>
#include <ct/string.hpp>
#include <ct/text.hpp>
#include <ct/variant.hpp>
#include <ct/vector.hpp>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{
    volatile uint64_t g_sink = 0;

    double now_ms()
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) * 1e3 + static_cast<double>(ts.tv_nsec) / 1e6;
    }

    template <typename F>
    void run(const char *name, int reps, F f)
    {
        double best = 1e300;
        for (int i = 0; i < reps; ++i)
        {
            const double t0 = now_ms();
            g_sink += f();
            const double t1 = now_ms();
            if (t1 - t0 < best)
                best = t1 - t0;
        }
        printf("%-48s %10.3f ms\n", name, best);
        fflush(stdout);
    }

    uint32_t next(uint32_t &s) { return s = s * 1664525u + 1013904223u; }

    // ---- Json ---------------------------------------------------------------------------------------------

    ct::String make_records(int n)
    {
        ct::String t("[");
        for (int i = 0; i < n; ++i)
        {
            if (i)
                t.push_back(',');
            t.append("{\"id\":");
            t.append_number(i);
            t.append(",\"name\":\"item number ");
            t.append_number(i);
            t.append(" with some text\\n\",\"price\":");
            t.append_number(12.5 + i * 0.37, 12);
            t.append(",\"tags\":[\"a\",\"bb\",\"ccc\"],\"active\":true,\"nested\":{\"x\":-");
            t.append_number(i);
            t.append(",\"y\":");
            t.append_number(i * 1e-5, 9);
            t.append("},\"none\":null}");
        }
        t.push_back(']');
        return t;
    }

    ct::String make_numbers(int n, bool reals)
    {
        ct::String t("[");
        uint32_t s = 7;
        for (int i = 0; i < n; ++i)
        {
            if (i)
                t.push_back(',');
            const uint32_t r = next(s);
            if (reals)
                t.append_number(static_cast<double>(r) / 65536.0 - 30000.0, 15);
            else
                t.append_number(static_cast<long long>(r) - 2000000000ll);
        }
        t.push_back(']');
        return t;
    }

    uint64_t json_parse(const ct::String &text)
    {
        ct::Json j = ct::Json::parse(text);
        return j.size();
    }

    uint64_t json_dump(const ct::Json &j, int indent)
    {
        ct::String out = j.dump(indent);
        return out.size();
    }

    uint64_t json_copy(const ct::Json &j)
    {
        ct::Json c(j);
        return c.size();
    }

    uint64_t json_build()
    {
        uint64_t n = 0;
        for (int i = 0; i < 20000; ++i)
        {
            ct::Json o = ct::Json::object();
            o["id"] = i;
            o["name"] = "some name";
            o["score"] = i * 0.5;
            ct::Json &tags = o["tags"];
            for (int k = 0; k < 5; ++k)
                tags.push_back(k);
            n += o.size();
        }
        return n;
    }

    uint64_t json_find_many(const ct::Json &j)
    {
        uint64_t n = 0;
        for (int rep = 0; rep < 5; ++rep)
            for (size_t i = 0; i < j.size(); ++i)
            {
                const ct::Json &r = j[i];
                n += static_cast<uint64_t>(r["id"].as_int()) + r["nested"]["x"].as_int() * 0 + (r.contains("none") ? 1 : 0);
            }
        return n;
    }

    uint64_t json_assign_scalars()
    {
        ct::Json a;
        uint64_t n = 0;
        for (int i = 0; i < 3000000; ++i)
        {
            if (i & 1)
                a = ct::Json(i);
            else
                a = ct::Json(true);
            n += a.type();
        }
        return n;
    }

    // ---- Vector -------------------------------------------------------------------------------------------

    uint64_t vector_insert_middle_int()
    {
        ct::Vector<int> v;
        for (int i = 0; i < 20000; ++i)
            v.insert(v.begin() + v.size() / 2, i);
        return v.size();
    }

    uint64_t vector_insert_front_string()
    {
        ct::Vector<ct::String> v;
        for (int i = 0; i < 5000; ++i)
        {
            ct::String s("element number ");
            s.append_number(i);
            v.insert(v.begin(), ct::move(s));
        }
        return v.size();
    }

    uint64_t vector_erase_front_string()
    {
        ct::Vector<ct::String> v;
        for (int i = 0; i < 5000; ++i)
        {
            ct::String s("element number ");
            s.append_number(i);
            v.push_back(ct::move(s));
        }
        while (!v.empty())
            v.erase(v.begin());
        return v.size();
    }

    uint64_t vector_erase_middle_int()
    {
        ct::Vector<int> v;
        v.resize(40000);
        while (v.size() > 10)
            v.erase(v.begin() + v.size() / 2);
        return v.size();
    }

    uint64_t vector_emplace_back_pairs()
    {
        struct P
        {
            int a;
            int b;
            P(int x, int y) : a(x), b(y) {}
        };
        ct::Vector<P> v;
        for (int i = 0; i < 3000000; ++i)
            v.emplace_back(i, i + 1);
        return v.size();
    }

    // ---- String -------------------------------------------------------------------------------------------

    uint64_t string_compare(const ct::Vector<ct::String> &v)
    {
        uint64_t acc = 0;
        for (int rep = 0; rep < 20; ++rep)
            for (size_t i = 1; i < v.size(); ++i)
                acc += static_cast<uint64_t>(v[i - 1].compare(v[i]) + 2);
        return acc;
    }

    uint64_t string_equal(const ct::Vector<ct::String> &v, const ct::Vector<ct::String> &w)
    {
        uint64_t acc = 0;
        for (int rep = 0; rep < 20; ++rep)
            for (size_t i = 0; i < v.size(); ++i)
                acc += (v[i] == w[i]) ? 1 : 0;
        return acc;
    }

    uint64_t string_hash(const ct::Vector<ct::String> &v)
    {
        uint64_t acc = 0;
        for (int rep = 0; rep < 20; ++rep)
            for (size_t i = 0; i < v.size(); ++i)
                acc += v[i].hash();
        return acc;
    }

    uint64_t string_append_strings()
    {
        ct::String piece("0123456789abcdef");
        ct::String out;
        for (int i = 0; i < 2000000; ++i)
            out.append(piece);
        return out.size();
    }

    uint64_t string_substr_find()
    {
        ct::String text;
        for (int i = 0; i < 20000; ++i)
        {
            text.append("key");
            text.append_number(i);
            text.append("=value");
            text.append_number(i * 7);
            text.push_back(';');
        }
        uint64_t acc = 0;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find(';', pos);
            if (end == ct::String::npos)
                break;
            size_t eq = text.find('=', pos);
            ct::String k = text.substr(pos, eq - pos);
            ct::String v = text.substr(eq + 1, end - eq - 1);
            acc += k.size() + v.size();
            pos = end + 1;
        }
        return acc;
    }

    uint64_t string_find_first_of()
    {
        ct::String text;
        for (int i = 0; i < 200000; ++i)
            text.append("abcdefghij");
        text.push_back('#');
        uint64_t acc = 0;
        for (int rep = 0; rep < 20; ++rep)
            acc += text.find_first_of("#%");
        return acc;
    }

    // linhas curtas partidas em campos com find_first_of / find_first_not_of (o uso normal: strings pequenas, conjuntos de 2 a 6 caracteres)
    uint64_t string_tokenize(const ct::String &text)
    {
        uint64_t acc = 0;
        for (int rep = 0; rep < 4; ++rep)
        {
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t start = text.find_first_not_of(" ,;\t", pos);
                if (start == ct::String::npos)
                    break;
                size_t end = text.find_first_of(" ,;\t\n", start);
                if (end == ct::String::npos)
                    end = text.size();
                acc += end - start;
                pos = end + 1;
            }
            acc += text.find_last_of("#!") == ct::String::npos ? 1 : 2;
        }
        return acc;
    }

    // ---- HashMap ------------------------------------------------------------------------------------------

    template <typename K>
    K make_key(uint32_t i);
    template <>
    uint64_t make_key<uint64_t>(uint32_t i)
    {
        return (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(i) << 32);
    }
    template <>
    double make_key<double>(uint32_t i)
    {
        return static_cast<double>(i) * 1.5 + 0.25;
    }
    template <>
    void *make_key<void *>(uint32_t i)
    {
        return reinterpret_cast<void *>(static_cast<uintptr_t>(0x10000000u) + static_cast<uintptr_t>(i) * 64u);
    }
    template <>
    ct::Handle<int> make_key<ct::Handle<int>>(uint32_t i)
    {
        return ct::Handle<int>(i & 0xFFFFu, 1 + (i >> 16));
    }

    template <typename K>
    uint64_t hash_find_load(size_t entries, size_t slots_hint)
    {
        ct::HashMap<K, int> m;
        m.reserve(slots_hint);
        for (size_t i = 0; i < entries; ++i)
            m.put(make_key<K>(static_cast<uint32_t>(i)), static_cast<int>(i));
        uint64_t acc = 0;
        for (int rep = 0; rep < 3; ++rep)
        {
            for (size_t i = 0; i < entries; ++i)
            {
                const int *p = m.find(make_key<K>(static_cast<uint32_t>(i)));
                acc += p ? static_cast<uint64_t>(*p) : 0;
            }
            for (size_t i = 0; i < entries; ++i)
            {
                const int *p = m.find(make_key<K>(static_cast<uint32_t>(i + entries)));
                acc += p ? 1 : 0;
            }
        }
        return acc;
    }

    uint64_t hash_string_put_find(size_t n)
    {
        ct::HashMap<ct::String, int> m;
        char buf[40];
        for (size_t i = 0; i < n; ++i)
        {
            snprintf(buf, sizeof buf, "player-%zu", i);
            m.put(ct::String(buf), static_cast<int>(i));
        }
        uint64_t acc = 0;
        for (size_t i = 0; i < n; ++i)
        {
            snprintf(buf, sizeof buf, "player-%zu", i);
            const int *p = m.find(ct::StringView(buf));
            acc += p ? static_cast<uint64_t>(*p) : 0;
        }
        return acc;
    }

    // ---- Variant -------------------------------------------------------------------------------------------

    typedef ct::Variant<int, double, ct::String> Var3;

    uint64_t variant_assign_scalars()
    {
        Var3 a(1);
        Var3 b(2.5);
        uint64_t n = 0;
        for (int i = 0; i < 20000000; ++i)
        {
            if (i & 1)
                a = b;
            else
                b = a;
            n += a.index();
        }
        return n;
    }

    uint64_t variant_assign_strings()
    {
        Var3 a(ct::String("a string that is long enough to need the heap"));
        Var3 b(ct::String("another long string, also on the heap, a bit longer"));
        Var3 c(7);
        uint64_t n = 0;
        for (int i = 0; i < 2000000; ++i)
        {
            a = b;
            b = c;
            c = a;
            n += a.index();
        }
        return n;
    }

    uint64_t variant_move_strings()
    {
        Var3 v[4] = {Var3(ct::String("a string that is long enough to need the heap")), Var3(3), Var3(1.5),
                     Var3(ct::String("another long string, also on the heap, a bit longer"))};
        uint64_t n = 0;
        for (int i = 0; i < 20000000; ++i)
        {
            v[(i + 1) & 3] = ct::move(v[i & 3]);
            n += v[i & 3].index();
        }
        return n;
    }

    // As atribuicoes por caminho: mesmo indice (atribui no sitio), indice diferente com a alternativa actual a ter
    // destrutor (String) e sem ele (int).
    uint64_t variant_same_index_scalars()
    {
        Var3 a(1);
        Var3 b(2);
        uint64_t n = 0;
        for (int i = 0; i < 20000000; ++i)
        {
            if (i & 1)
                a = b;
            else
                b = a;
            n += static_cast<uint64_t>(a.get<int>());
        }
        return n;
    }

    uint64_t variant_same_index_strings()
    {
        Var3 a(ct::String("a string that is long enough to need the heap"));
        Var3 b(ct::String("another long string, also on the heap, a bit longer"));
        uint64_t n = 0;
        for (int i = 0; i < 5000000; ++i)
        {
            a = b;
            b = a;
            n += a.get<ct::String>().size();
        }
        return n;
    }

    uint64_t variant_move_same_index_strings()
    {
        Var3 a(ct::String("a string that is long enough to need the heap"));
        Var3 b(ct::String("another long string, also on the heap, a bit longer"));
        uint64_t n = 0;
        for (int i = 0; i < 20000000; ++i)
        {
            a = ct::move(b);
            b = ct::move(a);
            n += a.index();
        }
        return n;
    }

    // String<-int (a alternativa actual tem destrutor), int<-String (nao tem) e String<-String (mesmo indice)
    uint64_t variant_copy_index_changes()
    {
        Var3 a(ct::String("a string that is long enough to need the heap"));
        Var3 s(ct::String("another long string, also on the heap, a bit longer"));
        Var3 b(7);
        uint64_t n = 0;
        for (int i = 0; i < 2000000; ++i)
        {
            a = b;
            a = s;
            s = a;
            n += a.index();
        }
        return n;
    }

    uint64_t variant_move_index_changes()
    {
        Var3 a(ct::String("a string that is long enough to need the heap"));
        Var3 s(ct::String("another long string, also on the heap, a bit longer"));
        Var3 b(7);
        uint64_t n = 0;
        for (int i = 0; i < 20000000; ++i)
        {
            a = ct::move(b);
            a = ct::move(s);
            s = ct::move(a);
            n += a.index();
        }
        return n;
    }

    uint64_t variant_vector_push()
    {
        ct::Vector<Var3> v;
        for (int i = 0; i < 1000000; ++i)
        {
            if (i % 3 == 0)
                v.push_back(Var3(i));
            else if (i % 3 == 1)
                v.push_back(Var3(i * 0.5));
            else
                v.push_back(Var3(ct::String("short")));
        }
        return v.size();
    }

    // ---- Text / Stream -------------------------------------------------------------------------------------

    ct::String make_lines(int n)
    {
        ct::String t;
        for (int i = 0; i < n; ++i)
        {
            t.append("line number ");
            t.append_number(i);
            t.append(" with some more words in it\n");
        }
        return t;
    }

    uint64_t text_read_lines(const ct::String &text)
    {
        ct::MemoryStream ms(text.data(), text.size());
        ct::TextReader r(ms);
        ct::String line;
        uint64_t n = 0;
        while (r.read_line(line))
            n += line.size();
        return n;
    }

    uint64_t text_read_words(const ct::String &text)
    {
        ct::MemoryStream ms(text.data(), text.size());
        ct::TextReader r(ms);
        ct::String word;
        uint64_t n = 0;
        while (r.read_word(word))
            n += word.size();
        return n;
    }

    uint64_t text_write(int lines)
    {
        ct::MemoryStream ms;
        {
            ct::TextWriter w(ms);
            for (int i = 0; i < lines; ++i)
            {
                w.write("line ").number(i).write(' ').number(i * 0.25, 6).line(" end");
            }
        }
        return static_cast<uint64_t>(ms.size());
    }

    uint64_t binary_write_read(int n)
    {
        ct::MemoryStream ms;
        {
            ct::BinaryWriter w(ms);
            for (int i = 0; i < n; ++i)
            {
                w.u32(static_cast<uint32_t>(i));
                w.f32(static_cast<float>(i) * 0.5f);
                w.u8(static_cast<uint8_t>(i));
                w.varint(static_cast<uint32_t>(i) * 3u);
            }
        }
        ms.seek(0, ct::Seek::Set);
        ct::BinaryReader r(ms);
        uint64_t acc = 0;
        for (int i = 0; i < n; ++i)
        {
            acc += r.u32();
            acc += static_cast<uint64_t>(r.f32());
            acc += r.u8();
            acc += r.varint();
        }
        return acc;
    }

    uint64_t binary_strings(int n)
    {
        ct::MemoryStream ms;
        {
            ct::BinaryWriter w(ms);
            for (int i = 0; i < n; ++i)
                w.string("some short string value");
        }
        ms.seek(0, ct::Seek::Set);
        ct::BinaryReader r(ms);
        ct::String s;
        uint64_t acc = 0;
        for (int i = 0; i < n; ++i)
        {
            r.string(s);
            acc += s.size();
        }
        return acc;
    }

    uint64_t stream_read_all(const ct::String &text)
    {
        ct::MemoryStream ms(text.data(), text.size());
        ct::Vector<uint8_t> out;
        ms.read_all(out);
        uint64_t n = out.size();
        ms.seek(0, ct::Seek::Set);
        ct::String s;
        ms.read_all(s);
        return n + s.size();
    }
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : "";
    auto want = [&](const char *group) { return !*only || strstr(group, only) != nullptr; };

    if (want("json"))
    {
        const ct::String records = make_records(20000);
        const ct::String ints = make_numbers(200000, false);
        const ct::String reals = make_numbers(200000, true);
        printf("json inputs: records %zu B, ints %zu B, reals %zu B\n", records.size(), ints.size(), reals.size());
        run("json parse 20k records", 7, [&] { return json_parse(records); });
        run("json parse 200k ints", 7, [&] { return json_parse(ints); });
        run("json parse 200k reals", 7, [&] { return json_parse(reals); });
        ct::Json doc = ct::Json::parse(records);
        ct::Json real_doc = ct::Json::parse(reals);
        run("json dump compact 20k records", 7, [&] { return json_dump(doc, -1); });
        run("json dump indent 2, 20k records", 7, [&] { return json_dump(doc, 2); });
        run("json dump 200k reals", 7, [&] { return json_dump(real_doc, -1); });
        run("json copy 20k records", 7, [&] { return json_copy(doc); });
        run("json build 20k small objects", 7, [] { return json_build(); });
        run("json lookup by key x5 over 20k records", 7, [&] { return json_find_many(doc); });
        run("json assign scalar x3M", 7, [] { return json_assign_scalars(); });
    }
    if (want("vector"))
    {
        run("vector<int> insert at middle x20k", 7, [] { return vector_insert_middle_int(); });
        run("vector<int> erase at middle 40k", 7, [] { return vector_erase_middle_int(); });
        run("vector<String> insert at front x5k", 7, [] { return vector_insert_front_string(); });
        run("vector<String> erase at front x5k", 7, [] { return vector_erase_front_string(); });
        run("vector emplace_back(a, b) x3M", 7, [] { return vector_emplace_back_pairs(); });
    }
    if (want("string"))
    {
        ct::Vector<ct::String> keys;
        ct::Vector<ct::String> same;
        for (int i = 0; i < 100000; ++i)
        {
            ct::String s("some/common/prefix/path/element-");
            s.append_number(i);
            keys.push_back(s);
            same.push_back(s);
        }
        run("string compare 100k x20 (long common prefix)", 7, [&] { return string_compare(keys); });
        run("string == 100k x20 (equal)", 7, [&] { return string_equal(keys, same); });
        run("string hash() 100k x20", 7, [&] { return string_hash(keys); });
        run("string append(String) x2M", 7, [] { return string_append_strings(); });
        run("string find+substr key=value 20k", 7, [] { return string_substr_find(); });
        run("string find_first_of 2MB miss x20", 7, [] { return string_find_first_of(); });
        ct::String fields;
        for (int i = 0; i < 100000; ++i)
        {
            fields.append("key");
            fields.append_number(i);
            fields.append(", value ");
            fields.append_number(i * 7);
            fields.append("; flag\n");
        }
        run("string tokenize 100k lines x4 (first_of, not_of)", 7, [&] { return string_tokenize(fields); });
    }
    if (want("hashmap"))
    {
        // 700k entradas em tabelas de 1M (carga 0.67), 2M (0.33) e 4M (0.17) posicoes
        run("hashmap<u64> find, load 0.67 (700k)", 5, [] { return hash_find_load<uint64_t>(700000, 700000); });
        run("hashmap<u64> find, load 0.33 (700k)", 5, [] { return hash_find_load<uint64_t>(700000, 1400000); });
        run("hashmap<u64> find, load 0.17 (700k)", 5, [] { return hash_find_load<uint64_t>(700000, 2800000); });
        run("hashmap<double> find 300k", 5, [] { return hash_find_load<double>(300000, 300000); });
        run("hashmap<void*> find 300k", 5, [] { return hash_find_load<void *>(300000, 300000); });
        run("hashmap<Handle> find 300k", 5, [] { return hash_find_load<ct::Handle<int>>(300000, 300000); });
        run("hashmap<String> put+find 200k \"player-N\"", 5, [] { return hash_string_put_find(200000); });
    }
    if (want("variant"))
    {
        run("variant<int,double,String> assign scalars x20M", 7, [] { return variant_assign_scalars(); });
        run("variant<int,double,String> assign strings x2M x3", 7, [] { return variant_assign_strings(); });
        run("variant<int,double,String> move assign x20M", 7, [] { return variant_move_strings(); });
        run("variant<int,double,String> assign ints, same index x20M", 7, [] { return variant_same_index_scalars(); });
        run("variant<int,double,String> assign Strings, same index x5M x2", 7, [] { return variant_same_index_strings(); });
        run("variant<int,double,String> move Strings, same index x20M x2", 7, [] { return variant_move_same_index_strings(); });
        run("variant<int,double,String> copy String<-int,<-String x2M x3", 7, [] { return variant_copy_index_changes(); });
        run("variant<int,double,String> move String<-int,<-String x20M x3", 7, [] { return variant_move_index_changes(); });
        run("vector<variant> push_back 1M", 7, [] { return variant_vector_push(); });
    }
    if (want("text") || want("stream"))
    {
        const ct::String lines = make_lines(200000);
        printf("text input: %zu B\n", lines.size());
        run("text read_line 200k lines", 7, [&] { return text_read_lines(lines); });
        run("text read_word 200k lines", 7, [&] { return text_read_words(lines); });
        run("text write 200k lines", 7, [] { return text_write(200000); });
        run("binary u32+f32+u8+varint write+read x500k", 7, [] { return binary_write_read(500000); });
        run("binary string write+read x300k", 7, [] { return binary_strings(300000); });
        run("stream read_all Vector+String 6 MB", 7, [&] { return stream_read_all(lines); });
    }
    printf("sink %llu\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
