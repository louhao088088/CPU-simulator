#include "include/cpu_state.h"
#include "include/instruction.h"

// OutOfOrderProcessor 类的构造函数
OutOfOrderProcessor::OutOfOrderProcessor() {
    // 构造函数实现
}

// 主要的时钟周期函数
void OutOfOrderProcessor::tick(CPU_State &cpu) {
    // ！！！！注意：这几个阶段的执行顺序在模拟中很重要！！！！
    // 我们通常以后端到前端的倒序来模拟，防止一条指令在一个周期内穿越多个阶段
    // (e.g., commit an instruction and fetch a new one in its place)
    // 这样最容易写对。

    // commit(cpu);            // 阶段5: 提交
    // writeback(cpu);         // 阶段4: 写回/广播
    // execute(cpu);           // 阶段3: 执行
    // dispatch(cpu);          // 阶段2: 分派
    // decode_and_rename(cpu); // 阶段1.5: 解码与重命名
    // fetch(cpu);             // 阶段1: 取指
}

// 获取指令执行周期数
int OutOfOrderProcessor::get_execution_cycles(InstrType type) {
    switch (type) {
    case InstrType::ALU_ADD:
    case InstrType::ALU_SUB:
    case InstrType::ALU_AND:
    case InstrType::ALU_OR:
    case InstrType::ALU_XOR:
    case InstrType::ALU_SLL:
    case InstrType::ALU_SRL:
    case InstrType::ALU_SRA:
    case InstrType::ALU_SLT:
    case InstrType::ALU_SLTU:
    case InstrType::ALU_ADDI:
    case InstrType::ALU_ANDI:
    case InstrType::ALU_ORI:
    case InstrType::ALU_XORI:
    case InstrType::ALU_SLLI:
    case InstrType::ALU_SRLI:
    case InstrType::ALU_SRAI:
    case InstrType::ALU_SLTI:
    case InstrType::ALU_SLTIU:
        return 1; // ALU操作通常1个周期

    case InstrType::LOAD_LB:
    case InstrType::LOAD_LH:
    case InstrType::LOAD_LW:
    case InstrType::LOAD_LBU:
    case InstrType::LOAD_LHU:
        return 3; // 内存读取3个周期

    case InstrType::STORE_SB:
    case InstrType::STORE_SH:
    case InstrType::STORE_SW:
        return 2; // 内存写入2个周期

    case InstrType::BRANCH_BEQ:
    case InstrType::BRANCH_BNE:
    case InstrType::BRANCH_BLT:
    case InstrType::BRANCH_BGE:
    case InstrType::BRANCH_BLTU:
    case InstrType::BRANCH_BGEU:
        return 1; // 分支1个周期

    default:
        return 1; // 默认1个周期
    }
}
