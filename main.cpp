#include "riscv_simulator.h"

#include <iostream>

int cnt = 0;

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    // 创建模拟器，可以选择是否启用乱序执行
    bool enable_ooo = false; // 先使用顺序执行测试
    RISCV_Simulator simulator(enable_ooo);

    simulator.load_program();

    simulator.run();

    return 0;
}
