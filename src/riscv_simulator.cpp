#include "../include/riscv_simulator.h"

#include "../include/instruction.h"
#include "../include/process.h"

#include <iostream>
#include <string>

extern int cnt;

RISCV_Simulator::RISCV_Simulator() : is_halted(false) { cpu_core = new CPUCore(); }

RISCV_Simulator::~RISCV_Simulator() { delete cpu_core; }

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
    int max_cycles = 5000;
    int cycle_count = 0;

    while (!is_halted && cycle_count < max_cycles) {
        tick();
        cycle_count++;
        // cout << "Cycle: " << cycle_count << std::endl;
    }

    if (cycle_count >= max_cycles) {
        std::cout << "Warning: Simulation terminated due to cycle limit" << std::endl;
    }

    print_result();
}

void RISCV_Simulator::tick() {
    cpu_core->tick(cpu);
    // 检查是否所有指令都完成了
    if (cpu.fetch_stalled) {
        is_halted = true;
        return;
    }

    // 如果PC超出范围，也停止模拟
    if (cpu.pc >= MEMORY_SIZE - 3) {
        is_halted = true;
        return;
    }
}

uint32_t RISCV_Simulator::fetch_instruction() {
    if (cpu.pc >= MEMORY_SIZE - 3) {
        std::cout << "Error: Program Counter out of bounds!" << std::endl;
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
    std::cout << std::dec << result << std::endl;

    /*
    std::cout << "Cycles: " << cpu_core->get_cycle_count() << std::endl;
    std::cout << "Instructions: " << cpu_core->get_instruction_count() << std::endl;
    std::cout << "Branch mispredictions: " << cpu_core->get_branch_mispredictions() << std::endl;
    std::cout << "Final PC: " << std::hex << cpu.pc << std::dec << std::endl;
    std::cout << "Register a0 (x10): " << cpu.arf.regs[10] << std::endl;
    */
}
