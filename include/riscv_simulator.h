#ifndef RISCV_SIMULATOR_H
#define RISCV_SIMULATOR_H

#include "cpu_state.h"

class Processor;

class RISCV_Simulator {
  private:
    CPU_State cpu;
    bool is_halted;
    bool chaos_order;
    Processor *processor;

  public:
    RISCV_Simulator(bool chaos_order = false);
    ~RISCV_Simulator();

    void load_program();
    void run();

  private:
    void tick();

    uint32_t fetch_instruction();

    void print_result();
};

#endif
