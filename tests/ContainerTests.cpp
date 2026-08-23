#include "ct/arena.hpp"
#include "ct/deque.hpp"
#include "ct/hashmap.hpp"
#include "ct/hashset.hpp"
#include "ct/flatmap.hpp"
#include "ct/pool.hpp"
#include "ct/sort.hpp"
#include "ct/string.hpp"
#include "ct/treemap.hpp"
#include "ct/vector.hpp"

#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

struct Counted
{
    static int live;
    static int constructions;
    static int destructions;

    int value;

    explicit Counted(int value = 0) : value(value)
    {
        ++live;
        ++constructions;
    }

    Counted(const Counted &other) : value(other.value)
    {
        ++live;
        ++constructions;
    }

    Counted(Counted &&other) noexcept : value(other.value)
    {
        other.value = -1;
        ++live;
        ++constructions;
    }

    Counted &operator=(const Counted &other)
    {
        value = other.value;
        return *this;
    }

    Counted &operator=(Counted &&other) noexcept
    {
        value = other.value;
        other.value = -1;
        return *this;
    }

    ~Counted()
    {
        --live;
        ++destructions;
    }

    bool operator==(const Counted &other) const { return value == other.value; }
};

int Counted::live = 0;
int Counted::constructions = 0;
int Counted::destructions = 0;

struct MoveOnly
{
    int value;

    explicit MoveOnly(int value = 0) : value(value) {}
    MoveOnly(const MoveOnly &) = delete;
    MoveOnly &operator=(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&other) noexcept : value(other.value) { other.value = -1; }
    MoveOnly &operator=(MoveOnly &&other) noexcept
    {
        value = other.value;
        other.value = -1;
        return *this;
    }
};

struct CollidingHash
{
    std::uint64_t operator()(int) const { return 3; }
};

struct NonTrivialKey
{
    static int live;

    int value;

    explicit NonTrivialKey(int value = 0) : value(value) { ++live; }
    NonTrivialKey(const NonTrivialKey &other) : value(other.value) { ++live; }
    NonTrivialKey(NonTrivialKey &&other) noexcept : value(other.value)
    {
        other.value = -1;
        ++live;
    }
    NonTrivialKey &operator=(const NonTrivialKey &other)
    {
        value = other.value;
        return *this;
    }
    NonTrivialKey &operator=(NonTrivialKey &&other) noexcept
    {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~NonTrivialKey() { --live; }

    std::uint64_t hash() const { return 11; }
    bool operator==(const NonTrivialKey &other) const { return value == other.value; }
};

int NonTrivialKey::live = 0;

struct CountingAlloc
{
    static int allocations;
    static int deallocations;
    static bool aligned;

    void *allocate(std::size_t bytes, std::size_t align)
    {
        ++allocations;
        void *pointer = heap.allocate(bytes, align);
        aligned = aligned && reinterpret_cast<std::uintptr_t>(pointer) % align == 0;
        return pointer;
    }

    void deallocate(void *pointer, std::size_t bytes)
    {
        ++deallocations;
        heap.deallocate(pointer, bytes);
    }

    void *reallocate(void *pointer, std::size_t old_bytes, std::size_t new_bytes,
                     std::size_t align)
    {
        ++allocations;
        ++deallocations;
        void *result = heap.reallocate(pointer, old_bytes, new_bytes, align);
        aligned = aligned && reinterpret_cast<std::uintptr_t>(result) % align == 0;
        return result;
    }

private:
    ct::HeapAlloc heap;
};

int CountingAlloc::allocations = 0;
int CountingAlloc::deallocations = 0;
bool CountingAlloc::aligned = true;

void test_string()
{
    ct::String empty;
    assert(empty.empty());
    assert(empty.is_small());

    ct::String s(23, 'a');
    assert(s.size() == 23);
    assert(s.is_small());
    assert(s.c_str()[23] == '\0');

    s.push_back('b');
    assert(s.size() == 24);
    assert(!s.is_small());
    const char *old_data = s.data();
    s.append(s.data(), s.size());
    assert(s.size() == 48);
    assert(std::memcmp(s.data(), s.data() + 24, 24) == 0);
    assert(s.data() != old_data || s.size() == 24);
    (void)old_data;

    ct::String copy = s;
    ct::String moved = std::move(copy);
    assert(moved == s);
    assert(copy.empty());
    assert(ct::String(" a ").trimmed() == "a");
    assert(ct::String("a,b,,c").split(',', false).size() == 3);
    assert(ct::String("a,b,,c").split(',', true).size() == 4);

    std::mt19937 generator(0xC7A1u);
    std::string reference;
    ct::String actual;
    for (int step = 0; step < 5000; ++step)
    {
        const int operation = static_cast<int>(generator() % 7);
        if (operation == 0)
        {
            const char value = static_cast<char>('a' + generator() % 26);
            actual.push_back(value);
            reference.push_back(value);
        }
        else if (operation == 1 && !reference.empty())
        {
            actual.pop_back();
            reference.pop_back();
        }
        else if (operation == 2)
        {
            actual.append("xyz");
            reference.append("xyz");
        }
        else if (operation == 3)
        {
            const std::size_t count = generator() % 80;
            const char value = static_cast<char>('A' + generator() % 26);
            actual.resize(count, value);
            reference.resize(count, value);
        }
        else if (operation == 4)
        {
            actual.clear();
            reference.clear();
        }
        else if (operation == 5 && actual.size() <= 256)
        {
            actual.append(actual.data(), actual.size());
            reference += reference;
        }
        else
        {
            const std::size_t count = generator() % 40;
            std::string replacement(count, static_cast<char>('0' + generator() % 10));
            actual.assign(replacement.data(), replacement.size());
            reference = replacement;
        }
        assert(actual.size() == reference.size());
        assert(actual.c_str()[actual.size()] == '\0');
        assert(actual.empty() || std::memcmp(actual.data(), reference.data(), actual.size()) == 0);
    }
}

void test_sequence_containers()
{
    Counted::live = 0;
    {
        ct::Vector<Counted> values;
        values.emplace_back(1);
        values.emplace_back(2);
        values.emplace(values.begin() + 1, 3);
        values.reserve(8);
        while (values.size() < values.capacity())
            values.emplace_back(static_cast<int>(values.size()));
        values.push_back(values.front());
        assert(values.back().value == values.front().value);
        values.erase(values.begin());
        values.resize(8);
        values.resize(2);
        assert(values.size() == 2);

        ct::Deque<Counted> deque;
        deque.emplace_back(1);
        deque.emplace_front(0);
        deque.emplace_back(2);
        deque.pop_front();
        deque.push_front(Counted(4));
        deque.reserve(64);
        assert(deque.front().value == 4);
        assert(deque.back().value == 2);
    }
    assert(Counted::live == 0);
    assert(Counted::constructions == Counted::destructions);

    ct::Vector<MoveOnly> move_only;
    move_only.emplace_back(1);
    move_only.resize(4);
    move_only.erase(move_only.begin() + 1);
    assert(move_only.size() == 3);

    ct::Vector<int> empty;
    empty.insert(empty.end(), 7);
    assert(empty.size() == 1 && empty.front() == 7);
}

void test_hash_containers()
{
    ct::HashMap<int, int, CollidingHash> map;
    for (int i = 0; i < 200; ++i)
        map.put(i, i * 2);
    for (int i = 0; i < 200; ++i)
        assert(map.find(i) && *map.find(i) == i * 2);
    for (int i = 0; i < 200; i += 2)
        assert(map.erase(i));
    for (int i = 0; i < 200; ++i)
        assert(map.contains(i) == (i % 2 != 0));

    ct::HashSet<int, CollidingHash> set;
    for (int i = 0; i < 200; ++i)
        assert(set.insert(i));
    for (int i = 0; i < 200; i += 3)
        assert(set.erase(i));
    for (int i = 0; i < 200; ++i)
        assert(set.contains(i) == (i % 3 != 0));
}

void test_nontrivial_hash_lifecycle()
{
    NonTrivialKey::live = 0;
    Counted::live = 0;
    {
        ct::HashMap<NonTrivialKey, Counted> map;
        for (int i = 0; i < 400; ++i)
            map.put(NonTrivialKey(i), Counted(i * 3));
        for (int i = 0; i < 400; ++i)
            assert(map.find(NonTrivialKey(i)) && map.find(NonTrivialKey(i))->value == i * 3);
        for (int i = 0; i < 400; i += 2)
            assert(map.erase(NonTrivialKey(i)));
        assert(map.size() == 200);

        ct::HashMap<NonTrivialKey, Counted> copy(map);
        ct::HashMap<NonTrivialKey, Counted> moved(std::move(copy));
        map = moved;
        assert(map.size() == 200);

        ct::HashSet<NonTrivialKey> set;
        for (int i = 0; i < 400; ++i)
            assert(set.insert(NonTrivialKey(i)));
        for (int i = 0; i < 400; i += 3)
            assert(set.erase(NonTrivialKey(i)));
        assert(set.size() == 266);
        std::size_t iterated = 0;
        for (auto it = set.begin(); it != set.end(); ++it)
        {
            assert(set.contains(*it));
            ++iterated;
        }
        assert(iterated == set.size());
    }
    assert(NonTrivialKey::live == 0);
    assert(Counted::live == 0);

    ct::HashMap<int, MoveOnly> move_only;
    for (int i = 0; i < 300; ++i)
        move_only.put(i, MoveOnly(i + 1));
    for (int i = 0; i < 300; i += 2)
        assert(move_only.erase(i));
    for (int i = 1; i < 300; i += 2)
        assert(move_only.find(i) && move_only.find(i)->value == i + 1);
}

void test_ordered_containers()
{
    ct::FlatMap<int, int> flat;
    flat.put(4, 40);
    flat.put(1, 10);
    flat.put(3, 30);
    assert(flat.find(3) && *flat.find(3) == 30);
    assert(flat.erase(1));

    ct::TreeMap<int, int> tree;
    for (int i = 0; i < 100; ++i)
        tree.put(i, i + 1);
    for (int i = 0; i < 100; i += 2)
        assert(tree.erase(i));
    assert(tree.validate());
    assert(tree.size() == 50);
    assert(tree.find(3) && *tree.find(3) == 4);
}

void test_ordered_differential()
{
    std::mt19937 generator(0xA11CEu);
    ct::FlatMap<int, int> flat;
    ct::TreeMap<int, int> tree;
    std::map<int, int> standard;
    for (int step = 0; step < 12000; ++step)
    {
        const int key = static_cast<int>(generator() % 500);
        if (generator() & 1u)
        {
            const int value = static_cast<int>(generator());
            flat.put(key, value);
            tree.put(key, value);
            standard[key] = value;
        }
        else
        {
            const bool existed = standard.find(key) != standard.end();
            assert(flat.erase(key) == existed);
            assert(tree.erase(key) == existed);
            standard.erase(key);
        }

        assert(tree.validate());
        assert(flat.size() == standard.size());
        assert(tree.size() == standard.size());
        auto flat_it = flat.begin();
        auto tree_it = tree.begin();
        for (const auto &entry : standard)
        {
            assert(flat_it != flat.end());
            assert(flat_it->key == entry.first && flat_it->value == entry.second);
            assert(tree_it != tree.end());
            assert(tree_it->key == entry.first && tree_it->value == entry.second);
            ++flat_it;
            ++tree_it;
        }
        assert(flat_it == flat.end());
        assert(tree_it == tree.end());
    }
}

void test_allocator_policy()
{
    CountingAlloc::allocations = 0;
    CountingAlloc::deallocations = 0;
    CountingAlloc::aligned = true;
    {
        CountingAlloc alloc;
        ct::Vector<int, CountingAlloc> vector(alloc);
        vector.resize(100);
        ct::HashMap<int, int, ct::Hash<int>, CountingAlloc> map(ct::Hash<int>(), alloc);
        map.put(1, 2);
        ct::Pool<int, CountingAlloc> pool(4, alloc);
        int *value = pool.create(7);
        pool.destroy(value);
        ct::BasicArena<CountingAlloc> arena(64, alloc);
        arena.allocate(8, 16);
    }
    assert(CountingAlloc::allocations > 0);
    assert(CountingAlloc::allocations == CountingAlloc::deallocations);
    assert(CountingAlloc::aligned);
}

void test_pool_and_arena()
{
    Counted::live = 0;
    {
        ct::Pool<Counted> pool(3);
        Counted *a = pool.create(1);
        Counted *b = pool.create(2);
        pool.destroy(a);
        Counted *c = pool.create(3);
        assert(c == a);
        assert(pool.live() == 2);
        pool.clear();
        assert(pool.live() == 0);
        (void)b;
        (void)c;
    }
    assert(Counted::live == 0);

    ct::Arena arena(64);
    void *aligned = arena.allocate(1, 32);
    assert(reinterpret_cast<std::uintptr_t>(aligned) % 32 == 0);
    (void)aligned;
    int *values = arena.allocate_array<int>(8);
    for (int i = 0; i < 8; ++i)
        values[i] = i;
    assert(arena.owns(values));
    assert(arena.bytes_used() >= sizeof(int) * 8 + 1);
    arena.reset();
    assert(arena.bytes_used() == 0);
    arena.release();
    int *after_release = static_cast<int *>(arena.allocate(sizeof(int), alignof(int)));
    assert(after_release != nullptr);
    (void)after_release;
}

void test_sort()
{
    unsigned values[256];
    for (unsigned i = 0; i < 256; ++i)
        values[i] = 255 - i;
    ct::sort(values, values + 256);
    for (unsigned i = 0; i < 256; ++i)
        assert(values[i] == i);
}

void test_differential_sequences()
{
    std::mt19937 generator(0x51A7u);
    ct::Vector<int> vector;
    std::vector<int> standard_vector;
    ct::Deque<int> deque;
    std::deque<int> standard_deque;

    for (int step = 0; step < 10000; ++step)
    {
        const int operation = static_cast<int>(generator() % 8);
        const int value = static_cast<int>(generator() % 1000);
        if (operation == 0 || standard_vector.empty())
        {
            vector.push_back(value);
            standard_vector.push_back(value);
        }
        else if (operation == 1)
        {
            const std::size_t index = generator() % standard_vector.size();
            vector.erase(vector.begin() + index);
            standard_vector.erase(standard_vector.begin() + index);
        }
        else if (operation == 2)
        {
            const std::size_t index = generator() % (standard_vector.size() + 1);
            vector.insert(vector.begin() + index, value);
            standard_vector.insert(standard_vector.begin() + index, value);
        }
        else if (operation == 3)
        {
            const std::size_t count = generator() % 40;
            vector.resize(count, value);
            standard_vector.resize(count, value);
        }
        else if (operation == 4)
        {
            vector.clear();
            standard_vector.clear();
        }

        assert(vector.size() == standard_vector.size());
        for (std::size_t i = 0; i < vector.size(); ++i)
            assert(vector[i] == standard_vector[i]);

        const int deque_operation = static_cast<int>(generator() % 6);
        if (deque_operation == 0 || standard_deque.empty())
        {
            deque.push_back(value);
            standard_deque.push_back(value);
        }
        else if (deque_operation == 1)
        {
            deque.push_front(value);
            standard_deque.push_front(value);
        }
        else if (deque_operation == 2)
        {
            deque.pop_back();
            standard_deque.pop_back();
        }
        else if (deque_operation == 3)
        {
            deque.pop_front();
            standard_deque.pop_front();
        }
        else
        {
            const std::size_t count = generator() % 40;
            deque.resize(count, value);
            standard_deque.resize(count, value);
        }

        assert(deque.size() == standard_deque.size());
        for (std::size_t i = 0; i < deque.size(); ++i)
            assert(deque[i] == standard_deque[i]);

        std::size_t iterated = 0;
        for (auto it = deque.begin(); it != deque.end(); ++it)
        {
            assert(*it == standard_deque[iterated]);
            ++iterated;
        }
        assert(iterated == deque.size());
        const auto first = deque.first_span();
        const auto second = deque.second_span();
        assert(first.len + second.len == deque.size());
        for (std::size_t i = 0; i < first.len; ++i)
            assert(first.ptr[i] == standard_deque[i]);
        for (std::size_t i = 0; i < second.len; ++i)
            assert(second.ptr[i] == standard_deque[first.len + i]);
    }
}

void test_differential_hashes()
{
    std::mt19937 generator(0xBADC0DEu);
    ct::HashMap<int, int> map;
    std::unordered_map<int, int> standard_map;
    ct::HashSet<int> set;
    std::unordered_set<int> standard_set;
    for (int step = 0; step < 10000; ++step)
    {
        const int key = static_cast<int>(generator() % 300);
        if (generator() & 1u)
        {
            const int value = static_cast<int>(generator());
            map.put(key, value);
            standard_map[key] = value;
            set.insert(key);
            standard_set.insert(key);
        }
        else
        {
            map.erase(key);
            standard_map.erase(key);
            set.erase(key);
            standard_set.erase(key);
        }
        assert(map.size() == standard_map.size());
        assert(set.size() == standard_set.size());
        for (int i = 0; i < 300; ++i)
        {
            assert(map.contains(i) == (standard_map.find(i) != standard_map.end()));
            assert(set.contains(i) == (standard_set.find(i) != standard_set.end()));
        }
        std::size_t map_entries = 0;
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            auto standard_value = standard_map.find(it->key);
            assert(standard_value != standard_map.end() && standard_value->second == it->value);
            ++map_entries;
        }
        std::size_t set_entries = 0;
        for (auto it = set.begin(); it != set.end(); ++it)
        {
            assert(standard_set.find(*it) != standard_set.end());
            ++set_entries;
        }
        assert(map_entries == standard_map.size());
        assert(set_entries == standard_set.size());
    }
}

void test_differential_collisions()
{
    std::mt19937 generator(0xC0111DEu);
    ct::HashMap<int, int, CollidingHash> map;
    ct::HashSet<int, CollidingHash> set;
    std::map<int, int> standard_map;
    std::map<int, bool> standard_set;
    for (int step = 0; step < 15000; ++step)
    {
        const int key = static_cast<int>(generator() % 600);
        if (generator() & 1u)
        {
            const int value = static_cast<int>(generator());
            map.put(key, value);
            set.insert(key);
            standard_map[key] = value;
            standard_set[key] = true;
        }
        else
        {
            const bool map_existed = standard_map.erase(key) != 0;
            const bool set_existed = standard_set.erase(key) != 0;
            assert(map.erase(key) == map_existed);
            assert(set.erase(key) == set_existed);
        }
        assert(map.size() == standard_map.size());
        assert(set.size() == standard_set.size());
        if ((step & 15) != 0)
            continue;
        for (int i = 0; i < 600; ++i)
        {
            const auto map_value = standard_map.find(i);
            assert(map.contains(i) == (map_value != standard_map.end()));
            if (map_value != standard_map.end())
                assert(*map.find(i) == map_value->second);
            assert(set.contains(i) == (standard_set.find(i) != standard_set.end()));
        }
    }
}

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))

struct FailingAlloc
{
    void *allocate(std::size_t, std::size_t) { std::abort(); }
    void deallocate(void *, std::size_t) {}
    void *reallocate(void *, std::size_t, std::size_t, std::size_t) { std::abort(); }
};

using AbortOperation = void (*)();

bool expect_abort(AbortOperation operation)
{
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        operation();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

void overflow_vector()
{
    ct::Vector<int> values;
    values.reserve(values.max_size() + 1);
}

void overflow_deque()
{
    ct::Deque<int> values;
    values.reserve(values.max_size() + 1);
}

void overflow_string()
{
    ct::String value;
    value.reserve(value.max_size() + 1);
}

void overflow_hashmap()
{
    ct::HashMap<int, int> values;
    values.reserve((std::numeric_limits<std::size_t>::max)());
}

void overflow_arena()
{
    ct::Arena arena;
    arena.allocate_array<std::uint64_t>(
        (std::numeric_limits<std::size_t>::max)() / sizeof(std::uint64_t) + 1);
}

void overflow_pool()
{
    ct::Pool<int> pool((std::numeric_limits<std::size_t>::max)());
}

void allocator_failure()
{
    ct::Vector<int, FailingAlloc> values;
    values.reserve(1);
}

void test_failure_and_overflow_paths()
{
    assert(expect_abort(overflow_vector));
    assert(expect_abort(overflow_deque));
    assert(expect_abort(overflow_string));
    assert(expect_abort(overflow_hashmap));
    assert(expect_abort(overflow_arena));
    assert(expect_abort(overflow_pool));
    assert(expect_abort(allocator_failure));
}

#else

void test_failure_and_overflow_paths() {}

#endif

} 

int main()
{
    test_string();
    test_sequence_containers();
    test_hash_containers();
    test_nontrivial_hash_lifecycle();
    test_ordered_containers();
    test_ordered_differential();
    test_allocator_policy();
    test_pool_and_arena();
    test_sort();
    test_differential_sequences();
    test_differential_hashes();
    test_differential_collisions();
    test_failure_and_overflow_paths();
    return 0;
}