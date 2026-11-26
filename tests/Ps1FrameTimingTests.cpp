// Checks the PS1 loop's shared fixed-point frame delta (frame_timing.h).
#include <iostream>

extern "C" int CheckPS1FrameTiming(void);

int main() {
    if (CheckPS1FrameTiming() != 0) {
        std::cerr << "PS1 frame delta differs from the exported fixed-point division\n";
        return 1;
    }
    std::cout << "PS1 frame timing checks passed\n";
    return 0;
}
