#include <cassert>

#ifdef NDEBUG
#error "RoboDrummer core tests require assertions to remain enabled in Release CI builds"
#endif

int main() {
    volatile bool evaluated = false;
    assert((evaluated = true));
    return evaluated ? 0 : 1;
}
