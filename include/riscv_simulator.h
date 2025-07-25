#ifndef RISCV_SIMULATOR_H
#define RISCV_SIMULATOR_H

#include "cpu_state.h"

class CPUCore;

class RISCV_Simulator {
  private:
    CPU_State cpu;
    bool is_halted;
    CPUCore *cpu_core;

  public:
    RISCV_Simulator();
    ~RISCV_Simulator();

    void load_program();
    void run();

  private:
    void tick();
    uint32_t fetch_instruction();
    void print_result();
};

#endif
