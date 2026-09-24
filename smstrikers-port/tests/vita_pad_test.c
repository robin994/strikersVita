#undef NDEBUG
#include <assert.h>

#include "../src/platform/pad.c"

int main(void)
{
    // Signed GameCube convention: right/up positive, left/down negative.
    assert(vita_axis_x(0) == -128);
    assert(vita_axis_x(128) == 0);
    assert(vita_axis_x(255) == 127);

    assert(vita_axis_y(0) == 127);
    assert(vita_axis_y(127) == 0);
    assert(vita_axis_y(128) == -1);
    assert(vita_axis_y(255) == -128);

    // The one-count centre bias disappears through the normal GameCube clamp
    // dead zone and, critically, neither endpoint wraps sign.
    PADStatus status[PAD_MAX_CONTROLLERS] = {};
    status[0].err = PAD_ERR_NONE;
    status[0].stickY = vita_axis_y(128);
    PADClampCircle(status);
    assert(status[0].stickY == 0);
    return 0;
}
