#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "cpu_state.h"

#include <cstdint>

class InstructionProcessor {
  public:
    static void decode_and_execute(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc,
                                   bool &is_halted);

  private:
    // U-type instructions
    static void execute_lui(CPU_State &cpu, uint32_t instruction);
    static void execute_auipc(CPU_State &cpu, uint32_t instruction);

    // J-type instructions
    static void execute_jal(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc);
    static void execute_jalr(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc,
                             bool &is_halted);

    // B-type instructions
    static void execute_branch(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc,
                               bool &is_halted);

    // I-type instructions
    static void execute_load(CPU_State &cpu, uint32_t instruction, bool &is_halted);
    static void execute_immediate(CPU_State &cpu, uint32_t instruction, bool &is_halted);

    // S-type instructions
    static void execute_store(CPU_State &cpu, uint32_t instruction, bool &is_halted);

    // R-type instructions
    static void execute_register(CPU_State &cpu, uint32_t instruction, bool &is_halted);
};

#endif // INSTRUCTION_H
