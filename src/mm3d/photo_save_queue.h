// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace MM3D::PhotoCapture {
// The game retains one photo. Bound background work to the active save and the
// newest pending save; rapid replacements cannot accumulate full-size images.
class SaveQueue {
    std::mutex mutex;
    std::condition_variable changed;
    std::function<void()> pending;
    bool stopping=false;
    std::thread worker;
public:
    SaveQueue() : worker([this] {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,[this]{return stopping || bool(pending);});
                if(!pending)return;
                task=std::exchange(pending,{});
            }
            task();
        }
    }) {}
    ~SaveQueue() {
        {std::lock_guard lock(mutex);stopping=true;}
        changed.notify_one();worker.join(); // Finish the latest accepted photo on shutdown.
    }
    void Submit(std::function<void()> task) {
        {std::lock_guard lock(mutex);pending=std::move(task);}
        changed.notify_one();
    }
};
}
