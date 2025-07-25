#include "include/riscv_simulator.h"

#include <iostream>

int cnt = 0;

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    RISCV_Simulator simulator;

    simulator.load_program();

    simulator.run();

    return 0;
}
