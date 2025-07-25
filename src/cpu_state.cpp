#include "../include/cpu_state.h"

CPU_State::CPU_State()
    : pc(0), rob_head(0), rob_tail(0), rob_size(0), branch_predictor(false), fetch_stalled(false),
      pipeline_flushed(false) {
    for (int i = 0; i < MEMORY_SIZE; ++i) {
        memory[i] = 0;
    }


    // 初始化寄存器别名表
    for (int i = 0; i < 32; ++i) {
        rat[i] = RATEntry();
    }

    // 初始化ROB
    for (int i = 0; i < ROB_SIZE; ++i) {
        rob[i] = ROBEntry();
    }

    // 初始化预约站
    for (int i = 0; i < RS_SIZE; ++i) {
        rs_alu[i] = RSEntry();
    }
    for (int i = 0; i < RS_SIZE / 2; ++i) {
        rs_branch[i] = RSEntry();
    }

    // 初始化Load/Store队列
    for (int i = 0; i < LSQ_SIZE; ++i) {
        lsq[i] = LSQEntry();
    }
}
