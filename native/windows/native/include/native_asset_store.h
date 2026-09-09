#pragma once

#include "native_archive.h"
#include "native_filesystem.h"

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace melee::native {

enum class AssetStatus { Success, Failed, Cancelled };

struct AssetReadResult {
    std::uint64_t request_id = 0;
    std::string path;
    AssetStatus status = AssetStatus::Failed;
    std::vector<std::byte> data;
    std::string error;
};

// NativeAssetStore provides synchronous loose-file/archive reads and an
// asynchronous API. Completion callbacks are delivered by poll() in request
// order, regardless of worker completion order.
class NativeAssetStore final {
public:
    using RequestId = std::uint64_t;
    using Completion = std::function<void(AssetReadResult)>;

    explicit NativeAssetStore(NativeFileSystem file_system, std::size_t worker_count = 1);
    ~NativeAssetStore();
    NativeAssetStore(const NativeAssetStore&) = delete;
    NativeAssetStore& operator=(const NativeAssetStore&) = delete;

    std::vector<std::byte> read_file(std::string_view relative) const;
    std::vector<std::byte> read_archive_entry(std::string_view archive_path,
                                               std::string_view entry_name) const;

    RequestId request(std::string relative, Completion completion);
    bool cancel(RequestId request_id);
    std::size_t poll();
    void wait();

private:
    struct Job {
        RequestId id;
        std::string path;
        Completion completion;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    struct Completed {
        AssetReadResult result;
        Completion completion;
    };
    void worker_loop();

    NativeFileSystem file_system_;
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable completion_ready_;
    std::deque<Job> jobs_;
    std::unordered_map<RequestId, Completed> completed_;
    std::unordered_map<RequestId, std::shared_ptr<std::atomic_bool>> cancellation_;
    std::vector<std::thread> workers_;
    RequestId next_id_ = 1;
    RequestId next_dispatch_ = 1;
    std::size_t outstanding_ = 0;
    bool stopping_ = false;
};

} // namespace melee::native
