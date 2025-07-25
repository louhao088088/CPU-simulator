#ifndef CPU_CORE_H
#define CPU_CORE_H

#include "cpu_state.h"
#include "instruction.h"

// CPU核心处理器
class CPUCore {
  public:
    CPUCore();
    ~CPUCore() = default;

    // 主执行函数
    void tick(CPU_State &cpu);

    // 获取统计信息
    uint64_t get_cycle_count() const { return cycle_count_; }
    uint64_t get_instruction_count() const { return instruction_count_; }
    uint64_t get_branch_mispredictions() const { return branch_mispredictions_; }

  private:
    

    void commit_stage(CPU_State &cpu);
    void writeback_stage(CPU_State &cpu);
    void execute_stage(CPU_State &cpu);
    void dispatch_stage(CPU_State &cpu);
    void decode_rename_stage(CPU_State &cpu);
    void fetch_stage(CPU_State &cpu);

    // ROB管理
    bool rob_full(const CPU_State &cpu) const;
    bool rob_empty(const CPU_State &cpu) const;
    uint32_t allocate_rob_entry(CPU_State &cpu);
    void free_rob_entry(CPU_State &cpu);

    // 预约站管理
    bool rs_available(const CPU_State &cpu, InstrType type) const;
    uint32_t allocate_rs_entry(CPU_State &cpu, InstrType type);
    void free_rs_entry(CPU_State &cpu, uint32_t rs_idx, InstrType type);

    // LSQ管理
    bool lsq_available(const CPU_State &cpu) const;
    uint32_t allocate_lsq_entry(CPU_State &cpu);
    void free_lsq_entry(CPU_State &cpu, uint32_t lsq_idx);

    // 寄存器重命名
    void rename_registers(CPU_State &cpu, const Instruction &instr, uint32_t rob_idx);
    uint32_t read_operand(const CPU_State &cpu, uint32_t reg_idx, uint32_t &rob_dependency);

    // CDB广播
    void broadcast_result(CPU_State &cpu, uint32_t rob_idx, uint32_t value);

    // 内存依赖检查
    bool check_load_dependencies(const CPU_State &cpu, uint32_t load_addr, uint32_t load_rob_idx,
                                 uint32_t &forwarded_value);

    // 分支预测和处理
    bool predict_branch_taken(const CPU_State &cpu);
    void handle_branch_misprediction(CPU_State &cpu, uint32_t correct_pc);
    void flush_pipeline(CPU_State &cpu);

    // 统计信息
    uint64_t cycle_count_;
    uint64_t instruction_count_;
    uint64_t branch_mispredictions_; // 分支预测错误计数
};
 
#endif // CPU_CORE_H
