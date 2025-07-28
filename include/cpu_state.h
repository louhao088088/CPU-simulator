#ifndef CPU_STATE_H
#define CPU_STATE_H

#include <cstdint>
#include <iostream>
#include <string>

using std::cerr;
using std::cout;
const int MEMORY_SIZE = 1024 * 1024;
const uint32_t HALT_INSTRUCTION = 0x0ff00513;
const int ROB_SIZE = 5;
const int RS_SIZE = 16;
const int LSB_SIZE = 16;
const int FETCH_BUFFER_SIZE = 5;
const uint32_t MAX_ALU_UNITS = 1;
const uint32_t MAX_LOAD_UNITS = 1;

enum class InstrType {
    ALU_ADD,
    ALU_SUB,
    ALU_AND,
    ALU_OR,
    ALU_XOR,
    ALU_SLL,
    ALU_SRL,
    ALU_SRA,
    ALU_SLT,
    ALU_SLTU,
    ALU_ADDI,
    ALU_ANDI,
    ALU_ORI,
    ALU_XORI,
    ALU_SLLI,
    ALU_SRLI,
    ALU_SRAI,
    ALU_SLTI,
    ALU_SLTIU,
    LOAD_LB,
    LOAD_LH,
    LOAD_LW,
    LOAD_LBU,
    LOAD_LHU,
    STORE_SB,
    STORE_SH,
    STORE_SW,
    BRANCH_BEQ,
    BRANCH_BNE,
    BRANCH_BLT,
    BRANCH_BGE,
    BRANCH_BLTU,
    BRANCH_BGEU,
    JUMP_JAL,
    JUMP_JALR,
    LUI,
    AUIPC,
    HALT
};

std::string Type_string(InstrType type);

// 取指缓存条目
struct FetchBufferEntry {
    bool valid;           // 条目是否有效
    uint32_t instruction; // 指令内容
    uint32_t pc;          // 指令地址

    FetchBufferEntry() : valid(false), instruction(0), pc(0) {}
};

// 指令状态枚举
enum class InstrState {
    Dispatch,  // 已分派到预约站
    Execute,   // 正在执行
    Writeback, // 写回完成
    Commit     // 可以提交
};

// 寄存器别名表
struct RATEntry {
    bool busy;        // 是否被某个在执行的指令预定
    uint32_t rob_idx; // 预定它的指令的ROB索引

    RATEntry() : busy(false), rob_idx(0) {}
};

// 架构寄存器
struct ArchRegisterFile {
    uint32_t regs[32];

    ArchRegisterFile() {
        for (int i = 0; i < 32; ++i) {
            regs[i] = 0;
        }
    }
};

// 重排序缓冲区
struct ROBEntry {
    bool busy;            // 是否被占用
    InstrType instr_type; // 指令类型
    InstrState state;     // 指令状态
    uint32_t dest_reg;    // 目标寄存器编号
    uint32_t value;       // 计算结果
    uint32_t mem_address; // 内存地址（Load/Store用）
    uint32_t pc;          // 指令地址

    // 分支指令专用
    bool is_branch;       // 是否是分支指令
    bool predicted_taken; // 预测是否跳转
    uint32_t target_pc;   // 跳转目标地址
    bool actual_taken;    // 实际是否跳转

    // 源操作数信息
    uint32_t rs1, rs2; // 源寄存器编号
    uint32_t imm;      // 立即数

    ROBEntry()
        : busy(false), value(0), is_branch(false), predicted_taken(false), actual_taken(false),
          rs1(0), rs2(0), imm(0) {}
};

// 预约站
struct RSEntry {
    bool busy;             // 是否被占用
    InstrType op;          // 操作类型
    uint32_t Vj, Vk;       // 操作数值
    uint32_t Qj, Qk;       // 操作数依赖的ROB索引
    uint32_t dest_rob_idx; // 对应的ROB条目索引
    uint32_t imm;          // 立即数

    // 执行计时器
    int execution_cycles_left;

    RSEntry() : busy(false), Qj(ROB_SIZE), Qk(ROB_SIZE), execution_cycles_left(0) {}

    // 检查操作数是否都就绪
    bool operands_ready() const { return Qj == ROB_SIZE && Qk == ROB_SIZE; }
};

// Load/Store队列
struct LSBEntry {
    bool busy;             // 是否被占用
    InstrType op;          // LOAD或STORE类型
    uint32_t address;      // 内存地址
    uint32_t value;        // 要存储的值
    uint32_t dest_rob_idx; // 对应的ROB条目索引
    bool address_ready;    // 地址是否已计算
    bool value_ready;      // 值是否就绪
    uint32_t rob_idx;      // 在ROB中的位置

    // 地址计算操作数
    uint32_t base_value;   // 基址寄存器的值
    uint32_t base_rob_idx; // 基址寄存器依赖的ROB索引
    uint32_t offset;       // 偏移量

    uint32_t value_rob_idx; // 存储值依赖的ROB索引

    uint32_t execution_cycles_left;

    bool execute_completed; // 标记execute阶段是否已完成

    LSBEntry()
        : busy(false), address_ready(false), value_ready(false), base_rob_idx(0), value_rob_idx(0),
          execution_cycles_left(0), execute_completed(false) {}
};

// 公共数据总线广播结果
struct BroadcastResult {
    bool valid;       // 是否有有效广播
    uint32_t rob_idx; // 产生结果的ROB索引
    uint32_t value;   // 广播的值

    BroadcastResult() : valid(false), rob_idx(0), value(0) {}
};

// CPU状态
struct CPU_State {
    uint32_t pc;                 // 程序计数器
    ArchRegisterFile arf;        // 架构寄存器文件
    uint8_t memory[MEMORY_SIZE]; // 内存

    // 取指缓存队列

    FetchBufferEntry fetch_buffer[FETCH_BUFFER_SIZE]; // 指令缓存队列
    int fetch_buffer_head;
    int fetch_buffer_tail;
    int fetch_buffer_size;

    // 乱序执行
    RATEntry rat[32];               // 寄存器别名表
    ROBEntry rob[ROB_SIZE];         // 重排序缓冲区
    RSEntry rs_alu[RS_SIZE];        // ALU预约站
    RSEntry rs_branch[RS_SIZE / 2]; // 分支预约站
    LSBEntry LSB[LSB_SIZE];         // Load/Store队列

    // ROB
    uint32_t rob_head;
    uint32_t rob_tail;
    uint32_t rob_size;

    // 分支预测器
    bool branch_predictor;

    // 流水线状态
    bool fetch_stalled;    // 取指是否停滞
    bool pipeline_flushed; // 流水线是否被冲刷

    CPU_State();
};

#endif // CPU_STATE_H
