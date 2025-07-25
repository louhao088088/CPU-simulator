#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "cpu_state.h"

#include <cstdint>

// 原有的指令格式结构体（保持兼容性）
struct UTypeInstruction {
    uint32_t rd_index; // 目标寄存器索引
    uint32_t imm;      // 立即数
};

struct JTypeInstruction {
    uint32_t rd_index; // 目标寄存器索引
    int32_t offset;    // 跳转偏移量
};

struct ITypeInstruction {
    uint32_t rd_index;  // 目标寄存器索引
    uint32_t funct3;    // 功能码
    uint32_t value1;    // rs1寄存器的值
    int32_t imm;        // 立即数
    uint32_t rs1_index; // 保留索引用于特殊情况
};

struct BTypeInstruction {
    uint32_t funct3; // 功能码
    uint32_t value1; // rs1寄存器的值
    uint32_t value2; // rs2寄存器的值
    int32_t offset;  // 分支偏移量
};

struct STypeInstruction {
    uint32_t funct3; // 功能码
    uint32_t value1; // rs1寄存器的值（基地址）
    uint32_t value2; // rs2寄存器的值（要存储的数据）
    int32_t offset;  // 偏移量
};

struct RTypeInstruction {
    uint32_t rd_index; // 目标寄存器索引
    uint32_t funct3;   // 功能码
    uint32_t value1;   // rs1寄存器的值
    uint32_t value2;   // rs2寄存器的值
    uint32_t funct7;   // 扩展功能码
};

// 解码后的指令信息（新的乱序执行用）
struct DecodedInstruction {
    InstrType type;     // 指令类型
    uint32_t rd;        // 目标寄存器
    uint32_t rs1, rs2;  // 源寄存器
    int32_t imm;        // 立即数
    uint32_t pc;        // 指令地址
    uint32_t raw_instr; // 原始指令编码

    DecodedInstruction()
        : type(InstrType::HALT), rd(0), rs1(0), rs2(0), imm(0), pc(0), raw_instr(0) {}
};

// 乱序执行处理器
class Processor {
  public:
    Processor();

    // 主要执行函数
    void tick(CPU_State &cpu);

    // 获取执行周期数
    int get_execution_cycles(InstrType type);

  private:
    // 流水线阶段（按倒序执行）
    void commit(CPU_State &cpu);
    void writeback(CPU_State &cpu);
    void execute(CPU_State &cpu);
    void dispatch(CPU_State &cpu);
    void decode_and_rename(CPU_State &cpu);
    void fetch(CPU_State &cpu);

    // 辅助函数
    DecodedInstruction decode_instruction(uint32_t instruction, uint32_t pc);
    InstrType get_instruction_type(uint32_t instruction);
    bool allocate_rob_entry(CPU_State &cpu, uint32_t &rob_idx);
    bool allocate_rs_entry(CPU_State &cpu, InstrType type, uint32_t &rs_idx);
    bool allocate_lsq_entry(CPU_State &cpu, uint32_t &lsq_idx);

    // ROB管理
    bool rob_full(const CPU_State &cpu);
    bool rob_empty(const CPU_State &cpu);

    // 指令类型判断
    bool is_alu_instruction(InstrType type);
    bool is_branch_instruction(InstrType type);
    bool is_load_instruction(InstrType type);
    bool is_store_instruction(InstrType type);

    // CDB结果广播
    void broadcast_cdb_result(CPU_State &cpu, uint32_t rob_index, uint32_t value);
    void execute_branch_instruction(CPU_State &cpu, RSEntry &rs, ROBEntry &rob);
    uint32_t calculate_memory_address(const RSEntry &rs);

    // Load/Store处理
    void execute_load(CPU_State &cpu, LSQEntry &lsq, ROBEntry &rob);
    void execute_store(CPU_State &cpu, LSQEntry &lsq, ROBEntry &rob);
    bool check_store_to_load_forwarding(const CPU_State &cpu, uint32_t load_addr,
                                        uint32_t load_rob_idx, uint32_t &forwarded_value);

    // 内存依赖检查 - 新增关键函数
    bool check_memory_dependencies(const CPU_State &cpu, uint32_t load_address,
                                   uint32_t load_rob_idx, uint32_t &forwarded_value);
    bool can_load_proceed(const CPU_State &cpu, uint32_t load_address, uint32_t load_rob_idx);

    // 分支预测和处理
    bool predict_branch(CPU_State &cpu, uint32_t pc);
    void handle_branch_misprediction(CPU_State &cpu, uint32_t correct_pc);
    void flush_pipeline(CPU_State &cpu);

    // 提交处理
    void commit_instruction(CPU_State &cpu, ROBEntry &rob);
    void commit_store(CPU_State &cpu, const ROBEntry &rob, const LSQEntry &lsq);

    // 公共数据总线
    BroadcastResult cdb_result;

    // 临时存储待处理的指令
    DecodedInstruction fetched_instruction;
    bool has_fetched_instruction;

    // 性能计数器
    uint64_t cycle_count;
    uint64_t instruction_count;
    uint64_t branch_mispredictions;
};

// 传统指令处理器（保持兼容性）
class InstructionProcessor {
  public:
    static void decode_and_execute(CPU_State &cpu, uint32_t instruction, uint32_t &next_pc,
                                   bool &is_halted);

  private:
    static UTypeInstruction parse_u_type(uint32_t instruction, CPU_State &cpu);
    static JTypeInstruction parse_j_type(uint32_t instruction, CPU_State &cpu);
    static ITypeInstruction parse_i_type(uint32_t instruction, CPU_State &cpu);
    static BTypeInstruction parse_b_type(uint32_t instruction, CPU_State &cpu);
    static STypeInstruction parse_s_type(uint32_t instruction, CPU_State &cpu);
    static RTypeInstruction parse_r_type(uint32_t instruction, CPU_State &cpu);

    static void execute_lui(CPU_State &cpu, const UTypeInstruction &inst);
    static void execute_auipc(CPU_State &cpu, const UTypeInstruction &inst);
    static void execute_jal(CPU_State &cpu, const JTypeInstruction &inst, uint32_t &next_pc);
    static void execute_jalr(CPU_State &cpu, const ITypeInstruction &inst, uint32_t &next_pc,
                             bool &is_halted);
    static void execute_branch(CPU_State &cpu, const BTypeInstruction &inst, uint32_t &next_pc,
                               bool &is_halted);
    static void execute_load(CPU_State &cpu, const ITypeInstruction &inst, bool &is_halted);
    static void execute_immediate(CPU_State &cpu, const ITypeInstruction &inst, bool &is_halted);
    static void execute_store(CPU_State &cpu, const STypeInstruction &inst, bool &is_halted);
    static void execute_register(CPU_State &cpu, const RTypeInstruction &inst, bool &is_halted);
};

#endif // INSTRUCTION_H
