#include "../include/cpu_state.h"

CPU_Core::CPU_Core()
    : pc(0), fetch_buffer_head(0), fetch_buffer_tail(0), fetch_buffer_size(0), rob_head(0),
      rob_tail(0), rob_size(0), branch_predictor(false), fetch_stalled(false),
      pipeline_flushed(false), clear_flag(0), commit_flag(0), next_pc(0) {

    for (int i = 0; i < FETCH_BUFFER_SIZE; ++i) {
        fetch_buffer[i] = FetchBufferEntry();
    }

    Regs.flush();

    for (int i = 0; i < ROB_SIZE; ++i) {
        rob[i] = ROBEntry();
    }

    for (int i = 0; i < RS_SIZE; ++i) {
        rs_alu[i] = RSEntry();
    }
    for (int i = 0; i < RS_SIZE / 2; ++i) {
        rs_branch[i] = RSEntry();
    }

    for (int i = 0; i < LSB_SIZE; ++i) {
        LSB[i] = LSBEntry();
    }
}

CPU_State::CPU_State() {
    for (int i = 0; i < MEMORY_SIZE; i++)
        memory[i] = 0;
}

std::string Type_string(InstrType type) {
    switch (type) {
    case InstrType::ALU_ADD:
        return "ALU_ADD";
    case InstrType::ALU_SUB:
        return "ALU_SUB";
    case InstrType::ALU_AND:
        return "ALU_AND";
    case InstrType::ALU_OR:
        return "ALU_OR";
    case InstrType::ALU_XOR:
        return "ALU_XOR";
    case InstrType::ALU_SLL:
        return "ALU_SLL";
    case InstrType::ALU_SRL:
        return "ALU_SRL";
    case InstrType::ALU_SRA:
        return "ALU_SRA";
    case InstrType::ALU_SLT:
        return "ALU_SLT";
    case InstrType::ALU_SLTU:
        return "ALU_SLTU";
    case InstrType::ALU_ADDI:
        return "ALU_ADDI";
    case InstrType::ALU_ANDI:
        return "ALU_ANDI";
    case InstrType::ALU_ORI:
        return "ALU_ORI";
    case InstrType::ALU_XORI:
        return "ALU_XORI";
    case InstrType::ALU_SLLI:
        return "ALU_SLLI";
    case InstrType::ALU_SRLI:
        return "ALU_SRLI";
    case InstrType::ALU_SRAI:
        return "ALU_SRAI";
    case InstrType::ALU_SLTI:
        return "ALU_SLTI";
    case InstrType::ALU_SLTIU:
        return "ALU_SLTIU";
    case InstrType::LOAD_LB:
        return "LOAD_LB";
    case InstrType::LOAD_LH:
        return "LOAD_LH";
    case InstrType::LOAD_LW:
        return "LOAD_LW";
    case InstrType::LOAD_LBU:
        return "LOAD_LBU";
    case InstrType::LOAD_LHU:
        return "LOAD_LHU";
    case InstrType::STORE_SB:
        return "STORE_SB";
    case InstrType::STORE_SH:
        return "STORE_SH";
    case InstrType::STORE_SW:
        return "STORE_SW";
    case InstrType::BRANCH_BEQ:
        return "BRANCH_BEQ";
    case InstrType::BRANCH_BNE:
        return "BRANCH_BNE";
    case InstrType::BRANCH_BLT:
        return "BRANCH_BLT";
    case InstrType::BRANCH_BGE:
        return "BRANCH_BGE";
    case InstrType::BRANCH_BLTU:
        return "BRANCH_BLTU";
    case InstrType::BRANCH_BGEU:
        return "BRANCH_BGEU";
    case InstrType::JUMP_JAL:
        return "JUMP_JAL";
    case InstrType::JUMP_JALR:
        return "JUMP_JALR";
    case InstrType::LUI:
        return "LUI";
    case InstrType::AUIPC:
        return "AUIPC";
    case InstrType::HALT:
        return "HALT";
    default:
        return "UNKNOWN";
    }
}