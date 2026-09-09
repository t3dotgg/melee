#include "native_asset_store.h"

#include <stdexcept>

namespace melee::native {

NativeAssetStore::NativeAssetStore(NativeFileSystem file_system, std::size_t worker_count)
    : file_system_(std::move(file_system))
{
    if (worker_count == 0) worker_count = 1;
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) workers_.emplace_back(&NativeAssetStore::worker_loop, this);
}

NativeAssetStore::~NativeAssetStore()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    work_ready_.notify_all();
    for (auto& worker : workers_) worker.join();
}

std::vector<std::byte> NativeAssetStore::read_file(std::string_view relative) const
{
    return file_system_.read_file(relative);
}

std::vector<std::byte> NativeAssetStore::read_archive_entry(std::string_view archive_path,
                                                              std::string_view entry_name) const
{
    const auto blob = file_system_.read_file(archive_path);
    const auto archive = NativeArchive::parse(blob);
    const auto* entry = archive.find(entry_name);
    if (entry == nullptr) throw std::out_of_range("archive entry was not found");
    const auto payload = archive.data(*entry);
    return {payload.begin(), payload.end()};
}

NativeAssetStore::RequestId NativeAssetStore::request(std::string relative, Completion completion)
{
    if (!completion) throw std::invalid_argument("asset completion callback is empty");
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    std::lock_guard lock(mutex_);
    if (stopping_) throw std::runtime_error("asset store is shutting down");
    const RequestId id = next_id_++;
    cancellation_.emplace(id, cancelled);
    jobs_.push_back({id, std::move(relative), std::move(completion), std::move(cancelled)});
    ++outstanding_;
    work_ready_.notify_one();
    return id;
}

bool NativeAssetStore::cancel(RequestId request_id)
{
    std::lock_guard lock(mutex_);
    const auto it = cancellation_.find(request_id);
    if (it == cancellation_.end()) return false;
    it->second->store(true, std::memory_order_release);
    const auto completed = completed_.find(request_id);
    if (completed != completed_.end()) {
        completed->second.result.status = AssetStatus::Cancelled;
        completed->second.result.data.clear();
        completed->second.result.error.clear();
    }
    return true;
}

std::size_t NativeAssetStore::poll()
{
    std::size_t dispatched = 0;
    for (;;) {
        Completed completed;
        {
            std::lock_guard lock(mutex_);
            const auto it = completed_.find(next_dispatch_);
            if (it == completed_.end()) break;
            completed = std::move(it->second);
            completed_.erase(it);
            cancellation_.erase(next_dispatch_);
            ++next_dispatch_;
            --outstanding_;
        }
        completed.completion(std::move(completed.result));
        ++dispatched;
    }
    if (dispatched != 0) completion_ready_.notify_all();
    return dispatched;
}

void NativeAssetStore::wait()
{
    for (;;) {
        poll();
        std::unique_lock lock(mutex_);
        if (outstanding_ == 0) return;
        completion_ready_.wait(lock, [this] { return completed_.contains(next_dispatch_) || outstanding_ == 0; });
    }
}

void NativeAssetStore::worker_loop()
{
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            work_ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        AssetReadResult result;
        result.request_id = job.id;
        result.path = job.path;
        if (job.cancelled->load(std::memory_order_acquire)) {
            result.status = AssetStatus::Cancelled;
        } else {
            try {
                result.data = file_system_.read_file(job.path);
                result.status = job.cancelled->load(std::memory_order_acquire) ? AssetStatus::Cancelled : AssetStatus::Success;
                if (result.status == AssetStatus::Cancelled) result.data.clear();
            } catch (const std::exception& error) {
                result.status = job.cancelled->load(std::memory_order_acquire) ? AssetStatus::Cancelled : AssetStatus::Failed;
                if (result.status == AssetStatus::Failed) result.error = error.what();
            }
        }
        {
            std::lock_guard lock(mutex_);
            completed_.emplace(job.id, Completed{std::move(result), std::move(job.completion)});
        }
        completion_ready_.notify_all();
    }
}

} // namespace melee::native
