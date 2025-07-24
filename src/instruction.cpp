#include "instruction.h"

#include <iostream>

using std::cerr;

void InstructionProcessor::decode_and_execute(CPU_State &cpu, uint32_t instruction,
                                              uint32_t &next_pc, bool &is_halted) {
    uint32_t opcode = instruction & 0x7F;

    switch (opcode) {
    case 0b0110111: // U-TYPE lui
        execute_lui(cpu, instruction);
        break;

    case 0b0010111: // U-TYPE auipc
        execute_auipc(cpu, instruction);
        break;

    case 0b1101111: // J-TYPE jal
        execute_jal(cpu, instruction, next_pc);
        break;

    case 0b1100111: // I-TYPE JALR
        execute_jalr(cpu, instruction, next_pc, is_halted);
        break;

    case 0b1100011: // B-TYPE
        execute_branch(cpu, instruction, next_pc, is_halted);
        break;

    case 0b0000011: // I-TYPE LOAD
        execute_load(cpu, instruction, is_halted);
        break;

    case 0b0100011: // S-TYPE STORE
        execute_store(cpu, instruction, is_halted);
        break;

    case 0b0010011: // I-TYPE Immediate
        execute_immediate(cpu, instruction, is_halted);
        break;

    case 0b0110011: // R-TYPE
        execute_register(cpu, instruction, is_halted);
        break;

    default:
        std::cerr << std::dec << "Error: Unknown instruction " << std::hex << instruction
                  << " at pc " << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_lui(CPU_State &cpu, uint32_t instruction) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm = (instruction >> 12) & 0xFFFFF;
    if (rd != 0)
        cpu.regs[rd] = (imm << 12);
}

void InstructionProcessor::execute_auipc(CPU_State &cpu, uint32_t instruction) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm = (instruction >> 12) & 0xFFFFF;
    if (rd != 0)
        cpu.regs[rd] = (imm << 12) + cpu.pc;
}

void InstructionProcessor::execute_jal(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t imm_20 = (instruction >> 31) & 1;
    uint32_t imm_10_1 = (instruction >> 21) & 0x3FF;
    uint32_t imm_11 = (instruction >> 20) & 1;
    uint32_t imm_19_12 = (instruction >> 12) & 0xFF;

    uint32_t imm = ((imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1));
    int32_t offset;
    if (imm_20 == 1) {
        offset = static_cast<int32_t>(imm | 0xFFF00000);
    } else {
        offset = static_cast<int32_t>(imm);
    }
    if (rd != 0) {
        cpu.regs[rd] = cpu.pc + 4;
    }
    next_pc = cpu.pc + offset;
}

void InstructionProcessor::execute_jalr(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc,
                                        bool &is_halted) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1 = (instruction >> 15) & 0x1F;
    int32_t imm = static_cast<int32_t>(instruction) >> 20;

    if (funct3 == 0b000) {
        next_pc = (static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + imm)) & (~1U);
        if (rd != 0) {
            cpu.regs[rd] = cpu.pc + 4;
        }
    } else {
        std::cerr << "Error: Unknown J-type instruction " << std::hex << instruction << " at pc "
                  << cpu.pc << std::endl;
        is_halted = true;
    }
}

void InstructionProcessor::execute_branch(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc,
                                          bool &is_halted) {
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1 = (instruction >> 15) & 0x1F;
    uint32_t rs2 = (instruction >> 20) & 0x1F;

    uint32_t imm_12 = (instruction >> 31) & 0x1;
    uint32_t imm_10_5 = (instruction >> 25) & 0x3F;
    uint32_t imm_4_1 = (instruction >> 8) & 0xF;
    uint32_t imm_11 = (instruction >> 7) & 0x1;

    uint32_t imm = (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);
    int32_t offset = static_cast<int32_t>(imm << 19) >> 19;

    switch (funct3) {
    case 0b000: // beq
        if (cpu.regs[rs1] == cpu.regs[rs2]) {
            next_pc = cpu.pc + offset;
        }
        break;
    case 0b001: // bne
        if (cpu.regs[rs1] != cpu.regs[rs2]) {
            next_pc = cpu.pc + offset;
        }
        break;
    case 0b100: // blt
        if (static_cast<int32_t>(cpu.regs[rs1]) < static_cast<int32_t>(cpu.regs[rs2])) {
            next_pc = cpu.pc + offset;
        }
        break;
    case 0b101: // bge
        if (static_cast<int32_t>(cpu.regs[rs1]) >= static_cast<int32_t>(cpu.regs[rs2])) {
            next_pc = cpu.pc + offset;
        }
        break;
    case 0b110: // bltu
        if (cpu.regs[rs1] < cpu.regs[rs2]) {
            next_pc = cpu.pc + offset;
        }
        break;
    case 0b111: // bgeu
        if (cpu.regs[rs1] >= cpu.regs[rs2]) {
            next_pc = cpu.pc + offset;
        }
        break;
    default:
        std::cerr << "Error: Unknown branch instruction " << std::hex << instruction << " at pc "
                  << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_load(CPU_State &cpu, uint32_t instruction, bool &is_halted) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1 = (instruction >> 15) & 0x1F;
    int32_t offset = static_cast<int32_t>(instruction) >> 20;

    switch (funct3) {
    case 0b000: { // lb
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
        if (address >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (rd != 0)
            cpu.regs[rd] = static_cast<int32_t>(static_cast<int8_t>(cpu.memory[address]));
        break;
    }
    case 0b001: { // lh
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
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
        if (rd != 0)
            cpu.regs[rd] = static_cast<int32_t>(
                static_cast<int16_t>(static_cast<uint16_t>(cpu.memory[address]) |
                                     (static_cast<uint16_t>(cpu.memory[address + 1]) << 8)));
        break;
    }
    case 0b010: { // lw
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
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
        if (rd != 0)
            cpu.regs[rd] = static_cast<uint32_t>(cpu.memory[address]) |
                           (static_cast<uint32_t>(cpu.memory[address + 1]) << 8) |
                           (static_cast<uint32_t>(cpu.memory[address + 2]) << 16) |
                           (static_cast<uint32_t>(cpu.memory[address + 3]) << 24);
        break;
    }
    case 0b100: { // lbu
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
        if (address >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        if (rd != 0)
            cpu.regs[rd] = cpu.memory[address];
        break;
    }
    case 0b101: { // lhu
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
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
        if (rd != 0)
            cpu.regs[rd] = static_cast<uint32_t>(cpu.memory[address]) |
                           (static_cast<uint32_t>(cpu.memory[address + 1]) << 8);
        break;
    }
    default:
        std::cerr << "Error: Unknown load instruction " << std::hex << instruction << " at pc "
                  << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_store(CPU_State &cpu, uint32_t instruction, bool &is_halted) {
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1 = (instruction >> 15) & 0x1F;
    uint32_t rs2 = (instruction >> 20) & 0x1F;
    int32_t offset =
        static_cast<int32_t>((((instruction >> 25) & 0x7F) << 5) | ((instruction >> 7) & 0x1F));
    if (offset & 0x800) {
        offset |= 0xFFFFF000;
    }

    switch (funct3) {
    case 0b000: { // sb
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
        if (address >= MEMORY_SIZE) {
            std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                      << ", trying to access " << address << std::endl;
            is_halted = true;
            return;
        }
        cpu.memory[address] = static_cast<uint8_t>(cpu.regs[rs2]);
        break;
    }
    case 0b001: { // sh
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
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
        cpu.memory[address] = static_cast<uint8_t>(cpu.regs[rs2]);
        cpu.memory[address + 1] = static_cast<uint8_t>(cpu.regs[rs2] >> 8);
        break;
    }
    case 0b010: { // sw
        uint32_t address = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
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
        cpu.memory[address] = static_cast<uint8_t>(cpu.regs[rs2]);
        cpu.memory[address + 1] = static_cast<uint8_t>(cpu.regs[rs2] >> 8);
        cpu.memory[address + 2] = static_cast<uint8_t>(cpu.regs[rs2] >> 16);
        cpu.memory[address + 3] = static_cast<uint8_t>(cpu.regs[rs2] >> 24);
        break;
    }
    default:
        std::cerr << "Error: Unknown store instruction " << std::hex << instruction << " at pc "
                  << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_immediate(CPU_State &cpu, uint32_t instruction,
                                             bool &is_halted) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1 = (instruction >> 15) & 0x1F;
    int32_t imm = static_cast<int32_t>(instruction) >> 20;

    switch (funct3) {
    case 0b000: // addi
        if (rd != 0)
            cpu.regs[rd] = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + imm);
        break;
    case 0b010: // slti
        if (rd != 0)
            cpu.regs[rd] = (static_cast<int32_t>(cpu.regs[rs1]) < (imm)) ? 1 : 0;
        break;
    case 0b011: // sltiu
        if (rd != 0)
            cpu.regs[rd] = (cpu.regs[rs1] < static_cast<uint32_t>(imm)) ? 1 : 0;
        break;
    case 0b100: // xori
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] ^ imm;
        break;
    case 0b110: // ori
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] | imm;
        break;
    case 0b111: // andi
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] & imm;
        break;
    case 0b001: // slli
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] << (imm & 0x1F);
        break;
    case 0b101:
        if (((instruction >> 30) & 1) == 0) { // srli
            if (rd != 0)
                cpu.regs[rd] = cpu.regs[rs1] >> (imm & 0x1F);
        } else { // srai
            if (rd != 0)
                cpu.regs[rd] =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) >> (imm & 0x1F));
        }
        break;
    default:
        std::cerr << "Error: Unknown I-type instruction " << std::hex << instruction << " at pc "
                  << cpu.pc << std::endl;
        is_halted = true;
        break;
    }
}

void InstructionProcessor::execute_register(CPU_State &cpu, uint32_t instruction, bool &is_halted) {
    uint32_t rd = (instruction >> 7) & 0x1F;
    uint32_t funct3 = (instruction >> 12) & 0x7;
    uint32_t rs1 = (instruction >> 15) & 0x1F;
    uint32_t rs2 = (instruction >> 20) & 0x1F;
    uint32_t funct7 = (instruction >> 25) & 0x7F;

    if (funct3 == 0 && funct7 == 0) { // add
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] + cpu.regs[rs2];
    } else if (funct3 == 0 && funct7 == 0b0100000) { // sub
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] - cpu.regs[rs2];
    } else if (funct3 == 0b001 && funct7 == 0b0000000) { // sll
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] << (cpu.regs[rs2] & 0x1F);
    } else if (funct3 == 0b010 && funct7 == 0b0000000) { // slt
        if (static_cast<int32_t>(cpu.regs[rs1]) < static_cast<int32_t>(cpu.regs[rs2])) {
            if (rd != 0)
                cpu.regs[rd] = 1;
        } else {
            if (rd != 0)
                cpu.regs[rd] = 0;
        }
    } else if (funct3 == 0b011 && funct7 == 0b0000000) { // sltu
        if (cpu.regs[rs1] < cpu.regs[rs2]) {
            if (rd != 0)
                cpu.regs[rd] = 1;
        } else {
            if (rd != 0)
                cpu.regs[rd] = 0;
        }
    } else if (funct3 == 0b100 && funct7 == 0b0000000) { // xor
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] ^ cpu.regs[rs2];
    } else if (funct3 == 0b101 && funct7 == 0b0100000) { // sra
        if (rd != 0)
            cpu.regs[rd] = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) >>
                                                 (cpu.regs[rs2] & 0x1F));
    } else if (funct3 == 0b101 && funct7 == 0b0000000) { // srl
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] >> (cpu.regs[rs2] & 0x1F);
    } else if (funct3 == 0b110 && funct7 == 0b0000000) { // or
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] | cpu.regs[rs2];
    } else if (funct3 == 0b111 && funct7 == 0b0000000) { // and
        if (rd != 0)
            cpu.regs[rd] = cpu.regs[rs1] & cpu.regs[rs2];
    } else {
        std::cerr << "Error: Unknown R-type instruction " << std::hex << instruction << " at pc "
                  << cpu.pc << std::endl;
        is_halted = true;
    }
}
