#include "../include/instruction.h"

Instruction InstructionProcessor::decode(uint32_t raw_instruction, uint32_t pc) {
    Instruction instr;
    instr.raw = raw_instruction;
    instr.pc = pc;
    instr.rd = (raw_instruction >> 7) & 0x1F;
    instr.rs1 = (raw_instruction >> 15) & 0x1F;
    instr.rs2 = (raw_instruction >> 20) & 0x1F;

    instr.type = decode_opcode(raw_instruction);
    instr.imm = extract_immediate(raw_instruction, instr.type);

    return instr;
}

InstrType InstructionProcessor::decode_opcode(uint32_t instruction) {
    uint32_t opcode = instruction & 0x7F;
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t funct7 = (instruction >> 25) & 0x7F;

    if (instruction == 0x0ff00513) {
        return InstrType::HALT;
    }

    switch (opcode) {
    case 0x33: // R-type
        if (funct7 == 0x00) {
            switch (funct3) {
            case 0x0:
                return InstrType::ALU_ADD;
            case 0x1:
                return InstrType::ALU_SLL;
            case 0x2:
                return InstrType::ALU_SLT;
            case 0x3:
                return InstrType::ALU_SLTU;
            case 0x4:
                return InstrType::ALU_XOR;
            case 0x5:
                return InstrType::ALU_SRL;
            case 0x6:
                return InstrType::ALU_OR;
            case 0x7:
                return InstrType::ALU_AND;
            }
        } else if (funct7 == 0x20) {
            switch (funct3) {
            case 0x0:
                return InstrType::ALU_SUB;
            case 0x5:
                return InstrType::ALU_SRA;
            }
        }
        break;

    case 0x13: // I-type immediate
        switch (funct3) {
        case 0x0:
            return InstrType::ALU_ADDI;
        case 0x2:
            return InstrType::ALU_SLTI;
        case 0x3:
            return InstrType::ALU_SLTIU;
        case 0x4:
            return InstrType::ALU_XORI;
        case 0x6:
            return InstrType::ALU_ORI;
        case 0x7:
            return InstrType::ALU_ANDI;
        case 0x1:
            return InstrType::ALU_SLLI;
        case 0x5:
            return (funct7 == 0x00) ? InstrType::ALU_SRLI : InstrType::ALU_SRAI;
        }
        break;

    case 0x03: // Load
        switch (funct3) {
        case 0x0:
            return InstrType::LOAD_LB;
        case 0x1:
            return InstrType::LOAD_LH;
        case 0x2:
            return InstrType::LOAD_LW;
        case 0x4:
            return InstrType::LOAD_LBU;
        case 0x5:
            return InstrType::LOAD_LHU;
        }
        break;

    case 0x23: // Store
        switch (funct3) {
        case 0x0:
            return InstrType::STORE_SB;
        case 0x1:
            return InstrType::STORE_SH;
        case 0x2:
            return InstrType::STORE_SW;
        }
        break;

    case 0x63: // Branch
        switch (funct3) {
        case 0x0:
            return InstrType::BRANCH_BEQ;
        case 0x1:
            return InstrType::BRANCH_BNE;
        case 0x4:
            return InstrType::BRANCH_BLT;
        case 0x5:
            return InstrType::BRANCH_BGE;
        case 0x6:
            return InstrType::BRANCH_BLTU;
        case 0x7:
            return InstrType::BRANCH_BGEU;
        }
        break;

    case 0x37:
        return InstrType::LUI;
    case 0x17:
        return InstrType::AUIPC;
    case 0x6F:
        return InstrType::JUMP_JAL;
    case 0x67:
        return InstrType::JUMP_JALR;
    }

    // 检查HALT指令

    return InstrType::ALU_ADD; // 默认值
}

int32_t InstructionProcessor::extract_immediate(uint32_t instruction, InstrType type) {
    uint32_t opcode = instruction & 0x7F;

    switch (opcode) {
    case 0x13:                                          // I-type
    case 0x03:                                          // Load
    case 0x67:                                          // JALR
        return static_cast<int32_t>(instruction) >> 20; // 符号扩展

    case 0x23: // Store (S-type)
        return ((instruction >> 7) & 0x1F) | (((int32_t) instruction >> 25) << 5);

    case 0x63: // Branch (B-type)
    {
        uint32_t imm_12 = (instruction >> 31) & 1;
        uint32_t imm_10_5 = (instruction >> 25) & 0x3F;
        uint32_t imm_4_1 = (instruction >> 8) & 0xF;
        uint32_t imm_11 = (instruction >> 7) & 1;
        uint32_t imm = (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);
        return imm_12 ? (imm | 0xFFFFF000) : imm; // 符号扩展
    }

    case 0x37: // LUI (U-type)
    case 0x17: // AUIPC (U-type)
        return instruction & 0xFFFFF000;

    case 0x6F: // JAL (J-type)
    {
        uint32_t imm_20 = (instruction >> 31) & 1;
        uint32_t imm_10_1 = (instruction >> 21) & 0x3FF;
        uint32_t imm_11 = (instruction >> 20) & 1;
        uint32_t imm_19_12 = (instruction >> 12) & 0xFF;
        uint32_t imm = (imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1);
        return imm_20 ? (imm | 0xFFE00000) : imm; // 符号扩展
    }
    }

    return 0;
}

bool InstructionProcessor::is_alu_type(InstrType type) {
    return type >= InstrType::ALU_ADD && type <= InstrType::ALU_SLTIU;
}

bool InstructionProcessor::is_branch_type(InstrType type) {
    return type >= InstrType::BRANCH_BEQ && type <= InstrType::BRANCH_BGEU;
}

bool InstructionProcessor::is_load_type(InstrType type) {
    return type >= InstrType::LOAD_LB && type <= InstrType::LOAD_LHU;
}

bool InstructionProcessor::is_store_type(InstrType type) {
    return type >= InstrType::STORE_SB && type <= InstrType::STORE_SW;
}

uint32_t InstructionProcessor::execute_alu(InstrType op, uint32_t val1, uint32_t val2,
                                           int32_t imm) {
    switch (op) {
    // R-type指令
    case InstrType::ALU_ADD:
        return val1 + val2;
    case InstrType::ALU_SUB:
        return val1 - val2;
    case InstrType::ALU_AND:
        return val1 & val2;
    case InstrType::ALU_OR:
        return val1 | val2;
    case InstrType::ALU_XOR:
        return val1 ^ val2;
    case InstrType::ALU_SLL:
        return val1 << (val2 & 0x1F);
    case InstrType::ALU_SRL:
        return val1 >> (val2 & 0x1F);
    case InstrType::ALU_SRA:
        return static_cast<int32_t>(val1) >> (val2 & 0x1F);
    case InstrType::ALU_SLT:
        return static_cast<int32_t>(val1) < static_cast<int32_t>(val2) ? 1 : 0;
    case InstrType::ALU_SLTU:
        return val1 < val2 ? 1 : 0;

    // I-type立即数操作
    case InstrType::ALU_ADDI:
        return val1 + imm;
    case InstrType::ALU_SLTI:
        return static_cast<int32_t>(val1) < imm ? 1 : 0;
    case InstrType::ALU_SLTIU:
        return val1 < static_cast<uint32_t>(imm) ? 1 : 0;
    case InstrType::ALU_XORI:
        return val1 ^ imm;
    case InstrType::ALU_ORI:
        return val1 | imm;
    case InstrType::ALU_ANDI:
        return val1 & imm;
    case InstrType::ALU_SLLI:
        return val1 << (imm & 0x1F);
    case InstrType::ALU_SRLI:
        return val1 >> (imm & 0x1F);
    case InstrType::ALU_SRAI:
        return static_cast<int32_t>(val1) >> (imm & 0x1F);

    // 特殊指令
    case InstrType::LUI:
        return imm;
    case InstrType::AUIPC:
        return val1 + imm; // val1是PC
    case InstrType::JUMP_JAL:
        return val1 + 4; // 返回地址是PC+4
    case InstrType::JUMP_JALR:
        return val1 + 4; // 返回地址是PC+4

    default:
        return 0;
    }
}

bool InstructionProcessor::check_branch_condition(InstrType branch_type, uint32_t val1,
                                                  uint32_t val2) {
    switch (branch_type) {
    case InstrType::BRANCH_BEQ:
        return val1 == val2;
    case InstrType::BRANCH_BNE:
        return val1 != val2;
    case InstrType::BRANCH_BLT:
        return static_cast<int32_t>(val1) < static_cast<int32_t>(val2);
    case InstrType::BRANCH_BGE:
        return static_cast<int32_t>(val1) >= static_cast<int32_t>(val2);
    case InstrType::BRANCH_BLTU:
        return val1 < val2;
    case InstrType::BRANCH_BGEU:
        return val1 >= val2;
    default:
        return false;
    }
}

int InstructionProcessor::get_execution_cycles(InstrType type) {
    if (is_load_type(type) || is_store_type(type)) {
        return 3; // 内存操作需要3个周期
    }
    // 其他操作只需要1个周期
    return 1;
}
