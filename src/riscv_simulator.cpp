#include "../include/riscv_simulator.h"

#include "../include/instruction.h"

#include <iostream>
#include <string>

extern int cnt;

RISCV_Simulator::RISCV_Simulator(bool flag) : is_halted(false), chaos_order(flag) {
    if (chaos_order) {
        processor = new Processor();
    } else {
        processor = nullptr;
    }
}

RISCV_Simulator::~RISCV_Simulator() {
    if (processor) {
        delete processor;
    }
}

void RISCV_Simulator::load_program() {
    std::string line;
    unsigned int current_address = 0;

    char at_symbol;
    while (std::cin >> at_symbol) {
        if (at_symbol == '@') {
            std::cin >> std::hex >> current_address;
        } else {
            std::cin.putback(at_symbol);
            unsigned int byte_value;
            std::cin >> std::hex >> byte_value;
            if (current_address < MEMORY_SIZE) {
                cpu.memory[current_address++] = static_cast<uint8_t>(byte_value);
            }
        }
    }
}

void RISCV_Simulator::run() {
    while (!is_halted) {
        tick();
    }
    print_result();
}

void RISCV_Simulator::tick() {
    if (chaos_order && processor) {

        processor->tick(cpu);

        if (cpu.rob_size == 0 && cpu.fetch_stalled) {

            if (cpu.pc < MEMORY_SIZE - 3) {
                uint32_t next_instruction = cpu.memory[cpu.pc] | (cpu.memory[cpu.pc + 1] << 8) |
                                            (cpu.memory[cpu.pc + 2] << 16) |
                                            (cpu.memory[cpu.pc + 3] << 24);
                if (next_instruction == HALT_INSTRUCTION) {
                    is_halted = true;
                }
            } else {
                is_halted = true;
            }
        }
    }

    /*else {
        is_halted = true;
        return;
        uint32_t instruction = fetch_instruction();

        if (instruction == HALT_INSTRUCTION) {
            is_halted = true;
            return;
        }

        uint32_t next_pc = cpu.pc + 4;

        InstructionProcessor::decode_and_execute(cpu, instruction, next_pc, is_halted);

        cpu.pc = next_pc;
        cpu.arf.regs[0] = 0;
    }*/
}

uint32_t RISCV_Simulator::fetch_instruction() {
    if (cpu.pc >= MEMORY_SIZE - 3) {
        std::cerr << "Error: Program Counter out of bounds!" << std::endl;
        is_halted = true;
        return 0;
    }

    uint32_t inst = 0;
    inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 0]);
    inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 1]) << 8;
    inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 2]) << 16;
    inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 3]) << 24;
    return inst;
}

void RISCV_Simulator::print_result() {
    uint32_t result = cpu.arf.regs[10] & 0xFF;
    std::cout << result << std::endl;
}
