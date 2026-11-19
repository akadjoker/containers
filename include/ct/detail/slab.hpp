#pragma once

// Blocos de tamanho fixo em pedacos (HashMap com valores grandes, BTreeMap/BTreeSet). Sem `std::` de proposito: ficheiro
// novo, e o tools/check.sh nostd nao deixa juntar usos de std.

#include <stddef.h>
#include <string.h>

#include "utils.hpp"

namespace ct
{
    namespace detail
    {
        // Blocos de T em pedacos de ~4 KiB: sem malloc por bloco (um malloc por pedaco), e um bloco devolvido nao volta ao
        // alocador, vai para uma pilha de livres (um ponteiro cada, num vetor a parte, por isso devolver um bloco nao lhe toca:
        // numa tabela de 1e5 valores de 256 bytes tocar no bloco no erase era uma falha de cache por apagamento). Os
        // pedacos so voltam ao alocador em release_all. Os blocos nunca se movem. Sem estado partilhado: cada dono tem o seu.
        // Requisito: sizeof(T) >= sizeof(void *) (nao precisa, so evita blocos absurdamente pequenos).
        template <typename T>
        class BlockSlab
        {
            struct Chunk
            {
                Chunk *next;
                size_t blocks;
            };
            static constexpr size_t kAlign = alignof(T) > alignof(Chunk) ? alignof(T) : alignof(Chunk);
            static constexpr size_t kHeader = ((sizeof(Chunk) + kAlign - 1) / kAlign) * kAlign;
            static constexpr size_t kChunkBlocks = (4096 / sizeof(T)) < 8 ? 8 : (4096 / sizeof(T));

            Chunk *chunks_;
            unsigned char *cur_;
            unsigned char *end_;
            T **freed_;
            size_t nfree_;
            size_t capfree_;

            template <typename A>
            void add_chunk(A &a, size_t blocks)
            {
                size_t body = 0, bytes = 0;
                if (!checked_mul(blocks, sizeof(T), body) || !checked_add(body, kHeader, bytes))
                    fatal("ct::BlockSlab: tamanho invalido");
                unsigned char *raw = static_cast<unsigned char *>(a.allocate(bytes, kAlign));
                Chunk *c = reinterpret_cast<Chunk *>(raw);
                c->next = chunks_;
                c->blocks = blocks;
                chunks_ = c;
                cur_ = raw + kHeader;
                end_ = cur_ + body;
            }

            template <typename A>
            CT_NOINLINE void grow_freed(A &a)
            {
                size_t ncap = capfree_ ? capfree_ * 2 : 64, bytes = 0;
                if (!checked_mul(ncap, sizeof(T *), bytes))
                    fatal("ct::BlockSlab: tamanho invalido");
                T **nf = static_cast<T **>(a.allocate(bytes, alignof(T *)));
                if (nfree_)
                    memcpy(static_cast<void *>(nf), static_cast<const void *>(freed_), nfree_ * sizeof(T *));
                if (freed_)
                    a.deallocate(freed_, capfree_ * sizeof(T *));
                freed_ = nf;
                capfree_ = ncap;
            }

        public:
            BlockSlab() noexcept : chunks_(nullptr), cur_(nullptr), end_(nullptr), freed_(nullptr), nfree_(0), capfree_(0) {}
            BlockSlab(BlockSlab &&o) noexcept
                : chunks_(o.chunks_), cur_(o.cur_), end_(o.end_), freed_(o.freed_), nfree_(o.nfree_), capfree_(o.capfree_)
            {
                o.reset();
            }
            BlockSlab(const BlockSlab &) = delete;
            BlockSlab &operator=(const BlockSlab &) = delete;

            // Fica com o estado de o (quem recebe tem de ter largado os pedacos antes: release_all)
            void adopt(BlockSlab &o) noexcept
            {
                chunks_ = o.chunks_;
                cur_ = o.cur_;
                end_ = o.end_;
                freed_ = o.freed_;
                nfree_ = o.nfree_;
                capfree_ = o.capfree_;
                o.reset();
            }

            template <typename A>
            T *take(A &a)
            {
                if (nfree_)
                    return freed_[--nfree_];
                if (CT_UNLIKELY(cur_ == end_))
                    add_chunk(a, kChunkBlocks);
                T *p = reinterpret_cast<T *>(cur_);
                cur_ += sizeof(T);
                return p;
            }

            template <typename A>
            void give(A &a, T *p)
            {
                if (CT_UNLIKELY(nfree_ == capfree_))
                    grow_freed(a);
                freed_[nfree_++] = p;
            }

            // Garante lugar para mais n blocos sem outro pedaco: a copia e o reserve sabem quantos vem e pedem um so
            // pedaco do tamanho certo (conta o que falta: os livres e o fim do pedaco actual contam).
            template <typename A>
            void prepare(A &a, size_t n)
            {
                const size_t have = static_cast<size_t>(end_ - cur_) / sizeof(T) + nfree_;
                if (have < n)
                    add_chunk(a, n - have);
            }

            template <typename A>
            void release_all(A &a) noexcept
            {
                Chunk *c = chunks_;
                while (c)
                {
                    Chunk *next = c->next;
                    a.deallocate(c, kHeader + c->blocks * sizeof(T));
                    c = next;
                }
                if (freed_)
                    a.deallocate(freed_, capfree_ * sizeof(T *));
                reset();
            }

        private:
            void reset() noexcept
            {
                chunks_ = nullptr;
                cur_ = end_ = nullptr;
                freed_ = nullptr;
                nfree_ = capfree_ = 0;
            }
        };

        // Sem slab (as tabelas de hash com a entrada inteira no slot)
        struct NoSlab
        {
            template <typename A>
            void prepare(A &, size_t) noexcept {}
            template <typename A>
            void release_all(A &) noexcept {}
            void adopt(NoSlab &) noexcept {}
        };
    } // namespace detail
} // namespace ct
