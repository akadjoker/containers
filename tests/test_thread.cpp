#include <ct/thread.hpp>
#include <ct/threadpool.hpp>

#include <gtest/gtest.h>

#include <vector>

using ct::Atomic;
using ct::CondVar;
using ct::LockGuard;
using ct::Mutex;
using ct::Thread;
using ct::ThreadPool;

TEST(Thread, RunsAndJoins)
{
    int value = 0;
    Thread t([&] { value = 42; });
    EXPECT_TRUE(t.joinable());
    t.join();
    EXPECT_FALSE(t.joinable());
    EXPECT_EQ(value, 42);
}

TEST(Thread, DestructorJoins)
{
    Atomic<int> done(0);
    {
        Thread t([&] {
            Thread::sleep_ms(5);
            done.store(1);
        });
    }
    EXPECT_EQ(done.load(), 1);
}

TEST(Thread, MoveTransfersOwnership)
{
    Atomic<int> v(0);
    Thread a([&] { v.fetch_add(1); });
    Thread b(ct::detail::move(a));
    EXPECT_FALSE(a.joinable());
    EXPECT_TRUE(b.joinable());
    b.join();
    EXPECT_EQ(v.load(), 1);
    Thread c;
    c = Thread([&] { v.fetch_add(1); });
    c.join();
    EXPECT_EQ(v.load(), 2);
}

TEST(Thread, StartLater)
{
    Thread t;
    EXPECT_FALSE(t.joinable());
    int x = 0;
    t.start([&] { x = 7; });
    t.join();
    EXPECT_EQ(x, 7);
    EXPECT_GE(Thread::hardware_concurrency(), 1u);
    Thread::yield();
}

TEST(Atomic, CountsAcrossThreads)
{
    Atomic<int> counter(0);
    Atomic<long long> big(0);
    ct::Vector<Thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back(Thread::Fn([&] {
            for (int k = 0; k < 10000; ++k)
            {
                ++counter;
                big += 3;
            }
        }));
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    EXPECT_EQ(counter.load(), 80000);
    EXPECT_EQ(big.load(), 240000LL);
}

TEST(Atomic, ExchangeAndCompareExchange)
{
    Atomic<unsigned> a(5);
    EXPECT_EQ(a.exchange(9), 5u);
    unsigned expected = 9;
    EXPECT_TRUE(a.compare_exchange(expected, 11));
    EXPECT_EQ(a.load(), 11u);
    expected = 3;
    EXPECT_FALSE(a.compare_exchange(expected, 1));
    EXPECT_EQ(expected, 11u);
    EXPECT_EQ(a--, 11u);
    EXPECT_EQ(--a, 9u);
    a = 2;
    EXPECT_EQ(unsigned(a), 2u);
    int dummy = 0;
    Atomic<int *> p(nullptr);
    EXPECT_EQ(p.load(), nullptr);
    p.store(&dummy);
    EXPECT_EQ(p.load(), &dummy);
}

TEST(Mutex, ProtectsCounter)
{
    Mutex m;
    long counter = 0;
    ct::Vector<Thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(Thread::Fn([&] {
            for (int k = 0; k < 20000; ++k)
            {
                LockGuard g(m);
                ++counter;
            }
        }));
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    EXPECT_EQ(counter, 80000);
    EXPECT_TRUE(m.try_lock());
    EXPECT_FALSE(m.try_lock());
    m.unlock();
}

TEST(CondVar, ProducerConsumer)
{
    Mutex m;
    CondVar cv;
    ct::Vector<int> queue;
    bool done = false;
    long sum = 0;
    Thread consumer([&] {
        for (;;)
        {
            LockGuard g(m);
            cv.wait(m, [&] { return !queue.empty() || done; });
            while (!queue.empty())
            {
                sum += queue.back();
                queue.pop_back();
            }
            if (done)
                return;
        }
    });
    for (int i = 1; i <= 1000; ++i)
    {
        {
            LockGuard g(m);
            queue.push_back(i);
        }
        cv.notify_one();
    }
    {
        LockGuard g(m);
        done = true;
    }
    cv.notify_all();
    consumer.join();
    EXPECT_EQ(sum, 500500);
}

TEST(ThreadPool, SubmitAndWaitAll)
{
    ThreadPool pool(4);
    EXPECT_EQ(pool.size(), 4u);
    Atomic<int> counter(0);
    for (int i = 0; i < 1000; ++i)
        pool.submit([&] { counter.fetch_add(1); });
    pool.wait_all();
    EXPECT_EQ(counter.load(), 1000);
    EXPECT_EQ(pool.pending(), 0u);
    pool.wait_all();
}

TEST(ThreadPool, ParallelForCoversEveryIndexOnce)
{
    ThreadPool pool(3);
    std::vector<int> hits(100000, 0);
    pool.parallel_for(0, hits.size(), [&](std::size_t i) { hits[i] += 1; });
    for (std::size_t i = 0; i < hits.size(); ++i)
        ASSERT_EQ(hits[i], 1) << i;
    std::vector<int> small(7, 0);
    pool.parallel_for(2, 7, [&](std::size_t i) { small[i] = int(i); }, 1);
    EXPECT_EQ(small[1], 0);
    for (std::size_t i = 2; i < 7; ++i)
        EXPECT_EQ(small[i], int(i));
    pool.parallel_for(5, 5, [&](std::size_t) { FAIL(); });
}

TEST(ThreadPool, ParallelForSumsWithAtomic)
{
    ThreadPool pool;
    Atomic<long long> sum(0);
    pool.parallel_for(1, 100001, [&](std::size_t i) { sum.fetch_add(static_cast<long long>(i)); });
    EXPECT_EQ(sum.load(), 5000050000LL);
}

TEST(ThreadPool, NestedParallelForFromJobsDoesNotDeadlock)
{
    ThreadPool pool(2);
    Atomic<int> total(0);
    for (int j = 0; j < 8; ++j)
        pool.submit([&] { pool.parallel_for(0, 1000, [&](std::size_t) { total.fetch_add(1); }); });
    pool.wait_all();
    EXPECT_EQ(total.load(), 8000);
}

TEST(ThreadPool, DestructorRunsPendingJobs)
{
    Atomic<int> counter(0);
    {
        ThreadPool pool(2);
        for (int i = 0; i < 200; ++i)
            pool.submit([&] {
                Thread::yield();
                counter.fetch_add(1);
            });
    }
    EXPECT_EQ(counter.load(), 200);
}

TEST(ThreadPool, SingleWorkerStillParallelForWithCallerHelping)
{
    ThreadPool pool(1);
    Atomic<int> n(0);
    pool.parallel_for(0, 5000, [&](std::size_t) { n.fetch_add(1); });
    EXPECT_EQ(n.load(), 5000);
}
