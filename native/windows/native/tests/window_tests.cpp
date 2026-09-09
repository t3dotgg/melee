#include "native_window.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeWindow window(640, 480, false);
    assert(window.valid());
    assert(window.width() == 640 && window.height() == 480);
    assert(window.pump_messages());
    window.show();
    assert(window.pump_messages());
    std::cout << "native window tests passed\n";
}
