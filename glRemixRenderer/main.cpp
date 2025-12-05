#include "rt_app.h"

int main(int argc, char* argv[])
{
    // Enable debug layer for Debug and RelWithDebInfo builds
#if defined(_DEBUG) || defined(CONFIG_RELWITHDEBINFO)
    constexpr bool enable_debug = true;
#else
    constexpr bool enable_debug = false;
#endif

    glRemix::glRemixRenderer renderer;
    renderer.run_with_hwnd(enable_debug);
    return 0;
}
