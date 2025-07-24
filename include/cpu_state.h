#ifndef CPU_STATE_H
#define CPU_STATE_H

#include <cstdint>

const int MEMORY_SIZE = 1024 * 1024;
const uint32_t HALT_INSTRUCTION = 0x0ff00513;

struct CPU_State {
    uint32_t pc;
    uint32_t regs[32];
    uint8_t memory[MEMORY_SIZE];

    CPU_State();
};

#endif // CPU_STATE_H
