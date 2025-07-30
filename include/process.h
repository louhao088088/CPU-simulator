#ifndef CPU_CORE_H
#define CPU_CORE_H

#include "cpu_state.h"
#include "instruction.h"

#include <cstdint>

// CPU核心处理器
class CPU {
  public:
    CPU();
    ~CPU() = default;

    // 主执行函数
    void tick(CPU_State &cpu);

    // 获取统计信息
    uint64_t get_cycle_count() const { return cycle_count_; }
    uint64_t get_instruction_count() const { return instruction_count_; }
    uint64_t get_branch_mispredictions() const { return branch_mispredictions_; }

  private:
    void commit_stage(const CPU_Core &now_state, CPU_Core &next_state, uint8_t memory[]);
    void writeback_stage(const CPU_Core &now_state, CPU_Core &next_state);
    void execute_stage(const CPU_Core &now_state, CPU_Core &next_state,
                       const uint8_t memory[]);
    void dispatch_stage(const CPU_Core &now_state, CPU_Core &next_state);
    void decode_rename_stage(const CPU_Core &now_state, CPU_Core &next_state,
                             const uint8_t memory[]);
    void fetch_stage(const CPU_Core &now_state, CPU_Core &next_state,
                     const uint8_t memory[]);

    // ROB管理
    bool rob_full(const CPU_Core &cpu) const;
    bool rob_empty(const CPU_Core &cpu) const;
    uint32_t allocate_rob_entry(CPU_Core &cpu);
    void free_rob_entry(CPU_Core &cpu);

    // 预约站管理
    bool rs_available(const CPU_Core &cpu, InstrType type) const;
    uint32_t allocate_rs_entry(const CPU_Core &cpu, InstrType type);
    void free_rs_entry(CPU_Core &cpu, uint32_t rs_idx, InstrType type);

    // LSB管理
    bool LSB_available(const CPU_Core &cpu) const;
    uint32_t allocate_LSB_entry(const CPU_Core &cpu);
    void free_LSB_entry(CPU_Core &cpu, uint32_t LSB_idx);

    // 寄存器重命名
    void rename_registers(CPU_Core &cpu, const ROBEntry &rob_entry, uint32_t rob_idx);
    uint32_t read_operand(const CPU_Core &cpu, uint32_t reg_idx, uint32_t &rob_dependency,
                          bool &ready);

    // 广播
    void Broadcast(CPU_Core &cpu, const CDB);
    void broadcast_result(const CPU_Core &cpu, CPU_Core &next_state, uint32_t rob_idx,
                          uint32_t value);

    // 内存依赖检查
    bool is_earlier_instruction(const CPU_Core &cpu, uint32_t rob_idx1, uint32_t rob_idx2);

    bool check_load_dependencies(const CPU_Core &cpu, uint32_t load_addr, uint32_t load_rob_idx,
                                 uint32_t &forwarded_value);
    bool get_load_values(const CPU_Core &cpu, uint32_t load_addr, uint32_t load_rob_idx,
                         uint32_t &forwarded_value);

    // 分支预测和处理
    bool predict_branch_taken(const CPU_Core &cpu);
    void handle_branch_misprediction(CPU_Core &cpu, uint32_t correct_pc);
    void flush_pipeline(CPU_Core &cpu);

    // 统计信息
    uint64_t cycle_count_;
    uint64_t instruction_count_;
    uint64_t branch_mispredictions_; // 分支预测错误计数
};

#endif // CPU_CORE_H
