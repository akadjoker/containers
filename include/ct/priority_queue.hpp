#pragma once

#include "vector.hpp"

namespace ct
{
    template <typename T, typename Compare = Less<T>, typename Container = Vector<T>>
    class PriorityQueue
    {
    public:
        using value_type = typename Container::value_type;
        using size_type = typename Container::size_type;
        using reference = typename Container::reference;
        using const_reference = typename Container::const_reference;

        PriorityQueue() = default;
        explicit PriorityQueue(const Compare &compare) : compare_(compare) {}
        explicit PriorityQueue(const Container &container, const Compare &compare = Compare())
            : compare_(compare), container_(container) { makeHeap(); }
        explicit PriorityQueue(Container &&container, const Compare &compare = Compare())
            : compare_(compare), container_(detail::move(container)) { makeHeap(); }

        bool empty() const noexcept { return container_.empty(); }
        size_type size() const noexcept { return container_.size(); }
        size_type capacity() const noexcept { return container_.capacity(); }
        void reserve(size_type count) { container_.reserve(count); }

        const_reference top() const { return container_.front(); }

        void push(const T &value)
        {
            container_.push_back(value);
            siftUp(container_.size() - 1);
        }

        void push(T &&value)
        {
            container_.push_back(detail::move(value));
            siftUp(container_.size() - 1);
        }

        template <typename... Args>
        void emplace(Args &&...args)
        {
            container_.emplace_back(detail::forward<Args>(args)...);
            siftUp(container_.size() - 1);
        }

        void pop()
        {
            const size_type count = container_.size();
            if (count < 2) { container_.clear(); return; }
            T last(detail::move(container_[count - 1]));
            container_.pop_back();
            adjustHeap(0, container_.size(), detail::move(last));
        }

        void clear() noexcept { container_.clear(); }
        void swap(PriorityQueue &other) noexcept
        {
            container_.swap(other.container_);
            detail::swap_vals(compare_, other.compare_);
        }

        const Container &container() const noexcept { return container_; }

    private:
        Compare compare_;
        Container container_;

        void makeHeap()
        {
            for (size_type index = container_.size() / 2; index > 0; --index)
                siftDown(index - 1);
        }

        // A tecnica do buraco: o valor que sobe ou desce fica de fora e os outros movem-se um passo (um move por nivel
        // em vez de um swap, que sao tres), e no pop o buraco desce ate a folha escolhendo o filho maior sem o comparar
        // com o valor, que depois sobe do fundo ao seu lugar (menos comparacoes, como o libstdc++).
        void siftUp(size_type index)
        {
            if (!index) return;
            size_type parent = (index - 1) / 2;
            if (!compare_(container_[parent], container_[index])) return;
            T value(detail::move(container_[index]));
            do
            {
                container_[index] = detail::move(container_[parent]);
                index = parent;
                if (!index) break;
                parent = (index - 1) / 2;
            } while (compare_(container_[parent], value));
            container_[index] = detail::move(value);
        }

        // value vai para o lugar certo da sub-arvore com raiz em index, que esta vazia (buraco); count e o tamanho
        void adjustHeap(size_type index, size_type count, T &&value)
        {
            const size_type top = index;
            size_type child = index * 2 + 1;
            while (child < count)
            {
                if (child + 1 < count && compare_(container_[child], container_[child + 1])) ++child;
                container_[index] = detail::move(container_[child]);
                index = child;
                child = index * 2 + 1;
            }
            while (index > top)
            {
                const size_type parent = (index - 1) / 2;
                if (!compare_(container_[parent], value)) break;
                container_[index] = detail::move(container_[parent]);
                index = parent;
            }
            container_[index] = detail::move(value);
        }

        void siftDown(size_type index)
        {
            const size_type count = container_.size();
            T value(detail::move(container_[index]));
            adjustHeap(index, count, detail::move(value));
        }
    };

    template <typename T, typename Compare, typename Container>
    inline void swap(PriorityQueue<T, Compare, Container> &a, PriorityQueue<T, Compare, Container> &b) noexcept
    {
        a.swap(b);
    }
}
