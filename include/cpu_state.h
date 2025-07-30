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
const int BROAD_SIZE = 16;
const int FETCH_BUFFER_SIZE = 5;
const int MAX_ALU_UNITS = 1;
const int MAX_LOAD_UNITS = 1;

//指令类别
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

// 寄存器
struct Registers {
    struct Reg {
        uint32_t value;   // 架构寄存器值
        bool busy;        // 是否被预定
        uint32_t rob_idx; //  预定它的ROB索引

        Reg() : value(0), busy(false), rob_idx(ROB_SIZE) {}
    };

    Reg reg[32];

    Registers() {
        reg[0].value = 0;
        reg[0].busy = false;
    }

    uint32_t get_value(uint32_t reg_idx) const { return reg[reg_idx].value; }
    uint32_t get_rob_index(uint32_t reg_idx) const { return reg[reg_idx].rob_idx; }

    void set_value(uint32_t reg_idx, uint32_t value) {
        if (reg_idx != 0) {
            reg[reg_idx].value = value;
        }
    }

    bool is_busy(uint32_t reg_idx) const { return (reg_idx == 0) ? false : reg[reg_idx].busy; }

    bool check_buzy(uint32_t reg_idx, uint32_t rob_idx) const {
        if (reg[reg_idx].busy && reg[reg_idx].rob_idx == rob_idx)
            return 1;
        return 0;
    }

    void set_busy(uint32_t reg_idx, uint32_t rob_idx) {
        if (reg_idx != 0) {
            reg[reg_idx].busy = true;
            reg[reg_idx].rob_idx = rob_idx;
        }
    }

    void clear_busy(uint32_t reg_idx) {
        if (reg_idx != 0) {
            reg[reg_idx].busy = false;
            reg[reg_idx].rob_idx = ROB_SIZE;
        }
    }
    void flush() {
        for (int i = 0; i < 32; i++)
            reg[i].busy = 0, reg[i].rob_idx = ROB_SIZE;
    }
    void print_status() const {
        for (int i = 0; i < 32; i++) {
            cout << reg[i].value << " ";
        }
        cout << "\n";
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
    uint32_t rob_idx;      // 在ROB中的位置

    // 地址计算操作数
    uint32_t base_value;   // 基址寄存器的值
    uint32_t base_rob_idx; // 基址寄存器依赖的ROB索引
    uint32_t offset;       // 偏移量

    uint32_t value_rob_idx; // 存储值依赖的ROB索引

    uint32_t execution_cycles_left; // 执行周期计数器

    bool execute_completed; // 标记execute阶段是否已完成

    LSBEntry()
        : busy(false), address_ready(false), base_rob_idx(0), value_rob_idx(0),
          execution_cycles_left(0), execute_completed(false) {}
};

// 公共数据总线广播结果
struct BroadcastResult {
    bool valid;       // 是否有有效广播
    uint32_t rob_idx; // 产生结果的ROB索引
    uint32_t value;   // 广播的值

    BroadcastResult() : valid(false), rob_idx(0), value(0) {}
};

struct CDB {
    BroadcastResult result[BROAD_SIZE];
};

// CPU核心
struct CPU_Core {

    CPU_Core();

    uint32_t pc;    // 内存访问地址
    Registers Regs; // 寄存器

    FetchBufferEntry fetch_buffer[FETCH_BUFFER_SIZE]; // 指令缓存队列 // 寄存器别名表
    ROBEntry rob[ROB_SIZE];                           // 重排序缓冲区
    RSEntry rs_alu[RS_SIZE];                          // ALU预约站
    RSEntry rs_branch[RS_SIZE / 2];                   // 分支预约站
    LSBEntry LSB[LSB_SIZE];                           // Load/Store队列

    // 指令缓存队列
    uint32_t fetch_buffer_head;
    uint32_t fetch_buffer_tail;
    uint32_t fetch_buffer_size;

    // ROB队列
    uint32_t rob_head;
    uint32_t rob_tail;
    uint32_t rob_size;

    // 分支预测器
    bool branch_predictor;

    // 流水线状态
    bool fetch_stalled;    // 取指是否停滞
    bool pipeline_flushed; // 流水线是否被冲刷

    bool clear_flag;  //标记上回合是否被清空
    bool commit_flag; // 周期中有commit
    uint32_t next_pc;

};

struct CPU_State {
    CPU_Core core;
    uint8_t memory[MEMORY_SIZE];

    CPU_State();

    uint32_t &pc() { return core.pc; }
    const uint32_t &pc() const { return core.pc; }

    uint32_t &rob_head() { return core.rob_head; }
    const uint32_t &rob_head() const { return core.rob_head; }

    uint32_t &rob_tail() { return core.rob_tail; }
    const uint32_t &rob_tail() const { return core.rob_tail; }

    uint32_t &rob_size() { return core.rob_size; }
    const uint32_t &rob_size() const { return core.rob_size; }

    ROBEntry *rob() { return core.rob; }
    const ROBEntry *rob() const { return core.rob; }

    uint32_t &fetch_buffer_head() { return core.fetch_buffer_head; }
    const uint32_t &fetch_buffer_head() const { return core.fetch_buffer_head; }

    uint32_t &fetch_buffer_tail() { return core.fetch_buffer_tail; }
    const uint32_t &fetch_buffer_tail() const { return core.fetch_buffer_tail; }

    uint32_t &fetch_buffer_size() { return core.fetch_buffer_size; }
    const uint32_t &fetch_buffer_size() const { return core.fetch_buffer_size; }

    FetchBufferEntry *fetch_buffer() { return core.fetch_buffer; }
    const FetchBufferEntry *fetch_buffer() const { return core.fetch_buffer; }

    bool &clear_flag() { return core.clear_flag; }
    const bool &clear_flag() const { return core.clear_flag; }

    bool &fetch_stalled() { return core.fetch_stalled; }
    const bool &fetch_stalled() const { return core.fetch_stalled; }

    uint32_t &next_pc() { return core.next_pc; }
    const uint32_t &next_pc() const { return core.next_pc; }

    bool &commit_flag() { return core.commit_flag; }
    const bool &commit_flag() const { return core.commit_flag; }

    bool &pipeline_flushed() { return core.pipeline_flushed; }
    const bool &pipeline_flushed() const { return core.pipeline_flushed; }

    RSEntry *rs_alu() { return core.rs_alu; }
    const RSEntry *rs_alu() const { return core.rs_alu; }

    LSBEntry *LSB() { return core.LSB; }
    const LSBEntry *LSB() const { return core.LSB; }

    Registers &Regs() { return core.Regs; }
    const Registers &Regs() const { return core.Regs; }

    ROBEntry &rob(size_t index) { return core.rob[index]; }
    const ROBEntry &rob(size_t index) const { return core.rob[index]; }

    RSEntry &rs_alu(size_t index) { return core.rs_alu[index]; }
    const RSEntry &rs_alu(size_t index) const { return core.rs_alu[index]; }

    LSBEntry &LSB(size_t index) { return core.LSB[index]; }
    const LSBEntry &LSB(size_t index) const { return core.LSB[index]; }

    FetchBufferEntry &fetch_buffer(size_t index) { return core.fetch_buffer[index]; }
    const FetchBufferEntry &fetch_buffer(size_t index) const { return core.fetch_buffer[index]; }
};

#endif // CPU_STATE_H
