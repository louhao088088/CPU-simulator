#ifndef RISCV_SIMULATOR_H
#define RISCV_SIMULATOR_H

#include "cpu_state.h"

class RISCV_Simulator {
  private:
    CPU_State cpu;
    bool is_halted;

  public:
    RISCV_Simulator();

    // Load program from stdin
    void load_program();

    // Run the simulation
    void run();

  private:
    // Execute one clock cycle
    void tick();

    // Fetch instruction from memory
    uint32_t fetch_instruction();

    // Print final result
    void print_result();
};

#endif // RISCV_SIMULATOR_H
