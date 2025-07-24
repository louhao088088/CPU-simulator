#include "cpu_state.h"

CPU_State::CPU_State() {
    pc = 0;
    for (int i = 0; i < 32; ++i) {
        regs[i] = 0;
    }
    for (int i = 0; i < MEMORY_SIZE; ++i) {
        memory[i] = 0;
    }
}
