// Fuzz diferencial, segunda ronda: sequencias aleatorias de operacoes em MemoryStream e SubStream (com leituras e
// escritas curtas), BinaryWriter/BinaryReader (todos os tipos, truncados), Json (arvores aleatorias, ida e volta por
// texto, atribuicao por move de um filho), Function, Rc/Weak, Variant, SlotMap, Pool, Arena, sort e PriorityQueue,
// comparadas com um modelo escrito no proprio teste em arrays C simples (sem biblioteca standard). Os objetos contados
// apanham fugas, destruicoes a mais e uso depois de destruir.
//
// CT_FUZZ_ITERS (variavel de ambiente) muda o numero de operacoes por semente; por omissao fica rapido. As sementes
// sao fixas: uma falha repete-se com a mesma semente.

#include <ct/arena.hpp>
#include <ct/binary.hpp>
#include <ct/function.hpp>
#include <ct/json.hpp>
#include <ct/pool.hpp>
#include <ct/priority_queue.hpp>
#include <ct/ptr.hpp>
#include <ct/slotmap.hpp>
#include <ct/sort.hpp>
#include <ct/stream.hpp>
#include <ct/string.hpp>
#include <ct/text.hpp>
#include <ct/variant.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x7654321ull) {}
        uint32_t next()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return static_cast<uint32_t>(s >> 16);
        }
        uint32_t below(uint32_t n) { return n ? next() % n : 0; }
        uint64_t next64() { return (static_cast<uint64_t>(next()) << 32) ^ next() ^ (static_cast<uint64_t>(next()) << 11); }
    };

    int fuzz_iters(int fallback)
    {
        const char *e = getenv("CT_FUZZ_ITERS");
        if (!e || !*e)
            return fallback;
        const int v = atoi(e);
        return v > 0 ? v : fallback;
    }

    struct Counted
    {
        static int live;
        static int bad;
        int id;
        uint32_t magic;
        Counted() : id(0), magic(0xC0FFEEu) { ++live; }
        explicit Counted(int v) : id(v), magic(0xC0FFEEu) { ++live; }
        Counted(const Counted &o) : id(o.id), magic(0xC0FFEEu)
        {
            if (o.magic != 0xC0FFEEu)
                ++bad;
            ++live;
        }
        Counted(Counted &&o) noexcept : id(o.id), magic(0xC0FFEEu)
        {
            if (o.magic != 0xC0FFEEu)
                ++bad;
            ++live;
        }
        Counted &operator=(const Counted &o)
        {
            if (magic != 0xC0FFEEu || o.magic != 0xC0FFEEu)
                ++bad;
            id = o.id;
            return *this;
        }
        Counted &operator=(Counted &&o) noexcept
        {
            if (magic != 0xC0FFEEu || o.magic != 0xC0FFEEu)
                ++bad;
            id = o.id;
            return *this;
        }
        ~Counted()
        {
            if (magic != 0xC0FFEEu)
                ++bad;
            magic = 0xDEADu;
            --live;
        }
    };
    int Counted::live = 0;
    int Counted::bad = 0;

    // ---- Streams ---------------------------------------------------------------------------------------------

    enum
    {
        kMaxBytes = 600
    };

    // Stream que entrega e aceita so alguns bytes de cada vez, por cima de outro Stream.
    class Dribble : public ct::Stream
    {
    public:
        Dribble(ct::Stream &base, Rng &rng, uint32_t max_step) : base_(base), rng_(rng), max_step_(max_step) {}
        size_t read(void *dst, size_t n) override
        {
            if (n > 1)
                n = 1 + rng_.below(static_cast<uint32_t>(n < max_step_ ? n : max_step_));
            return base_.read(dst, n);
        }
        size_t write(const void *src, size_t n) override
        {
            if (n > 1)
                n = 1 + rng_.below(static_cast<uint32_t>(n < max_step_ ? n : max_step_));
            return base_.write(src, n);
        }
        bool seek(int64_t o, ct::Seek w) override { return base_.seek(o, w); }
        int64_t tell() const override { return base_.tell(); }
        int64_t size() const override { return base_.size(); }
        void close() override { base_.close(); }
        bool is_open() const override { return base_.is_open(); }
        bool eof() const override { return base_.eof(); }
        bool can_read() const override { return base_.can_read(); }
        bool can_write() const override { return base_.can_write(); }
        bool can_seek() const override { return base_.can_seek(); }

    private:
        ct::Stream &base_;
        Rng &rng_;
        uint32_t max_step_;
    };

    void fuzz_memory_stream(uint64_t seed, int iters)
    {
        Rng rng(seed);
        ct::MemoryStream ms;
        unsigned char ref[kMaxBytes + 80];
        size_t size = 0;
        size_t pos = 0;
        unsigned char scratch[96];

        for (int it = 0; it < iters; ++it)
        {
            switch (rng.below(10))
            {
            case 0:
            case 1:
            {
                const size_t n = rng.below(40);
                if (pos + n > kMaxBytes)
                    break;
                for (size_t i = 0; i < n; ++i)
                    scratch[i] = static_cast<unsigned char>(rng.next());
                const size_t w = ms.write(n ? scratch : nullptr, n);
                ASSERT_EQ(w, n);
                for (size_t i = 0; i < n; ++i)
                    ref[pos + i] = scratch[i];
                pos += n;
                if (pos > size)
                    size = pos;
                break;
            }
            case 2:
            case 3:
            {
                const size_t n = rng.below(60);
                const size_t want = pos >= size ? 0 : (n < size - pos ? n : size - pos);
                const size_t r = ms.read(n ? scratch : nullptr, n);
                ASSERT_EQ(r, want);
                ASSERT_EQ(r ? memcmp(scratch, ref + pos, r) : 0, 0);
                pos += r;
                break;
            }
            case 4:
            {
                const int64_t off = static_cast<int64_t>(rng.below(2 * kMaxBytes)) - kMaxBytes;
                const uint32_t origin = rng.below(3);
                const int64_t base = origin == 0 ? 0 : origin == 1 ? static_cast<int64_t>(pos) : static_cast<int64_t>(size);
                const int64_t next = base + off;
                const bool want = next >= 0 && static_cast<uint64_t>(next) <= size;
                const bool got = ms.seek(off, origin == 0 ? ct::Seek::Set : origin == 1 ? ct::Seek::Cur : ct::Seek::End);
                ASSERT_EQ(got, want);
                if (want)
                    pos = static_cast<size_t>(next);
                break;
            }
            case 5:
                ASSERT_EQ(ms.tell(), static_cast<int64_t>(pos));
                ASSERT_EQ(ms.size(), static_cast<int64_t>(size));
                ASSERT_EQ(ms.eof(), pos >= size);
                ASSERT_EQ(ms.can_seek(), true);
                break;
            case 6:
            {
                const bool far = rng.below(4) == 0;
                const int64_t off = far ? -(static_cast<int64_t>(1) << 62) : (static_cast<int64_t>(1) << 62);
                ASSERT_FALSE(ms.seek(off, ct::Seek::Cur));
                ASSERT_FALSE(ms.seek(off, ct::Seek::End));
                ASSERT_EQ(ms.tell(), static_cast<int64_t>(pos));
                break;
            }
            case 7:
            {
                ct::Vector<uint8_t> all;
                ASSERT_TRUE(ms.seek(0, ct::Seek::Set));
                ASSERT_TRUE(ms.read_all(all));
                ASSERT_EQ(all.size(), size);
                ASSERT_EQ(size ? memcmp(all.data(), ref, size) : 0, 0);
                pos = size;
                break;
            }
            case 8:
                if (rng.below(6) == 0)
                {
                    ct::Vector<uint8_t> taken = ms.take();
                    ASSERT_EQ(taken.size(), size);
                    ASSERT_EQ(size ? memcmp(taken.data(), ref, size) : 0, 0);
                    size = 0;
                    pos = 0;
                    ASSERT_EQ(ms.size(), 0);
                    if (rng.below(2))
                    {
                        ms = ct::MemoryStream(ct::move(taken));
                        // um MemoryStream construido de um vetor comeca no principio com o conteudo do vetor
                        size = static_cast<size_t>(ms.size());
                        pos = 0;
                    }
                }
                break;
            case 9:
            {
                // vista sobre uma copia do conteudo: leitura aleatoria por cima de um Dribble
                ct::Vector<uint8_t> copy;
                for (size_t i = 0; i < size; ++i)
                    copy.push_back(ref[i]);
                ct::MemoryStream view(copy.data(), copy.size());
                Dribble slow(view, rng, 1 + rng.below(5));
                ct::Vector<uint8_t> out;
                ASSERT_TRUE(slow.read_all(out));
                ASSERT_EQ(out.size(), size);
                ASSERT_EQ(size ? memcmp(out.data(), ref, size) : 0, 0);
                ct::String text;
                view.seek(0, ct::Seek::Set);
                Dribble slow2(view, rng, 1 + rng.below(5));
                ASSERT_TRUE(slow2.read_all(text));
                ASSERT_EQ(text.size(), size);
                ASSERT_EQ(size ? memcmp(text.data(), ref, size) : 0, 0);
                break;
            }
            }
        }
    }

    void fuzz_substream(uint64_t seed, int iters)
    {
        Rng rng(seed);
        unsigned char data[kMaxBytes];
        const size_t total = 1 + rng.below(kMaxBytes);
        for (size_t i = 0; i < total; ++i)
            data[i] = static_cast<unsigned char>(rng.next());
        ct::MemoryStream base(data, total);
        const int64_t total64 = static_cast<int64_t>(total);

        for (int round = 0; round < iters / 40 + 1; ++round)
        {
            const int64_t offset = static_cast<int64_t>(rng.below(static_cast<uint32_t>(total + 40))) - 10;
            const int64_t length = static_cast<int64_t>(rng.below(static_cast<uint32_t>(total + 40))) - 5;
            const int64_t off = offset < 0 ? 0 : offset;                 // o SubStream recorta o que for negativo
            const int64_t len = length < 0 ? 0 : length;
            const int64_t avail = off < total64 ? total64 - off : 0;     // o que a base tem a partir de off
            const int64_t window = len < avail ? len : avail;
            ct::SubStream sub(base, offset, length);
            ASSERT_EQ(sub.size(), window);
            ASSERT_EQ(sub.tell(), 0);
            int64_t pos = 0;
            for (int it = 0; it < 40; ++it)
            {
                switch (rng.below(5))
                {
                case 0:
                case 1:
                {
                    unsigned char buf[64];
                    const size_t n = rng.below(60);
                    const size_t r = sub.read(buf, n);
                    const size_t want = pos < window ? (n < static_cast<size_t>(window - pos) ? n : static_cast<size_t>(window - pos)) : 0;
                    ASSERT_EQ(r, want);
                    for (size_t i = 0; i < r; ++i)
                        ASSERT_EQ(buf[i], data[off + pos + static_cast<int64_t>(i)]);
                    pos += static_cast<int64_t>(r);
                    break;
                }
                case 2:
                {
                    // a posicao aceita vai de 0 a len (mesmo alem do que a base tem)
                    const int64_t delta = static_cast<int64_t>(rng.below(static_cast<uint32_t>(2 * len + 41))) - (len + 20);
                    const uint32_t origin = rng.below(3);
                    const int64_t from = origin == 0 ? 0 : origin == 1 ? pos : len;
                    const int64_t next = from + delta;
                    const bool want = next >= 0 && next <= len;
                    const bool got = sub.seek(delta, origin == 0 ? ct::Seek::Set : origin == 1 ? ct::Seek::Cur : ct::Seek::End);
                    ASSERT_EQ(got, want);
                    if (want)
                        pos = next;
                    break;
                }
                case 3:
                    ASSERT_EQ(sub.tell(), pos);
                    ASSERT_EQ(sub.eof(), pos >= window);
                    break;
                case 4:
                {
                    ASSERT_TRUE(sub.seek(0, ct::Seek::Set));
                    ct::Vector<uint8_t> all;
                    ASSERT_TRUE(sub.read_all(all));
                    ASSERT_EQ(static_cast<int64_t>(all.size()), window);
                    for (size_t i = 0; i < all.size(); ++i)
                        ASSERT_EQ(all[i], data[off + static_cast<int64_t>(i)]);
                    pos = window;
                    break;
                }
                }
            }
        }
    }

    // ---- BinaryWriter / BinaryReader ---------------------------------------------------------------------------

    enum Kind
    {
        kU8,
        kU16,
        kU32,
        kU64,
        kI8,
        kI16,
        kI32,
        kI64,
        kF32,
        kF64,
        kBool,
        kVarint,
        kString,
        kBytes,
        kPod,
        kKinds
    };

    struct PodValue
    {
        int32_t a;
        uint16_t b;
        uint8_t c;
        uint8_t d;
        double e;
    };

    struct Item
    {
        Kind kind;
        uint64_t bits;
        ct::String text;
        PodValue pod;
    };

    uint64_t interesting(Rng &rng)
    {
        switch (rng.below(8))
        {
        case 0:
            return 0;
        case 1:
            return ~0ull;
        case 2:
            return 1ull << 63;
        case 3:
            return (1ull << 63) - 1;
        case 4:
            return 0x80ull << (8 * rng.below(8));
        case 5:
            return 0x7full << (8 * rng.below(8));
        default:
            return rng.next64();
        }
    }

    void fuzz_binary(uint64_t seed, int iters)
    {
        Rng rng(seed);
        for (int round = 0; round < iters / 30 + 1; ++round)
        {
            ct::Vector<Item> items;
            ct::MemoryStream mem;
            Dribble out(mem, rng, 1 + rng.below(7));
            ct::BinaryWriter w(out);
            const size_t count = 1 + rng.below(30);
            for (size_t i = 0; i < count; ++i)
            {
                Item it;
                it.kind = static_cast<Kind>(rng.below(kKinds));
                it.bits = interesting(rng);
                memset(&it.pod, 0, sizeof(it.pod));
                switch (it.kind)
                {
                case kU8: w.u8(static_cast<uint8_t>(it.bits)); break;
                case kU16: w.u16(static_cast<uint16_t>(it.bits)); break;
                case kU32: w.u32(static_cast<uint32_t>(it.bits)); break;
                case kU64: w.u64(it.bits); break;
                case kI8: w.i8(static_cast<int8_t>(it.bits)); break;
                case kI16: w.i16(static_cast<int16_t>(it.bits)); break;
                case kI32: w.i32(static_cast<int32_t>(it.bits)); break;
                case kI64: w.i64(static_cast<int64_t>(it.bits)); break;
                case kF32:
                {
                    const uint32_t b = static_cast<uint32_t>(it.bits);
                    float f;
                    memcpy(&f, &b, 4);
                    w.f32(f);
                    break;
                }
                case kF64:
                {
                    double d;
                    memcpy(&d, &it.bits, 8);
                    w.f64(d);
                    break;
                }
                case kBool: w.boolean((it.bits & 1) != 0); break;
                case kVarint: w.varint(static_cast<uint32_t>(it.bits)); break;
                case kString:
                case kBytes:
                {
                    const size_t n = rng.below(rng.below(5) == 0 ? 6000 : 40);
                    for (size_t k = 0; k < n; ++k)
                        it.text.push_back(static_cast<char>(rng.next()));
                    if (it.kind == kString)
                        w.string(it.text);
                    else
                    {
                        ct::Vector<uint8_t> raw;
                        for (size_t k = 0; k < n; ++k)
                            raw.push_back(static_cast<uint8_t>(it.text[k]));
                        ASSERT_TRUE(w.bytes(raw.data(), raw.size()));
                    }
                    break;
                }
                case kPod:
                    it.pod.a = static_cast<int32_t>(it.bits);
                    it.pod.b = static_cast<uint16_t>(it.bits >> 16);
                    it.pod.c = static_cast<uint8_t>(it.bits >> 32);
                    it.pod.d = static_cast<uint8_t>(it.bits >> 40);
                    memcpy(&it.pod.e, &it.bits, 8);
                    ASSERT_TRUE(w.pod(it.pod));
                    break;
                case kKinds: break;
                }
                items.push_back(ct::move(it));
            }
            ASSERT_TRUE(w.ok());

            // leitura completa por cima de um fluxo que da poucos bytes de cada vez
            const size_t cut_options = 1 + rng.below(3);
            for (size_t cut_try = 0; cut_try < cut_options; ++cut_try)
            {
                const size_t total = static_cast<size_t>(mem.size());
                const bool truncate = cut_try != 0 && total > 0;
                const size_t keep = truncate ? rng.below(static_cast<uint32_t>(total)) : total;
                ct::MemoryStream src(mem.data().data(), keep);
                Dribble in(src, rng, 1 + rng.below(7));
                ct::BinaryReader r(in);
                bool any_failed = false;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    const Item &it = items[i];
                    switch (it.kind)
                    {
                    case kU8: { const uint8_t v = r.u8(); if (r.ok()) { ASSERT_EQ(v, static_cast<uint8_t>(it.bits)); } break; }
                    case kU16: { const uint16_t v = r.u16(); if (r.ok()) { ASSERT_EQ(v, static_cast<uint16_t>(it.bits)); } break; }
                    case kU32: { const uint32_t v = r.u32(); if (r.ok()) { ASSERT_EQ(v, static_cast<uint32_t>(it.bits)); } break; }
                    case kU64: { const uint64_t v = r.u64(); if (r.ok()) { ASSERT_EQ(v, it.bits); } break; }
                    case kI8: { const int8_t v = r.i8(); if (r.ok()) { ASSERT_EQ(v, static_cast<int8_t>(it.bits)); } break; }
                    case kI16: { const int16_t v = r.i16(); if (r.ok()) { ASSERT_EQ(v, static_cast<int16_t>(it.bits)); } break; }
                    case kI32: { const int32_t v = r.i32(); if (r.ok()) { ASSERT_EQ(v, static_cast<int32_t>(it.bits)); } break; }
                    case kI64: { const int64_t v = r.i64(); if (r.ok()) { ASSERT_EQ(v, static_cast<int64_t>(it.bits)); } break; }
                    case kF32:
                    {
                        const float v = r.f32();
                        if (r.ok())
                        {
                            uint32_t got;
                            memcpy(&got, &v, 4);
                            ASSERT_EQ(got, static_cast<uint32_t>(it.bits));
                        }
                        break;
                    }
                    case kF64:
                    {
                        const double v = r.f64();
                        if (r.ok())
                        {
                            uint64_t got;
                            memcpy(&got, &v, 8);
                            ASSERT_EQ(got, it.bits);
                        }
                        break;
                    }
                    case kBool: { const bool v = r.boolean(); if (r.ok()) { ASSERT_EQ(v, (it.bits & 1) != 0); } break; }
                    case kVarint: { const uint32_t v = r.varint(); if (r.ok()) { ASSERT_EQ(v, static_cast<uint32_t>(it.bits)); } break; }
                    case kString:
                    {
                        ct::String s;
                        const bool ok = r.string(s);
                        ASSERT_EQ(ok, r.ok());
                        if (ok)
                        {
                                ASSERT_TRUE(s == it.text);
                        }
                        break;
                    }
                    case kBytes:
                    {
                        ct::Vector<uint8_t> raw;
                        const bool ok = r.bytes(raw, it.text.size());
                        ASSERT_EQ(ok, r.ok());
                        if (ok)
                        {
                            ASSERT_EQ(raw.size(), it.text.size());
                            ASSERT_EQ(raw.size() ? memcmp(raw.data(), it.text.data(), raw.size()) : 0, 0);
                        }
                        break;
                    }
                    case kPod:
                    {
                        PodValue got;
                        memset(&got, 0xAB, sizeof(got));
                        const bool ok = r.pod(got);
                        ASSERT_EQ(ok, r.ok());
                        if (ok)
                        {
                            ASSERT_EQ(got.a, it.pod.a);
                            ASSERT_EQ(got.b, it.pod.b);
                            ASSERT_EQ(got.c, it.pod.c);
                            ASSERT_EQ(got.d, it.pod.d);
                            ASSERT_EQ(memcmp(&got.e, &it.pod.e, 8), 0);
                        }
                        break;
                    }
                    case kKinds: break;
                    }
                    if (!r.ok())
                        any_failed = true;
                    if (any_failed)
                    {
                            ASSERT_FALSE(r.ok()) << "ok() voltou a verdadeiro depois de uma falha";
                    }
                }
                if (!truncate)
                {
                        ASSERT_TRUE(r.ok());
                }
                else
                    ASSERT_FALSE(r.ok()) << "um stream truncado em " << keep << " de " << total << " bytes leu tudo sem falhar";
            }
        }
    }

    // ---- TextReader ------------------------------------------------------------------------------------------

    bool model_space(int c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
    }

    // modelo: os bytes do ficheiro (sem o BOM, que o TextReader deita fora se for o primeiro que le) e uma posicao
    void fuzz_text_reader(uint64_t seed, int iters)
    {
        Rng rng(seed);
        static const char kAlphabet[] = "ab  \n\n\r\t1-9.e+x\v\f";
        for (int round = 0; round < iters / 25 + 1; ++round)
        {
            unsigned char data[700];
            size_t total = 0;
            const uint32_t bom = rng.below(6);
            if (bom == 0)
            {
                data[total++] = 0xEF;
                data[total++] = 0xBB;
                data[total++] = 0xBF;
            }
            else if (bom == 1)
            {
                data[total++] = 0xEF;
                data[total++] = 0xBB;
            }
            const size_t n = rng.below(rng.below(4) == 0 ? 600 : 40);
            for (size_t i = 0; i < n && total < 690; ++i)
                data[total++] = rng.below(40) == 0 ? static_cast<unsigned char>(0x80 + rng.below(128))
                                                  : static_cast<unsigned char>(kAlphabet[rng.below(sizeof(kAlphabet) - 1)]);
            const bool has_bom = total >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
            const unsigned char *body = data + (has_bom ? 3 : 0);
            const size_t body_size = total - (has_bom ? 3 : 0);

            ct::MemoryStream mem(data, total);
            Dribble slow(mem, rng, 1 + rng.below(9));
            ct::TextReader reader(slow);
            size_t pos = 0;
            bool started = false;
            bool finished = false;
            ct::String trace("dados:");   // para a mensagem de uma falha: o que havia e o que se fez
            for (size_t i = 0; i < total; ++i)
            {
                trace.push_back(' ');
                trace.append_number(static_cast<int>(data[i]));
            }
            trace.append(" | ops:");
            for (int op = 0; op < 40 && !finished; ++op)
            {
                const uint32_t which = rng.below(8);
                trace.push_back(' ');
                trace.append_number(static_cast<int>(which));
                SCOPED_TRACE(trace.c_str());
                switch (which)
                {
                case 0:
                {
                    const int got = reader.get();
                    started = true;
                    const int want = pos < body_size ? body[pos] : -1;
                    ASSERT_EQ(got, want);
                    if (want != -1)
                        ++pos;
                    break;
                }
                case 1:
                {
                    const int got = reader.peek();
                    started = true;
                    ASSERT_EQ(got, pos < body_size ? static_cast<int>(body[pos]) : -1);
                    break;
                }
                case 2:
                case 3:
                {
                    ct::String line("lixo antigo");
                    const bool got = reader.read_line(line);
                    started = true;
                    ct::String want;
                    bool want_ok = false;
                    while (pos < body_size)
                    {
                        const unsigned char c = body[pos++];
                        if (c == '\n')
                        {
                            want_ok = true;
                            break;
                        }
                        if (c == '\r')
                        {
                            if (pos < body_size && body[pos] == '\n')
                                ++pos;
                            want_ok = true;
                            break;
                        }
                        want.push_back(static_cast<char>(c));
                    }
                    if (!want_ok)
                        want_ok = !want.empty();
                    ASSERT_EQ(got, want_ok);
                    ASSERT_TRUE(line == want) << "read_line deu [" << line.c_str() << "] em vez de [" << want.c_str() << "]";
                    break;
                }
                case 4:
                {
                    ct::String word("lixo antigo");
                    const bool got = reader.read_word(word);
                    started = true;
                    while (pos < body_size && model_space(body[pos]))
                        ++pos;
                    ct::String want;
                    while (pos < body_size && !model_space(body[pos]))
                        want.push_back(static_cast<char>(body[pos++]));
                    if (!want.empty() && pos < body_size)
                        ++pos;   // o espaco que acaba a palavra e lido (get) e nao fica
                    ASSERT_EQ(got, !want.empty());
                    ASSERT_TRUE(word == want);
                    break;
                }
                case 5:
                {
                    long long v = 7;
                    const bool got = reader.read_int(v);
                    started = true;
                    while (pos < body_size && model_space(body[pos]))
                        ++pos;
                    ct::String want;
                    while (pos < body_size && !model_space(body[pos]))
                        want.push_back(static_cast<char>(body[pos++]));
                    if (!want.empty() && pos < body_size)
                        ++pos;
                    if (want.empty())
                        ASSERT_FALSE(got);
                    else
                    {
                        char *end = nullptr;
                        errno = 0;
                        const long long expected = strtoll(want.c_str(), &end, 10);
                        const bool ok = *end == '\0' && errno != ERANGE;
                        ASSERT_EQ(got, ok);
                        ASSERT_EQ(v, expected);
                    }
                    break;
                }
                case 6:
                {
                    ct::String all("lixo antigo");
                    ASSERT_TRUE(reader.read_all(all));
                    started = true;
                    ct::String want(reinterpret_cast<const char *>(body) + pos, body_size - pos);
                    ASSERT_TRUE(all == want);
                    pos = body_size;
                    finished = true;
                    break;
                }
                case 7:
                    if (started)
                    {
                        ASSERT_EQ(reader.eof(), pos >= body_size) << "pos " << pos << " de " << body_size;
                    }
                    break;
                }
            }
        }
    }

    // ---- Json ----------------------------------------------------------------------------------------------

    void random_string(Rng &rng, ct::String &out)
    {
        const size_t n = rng.below(rng.below(6) == 0 ? 60 : 12);
        for (size_t i = 0; i < n; ++i)
        {
            switch (rng.below(6))
            {
            case 0: out.push_back(static_cast<char>(rng.below(32))); break;
            case 1: out.push_back("\"\\/\b\f\n\r\t"[rng.below(8)]); break;
            case 2: out.push_back(static_cast<char>(0x80 + rng.below(128))); break;
            default: out.push_back(static_cast<char>(32 + rng.below(95))); break;
            }
        }
    }

    ct::Json random_json(Rng &rng, int depth)
    {
        const uint32_t k = rng.below(depth > 4 ? 8 : 10);
        switch (k)
        {
        case 0: return ct::Json();
        case 1: return ct::Json((rng.next() & 1) != 0);
        case 2: return ct::Json(static_cast<int64_t>(interesting(rng)));
        case 3:
        {
            // so uint de verdade: um que cabe em int64 le-se de volta como int
            return ct::Json(static_cast<uint64_t>(interesting(rng) | (1ull << 63)));
        }
        case 4:
        {
            uint64_t bits = interesting(rng);
            if (rng.below(2))
                bits = rng.next64();
            double d;
            memcpy(&d, &bits, 8);
            if (d != d || d - d != 0)
                d = 0.5;
            return ct::Json(d);
        }
        case 5:
        case 6:
        case 7:
        {
            ct::String s;
            random_string(rng, s);
            return ct::Json(ct::move(s));
        }
        case 8:
        {
            ct::Json a = ct::Json::array();
            const uint32_t n = rng.below(6);
            for (uint32_t i = 0; i < n; ++i)
                a.push_back(random_json(rng, depth + 1));
            return a;
        }
        default:
        {
            ct::Json o = ct::Json::object();
            const uint32_t n = rng.below(6);
            for (uint32_t i = 0; i < n; ++i)
            {
                ct::String key;
                random_string(rng, key);
                key.append_number(static_cast<int>(i));
                o.set(ct::move(key), random_json(rng, depth + 1));
            }
            return o;
        }
        }
    }

    // igualdade exacta: os reais comparam-se em bits (-0.0 e 0.0 distinguem-se) e os tipos tem de bater
    bool same_json(const ct::Json &a, const ct::Json &b)
    {
        if (a.type() != b.type())
            return false;
        switch (a.type())
        {
        case ct::Json::Null: return true;
        case ct::Json::Bool: return a.as_bool() == b.as_bool();
        case ct::Json::Int: return a.as_int() == b.as_int();
        case ct::Json::Uint: return a.as_uint() == b.as_uint();
        case ct::Json::Real:
        {
            const double x = a.as_double(), y = b.as_double();
            return memcmp(&x, &y, 8) == 0;
        }
        case ct::Json::Str: return a.str() == b.str();
        case ct::Json::Arr:
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (!same_json(a[i], b[i]))
                    return false;
            return true;
        case ct::Json::Obj:
        {
            if (a.size() != b.size())
                return false;
            const ct::Json::Object &x = a.members();
            const ct::Json::Object &y = b.members();
            for (size_t i = 0; i < x.size(); ++i)
                if (!(x[i].key == y[i].key) || !same_json(x[i].value, y[i].value))
                    return false;
            return true;
        }
        }
        return false;
    }

    void fuzz_json(uint64_t seed, int iters)
    {
        Rng rng(seed);
        for (int it = 0; it < iters / 8 + 1; ++it)
        {
            const ct::Json doc = random_json(rng, 0);
            const ct::String compact = doc.dump();
            const ct::String pretty = doc.dump(static_cast<int>(rng.below(5)));
            ct::Json::Error e1, e2;
            const ct::Json back = ct::Json::parse(compact, &e1);
            ASSERT_FALSE(e1) << e1.message << " em [" << compact.c_str() << "]";
            ASSERT_TRUE(same_json(doc, back)) << compact.c_str();
            const ct::Json back_pretty = ct::Json::parse(pretty, &e2);
            ASSERT_FALSE(e2) << e2.message << " em [" << pretty.c_str() << "]";
            ASSERT_TRUE(same_json(doc, back_pretty)) << pretty.c_str();
            ASSERT_TRUE(back.dump() == compact);

            ct::Json copy(doc);
            ASSERT_TRUE(same_json(doc, copy));
            ASSERT_TRUE(doc == copy);
            ct::Json moved(ct::move(copy));
            ASSERT_TRUE(same_json(doc, moved));
            ASSERT_TRUE(copy.is_null());
            copy = moved;
            ASSERT_TRUE(same_json(doc, copy));
            {
                ct::Json &same = copy;   // por referencia: o clang avisa da auto-atribuicao directa
                copy = same;
            }
            ASSERT_TRUE(same_json(doc, copy));
            ct::Json swapped;
            swapped.swap(copy);
            ASSERT_TRUE(same_json(doc, swapped));

            // atribuir-se um descendente por copia e por move
            if (doc.is_array() && doc.size())
            {
                ct::Json t(doc);
                const size_t at = rng.below(static_cast<uint32_t>(t.size()));
                const ct::String want = t[at].dump();
                if (rng.below(2))
                    t = t[at];
                else
                    t = ct::move(t[at]);
                ASSERT_TRUE(t.dump() == want);
            }
            if (doc.is_object() && doc.size())
            {
                ct::Json t(doc);
                const size_t at = rng.below(static_cast<uint32_t>(t.size()));
                const ct::String want = t.members()[at].value.dump();
                ct::Json &child = t.members()[at].value;
                if (rng.below(2))
                    t = child;
                else
                    t = ct::move(child);
                ASSERT_TRUE(t.dump() == want);
            }

            // empurrar para dentro de si mesmo uma copia de um elemento dele
            if (doc.is_array() && doc.size())
            {
                ct::Json t(doc);
                const size_t n = t.size();
                t.push_back(t[rng.below(static_cast<uint32_t>(n))]);
                ASSERT_EQ(t.size(), n + 1);
                ASSERT_TRUE(t[n] == t[n] && same_json(t[n], t[n]));
                ct::Json self = t;
                self.push_back(self);
                ASSERT_EQ(self.size(), n + 2);
                ASSERT_TRUE(same_json(self[n + 1], t));
            }

            // texto estragado: nao pode rebentar
            ct::String broken = compact;
            if (broken.size() > 1)
            {
                broken.resize(rng.below(static_cast<uint32_t>(broken.size())));
                ct::Json::Error e;
                ct::Json::parse(broken, &e);
            }
        }
    }

    // ---- Function, Rc, Variant ---------------------------------------------------------------------------------

    struct SmallCapture
    {
        Counted c;
        int operator()(int x) const { return c.id + x; }
    };

    struct BigCapture
    {
        Counted c;
        char pad[64];
        int operator()(int x) const { return c.id * 2 + x; }
    };

    int free_function(int x) { return x - 1000; }

    void fuzz_function(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Counted::live = 0;
        Counted::bad = 0;
        {
            enum
            {
                kN = 12
            };
            ct::Function<int(int)> f[kN];
            int kind[kN];   // 0 vazia, 1 pequena, 2 grande, 3 ponteiro para funcao
            int id[kN];
            for (int i = 0; i < kN; ++i)
            {
                kind[i] = 0;
                id[i] = 0;
            }
            for (int it = 0; it < iters; ++it)
            {
                const int a = static_cast<int>(rng.below(kN));
                const int b = static_cast<int>(rng.below(kN));
                const int v = static_cast<int>(rng.below(100000));
                switch (rng.below(10))
                {
                case 0:
                    f[a] = SmallCapture{Counted(v)};
                    kind[a] = 1;
                    id[a] = v;
                    break;
                case 1:
                {
                    BigCapture big;
                    big.c = Counted(v);
                    memset(big.pad, 7, sizeof(big.pad));
                    f[a] = big;
                    kind[a] = 2;
                    id[a] = v;
                    break;
                }
                case 2:
                    f[a] = ct::Function<int(int)>(&free_function);
                    kind[a] = 3;
                    id[a] = 0;
                    break;
                case 3:
                    f[a] = nullptr;
                    kind[a] = 0;
                    break;
                case 4:
                    if (a != b)
                    {
                        f[a] = f[b];
                        kind[a] = kind[b];
                        id[a] = id[b];
                    }
                    break;
                case 5:
                    if (a != b)
                    {
                        f[a] = ct::move(f[b]);
                        kind[a] = kind[b];
                        id[a] = id[b];
                        kind[b] = 0;
                    }
                    break;
                case 6:
                    if (a != b)
                    {
                        f[a].swap(f[b]);
                        const int k = kind[a], d = id[a];
                        kind[a] = kind[b];
                        id[a] = id[b];
                        kind[b] = k;
                        id[b] = d;
                    }
                    break;
                case 7:
                    f[a] = f[a];
                    break;
                case 8:
                {
                    ct::Function<int(int)> copy(f[a]);
                    ct::Function<int(int)> moved(ct::move(copy));
                    ASSERT_EQ(static_cast<bool>(moved), kind[a] != 0);
                    ASSERT_FALSE(static_cast<bool>(copy));
                    if (kind[a])
                    {
                        const int want = kind[a] == 1 ? id[a] + v : kind[a] == 2 ? id[a] * 2 + v : v - 1000;
                        ASSERT_EQ(moved(v), want);
                    }
                    break;
                }
                case 9:
                    break;
                }
                for (int i = 0; i < kN; ++i)
                {
                    ASSERT_EQ(static_cast<bool>(f[i]), kind[i] != 0);
                    if (kind[i])
                    {
                        const int want = kind[i] == 1 ? id[i] + 5 : kind[i] == 2 ? id[i] * 2 + 5 : 5 - 1000;
                        ASSERT_EQ(f[i](5), want);
                    }
                }
            }
        }
        ASSERT_EQ(Counted::live, 0);
        ASSERT_EQ(Counted::bad, 0);
    }

    void fuzz_rc(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Counted::live = 0;
        Counted::bad = 0;
        {
            enum
            {
                kN = 10
            };
            // cada Rc/Weak guarda o "uid" do objeto que partilha (0 = vazio): o modelo e esse numero
            ct::Rc<Counted> strong[kN];
            ct::Weak<Counted> weak[kN];
            int suid[kN];
            int wuid[kN];
            for (int i = 0; i < kN; ++i)
                suid[i] = wuid[i] = 0;
            int next_uid = 1;

            for (int it = 0; it < iters; ++it)
            {
                const int a = static_cast<int>(rng.below(kN));
                const int b = static_cast<int>(rng.below(kN));
                switch (rng.below(11))
                {
                case 0:
                    strong[a] = ct::make_rc<Counted>(next_uid);
                    suid[a] = next_uid++;
                    break;
                case 1:
                    strong[a] = strong[b];
                    suid[a] = suid[b];
                    break;
                case 2:
                    strong[a] = ct::move(strong[b]);
                    if (a != b)
                    {
                        suid[a] = suid[b];
                        suid[b] = 0;
                    }
                    break;
                case 3:
                    strong[a].reset();
                    suid[a] = 0;
                    break;
                case 4:
                    weak[a] = strong[b];
                    wuid[a] = suid[b];
                    break;
                case 5:
                    weak[a] = weak[b];
                    wuid[a] = wuid[b];
                    break;
                case 6:
                {
                    ct::Rc<Counted> got = weak[a].lock();
                    bool alive = false;
                    for (int i = 0; i < kN; ++i)
                        if (wuid[a] != 0 && suid[i] == wuid[a])
                            alive = true;
                    ASSERT_EQ(static_cast<bool>(got), alive);
                    ASSERT_EQ(weak[a].expired(), !alive);
                    if (alive)
                    {
                            ASSERT_EQ(got->id, wuid[a]);
                    }
                    break;
                }
                case 7:
                    weak[a].reset();
                    wuid[a] = 0;
                    break;
                case 8:
                {
                    strong[a].swap(strong[b]);
                    const int u = suid[a];
                    suid[a] = suid[b];
                    suid[b] = u;
                    break;
                }
                case 9:
                    strong[a] = strong[a];
                    break;
                case 10:
                    weak[a] = ct::move(weak[b]);
                    if (a != b)
                    {
                        wuid[a] = wuid[b];
                        wuid[b] = 0;
                    }
                    break;
                }
                int alive_objects = 0;
                for (int i = 0; i < kN; ++i)
                {
                    if (suid[i] == 0)
                    {
                        ASSERT_FALSE(static_cast<bool>(strong[i]));
                        ASSERT_EQ(strong[i].use_count(), 0u);
                        continue;
                    }
                    int count = 0;
                    bool first = true;
                    for (int j = 0; j < kN; ++j)
                    {
                        if (suid[j] == suid[i])
                        {
                            ++count;
                            if (j < i)
                                first = false;
                        }
                    }
                    if (first)
                        ++alive_objects;
                    ASSERT_EQ(strong[i].use_count(), static_cast<uint32_t>(count));
                    ASSERT_EQ(strong[i]->id, suid[i]);
                }
                ASSERT_EQ(Counted::live, alive_objects);
            }
        }
        ASSERT_EQ(Counted::live, 0);
        ASSERT_EQ(Counted::bad, 0);
    }

    typedef ct::Variant<int, Counted, ct::String> Var;

    void fuzz_variant(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Counted::live = 0;
        Counted::bad = 0;
        {
            enum
            {
                kN = 8
            };
            Var v[kN];
            int kind[kN];
            int val[kN];
            for (int i = 0; i < kN; ++i)
            {
                kind[i] = 0;
                val[i] = 0;
            }
            for (int it = 0; it < iters; ++it)
            {
                const int a = static_cast<int>(rng.below(kN));
                const int b = static_cast<int>(rng.below(kN));
                const int x = static_cast<int>(rng.below(1000));
                switch (rng.below(9))
                {
                case 0:
                    v[a] = x;
                    kind[a] = 0;
                    val[a] = x;
                    break;
                case 1:
                    v[a] = Counted(x);
                    kind[a] = 1;
                    val[a] = x;
                    break;
                case 2:
                {
                    ct::String s("uma string com mais de vinte e tres bytes, numero ");
                    s.append_number(x);
                    v[a] = ct::move(s);
                    kind[a] = 2;
                    val[a] = x;
                    break;
                }
                case 3:
                    v[a] = v[b];
                    kind[a] = kind[b];
                    val[a] = val[b];
                    break;
                case 4:
                    if (a != b)
                    {
                        v[a] = ct::move(v[b]);
                        kind[a] = kind[b];
                        val[a] = val[b];
                        // o movido fica num estado valido mas nao especificado: relê-se do proprio objeto
                        kind[b] = static_cast<int>(v[b].index());
                        val[b] = -1;
                    }
                    break;
                case 5:
                    if (a != b)
                    {
                        v[a].swap(v[b]);
                        const int k = kind[a], c = val[a];
                        kind[a] = kind[b];
                        val[a] = val[b];
                        kind[b] = k;
                        val[b] = c;
                    }
                    break;
                case 6:
                    v[a] = v[a];
                    break;
                case 7:
                {
                    Var copy(v[a]);
                    ASSERT_EQ(static_cast<int>(copy.index()), kind[a]);
                    Var moved(ct::move(copy));
                    ASSERT_EQ(static_cast<int>(moved.index()), kind[a]);
                    break;
                }
                case 8:
                    break;
                }
                for (int i = 0; i < kN; ++i)
                {
                    ASSERT_EQ(static_cast<int>(v[i].index()), kind[i]);
                    if (val[i] == -1)
                        continue;
                    if (kind[i] == 0)
                    {
                            ASSERT_EQ(v[i].get<int>(), val[i]);
                    }
                    else if (kind[i] == 1)
                    {
                            ASSERT_EQ(v[i].get<Counted>().id, val[i]);
                    }
                    else
                    {
                        ct::String want("uma string com mais de vinte e tres bytes, numero ");
                        want.append_number(val[i]);
                        ASSERT_TRUE(v[i].get<ct::String>() == want);
                    }
                }
            }
        }
        ASSERT_EQ(Counted::live, 0);
        ASSERT_EQ(Counted::bad, 0);
    }

    // ---- SlotMap, Pool, Arena ------------------------------------------------------------------------------------

    void fuzz_slotmap(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Counted::live = 0;
        Counted::bad = 0;
        {
            enum
            {
                kMax = 90
            };
            ct::SlotMap<Counted> map;
            ct::Handle<Counted> handles[kMax];
            int ids[kMax];
            bool alive[kMax];
            size_t n = 0;
            size_t live = 0;
            for (int it = 0; it < iters; ++it)
            {
                const size_t k = n ? rng.below(static_cast<uint32_t>(n)) : 0;
                switch (rng.below(8))
                {
                case 0:
                case 1:
                    if (n < kMax)
                    {
                        const int id = static_cast<int>(rng.below(100000));
                        handles[n] = rng.below(2) ? map.insert(Counted(id)) : map.emplace(id);
                        ids[n] = id;
                        alive[n] = true;
                        ++n;
                        ++live;
                    }
                    break;
                case 2:
                    if (n && alive[k])
                    {
                        ASSERT_TRUE(map.erase(handles[k]));
                        alive[k] = false;
                        --live;
                        ASSERT_FALSE(map.erase(handles[k]));
                    }
                    break;
                case 3:
                    if (n && !alive[k])
                    {
                        ASSERT_EQ(map.get(handles[k]), nullptr);
                        ASSERT_FALSE(map.contains(handles[k]));
                    }
                    break;
                case 4:
                    if (n && alive[k])
                    {
                        ASSERT_NE(map.get(handles[k]), nullptr);
                        ASSERT_EQ(map[handles[k]].id, ids[k]);
                        const size_t at = map.index_of(handles[k]);
                        ASSERT_TRUE(map.handle_at(at) == handles[k]);
                    }
                    break;
                case 5:
                    if (n && alive[k] && n < kMax)
                    {
                        // inserir um valor que e uma referencia para dentro do proprio mapa
                        handles[n] = map.insert(map[handles[k]]);
                        ids[n] = ids[k];
                        alive[n] = true;
                        ++n;
                        ++live;
                    }
                    break;
                case 6:
                    if (rng.below(10) == 0)
                    {
                        map.clear();
                        for (size_t i = 0; i < n; ++i)
                            alive[i] = false;
                        live = 0;
                    }
                    break;
                case 7:
                    break;
                }
                ASSERT_EQ(map.size(), live);
                ASSERT_EQ(static_cast<size_t>(Counted::live), live);
                if (it % 16 == 0)
                {
                    size_t seen = 0;
                    for (const Counted &c : map)
                    {
                        (void)c;
                        ++seen;
                    }
                    ASSERT_EQ(seen, live);
                    for (size_t i = 0; i < n; ++i)
                        ASSERT_EQ(map.contains(handles[i]), alive[i]);
                }
            }
        }
        ASSERT_EQ(Counted::live, 0);
        ASSERT_EQ(Counted::bad, 0);
    }

    struct alignas(32) Wide
    {
        int id;
        char pad[28];
    };

    void fuzz_pool(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Counted::live = 0;
        Counted::bad = 0;
        {
            enum
            {
                kMax = 70
            };
            ct::Pool<Counted> pool(1 + rng.below(9));
            ct::Pool<Wide> wide(1 + rng.below(5));
            Counted *items[kMax];
            int ids[kMax];
            Wide *wides[kMax];
            size_t n = 0, nw = 0;
            for (int it = 0; it < iters; ++it)
            {
                switch (rng.below(8))
                {
                case 0:
                case 1:
                    if (n < kMax)
                    {
                        const int id = static_cast<int>(rng.below(100000));
                        items[n] = pool.create(id);
                        ids[n] = id;
                        ++n;
                    }
                    break;
                case 2:
                    if (n)
                    {
                        const size_t k = rng.below(static_cast<uint32_t>(n));
                        ASSERT_EQ(items[k]->id, ids[k]);
                        pool.destroy(items[k]);
                        items[k] = items[n - 1];
                        ids[k] = ids[n - 1];
                        --n;
                    }
                    break;
                case 3:
                    if (nw < kMax)
                    {
                        Wide *w = wide.create();
                        ASSERT_EQ(reinterpret_cast<uintptr_t>(w) % 32, 0u);
                        w->id = static_cast<int>(nw);
                        wides[nw++] = w;
                    }
                    break;
                case 4:
                    if (nw)
                    {
                        const size_t k = rng.below(static_cast<uint32_t>(nw));
                        wide.destroy(wides[k]);
                        wides[k] = wides[nw - 1];
                        --nw;
                    }
                    break;
                case 5:
                    if (rng.below(12) == 0)
                    {
                        pool.clear();
                        n = 0;
                    }
                    break;
                case 6:
                    if (rng.below(12) == 0)
                    {
                        ct::Pool<Counted> moved(ct::move(pool));
                        pool = ct::move(moved);
                    }
                    break;
                case 7:
                    break;
                }
                ASSERT_EQ(pool.live(), n);
                ASSERT_EQ(static_cast<size_t>(Counted::live), n);
                ASSERT_EQ(wide.live(), nw);
                for (size_t i = 0; i < n && i < 4; ++i)
                    ASSERT_EQ(items[i]->id, ids[i]);
            }
        }
        ASSERT_EQ(Counted::live, 0);
        ASSERT_EQ(Counted::bad, 0);
    }

    void fuzz_arena(uint64_t seed, int iters)
    {
        Rng rng(seed);
        for (int round = 0; round < iters / 60 + 1; ++round)
        {
            ct::Arena arena(1 + rng.below(300));
            struct Block
            {
                unsigned char *p;
                size_t n;
                unsigned char fill;
                size_t align;
            };
            Block blocks[80];
            size_t nb = 0;
            size_t used_model = 0;
            for (int it = 0; it < 60; ++it)
            {
                switch (rng.below(6))
                {
                case 0:
                case 1:
                case 2:
                    if (nb < 80)
                    {
                        const size_t n = rng.below(rng.below(8) == 0 ? 900 : 70);
                        const size_t align = static_cast<size_t>(1) << rng.below(7);
                        unsigned char *p = static_cast<unsigned char *>(arena.allocate(n, align));
                        ASSERT_EQ(reinterpret_cast<uintptr_t>(p) % align, 0u);
                        ASSERT_TRUE(n == 0 || arena.owns(p));
                        const unsigned char fill = static_cast<unsigned char>(1 + rng.below(250));
                        memset(p, fill, n);
                        blocks[nb].p = p;
                        blocks[nb].n = n;
                        blocks[nb].fill = fill;
                        blocks[nb].align = align;
                        ++nb;
                        used_model += n;
                    }
                    break;
                case 3:
                    if (nb)
                    {
                        // realloc do ultimo bloco: cresce no sitio ou copia, o conteudo antigo fica
                        Block &b = blocks[nb - 1];
                        const size_t grown = b.n + rng.below(200);
                        unsigned char *p = static_cast<unsigned char *>(arena.reallocate(b.p, b.n, grown, b.align));
                        ASSERT_EQ(reinterpret_cast<uintptr_t>(p) % b.align, 0u);
                        for (size_t i = 0; i < b.n; ++i)
                            ASSERT_EQ(p[i], b.fill);
                        used_model += grown - b.n;   // aproximado: so serve para o limite inferior abaixo
                        memset(p, b.fill, grown);
                        b.p = p;
                        b.n = grown;
                    }
                    break;
                case 4:
                    if (rng.below(8) == 0)
                    {
                        arena.reset();
                        nb = 0;
                        used_model = 0;
                        ASSERT_EQ(arena.bytes_used(), 0u);
                    }
                    break;
                case 5:
                    break;
                }
                // nenhum bloco vivo foi pisado por outro
                for (size_t i = 0; i < nb; ++i)
                    for (size_t k = 0; k < blocks[i].n; ++k)
                        ASSERT_EQ(blocks[i].p[k], blocks[i].fill) << "bloco " << i << " pisado";
                ASSERT_GE(arena.bytes_reserved(), 1u);
            }
            (void)used_model;
        }
    }

    // ---- String: conjuntos de caracteres ----------------------------------------------------------------------------

    // modelo: dois ciclos, sem tabelas nem strchr; o NUL do texto nunca pertence ao conjunto (o conjunto acaba nele)
    bool model_in_set(const char *set, char c)
    {
        for (const char *p = set; *p; ++p)
            if (*p == c)
                return c != '\0';
        return false;
    }

    void fuzz_string_sets(uint64_t seed, int iters)
    {
        Rng rng(seed);
        for (int it = 0; it < iters; ++it)
        {
            ct::String s;
            const size_t n = rng.below(rng.below(6) == 0 ? 120 : 14);
            for (size_t i = 0; i < n; ++i)
            {
                const uint32_t k = rng.below(10);
                s.push_back(k == 0 ? '\0' : k == 1 ? static_cast<char>(0x80 + rng.below(128)) : static_cast<char>('a' + rng.below(5)));
            }
            char set[12];
            const size_t m = rng.below(8);
            size_t len = 0;
            for (size_t i = 0; i < m; ++i)
            {
                const uint32_t k = rng.below(8);
                set[len++] = k == 0 ? static_cast<char>(0x80 + rng.below(128)) : static_cast<char>('a' + rng.below(7));
            }
            set[len] = '\0';
            const size_t pos = rng.below(static_cast<uint32_t>(n + 4));

            size_t want_first = ct::String::npos;
            for (size_t i = pos; i < n; ++i)
                if (model_in_set(set, s[i]))
                {
                    want_first = i;
                    break;
                }
            ASSERT_EQ(s.find_first_of(set, pos), want_first) << "find_first_of pos " << pos;

            size_t want_not = ct::String::npos;
            for (size_t i = pos; i < n; ++i)
                if (!model_in_set(set, s[i]))
                {
                    want_not = i;
                    break;
                }
            ASSERT_EQ(s.find_first_not_of(set, pos), want_not) << "find_first_not_of pos " << pos;

            // find_last_of: pos inclusivo (procura em [0, pos]) como na biblioteca standard, por omissao (npos) procura tudo
            const size_t last_pos = rng.below(3) == 0 ? ct::String::npos : rng.below(static_cast<uint32_t>(n + 4));
            const size_t bound = last_pos < n ? last_pos + 1 : n;
            size_t want_last = ct::String::npos;
            for (size_t i = bound; i > 0; --i)
                if (model_in_set(set, s[i - 1]))
                {
                    want_last = i - 1;
                    break;
                }
            ASSERT_EQ(s.find_last_of(set, last_pos), want_last) << "find_last_of pos " << last_pos;

            // o resto da familia find_*, com pos explicito (de 0 a size() + 3): find(char), find(const char *) e
            // find(String), contra ciclos simples; rfind(char) nao tem pos
            const char c = static_cast<char>(rng.below(8) == 0 ? 0 : 'a' + rng.below(6));
            size_t want_char = ct::String::npos;
            for (size_t i = pos; i < n; ++i)
                if (s[i] == c)
                {
                    want_char = i;
                    break;
                }
            ASSERT_EQ(s.find(c, pos), want_char) << "find(char) pos " << pos;

            size_t want_rchar = ct::String::npos;
            for (size_t i = n; i > 0; --i)
                if (s[i - 1] == c)
                {
                    want_rchar = i - 1;
                    break;
                }
            ASSERT_EQ(s.rfind(c), want_rchar) << "rfind(char)";

            char needle[5];
            const size_t nl = rng.below(4);
            for (size_t i = 0; i < nl; ++i)
                needle[i] = static_cast<char>('a' + rng.below(4));
            needle[nl] = '\0';
            size_t want_needle = ct::String::npos;
            if (pos <= n)
            {
                for (size_t i = pos; i + nl <= n; ++i)
                {
                    size_t k = 0;
                    while (k < nl && s[i + k] == needle[k])
                        ++k;
                    if (k == nl)
                    {
                        want_needle = i;
                        break;
                    }
                }
            }
            ASSERT_EQ(s.find(needle, pos), want_needle) << "find(const char *) pos " << pos << " needle " << needle;
            ASSERT_EQ(s.find(ct::String(needle), pos), want_needle) << "find(String) pos " << pos;
        }
    }

    // ---- sort e PriorityQueue -----------------------------------------------------------------------------------

    struct Tagged
    {
        int key;
        int tag;
    };

    bool tagged_less(const Tagged &a, const Tagged &b) { return a.key < b.key; }

    template <typename T>
    void check_sorted_permutation(const T *sorted, const T *orig, size_t n, uint64_t seed)
    {
        for (size_t i = 1; i < n; ++i)
            ASSERT_FALSE(sorted[i] < sorted[i - 1]) << "fora de ordem em " << i << " de " << n << " (semente " << seed << ")";
        // mesma soma de contagens: cada valor de orig aparece em sorted tantas vezes quantas em orig
        for (size_t i = 0; i < n; ++i)
        {
            size_t in_orig = 0, in_sorted = 0;
            for (size_t j = 0; j < n; ++j)
            {
                in_orig += !(orig[j] < orig[i]) && !(orig[i] < orig[j]);
                in_sorted += !(sorted[j] < orig[i]) && !(orig[i] < sorted[j]);
            }
            ASSERT_EQ(in_orig, in_sorted);
            if (n > 200)
                break;   // quadratico: so confirma uma amostra nos arrays grandes
        }
    }

    template <typename T, typename Gen>
    void fuzz_sort_type(Rng &rng, int rounds, Gen gen, uint64_t seed)
    {
        for (int r = 0; r < rounds; ++r)
        {
            static const size_t sizes[] = {0, 1, 2, 3, 23, 24, 25, 100, 127, 128, 129, 300, 700};
            const size_t n = rng.below(3) == 0 ? sizes[rng.below(13)] : rng.below(400);
            ct::Vector<T> v;
            const uint32_t shape = rng.below(5);
            for (size_t i = 0; i < n; ++i)
            {
                v.push_back(gen(rng));
            }
            if (shape == 2)
                ct::sort(v.data(), v.data() + v.size());
            if (shape == 3 && n)
                for (size_t i = 0; i < n; ++i)
                    v[i] = v[0];
            ct::Vector<T> orig(v);
            ct::sort(v.data(), v.data() + v.size());
            check_sorted_permutation(v.data(), orig.data(), n, seed);
            if (testing::Test::HasFatalFailure())
                return;
            // ao contrario
            for (size_t i = 0; i < n; ++i)
                v[i] = orig[n - 1 - i];
            ct::sort(v.begin(), v.end());
            check_sorted_permutation(v.data(), orig.data(), n, seed);
            if (testing::Test::HasFatalFailure())
                return;
        }
    }

    void fuzz_sort(uint64_t seed, int iters)
    {
        Rng rng(seed);
        const int rounds = iters / 200 + 1;
        fuzz_sort_type<int>(rng, rounds, [](Rng &g) { return static_cast<int>(interesting(g)); }, seed);
        fuzz_sort_type<int>(rng, rounds, [](Rng &g) { return static_cast<int>(g.below(10)) - 5; }, seed);
        fuzz_sort_type<unsigned char>(rng, rounds, [](Rng &g) { return static_cast<unsigned char>(g.next()); }, seed);
        fuzz_sort_type<short>(rng, rounds, [](Rng &g) { return static_cast<short>(interesting(g)); }, seed);
        fuzz_sort_type<unsigned short>(rng, rounds, [](Rng &g) { return static_cast<unsigned short>(g.next()); }, seed);
        fuzz_sort_type<long long>(rng, rounds, [](Rng &g) { return static_cast<long long>(interesting(g)); }, seed);
        fuzz_sort_type<unsigned long long>(rng, rounds, [](Rng &g) { return interesting(g); }, seed);
        fuzz_sort_type<unsigned>(rng, rounds, [](Rng &g) { return static_cast<unsigned>(interesting(g)); }, seed);
        fuzz_sort_type<float>(rng, rounds, [](Rng &g) {
            const uint32_t b = static_cast<uint32_t>(interesting(g));
            float f;
            memcpy(&f, &b, 4);
            return (f != f || f - f != 0) ? static_cast<float>(g.below(100)) - 50.0f : f;
        }, seed);
        fuzz_sort_type<double>(rng, rounds, [](Rng &g) {
            const uint64_t b = interesting(g);
            double d;
            memcpy(&d, &b, 8);
            return (d != d || d - d != 0) ? static_cast<double>(g.below(100)) - 50.0 : d;
        }, seed);
        fuzz_sort_type<ct::String>(rng, rounds, [](Rng &g) {
            ct::String s;
            const size_t n = g.below(30);
            for (size_t i = 0; i < n; ++i)
                s.push_back(static_cast<char>('a' + g.below(3)));
            return s;
        }, seed);

        // sort e stable_sort com comparador: a estavel mantem a ordem de chaves iguais
        for (int r = 0; r < rounds; ++r)
        {
            const size_t n = rng.below(500);
            ct::Vector<Tagged> v;
            for (size_t i = 0; i < n; ++i)
            {
                Tagged t;
                t.key = static_cast<int>(rng.below(rng.below(2) ? 8 : 1000));
                t.tag = static_cast<int>(i);
                v.push_back(t);
            }
            ct::Vector<Tagged> unstable(v);
            ct::stable_sort(v.begin(), v.end(), tagged_less);
            for (size_t i = 1; i < n; ++i)
            {
                ASSERT_LE(v[i - 1].key, v[i].key);
                if (v[i - 1].key == v[i].key)
                {
                        ASSERT_LT(v[i - 1].tag, v[i].tag) << "stable_sort trocou chaves iguais";
                }
            }
            ct::sort(unstable.begin(), unstable.end(), tagged_less);
            for (size_t i = 1; i < n; ++i)
                ASSERT_LE(unstable[i - 1].key, unstable[i].key);
        }
    }

    void fuzz_priority_queue(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Counted::live = 0;
        {
            struct ByIdLess
            {
                bool operator()(const Counted &a, const Counted &b) const { return a.id < b.id; }
            };
            ct::PriorityQueue<Counted, ByIdLess> pq;
            int model[200];
            size_t n = 0;
            for (int it = 0; it < iters; ++it)
            {
                switch (rng.below(6))
                {
                case 0:
                case 1:
                case 2:
                    if (n < 200)
                    {
                        const int id = static_cast<int>(rng.below(500));
                        if (rng.below(2))
                            pq.push(Counted(id));
                        else
                            pq.emplace(id);
                        model[n++] = id;
                    }
                    break;
                case 3:
                    if (n)
                    {
                        size_t best = 0;
                        for (size_t i = 1; i < n; ++i)
                            if (model[i] > model[best])
                                best = i;
                        ASSERT_EQ(pq.top().id, model[best]);
                        pq.pop();
                        model[best] = model[--n];
                    }
                    break;
                case 4:
                    if (n && n < 200)
                    {
                        pq.push(pq.top());   // a referencia para dentro do proprio contentor
                        size_t best = 0;
                        for (size_t i = 1; i < n; ++i)
                            if (model[i] > model[best])
                                best = i;
                        model[n++] = model[best];
                    }
                    break;
                case 5:
                    if (rng.below(20) == 0)
                    {
                        pq.clear();
                        n = 0;
                    }
                    break;
                }
                ASSERT_EQ(pq.size(), n);
                ASSERT_EQ(static_cast<size_t>(Counted::live), n);
            }
        }
        ASSERT_EQ(Counted::live, 0);
    }
}

static const uint64_t kSeeds[] = {1, 2, 3, 17, 99, 12345};

#define CT_FUZZ2(name, fn, iters)                                         \
    TEST(Fuzz2, name)                                                     \
    {                                                                     \
        for (size_t s = 0; s < sizeof(kSeeds) / sizeof(kSeeds[0]); ++s)   \
        {                                                                 \
            fn(kSeeds[s], fuzz_iters(iters));                             \
            if (HasFatalFailure())                                        \
            {                                                             \
                ADD_FAILURE() << "semente " << kSeeds[s];                 \
                return;                                                   \
            }                                                             \
        }                                                                 \
    }

CT_FUZZ2(MemoryStream, fuzz_memory_stream, 4000)
CT_FUZZ2(SubStream, fuzz_substream, 4000)
CT_FUZZ2(BinaryRoundTrip, fuzz_binary, 600)
CT_FUZZ2(TextReader, fuzz_text_reader, 2000)
CT_FUZZ2(JsonRoundTrip, fuzz_json, 600)
CT_FUZZ2(Function, fuzz_function, 3000)
CT_FUZZ2(Rc, fuzz_rc, 3000)
CT_FUZZ2(Variant, fuzz_variant, 3000)
CT_FUZZ2(SlotMap, fuzz_slotmap, 3000)
CT_FUZZ2(Pool, fuzz_pool, 3000)
CT_FUZZ2(Arena, fuzz_arena, 600)
CT_FUZZ2(StringSets, fuzz_string_sets, 20000)
CT_FUZZ2(Sort, fuzz_sort, 400)
CT_FUZZ2(PriorityQueue, fuzz_priority_queue, 3000)
