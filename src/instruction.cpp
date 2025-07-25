#include "../include/instruction.h"

#include <iostream>

using std::cerr;

// 指令解析函数实现
UTypeInstruction InstructionProcessor::parse_u_type(uint32_t instruction, CPU_State &cpu) {
    UTypeInstruction inst;
    inst.rd_index = (instruction >> 7) & 0x1F;
    inst.imm = (instruction >> 12) & 0xFFFFF;
    return inst;
}

JTypeInstruction InstructionProcessor::parse_j_type(uint32_t instruction, CPU_State &cpu) {
    JTypeInstruction inst;
    inst.rd_index = (instruction >> 7) & 0x1F;

    uint32_t imm_20 = (instruction >> 31) & 1;
    uint32_t imm_10_1 = (instruction >> 21) & 0x3FF;
    uint32_t imm_11 = (instruction >> 20) & 1;
    uint32_t imm_19_12 = (instruction >> 12) & 0xFF;

    uint32_t imm = ((imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1));
    if (imm_20 == 1) {
        inst.offset = static_cast<int32_t>(imm | 0xFFF00000);
    } else {
        inst.offset = static_cast<int32_t>(imm);
    }
    return inst;
}

ITypeInstruction InstructionProcessor::parse_i_type(uint32_t instruction, CPU_State &cpu) {
    ITypeInstruction inst;
    inst.rd_index = (instruction >> 7) & 0x1F;
    inst.funct3 = (instruction >> 12) & 0x7;
    inst.rs1_index = (instruction >> 15) & 0x1F;
    inst.value1 = cpu.arf.regs[inst.rs1_index];
    inst.imm = static_cast<int32_t>(instruction) >> 20;
    return inst;
}

BTypeInstruction InstructionProcessor::parse_b_type(uint32_t instruction, CPU_State &cpu) {
    BTypeInstruction inst;
    inst.funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;

    inst.value1 = cpu.arf.regs[rs1_index]; // 直接读取rs1的值
    inst.value2 = cpu.arf.regs[rs2_index]; // 直接读取rs2的值

    uint32_t imm_12 = (instruction >> 31) & 0x1;
    uint32_t imm_10_5 = (instruction >> 25) & 0x3F;
    uint32_t imm_4_1 = (instruction >> 8) & 0xF;
    uint32_t imm_11 = (instruction >> 7) & 0x1;

    uint32_t imm = (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);
    inst.offset = static_cast<int32_t>(imm << 19) >> 19;
    return inst;
}

STypeInstruction InstructionProcessor::parse_s_type(uint32_t instruction, CPU_State &cpu) {
    STypeInstruction inst;
    inst.funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;

    inst.value1 = cpu.arf.regs[rs1_index]; // 基地址值
    inst.value2 = cpu.arf.regs[rs2_index]; // 要存储的数据值

    int32_t offset =
        static_cast<int32_t>((((instruction >> 25) & 0x7F) << 5) | ((instruction >> 7) & 0x1F));
    if (offset & 0x800) {
        offset |= 0xFFFFF000;
    }
    inst.offset = offset;
    return inst;
}

RTypeInstruction InstructionProcessor::parse_r_type(uint32_t instruction, CPU_State &cpu) {
    RTypeInstruction inst;
    inst.rd_index = (instruction >> 7) & 0x1F;
    inst.funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1_index = (instruction >> 15) & 0x1F;
    uint32_t rs2_index = (instruction >> 20) & 0x1F;
    inst.funct7 = (instruction >> 25) & 0x7F;

    inst.value1 = cpu.arf.regs[rs1_index]; // 直接读取rs1的值
    inst.value2 = cpu.arf.regs[rs2_index]; // 直接读取rs2的值
    return inst;
}

void InstructionProcessor::decode_and_execute(CPU_State &cpu, uint32_t instruction,
                                              uint32_t &next_pc, bool &is_halted) {
    uint32_t opcode = instruction & 0x7F;

    switch (opcode) {
    case 0b0110111: { // U-TYPE lui
        UTypeInstruction inst = parse_u_type(instruction, cpu);
        execute_lui(cpu, inst);
        break;
    }
    case 0b0010111: { // U-TYPE auipc
        UTypeInstruction inst = parse_u_type(instruction, cpu);
        execute_auipc(cpu, inst);
        break;
    }
    case 0b1101111: { // J-TYPE jal
        JTypeInstruction inst = parse_j_type(instruction, cpu);
        execute_jal(cpu, inst, next_pc);
        break;
    }
    case 0b1100111: { // I-TYPE JALR
        ITypeInstruction inst = parse_i_type(instruction, cpu);
        execute_jalr(cpu, inst, next_pc, is_halted);
        break;
    }
    case 0b1100011: { // B-TYPE
        BTypeInstruction inst = parse_b_type(instruction, cpu);
        execute_branch(cpu, inst, next_pc, is_halted);
        break;
    }
    case 0b0000011: { // I-TYPE LOAD
        ITypeInstruction inst = parse_i_type(instruction, cpu);
        execute_load(cpu, inst, is_halted);
        break;
    }
    case 0b0100011: { // S-TYPE STORE
        STypeInstruction inst = parse_s_type(instruction, cpu);
        execute_store(cpu, inst, is_halted);
        break;
    }
    case 0b0010011: { // I-TYPE Immediate
        ITypeInstruction inst = parse_i_type(instruction, cpu);
        execute_immediate(cpu, inst, is_halted);
        break;
    }
    case 0b0110011: { // R-TYPE
        RTypeInstruction inst = parse_r_type(instruction, cpu);
        execute_register(cpu, inst, is_halted);
        break;
    }
    default:
        std::cerr << std::dec << "Error: Unknown instruction " << std::hex << instruction
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_lui(CPU_State &cpu, const UTypeInstruction &inst) {
    if (inst.rd_index != 0)
        cpu.arf.regs[inst.rd_index] = (inst.imm << 12);
}

void InstructionProcessor::execute_auipc(CPU_State &cpu, const UTypeInstruction &inst) {
    if (inst.rd_index != 0)
        cpu.arf.regs[inst.rd_index] = (inst.imm << 12) + cpu.pc;
}

void InstructionProcessor::execute_jal(CPU_State &cpu, const JTypeInstruction &inst,
                                       uint32_t &next_pc) {
    if (inst.rd_index != 0) {
        cpu.arf.regs[inst.rd_index] = cpu.pc + 4;
    }
    next_pc = cpu.pc + inst.offset;
}

void InstructionProcessor::execute_jalr(CPU_State &cpu, const ITypeInstruction &inst,
                                        uint32_t &next_pc, bool &is_halted) {
    if (inst.funct3 == 0b000) {
        next_pc = (static_cast<uint32_t>(static_cast<int32_t>(inst.value1) + inst.imm)) & (~1U);
        if (inst.rd_index != 0) {
            cpu.arf.regs[inst.rd_index] = cpu.pc + 4;
        }
    } else {
        std::cerr << "Error: Unknown J-type instruction with funct3 " << std::hex << inst.funct3
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
    }
}

void InstructionProcessor::execute_branch(CPU_State &cpu, const BTypeInstruction &inst,
                                          uint32_t &next_pc, bool &is_halted) {
    switch (inst.funct3) {
    case 0b000: // beq
        if (inst.value1 == inst.value2) {
            next_pc = cpu.pc + inst.offset;
        }
        break;
    case 0b001: // bne
        if (inst.value1 != inst.value2) {
            next_pc = cpu.pc + inst.offset;
        }
        break;
    case 0b100: // blt
        if (static_cast<int32_t>(inst.value1) < static_cast<int32_t>(inst.value2)) {
            next_pc = cpu.pc + inst.offset;
        }
        break;
    case 0b101: // bge
        if (static_cast<int32_t>(inst.value1) >= static_cast<int32_t>(inst.value2)) {
            next_pc = cpu.pc + inst.offset;
        }
        break;
    case 0b110: // bltu
        if (inst.value1 < inst.value2) {
            next_pc = cpu.pc + inst.offset;
        }
        break;
    case 0b111: // bgeu
        if (inst.value1 >= inst.value2) {
            next_pc = cpu.pc + inst.offset;
        }
        break;
    default:
        std::cerr << "Error: Unknown branch instruction with funct3 " << std::hex << inst.funct3
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_load(CPU_State &cpu, const ITypeInstruction &inst,
                                        bool &is_halted) {
    uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(inst.value1) + inst.imm);

    switch (inst.funct3) {
    case 0b000: { // lb
        if (address >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] =
                static_cast<int32_t>(static_cast<int8_t>(cpu.memory[address]));
        break;
    }
    case 0b001: { // lh
        if (address >= MEMORY_SIZE - 1) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address + 1 << std::endl;
            is_halted = true;
            return;
        }
        if (address % 2 != 0) {
            std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = static_cast<int32_t>(
                static_cast<int16_t>(static_cast<uint16_t>(cpu.memory[address]) |
                                     (static_cast<uint16_t>(cpu.memory[address + 1]) << 8)));
        break;
    }
    case 0b010: { // lw
        if (address >= MEMORY_SIZE - 3) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address + 3 << std::endl;
            is_halted = true;
            return;
        }
        if (address % 4 != 0) {
            std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = static_cast<uint32_t>(cpu.memory[address]) |
                                          (static_cast<uint32_t>(cpu.memory[address + 1]) << 8) |
                                          (static_cast<uint32_t>(cpu.memory[address + 2]) << 16) |
                                          (static_cast<uint32_t>(cpu.memory[address + 3]) << 24);
        break;
    }
    case 0b100: { // lbu
        if (address >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = cpu.memory[address];
        break;
    }
    case 0b101: { // lhu
        if (address >= MEMORY_SIZE - 1) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address + 1 << std::endl;
            is_halted = true;
            return;
        }
        if (address % 2 != 0) {
            std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = static_cast<uint32_t>(cpu.memory[address]) |
                                          (static_cast<uint32_t>(cpu.memory[address + 1]) << 8);
        break;
    }
    default:
        std::cerr << "Error: Unknown load instruction with funct3 " << std::hex << inst.funct3
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_store(CPU_State &cpu, const STypeInstruction &inst,
                                         bool &is_halted) {
    uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(inst.value1) + inst.offset);

    switch (inst.funct3) {
    case 0b000: { // sb
        if (address >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        cpu.memory[address] = static_cast<uint8_t>(inst.value2);
        break;
    }
    case 0b001: { // sh
        if (address + 1 >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address + 1 << std::endl;
            is_halted = true;
            return;
        }
        if (address % 2 != 0) {
            std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        cpu.memory[address] = static_cast<uint8_t>(inst.value2);
        cpu.memory[address + 1] = static_cast<uint8_t>(inst.value2 >> 8);
        break;
    }
    case 0b010: { // sw
        if (address >= MEMORY_SIZE - 3) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address + 3 << std::endl;
            is_halted = true;
            return;
        }
        if (address % 4 != 0) {
            std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        cpu.memory[address] = static_cast<uint8_t>(inst.value2);
        cpu.memory[address + 1] = static_cast<uint8_t>(inst.value2 >> 8);
        cpu.memory[address + 2] = static_cast<uint8_t>(inst.value2 >> 16);
        cpu.memory[address + 3] = static_cast<uint8_t>(inst.value2 >> 24);
        break;
    }
    default:
        std::cerr << "Error: Unknown store instruction with funct3 " << std::hex << inst.funct3
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_immediate(CPU_State &cpu, const ITypeInstruction &inst,
                                             bool &is_halted) {
    switch (inst.funct3) {
    case 0b000: // addi
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] =
                static_cast<uint32_t>(static_cast<int32_t>(inst.value1) + inst.imm);
        break;
    case 0b010: // slti
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = (static_cast<int32_t>(inst.value1) < inst.imm) ? 1 : 0;
        break;
    case 0b011: // sltiu
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = (inst.value1 < static_cast<uint32_t>(inst.imm)) ? 1 : 0;
        break;
    case 0b100: // xori
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 ^ inst.imm;
        break;
    case 0b110: // ori
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 | inst.imm;
        break;
    case 0b111: // andi
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 & inst.imm;
        break;
    case 0b001: // slli
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 << (inst.imm & 0x1F);
        break;
    case 0b101:
        if (((static_cast<uint32_t>(inst.imm) >> 10) & 1) == 0) { // srli
            if (inst.rd_index != 0)
                cpu.arf.regs[inst.rd_index] = inst.value1 >> (inst.imm & 0x1F);
        } else { // srai
            if (inst.rd_index != 0)
                cpu.arf.regs[inst.rd_index] =
                    static_cast<uint32_t>(static_cast<int32_t>(inst.value1) >> (inst.imm & 0x1F));
        }
        break;
    default:
        std::cerr << "Error: Unknown I-type instruction with funct3 " << std::hex << inst.funct3
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_register(CPU_State &cpu, const RTypeInstruction &inst,
                                            bool &is_halted) {
    if (inst.funct3 == 0 && inst.funct7 == 0) { // add
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 + inst.value2;
    } else if (inst.funct3 == 0 && inst.funct7 == 0b0100000) { // sub
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 - inst.value2;
    } else if (inst.funct3 == 0b001 && inst.funct7 == 0b0000000) { // sll
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 << (inst.value2 & 0x1F);
    } else if (inst.funct3 == 0b010 && inst.funct7 == 0b0000000) { // slt
        if (static_cast<int32_t>(inst.value1) < static_cast<int32_t>(inst.value2)) {
            if (inst.rd_index != 0)
                cpu.arf.regs[inst.rd_index] = 1;
        } else {
            if (inst.rd_index != 0)
                cpu.arf.regs[inst.rd_index] = 0;
        }
    } else if (inst.funct3 == 0b011 && inst.funct7 == 0b0000000) { // sltu
        if (inst.value1 < inst.value2) {
            if (inst.rd_index != 0)
                cpu.arf.regs[inst.rd_index] = 1;
        } else {
            if (inst.rd_index != 0)
                cpu.arf.regs[inst.rd_index] = 0;
        }
    } else if (inst.funct3 == 0b100 && inst.funct7 == 0b0000000) { // xor
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 ^ inst.value2;
    } else if (inst.funct3 == 0b101 && inst.funct7 == 0b0100000) { // sra
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] =
                static_cast<uint32_t>(static_cast<int32_t>(inst.value1) >> (inst.value2 & 0x1F));
    } else if (inst.funct3 == 0b101 && inst.funct7 == 0b0000000) { // srl
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 >> (inst.value2 & 0x1F);
    } else if (inst.funct3 == 0b110 && inst.funct7 == 0b0000000) { // or
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 | inst.value2;
    } else if (inst.funct3 == 0b111 && inst.funct7 == 0b0000000) { // and
        if (inst.rd_index != 0)
            cpu.arf.regs[inst.rd_index] = inst.value1 & inst.value2;
    } else {
        std::cerr << "Error: Unknown R-type instruction with funct3 " << std::hex << inst.funct3
                  << " and funct7 " << inst.funct7 << " at pc " << cpu.pc << std::endl;
        is_halted = true;
    }
}
