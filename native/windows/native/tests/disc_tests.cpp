#include "native_disc.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    using namespace melee::native;
    const auto root = std::filesystem::temp_directory_path() / "melee_native_disc_test";
    std::filesystem::create_directories(root);
    { std::ofstream(root / "stage.dat", std::ios::binary) << "stage"; }
    NativeAssetStore assets{NativeFileSystem(root)};
    NativeDisc disc(assets, {{7, "stage.dat"}});
    const auto data = disc.read(7);
    assert(data.size() == 5);
    bool called = false;
    disc.request(7, [&called](DiscReadResult result) {
        called = true;
        assert(result.entry_id == 7 && result.status == AssetStatus::Success);
    });
    disc.wait();
    assert(called);
    bool rejected = false;
    try { (void)disc.read(99); } catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);
    std::filesystem::remove_all(root);
    std::cout << "native disc tests passed\n";
}
