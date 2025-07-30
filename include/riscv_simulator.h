#ifndef RISCV_SIMULATOR_H
#define RISCV_SIMULATOR_H

#include "cpu_state.h"
#include "process.h"

class CPUCore;

class RISCV_Simulator {
  private:
    CPU_State cpu;  // cpu具体信息
    bool is_halted; //是否停机
    CPU *cpu_core;  // cpu的核心步骤

  public:
    RISCV_Simulator();
    ~RISCV_Simulator();

    void load_program(); // 读取指令
    void run();          // 运行主程序

  private:
    void tick();                  //模拟cpu每一秒操作
    uint32_t fetch_instruction(); //读取指令
    void print_result();          //输出结果
};

#endif
