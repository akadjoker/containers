#pragma once

#include "queue.hpp"
#include "thread.hpp"
#include "vector.hpp"

namespace ct
{
    class ThreadPool
    {
    public:
        using Job = Function<void()>;

        explicit ThreadPool(unsigned threads = 0) : active_(0), stop_(false)
        {
            if (threads == 0)
            {
                unsigned hw = Thread::hardware_concurrency();
                threads = hw > 1 ? hw - 1 : 1;
            }
            workers_.reserve(threads);
            for (unsigned i = 0; i < threads; ++i)
                workers_.emplace_back(Job([this] { worker(); }));
        }

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        ~ThreadPool()
        {
            {
                LockGuard g(mutex_);
                stop_ = true;
            }
            work_cv_.notify_all();
            for (std::size_t i = 0; i < workers_.size(); ++i)
                workers_[i].join();
        }

        std::size_t size() const noexcept { return workers_.size(); }

        std::size_t pending() const
        {
            LockGuard g(mutex_);
            return jobs_.size() + active_;
        }

        void submit(Job job)
        {
            if (!job)
                detail::fatal("ct::ThreadPool::submit: funcao vazia");
            {
                LockGuard g(mutex_);
                jobs_.push(detail::move(job));
            }
            work_cv_.notify_one();
        }

        void wait_all()
        {
            const std::size_t own = frames_of(this);
            mutex_.lock();
            for (;;)
            {
                if (!jobs_.empty())
                {
                    help_one();
                    continue;
                }
                if (active_ == own)
                    break;
                done_cv_.wait(mutex_);
            }
            mutex_.unlock();
        }

        template <typename F>
        void parallel_for(std::size_t begin, std::size_t end, F &&fn, std::size_t chunk = 0)
        {
            if (end <= begin)
                return;
            std::size_t n = end - begin;
            if (chunk == 0)
            {
                std::size_t parts = (workers_.size() + 1) * 4;
                chunk = (n + parts - 1) / parts;
                if (chunk == 0)
                    chunk = 1;
            }
            using Fn = typename std::remove_reference<F>::type;
            Batch batch;
            batch.fn = static_cast<void *>(&fn);
            std::size_t count = 0;
            {
                LockGuard g(mutex_);
                for (std::size_t b = begin; b < end; b += chunk)
                {
                    Range r;
                    r.batch = &batch;
                    r.begin = b;
                    r.end = b + chunk < end ? b + chunk : end;
                    jobs_.emplace([r] { run_range<Fn>(r); });
                    ++count;
                }
                batch.remaining.store(count);
            }
            work_cv_.notify_all();
            mutex_.lock();
            while (batch.remaining.load() != 0)
            {
                if (!jobs_.empty())
                {
                    help_one();
                    continue;
                }
                mutex_.unlock();
                {
                    LockGuard g(batch.mutex);
                    while (batch.remaining.load() != 0)
                        batch.cv.wait(batch.mutex);
                }
                mutex_.lock();
            }
            mutex_.unlock();
            {
                LockGuard g(batch.mutex);
            }
        }

    private:
        struct Frame
        {
            const ThreadPool *pool;
            Frame *prev;
        };

        static Frame *&top_frame() noexcept
        {
            static thread_local Frame *top = nullptr;
            return top;
        }

        static std::size_t frames_of(const ThreadPool *pool) noexcept
        {
            std::size_t n = 0;
            for (Frame *f = top_frame(); f; f = f->prev)
                if (f->pool == pool)
                    ++n;
            return n;
        }

        void run_job(Job &job)
        {
            Frame frame;
            frame.pool = this;
            frame.prev = top_frame();
            top_frame() = &frame;
            job();
            top_frame() = frame.prev;
        }

        void help_one()
        {
            Job job(detail::move(jobs_.front()));
            jobs_.pop();
            ++active_;
            mutex_.unlock();
            run_job(job);
            mutex_.lock();
            --active_;
            if (jobs_.empty())
                done_cv_.notify_all();
        }

        struct Batch
        {
            void *fn;
            Atomic<std::size_t> remaining;
            Mutex mutex;
            CondVar cv;
        };

        struct Range
        {
            Batch *batch;
            std::size_t begin;
            std::size_t end;
        };

        template <typename F>
        static void run_range(const Range &r)
        {
            F &fn = *static_cast<F *>(r.batch->fn);
            for (std::size_t i = r.begin; i < r.end; ++i)
                fn(i);
            LockGuard g(r.batch->mutex);
            if (r.batch->remaining.fetch_sub(1) == 1)
                r.batch->cv.notify_all();
        }

        void worker()
        {
            for (;;)
            {
                Job job;
                {
                    mutex_.lock();
                    while (jobs_.empty() && !stop_)
                        work_cv_.wait(mutex_);
                    if (jobs_.empty() && stop_)
                    {
                        mutex_.unlock();
                        return;
                    }
                    job = detail::move(jobs_.front());
                    jobs_.pop();
                    ++active_;
                    mutex_.unlock();
                }
                run_job(job);
                mutex_.lock();
                --active_;
                const bool drained = jobs_.empty();
                mutex_.unlock();
                if (drained)
                    done_cv_.notify_all();
            }
        }

        mutable Mutex mutex_;
        CondVar work_cv_;
        CondVar done_cv_;
        Queue<Job> jobs_;
        Vector<Thread> workers_;
        std::size_t active_;
        bool stop_;
    };
}
