#include "native_filesystem.h"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace melee::native {
namespace fs = std::filesystem;

NativeFileSystem::NativeFileSystem(fs::path root)
{
    std::error_code ec;
    root_ = fs::weakly_canonical(root, ec);
    if (ec || !fs::is_directory(root_, ec) || ec) {
        throw std::invalid_argument("native filesystem root is not a directory");
    }
}

fs::path NativeFileSystem::resolve(std::string_view relative) const
{
    if (relative.empty() || relative.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("asset path is empty or contains NUL");
    }
    const fs::path requested{std::string(relative)};
    if (requested.has_root_name() || requested.has_root_directory() || requested.is_absolute()) {
        throw std::invalid_argument("asset path must be relative");
    }
    for (const auto& component : requested) {
        if (component == fs::path("..")) {
            throw std::invalid_argument("asset path escapes filesystem root");
        }
    }

    std::error_code ec;
    const fs::path result = fs::weakly_canonical(root_ / requested, ec);
    if (ec) {
        throw std::invalid_argument("unable to resolve asset path");
    }
    const fs::path relative_result = result.lexically_relative(root_);
    if (relative_result.empty() || *relative_result.begin() == fs::path("..")) {
        throw std::invalid_argument("asset path escapes filesystem root");
    }
    return result;
}

bool NativeFileSystem::exists(std::string_view relative) const
{
    std::error_code ec;
    const fs::path path = resolve(relative);
    return fs::exists(path, ec) && !ec;
}

std::vector<std::byte> NativeFileSystem::read_file(std::string_view relative) const
{
    const fs::path path = resolve(relative);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        throw std::invalid_argument("asset path is not a regular file");
    }
    const auto size = fs::file_size(path, ec);
    if (ec || size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("unable to determine asset size");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to open asset file");
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    if (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()))) {
        throw std::runtime_error("unable to read asset file");
    }
    return result;
}

} // namespace melee::native
