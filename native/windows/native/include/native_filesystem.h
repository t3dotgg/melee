#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace melee::native {

// Host-owned file access rooted at one directory. Every path passed to this
// class is relative to the root; traversal and symlink escapes are rejected.
class NativeFileSystem final {
public:
    explicit NativeFileSystem(std::filesystem::path root);

    const std::filesystem::path& root() const noexcept { return root_; }
    std::filesystem::path resolve(std::string_view relative) const;
    bool exists(std::string_view relative) const;
    std::vector<std::byte> read_file(std::string_view relative) const;

private:
    std::filesystem::path root_;
};

} // namespace melee::native