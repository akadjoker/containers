#include "ct/deque.hpp"
#include "ct/hashmap.hpp"
#include "ct/hashset.hpp"
#include "ct/string.hpp"
#include "ct/vector.hpp"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

namespace
{

volatile std::uint64_t sink = 0;

template <typename Function>
double measure(Function &&function, std::uint64_t &checksum)
{
    const auto begin = std::chrono::steady_clock::now();
    checksum = function();
    const auto end = std::chrono::steady_clock::now();
    sink ^= checksum;
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

template <typename Function>
void report(const char *name, Function &&function)
{
    double timings[5] = {};
    std::uint64_t checksum = 0;
    for (double &timing : timings)
        timing = measure(function, checksum);
    std::sort(timings, timings + 5);
    std::printf("%-24s %10.3f ms  min=%8.3f  checksum=%llu\n", name, timings[2], timings[0],
                static_cast<unsigned long long>(checksum));
}

std::uint64_t bench_ct_vector(std::size_t count)
{
    ct::Vector<int> values;
    for (std::size_t i = 0; i < count; ++i)
        values.push_back(static_cast<int>(i));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < values.size(); ++i)
        result = result * 33 + static_cast<std::uint64_t>(values[i]);
    return result;
}

std::uint64_t bench_std_vector(std::size_t count)
{
    std::vector<int> values;
    for (std::size_t i = 0; i < count; ++i)
        values.push_back(static_cast<int>(i));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < values.size(); ++i)
        result = result * 33 + static_cast<std::uint64_t>(values[i]);
    return result;
}

std::uint64_t bench_ct_vector_reserved(std::size_t count)
{
    ct::Vector<int> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.push_back(static_cast<int>(i));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < values.size(); ++i)
        result = result * 33 + static_cast<std::uint64_t>(values[i]);
    return result;
}

std::uint64_t bench_std_vector_reserved(std::size_t count)
{
    std::vector<int> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.push_back(static_cast<int>(i));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < values.size(); ++i)
        result = result * 33 + static_cast<std::uint64_t>(values[i]);
    return result;
}

std::uint64_t bench_ct_deque(std::size_t count)
{
    ct::Deque<int> values;
    for (std::size_t i = 0; i < count; ++i)
    {
        values.push_back(static_cast<int>(i));
        if ((i & 3) == 0)
            values.push_front(static_cast<int>(i ^ 0x55));
    }
    std::uint64_t result = values.size();
    while (!values.empty())
    {
        result = result * 33 + static_cast<std::uint64_t>(values.front());
        values.pop_front();
    }
    return result;
}

std::uint64_t bench_std_deque(std::size_t count)
{
    std::deque<int> values;
    for (std::size_t i = 0; i < count; ++i)
    {
        values.push_back(static_cast<int>(i));
        if ((i & 3) == 0)
            values.push_front(static_cast<int>(i ^ 0x55));
    }
    std::uint64_t result = values.size();
    while (!values.empty())
    {
        result = result * 33 + static_cast<std::uint64_t>(values.front());
        values.pop_front();
    }
    return result;
}

std::uint64_t bench_ct_hashmap(std::size_t count)
{
    ct::HashMap<std::uint32_t, std::uint32_t> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.put(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i * 3));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < count; ++i)
        result += *values.find(static_cast<std::uint32_t>(i));
    for (std::size_t i = 0; i < count; i += 3)
        values.erase(static_cast<std::uint32_t>(i));
    return result + values.size();
}

std::uint64_t bench_std_hashmap(std::size_t count)
{
    std::unordered_map<std::uint32_t, std::uint32_t> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.emplace(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i * 3));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < count; ++i)
        result += values.find(static_cast<std::uint32_t>(i))->second;
    for (std::size_t i = 0; i < count; i += 3)
        values.erase(static_cast<std::uint32_t>(i));
    return result + values.size();
}

std::uint64_t bench_ct_hashset(std::size_t count)
{
    ct::HashSet<std::uint32_t> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.insert(static_cast<std::uint32_t>(i));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < count; ++i)
        result += values.contains(static_cast<std::uint32_t>(i));
    for (std::size_t i = 0; i < count; i += 3)
        values.erase(static_cast<std::uint32_t>(i));
    return result + values.size();
}

std::uint64_t bench_std_hashset(std::size_t count)
{
    std::unordered_set<std::uint32_t> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        values.insert(static_cast<std::uint32_t>(i));
    std::uint64_t result = values.size();
    for (std::size_t i = 0; i < count; ++i)
        result += values.count(static_cast<std::uint32_t>(i));
    for (std::size_t i = 0; i < count; i += 3)
        values.erase(static_cast<std::uint32_t>(i));
    return result + values.size();
}

std::uint64_t bench_ct_string(std::size_t count)
{
    ct::String value;
    for (std::size_t i = 0; i < count; ++i)
        value.append("radion", 6);
    return value.hash() + value.size();
}

std::uint64_t bench_std_string(std::size_t count)
{
    std::string value;
    for (std::size_t i = 0; i < count; ++i)
        value.append("radion", 6);
    std::uint64_t result = 1469598103934665603ull;
    for (unsigned char c : value)
    {
        result ^= c;
        result *= 1099511628211ull;
    }
    return result + value.size();
}

} 

int main()
{
    const std::size_t sequence_count = 1000000;
    const std::size_t hash_count = 250000;
    const std::size_t string_count = 100000;

    std::printf("Radion ct container benchmark (median of 5 runs)\n");
    report("ct::Vector push/index", [=] { return bench_ct_vector(sequence_count); });
    report("std::vector push/index", [=] { return bench_std_vector(sequence_count); });
    report("ct::Vector reserved", [=] { return bench_ct_vector_reserved(sequence_count); });
    report("std::vector reserved", [=] { return bench_std_vector_reserved(sequence_count); });
    report("ct::Deque push/pop", [=] { return bench_ct_deque(sequence_count / 2); });
    report("std::deque push/pop", [=] { return bench_std_deque(sequence_count / 2); });
    report("ct::HashMap", [=] { return bench_ct_hashmap(hash_count); });
    report("std::unordered_map", [=] { return bench_std_hashmap(hash_count); });
    report("ct::HashSet", [=] { return bench_ct_hashset(hash_count); });
    report("std::unordered_set", [=] { return bench_std_hashset(hash_count); });
    report("ct::String append/hash", [=] { return bench_ct_string(string_count); });
    report("std::string append/hash", [=] { return bench_std_string(string_count); });
    std::printf("sink=%llu\n", static_cast<unsigned long long>(sink));
    return 0;
}