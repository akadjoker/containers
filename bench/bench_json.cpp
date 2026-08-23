
#include <ct/json.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{
    using nl = nlohmann::json;

    std::string load(const char *path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return std::string();
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string generate_scene(int entities)
    {
        std::string s = "{\"format\":\"radion-scene\",\"entities\":[";
        unsigned rng = 12345;
        auto frand = [&rng] {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<double>(static_cast<float>((rng >> 8) * 1e-5f));
        };
        char buf[256];
        for (int i = 0; i < entities; ++i)
        {
            if (i)
                s += ',';
            std::snprintf(buf, sizeof(buf),
                          "{\"id\":%d,\"name\":\"entidade_%d\",\"tag\":\"\",\"parent\":%s,"
                          "\"flags\":{\"active\":true,\"static\":false,\"visible\":true},",
                          i, i, i ? "0" : "null");
            s += buf;
            std::snprintf(buf, sizeof(buf),
                          "\"transform\":{\"position\":[%.17g,%.17g,%.17g],"
                          "\"rotation\":[%.17g,%.17g,%.17g,1.0],\"scale\":[1.0,1.0,1.0]},",
                          frand(), frand(), frand(), frand(), frand(), frand());
            s += buf;
            s += "\"components\":[{\"type\":\"MeshRenderer\",\"active\":true,\"version\":1,"
                 "\"mesh\":{\"source\":\"File\",\"file\":\"models/x.rmesh\","
                 "\"params\":[0.0,0.0,0.0,0.0]},\"materialOverrides\":[]}]}";
        }
        s += "]}";
        return s;
    }

    std::uint64_t walk_ct(const ct::Json &j)
    {
        switch (j.type())
        {
        case ct::Json::Arr:
        {
            std::uint64_t acc = 0;
            for (const ct::Json &v : j.items())
                acc += walk_ct(v);
            return acc;
        }
        case ct::Json::Obj:
        {
            std::uint64_t acc = 0;
            for (const ct::Json::Member &m : j.members())
                acc += m.key.size() + walk_ct(m.value);
            return acc;
        }
        case ct::Json::Str:
            return j.str().size();
        case ct::Json::Int:
        case ct::Json::Uint:
        case ct::Json::Real:
            return static_cast<std::uint64_t>(j.as_double());
        default:
            return 1;
        }
    }

    std::uint64_t walk_nl(const nl &j)
    {
        if (j.is_array())
        {
            std::uint64_t acc = 0;
            for (const nl &v : j)
                acc += walk_nl(v);
            return acc;
        }
        if (j.is_object())
        {
            std::uint64_t acc = 0;
            for (auto it = j.begin(); it != j.end(); ++it)
                acc += it.key().size() + walk_nl(it.value());
            return acc;
        }
        if (j.is_string())
            return j.get_ref<const std::string &>().size();
        if (j.is_number())
            return static_cast<std::uint64_t>(j.get<double>());
        return 1;
    }

    void run(const char *titulo, const std::string &texto)
    {
        char nome[128];
        std::snprintf(nome, sizeof(nome), "%s (%zu KB)", titulo, texto.size() / 1024);
        bench::header(nome);

        const int reps = texto.size() > 200000 ? 20 : 200;

        bench::compare(
            "parse",
            [&] {
                for (int i = 0; i < reps; ++i)
                {
                    ct::Json j = ct::Json::parse(texto.data(), texto.size());
                    bench::sink += j.size();
                    bench::escape(&j);
                }
            },
            [&] {
                for (int i = 0; i < reps; ++i)
                {
                    nl j = nl::parse(texto);
                    bench::sink += j.size();
                    bench::escape(&j);
                }
            });

        ct::Json a = ct::Json::parse(texto.data(), texto.size());
        nl b = nl::parse(texto);

        bench::compare(
            "dump compacto",
            [&] {
                for (int i = 0; i < reps; ++i)
                {
                    ct::String s = a.dump();
                    bench::sink += s.size();
                    bench::escape(&s);
                }
            },
            [&] {
                for (int i = 0; i < reps; ++i)
                {
                    std::string s = b.dump();
                    bench::sink += s.size();
                    bench::escape(&s);
                }
            });

        bench::compare(
            "dump(4) indentado",
            [&] {
                for (int i = 0; i < reps; ++i)
                {
                    ct::String s = a.dump(4);
                    bench::sink += s.size();
                    bench::escape(&s);
                }
            },
            [&] {
                for (int i = 0; i < reps; ++i)
                {
                    std::string s = b.dump(4);
                    bench::sink += s.size();
                    bench::escape(&s);
                }
            });

        const int walks = reps * 5;
        bench::compare(
            "walk completo (loader)",
            [&] {
                for (int w = 0; w < walks; ++w)
                    bench::sink += walk_ct(a);
            },
            [&] {
                for (int w = 0; w < walks; ++w)
                    bench::sink += walk_nl(b);
            });

        bench::compare(
            "lookup por chave",
            [&] {
                for (int w = 0; w < walks * 20; ++w)
                {
                    const ct::Json &ca = a;
                    bench::sink += ca["format"].is_string() ? 1 : 0;
                    bench::sink += ca["scene"]["objects"].size();
                    bench::sink += ca["renderSettings"]["lighting"].size();
                }
            },
            [&] {
                for (int w = 0; w < walks * 20; ++w)
                {
                    const nl &cb = b; 
                    bench::sink += cb.contains("format") && cb["format"].is_string() ? 1 : 0;
                    if (cb.contains("scene") && cb["scene"].contains("objects"))
                        bench::sink += cb["scene"]["objects"].size();
                    if (cb.contains("renderSettings") && cb["renderSettings"].contains("lighting"))
                        bench::sink += cb["renderSettings"]["lighting"].size();
                }
            });
    }

    void build_bench(int entities)
    {
        bench::header("construir do zero");
        bench::compare(
            "montar cena com N entidades",
            [&] {
                ct::Json doc = ct::Json::object();
                doc.set("format", "radion-scene");
                ct::Json &ents = doc.set("entities", ct::Json::array());
                ents.reserve(static_cast<std::size_t>(entities));
                for (int i = 0; i < entities; ++i)
                {
                    ct::Json e = ct::Json::object();
                    e.set("id", i);
                    e.set("name", "entidade");
                    ct::Json pos = ct::Json::array();
                    pos.push_back(1.5);
                    pos.push_back(2.5);
                    pos.push_back(3.5);
                    e.set("position", ct::detail::move(pos));
                    ents.push_back(ct::detail::move(e));
                }
                bench::sink += doc["entities"].size();
                bench::escape(&doc);
            },
            [&] {
                nl doc = nl::object();
                doc["format"] = "radion-scene";
                doc["entities"] = nl::array();
                for (int i = 0; i < entities; ++i)
                {
                    nl e = nl::object();
                    e["id"] = i;
                    e["name"] = "entidade";
                    e["position"] = nl::array({1.5, 2.5, 3.5});
                    doc["entities"].push_back(std::move(e));
                }
                bench::sink += doc["entities"].size();
                bench::escape(&doc);
            });
    }
}

int main(int argc, char **argv)
{
    std::printf("ct::Json vs nlohmann::json %d.%d.%d\n", NLOHMANN_JSON_VERSION_MAJOR,
                NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH);
    std::printf("sizeof: ct::Json=%zu  nlohmann::json=%zu bytes\n", sizeof(ct::Json),
                sizeof(nl));

    const char *candidatos[] = {
        nullptr,
        "/media/projectos/projects/cpp/Radion/teste/Scenes/plane.scene.json",
        "/media/projectos/projects/cpp/Radion/teste/Scenes/game.scene.json"};
    if (argc > 1)
        candidatos[0] = argv[1];

    bool achou = false;
    for (const char *p : candidatos)
    {
        if (!p)
            continue;
        const std::string texto = load(p);
        if (texto.empty())
            continue;
        const char *nome = std::strrchr(p, '/');
        run(nome ? nome + 1 : p, texto);
        achou = true;
    }
    if (!achou)
        std::printf("\n(sem cenas reais a mao — so o documento gerado)\n");

    run("cena gerada", generate_scene(4000));
    build_bench(20000);
    return 0;
}