#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

#include "absl/synchronization/mutex.h"

class ThreadPool {
 public:
  explicit ThreadPool(size_t num) {
    if (num > 32) {
      num = 4;
    }
    for (size_t i = 0; i < num; ++i) {
      _workers.emplace_back(std::thread(&ThreadPool::WorkerLoop, this));
    }
  }
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ~ThreadPool() {
    {
      absl::MutexLock lck(&_m);
      for (size_t i = 0; i < _workers.size(); ++i) {
        _tasks.emplace(nullptr);
      }
    }
    for (auto& worker : _workers) {
      worker.join();
    }
  }

  template <typename F, typename... Args>
  auto Schedule(F&& f, Args&&... args)
      -> std::future<typename std::result_of_t<F(Args...)>> {
    using return_type = typename std::result_of_t<F(Args...)>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    auto fut = task->get_future();
    {
      absl::MutexLock lck(&_m);
      _tasks.emplace([task]() { (*task)(); });
    }
    return fut;
  }

 private:
  void WorkerLoop() {
    while (1) {
      std::function<void()> f;
      {
        absl::MutexLock l(&_m);
        _m.Await(absl::Condition(this, &ThreadPool::Avaliavle));
        f = std::move(_tasks.front());
        _tasks.pop();
      }
      if (f == nullptr) {
        break;
      }
      f();
    }
  }
  bool Avaliavle() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(_m) {
    return !_tasks.empty();
  }
  absl::Mutex _m;
  std::vector<std::thread> _workers;
  std::queue<std::function<void()>> _tasks GUARDED_BY(_m);
};