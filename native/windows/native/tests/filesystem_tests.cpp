#include "native_asset_store.h"

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace melee::native;

static void put16(std::vector<std::byte>& b, std::size_t p, std::uint16_t v)
{
    b[p] = static_cast<std::byte>(v >> 8); b[p + 1] = static_cast<std::byte>(v);
}
static void put32(std::vector<std::byte>& b, std::size_t p, std::uint32_t v)
{
    b[p] = static_cast<std::byte>(v >> 24); b[p + 1] = static_cast<std::byte>(v >> 16);
    b[p + 2] = static_cast<std::byte>(v >> 8); b[p + 3] = static_cast<std::byte>(v);
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "melee-native-assets-test";
    fs::remove_all(root);
    fs::create_directories(root / "sub");
    { std::ofstream(root / "hello.bin", std::ios::binary) << "hello"; }
    { std::ofstream(root / "sub" / "world.bin", std::ios::binary) << "world"; }

    NativeFileSystem files(root);
    assert(files.exists("hello.bin"));
    assert(!files.exists("missing.bin"));
    assert(files.read_file("hello.bin").size() == 5);
    bool rejected = false;
    try { (void)files.read_file("../hello.bin"); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)files.read_file(root.string()); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    // Build a tiny MARC package and verify archive entries are copied out.
    std::vector<std::byte> archive(64);
    archive[0] = std::byte{'M'}; archive[1] = std::byte{'A'}; archive[2] = std::byte{'R'}; archive[3] = std::byte{'C'};
    put16(archive, 4, 1); put16(archive, 6, 1);
    put32(archive, 8, 48); put32(archive, 12, 3); put32(archive, 16, 40); put16(archive, 20, 4);
    archive[40] = std::byte{'t'}; archive[41] = std::byte{'e'}; archive[42] = std::byte{'s'}; archive[43] = std::byte{'t'};
    archive[48] = std::byte{7}; archive[49] = std::byte{8}; archive[50] = std::byte{9};
    { std::ofstream out(root / "test.marc", std::ios::binary); out.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size())); }
    NativeAssetStore store(NativeFileSystem(root), 2);
    const auto payload = store.read_archive_entry("test.marc", "test");
    assert(payload.size() == 3 && std::to_integer<unsigned>(payload[2]) == 9);

    std::vector<NativeAssetStore::RequestId> order;
    std::vector<AssetStatus> statuses;
    const auto first = store.request("hello.bin", [&](AssetReadResult result) {
        order.push_back(result.request_id); statuses.push_back(result.status); assert(result.data.size() == 5);
    });
    const auto second = store.request("missing.bin", [&](AssetReadResult result) {
        order.push_back(result.request_id); statuses.push_back(result.status); assert(!result.error.empty());
    });
    const auto cancelled = store.request("sub/world.bin", [&](AssetReadResult result) {
        order.push_back(result.request_id); statuses.push_back(result.status); assert(result.data.empty());
    });
    assert(first == 1 && second == 2 && cancelled == 3);
    assert(store.cancel(cancelled));
    assert(!store.cancel(999));
    store.wait();
    assert((order == std::vector<NativeAssetStore::RequestId>{1, 2, 3}));
    assert(statuses[0] == AssetStatus::Success && statuses[1] == AssetStatus::Failed && statuses[2] == AssetStatus::Cancelled);
    fs::remove_all(root);
    std::cout << "native filesystem and asset tests passed\n";
}
