#include "include/riscv_simulator.h"

#include <iostream>

int main() {
    std::cout << "Starting simulator..." << std::endl;

    RISCV_Simulator simulator;
    std::cout << "Simulator created." << std::endl;

    simulator.load_program();
    std::cout << "Program loaded." << std::endl;

    // 只运行几个周期来测试
    for (int i = 0; i < 10; i++) {
        simulator.tick();
        std::cout << "Cycle " << i << " completed." << std::endl;
    }

    return 0;
}
