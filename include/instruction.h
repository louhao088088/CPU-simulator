#ifndef UNIFIED_INSTRUCTION_H
#define UNIFIED_INSTRUCTION_H

#include "cpu_state.h"

#include <cstdint>

// 解码后的指令信息
struct Instruction {
    InstrType type;
    uint32_t rd, rs1, rs2; // 寄存器索引
    int32_t imm;           // 立即数
    uint32_t pc;           // 指令地址
    uint32_t raw;          // 原始指令编码

    Instruction() : type(InstrType::HALT), rd(0), rs1(0), rs2(0), imm(0), pc(0), raw(0) {}
};

class InstructionProcessor {
  public:
    // 指令解码
    static Instruction decode(uint32_t raw_instruction, uint32_t pc);

    // 指令类型判断
    static bool is_alu_type(InstrType type);
    static bool is_branch_type(InstrType type);
    static bool is_load_type(InstrType type);
    static bool is_store_type(InstrType type);

    // ALU操作执行
    static uint32_t execute_alu(InstrType op, uint32_t val1, uint32_t val2, int32_t imm);

    // 分支条件检查
    static bool check_branch_condition(InstrType branch_type, uint32_t val1, uint32_t val2);

    // 执行周期获取
    static int get_execution_cycles(InstrType type);

  private:
    static InstrType decode_opcode(uint32_t instruction);
    static int32_t extract_immediate(uint32_t instruction, InstrType type);
};

#endif // UNIFIED_INSTRUCTION_H
