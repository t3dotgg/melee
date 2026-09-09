#include "native_dat_scene.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    using namespace melee::native;
    bool rejected = false;
    try {
        NativeDatArchive archive = NativeDatArchive::parse({});
        (void)collect_hsd_joint_materials(archive, 0, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    const char* path = std::getenv("MELEE_DAT_JOINT");
    const char* offset_text = std::getenv("MELEE_DAT_JOINT_OFFSET");
    if (path != nullptr && offset_text != nullptr) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("MELEE_DAT_JOINT cannot be opened");
        const std::vector<char> raw((std::istreambuf_iterator<char>(input)), {});
        const auto archive = NativeDatArchive::parse(std::as_bytes(std::span(raw)));
        const auto offset = std::stoull(offset_text);
        const auto materials = collect_hsd_joint_materials(archive, offset);
        std::cout << "native DAT scene: " << path << " materials=" << materials.size()
                  << " root=" << offset << "\n";
    }
    std::cout << "native DAT scene tests passed\n";
}
