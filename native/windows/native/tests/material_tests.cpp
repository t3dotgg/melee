#include "native_material.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using namespace melee::native;
    NativeMaterial material;
    material.tev_stages.push_back({{0, 8, 12, 15}, {0, 4, 6, 7}, 0, 0, 0, true, 0,
                                    0, 0, 0, true, 0, 12, 16, 0xff, 0xff, 0xff});
    std::string error;
    assert(validate_material(material, &error));
    assert(error.empty());
    material.tev_stages[0].color_inputs[0] = 19;
    assert(!validate_material(material, &error) && error == "invalid TEV color input");
    material.tev_stages.clear();
    material.alpha = 1.1F;
    assert(!validate_material(material, &error));

    const std::byte bytes[] = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
                               std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};
    NativePalette palette;
    assert(decode_rgba8_palette(bytes, palette, &error));
    assert(palette.entries.size() == 2);
    assert((palette.entries[0] == NativeColor{0x10, 0x20, 0x30, 0x40}));
    const std::byte malformed[] = {std::byte{0}};
    assert(!decode_rgba8_palette(malformed, palette, &error));
    std::cout << "native material tests passed\n";
}
