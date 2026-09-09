#include <stdint.h>

/* These symbols are stack markers in the GameCube linker script. Native
 * diagnostics only need stable bounds for a host process. */
unsigned char _stack_addr[1];
unsigned char _stack_end[1];

uint64_t __cvt_dbl_usll(double value)
{
    if (!(value > 0.0)) {
        return 0;
    }
    if (value >= (double) UINT64_MAX) {
        return UINT64_MAX;
    }
    return (uint64_t) value;
}
