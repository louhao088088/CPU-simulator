#include "include/instruction.h"
#include "include/riscv_simulator.h"

#include <iostream>

int main() {
    std::cout << "=== Testing OutOfOrderProcessor ===" << std::endl;

    // 创建乱序处理器实例
    OutOfOrderProcessor ooo_processor;

    // 创建CPU状态
    CPU_State cpu;

    // 简单测试：设置一些基本状态
    cpu.pc = 0;
    cpu.rob_size = 0;
    cpu.fetch_stalled = false;
    cpu.pipeline_flushed = false;

    // 模拟一条简单的ADD指令：add x1, x2, x3
    // 0x003100B3 = 00000000001100010000000010110011
    uint32_t add_instr = 0x003100B3;
    cpu.memory[0] = add_instr & 0xFF;
    cpu.memory[1] = (add_instr >> 8) & 0xFF;
    cpu.memory[2] = (add_instr >> 16) & 0xFF;
    cpu.memory[3] = (add_instr >> 24) & 0xFF;

    // 设置源寄存器的值
    cpu.arf.regs[2] = 10; // x2 = 10
    cpu.arf.regs[3] = 20; // x3 = 20

    std::cout << "Initial state:" << std::endl;
    std::cout << "  x1 = " << cpu.arf.regs[1] << std::endl;
    std::cout << "  x2 = " << cpu.arf.regs[2] << std::endl;
    std::cout << "  x3 = " << cpu.arf.regs[3] << std::endl;
    std::cout << "  PC = " << cpu.pc << std::endl;
    std::cout << "  ROB size = " << cpu.rob_size << std::endl;

    // 执行几个时钟周期
    std::cout << "\nExecuting OOO processor cycles..." << std::endl;
    for (int i = 0; i < 10; i++) {
        std::cout << "Cycle " << i << ": ";
        ooo_processor.tick(cpu);
        std::cout << "PC=" << cpu.pc << ", ROB_size=" << cpu.rob_size << ", x1=" << cpu.arf.regs[1]
                  << std::endl;

        // 如果指令已提交，跳出循环
        if (cpu.arf.regs[1] == 30) { // x1应该等于x2+x3=30
            std::cout << "Instruction committed successfully!" << std::endl;
            break;
        }
    }

    std::cout << "\nFinal state:" << std::endl;
    std::cout << "  x1 = " << cpu.arf.regs[1] << " (expected: 30)" << std::endl;
    std::cout << "  x2 = " << cpu.arf.regs[2] << std::endl;
    std::cout << "  x3 = " << cpu.arf.regs[3] << std::endl;
    std::cout << "  PC = " << cpu.pc << std::endl;

    if (cpu.arf.regs[1] == 30) {
        std::cout << "\n✅ OutOfOrderProcessor test PASSED!" << std::endl;
    } else {
        std::cout << "\n❌ OutOfOrderProcessor test FAILED!" << std::endl;
    }

    return 0;
}
