#include "../include/cpu_state.h"
#include "../include/instruction.h"

#include <iostream>

// Processor 类的构造函数
Processor::Processor()
    : has_fetched_instruction(false), cycle_count(0), instruction_count(0),
      branch_mispredictions(0) {
    // 初始化CDB结果
    cdb_result.valid = false;
    cdb_result.rob_idx = 0;
    cdb_result.value = 0;
}

// 主要的时钟周期函数
void Processor::tick(CPU_State &cpu) {
    // ！！！！注意：这几个阶段的执行顺序在模拟中很重要！！！！
    // 我们通常以后端到前端的倒序来模拟，防止一条指令在一个周期内穿越多个阶段
    // 这样最容易写对。

    cycle_count++;

    commit(cpu);            // 阶段5: 提交
    writeback(cpu);         // 阶段4: 写回/广播
    execute(cpu);           // 阶段3: 执行
    dispatch(cpu);          // 阶段2: 分派
    decode_and_rename(cpu); // 阶段1.5: 解码与重命名
    fetch(cpu);             // 阶段1: 取指
}

// 获取指令执行周期数
int Processor::get_execution_cycles(InstrType type) {
    switch (type) {
    case InstrType::ALU_ADD:
    case InstrType::ALU_SUB:
    case InstrType::ALU_AND:
    case InstrType::ALU_OR:
    case InstrType::ALU_XOR:
    case InstrType::ALU_SLL:
    case InstrType::ALU_SRL:
    case InstrType::ALU_SRA:
    case InstrType::ALU_SLT:
    case InstrType::ALU_SLTU:
    case InstrType::ALU_ADDI:
    case InstrType::ALU_ANDI:
    case InstrType::ALU_ORI:
    case InstrType::ALU_XORI:
    case InstrType::ALU_SLLI:
    case InstrType::ALU_SRLI:
    case InstrType::ALU_SRAI:
    case InstrType::ALU_SLTI:
    case InstrType::ALU_SLTIU:
        return 1; // ALU操作1周期
    case InstrType::LOAD_LW:
    case InstrType::LOAD_LH:
    case InstrType::LOAD_LB:
    case InstrType::LOAD_LBU:
    case InstrType::LOAD_LHU:
        return 3; // Load操作3周期（包含内存访问）
    case InstrType::STORE_SW:
    case InstrType::STORE_SH:
    case InstrType::STORE_SB:
        return 2; // Store操作2周期
    case InstrType::BRANCH_BEQ:
    case InstrType::BRANCH_BNE:
    case InstrType::BRANCH_BLT:
    case InstrType::BRANCH_BGE:
    case InstrType::BRANCH_BLTU:
    case InstrType::BRANCH_BGEU:
        return 1; // 分支操作1周期
    case InstrType::JUMP_JAL:
    case InstrType::JUMP_JALR:
        return 1; // 跳转操作1周期
    case InstrType::LUI:
    case InstrType::AUIPC:
        return 1; // 立即数操作1周期
    default:
        return 1;
    }
}

// 1. 取指阶段
void Processor::fetch(CPU_State &cpu) {
    // 如果取指被停滞或流水线被冲刷，跳过
    if (cpu.fetch_stalled || cpu.pipeline_flushed) {
        cpu.pipeline_flushed = false; // 重置冲刷标志
        return;
    }

    // 检查ROB是否有空位
    if (cpu.rob_size >= ROB_SIZE) {
        cpu.fetch_stalled = true; // ROB满了，停止取指
        return;
    }

    // 检查是否还有空闲的预约站
    bool has_free_rs = false;
    for (int i = 0; i < RS_SIZE; i++) {
        if (!cpu.rs_alu[i].busy) {
            has_free_rs = true;
            break;
        }
    }
    for (int i = 0; i < RS_SIZE / 2; i++) {
        if (!cpu.rs_branch[i].busy) {
            has_free_rs = true;
            break;
        }
    }
    if (!has_free_rs) {
        return; // 没有空闲的预约站，暂停取指
    }

    // 从内存中取指令
    if (cpu.pc + 3 >= MEMORY_SIZE) {
        cpu.fetch_stalled = true;
        return; // PC超出内存范围
    }

    uint32_t instruction = cpu.memory[cpu.pc] | (cpu.memory[cpu.pc + 1] << 8) |
                           (cpu.memory[cpu.pc + 2] << 16) | (cpu.memory[cpu.pc + 3] << 24);

    // 检查是否是HALT指令
    if (instruction == HALT_INSTRUCTION) {
        cpu.fetch_stalled = true;
        return;
    }

    // 为ROB分配一个新条目
    uint32_t rob_idx = cpu.rob_tail;
    cpu.rob[rob_idx].busy = true;
    cpu.rob[rob_idx].pc = cpu.pc;
    cpu.rob[rob_idx].state = InstrState::Dispatch;
    cpu.rob[rob_idx].ready = false;

    // 解码指令并设置类型
    DecodedInstruction decoded = decode_instruction(instruction, cpu.pc);
    cpu.rob[rob_idx].instr_type = decoded.type;
    cpu.rob[rob_idx].dest_reg = decoded.rd;
    cpu.rob[rob_idx].rs1 = decoded.rs1;
    cpu.rob[rob_idx].rs2 = decoded.rs2;
    cpu.rob[rob_idx].imm = decoded.imm;

    // 更新ROB尾指针
    cpu.rob_tail = (cpu.rob_tail + 1) % ROB_SIZE;
    cpu.rob_size++;
    instruction_count++;

    // PC的更新：默认情况下，PC = PC + 4，这叫推测执行
    // 对分支指令进行预测
    if (is_branch_instruction(decoded.type)) {
        cpu.rob[rob_idx].is_branch = true;
        bool predicted = predict_branch(cpu, cpu.pc);
        cpu.rob[rob_idx].predicted_taken = predicted;

        if (predicted) {
            // 这里需要计算分支目标地址
            cpu.rob[rob_idx].target_pc = cpu.pc + decoded.imm;
            cpu.pc = cpu.rob[rob_idx].target_pc;
        } else {
            cpu.pc += 4;
        }
    } else {
        cpu.pc += 4;
    }
}

// 2. 解码和重命名阶段
void Processor::decode_and_rename(CPU_State &cpu) {
    // 找到最老的处于Dispatch状态的指令
    for (int i = 0; i < ROB_SIZE; i++) {
        uint32_t idx = (cpu.rob_head + i) % ROB_SIZE;
        if (!cpu.rob[idx].busy || cpu.rob[idx].state != InstrState::Dispatch) {
            continue;
        }

        ROBEntry &rob_entry = cpu.rob[idx];

        // 重命名：处理源操作数的重命名
        uint32_t rs1_value = 0, rs2_value = 0;
        uint32_t rs1_rob_idx = 0, rs2_rob_idx = 0;

        // 处理第一个源操作数rs1
        if (rob_entry.rs1 != 0) { // 寄存器x0始终为0
            if (!cpu.rat[rob_entry.rs1].busy) {
                // 值已就绪，从架构寄存器文件读取
                rs1_value = cpu.arf.regs[rob_entry.rs1];
            } else {
                // 有依赖，记录ROB索引
                rs1_rob_idx = cpu.rat[rob_entry.rs1].rob_idx;
                // 检查依赖的指令是否已经完成
                if (cpu.rob[rs1_rob_idx].ready) {
                    rs1_value = cpu.rob[rs1_rob_idx].value;
                    rs1_rob_idx = 0; // 标记为就绪
                }
            }
        }

        // 处理第二个源操作数rs2
        if (rob_entry.rs2 != 0) {
            if (!cpu.rat[rob_entry.rs2].busy) {
                rs2_value = cpu.arf.regs[rob_entry.rs2];
            } else {
                rs2_rob_idx = cpu.rat[rob_entry.rs2].rob_idx;
                if (cpu.rob[rs2_rob_idx].ready) {
                    rs2_value = cpu.rob[rs2_rob_idx].value;
                    rs2_rob_idx = 0;
                }
            }
        }

        // 更新目标寄存器的RAT
        if (rob_entry.dest_reg != 0) { // 寄存器x0不能写入
            cpu.rat[rob_entry.dest_reg].busy = true;
            cpu.rat[rob_entry.dest_reg].rob_idx = idx;
        }

        // 根据指令类型分配到不同的执行单元
        if (is_load_instruction(rob_entry.instr_type) ||
            is_store_instruction(rob_entry.instr_type)) {
            // 分配LSQ条目
            uint32_t lsq_idx;
            if (allocate_lsq_entry(cpu, lsq_idx)) {
                LSQEntry &lsq = cpu.lsq[lsq_idx];
                lsq.op = rob_entry.instr_type;
                lsq.rob_idx = idx;
                lsq.dest_rob_idx = idx;
                lsq.offset = rob_entry.imm;

                // 设置基址寄存器信息
                lsq.base_value = rs1_value;
                lsq.base_rob_idx = rs1_rob_idx;

                if (is_store_instruction(rob_entry.instr_type)) {
                    // STORE指令需要存储的数据值
                    lsq.value = rs2_value;
                    lsq.value_ready = (rs2_rob_idx == 0);
                    // 如果还有依赖，需要记录依赖的ROB索引
                } else {
                    lsq.value_ready = true; // LOAD不需要数据值
                }

                // 检查地址是否就绪
                lsq.address_ready = (rs1_rob_idx == 0);
                if (lsq.address_ready) {
                    lsq.address = lsq.base_value + lsq.offset;
                }
            }
        } else {
            // 分配预约站条目
            uint32_t rs_idx;
            if (allocate_rs_entry(cpu, rob_entry.instr_type, rs_idx)) {
                RSEntry *rs_entry = nullptr;
                if (is_branch_instruction(rob_entry.instr_type)) {
                    rs_entry = &cpu.rs_branch[rs_idx];
                } else {
                    rs_entry = &cpu.rs_alu[rs_idx];
                }

                rs_entry->op = rob_entry.instr_type;
                rs_entry->dest_rob_idx = idx;
                rs_entry->Vj = rs1_value;
                rs_entry->Vk = rs2_value;
                rs_entry->Qj = rs1_rob_idx;
                rs_entry->Qk = rs2_rob_idx;
                rs_entry->imm = rob_entry.imm;
                rs_entry->execution_cycles_left = get_execution_cycles(rob_entry.instr_type);
            }
        }

        // 更新指令状态
        rob_entry.state = InstrState::Execute;
        break; // 每个周期只处理一条指令
    }
}

// 3. 分派阶段
void Processor::dispatch(CPU_State &cpu) {
    // 在decode_and_rename阶段已经完成了分派工作
    // 这里可以添加额外的分派逻辑，比如检查执行单元可用性
}

// 4. 执行阶段
void Processor::execute(CPU_State &cpu) {
    // 清除上一周期的CDB广播
    cdb_result.valid = false;

    // 执行ALU预约站中的指令
    for (int i = 0; i < RS_SIZE; i++) {
        RSEntry &rs = cpu.rs_alu[i];
        if (!rs.busy)
            continue;

        // 检查操作数是否都就绪
        if (rs.operands_ready() && rs.execution_cycles_left > 0) {
            rs.execution_cycles_left--;

            if (rs.execution_cycles_left == 0) {
                // 执行完成，计算结果
                uint32_t result = 0;
                switch (rs.op) {
                case InstrType::ALU_ADD:
                case InstrType::ALU_ADDI:
                    result = rs.Vj + (rs.Qk == 0 ? rs.Vk : rs.imm);
                    break;
                case InstrType::ALU_SUB:
                    result = rs.Vj - rs.Vk;
                    break;
                case InstrType::ALU_AND:
                case InstrType::ALU_ANDI:
                    result = rs.Vj & (rs.Qk == 0 ? rs.Vk : rs.imm);
                    break;
                case InstrType::ALU_OR:
                case InstrType::ALU_ORI:
                    result = rs.Vj | (rs.Qk == 0 ? rs.Vk : rs.imm);
                    break;
                case InstrType::ALU_XOR:
                case InstrType::ALU_XORI:
                    result = rs.Vj ^ (rs.Qk == 0 ? rs.Vk : rs.imm);
                    break;
                case InstrType::LUI:
                    result = rs.imm;
                    break;
                case InstrType::AUIPC:
                    result = cpu.rob[rs.dest_rob_idx].pc + rs.imm;
                    break;
                // 添加更多ALU操作...
                default:
                    result = rs.Vj + rs.Vk;
                    break;
                }

                // 广播结果到CDB
                broadcast_cdb_result(cpu, rs.dest_rob_idx, result);

                // 释放预约站
                rs.busy = false;
                break; // 每周期只能有一个结果广播
            }
        }
    }

    // 执行分支预约站中的指令
    if (!cdb_result.valid) { // 如果CDB还没被占用
        for (int i = 0; i < RS_SIZE / 2; i++) {
            RSEntry &rs = cpu.rs_branch[i];
            if (!rs.busy)
                continue;

            if (rs.operands_ready() && rs.execution_cycles_left > 0) {
                rs.execution_cycles_left--;

                if (rs.execution_cycles_left == 0) {
                    execute_branch_instruction(cpu, rs, cpu.rob[rs.dest_rob_idx]);
                    rs.busy = false;
                    break;
                }
            }
        }
    }

    // 执行LSQ中的指令
    for (int i = 0; i < LSQ_SIZE; i++) {
        LSQEntry &lsq = cpu.lsq[i];
        if (!lsq.busy)
            continue;

        // 首先确保地址已计算出来
        if (!lsq.address_ready && lsq.base_rob_idx == 0) {
            lsq.address = lsq.base_value + lsq.offset;
            lsq.address_ready = true;
        }

        if (lsq.address_ready) {
            if (is_load_instruction(lsq.op)) {
                execute_load(cpu, lsq, cpu.rob[lsq.rob_idx]);
            } else if (is_store_instruction(lsq.op)) {
                execute_store(cpu, lsq, cpu.rob[lsq.rob_idx]);
            }
        }
    }
}

// 5. 写回阶段
void Processor::writeback(CPU_State &cpu) {
    // CDB广播结果已经在execute阶段完成
    // 这里处理CDB结果的接收和转发

    if (cdb_result.valid) {
        // 更新ROB条目
        ROBEntry &rob = cpu.rob[cdb_result.rob_idx];
        rob.value = cdb_result.value;
        rob.ready = true;
        rob.state = InstrState::Writeback;

        // 更新所有等待这个结果的预约站
        for (int i = 0; i < RS_SIZE; i++) {
            RSEntry &rs = cpu.rs_alu[i];
            if (rs.busy) {
                if (rs.Qj == cdb_result.rob_idx) {
                    rs.Vj = cdb_result.value;
                    rs.Qj = 0;
                }
                if (rs.Qk == cdb_result.rob_idx) {
                    rs.Vk = cdb_result.value;
                    rs.Qk = 0;
                }
            }
        }

        for (int i = 0; i < RS_SIZE / 2; i++) {
            RSEntry &rs = cpu.rs_branch[i];
            if (rs.busy) {
                if (rs.Qj == cdb_result.rob_idx) {
                    rs.Vj = cdb_result.value;
                    rs.Qj = 0;
                }
                if (rs.Qk == cdb_result.rob_idx) {
                    rs.Vk = cdb_result.value;
                    rs.Qk = 0;
                }
            }
        }

        // 更新LSQ中等待的条目
        for (int i = 0; i < LSQ_SIZE; i++) {
            LSQEntry &lsq = cpu.lsq[i];
            if (lsq.busy) {
                if (lsq.base_rob_idx == cdb_result.rob_idx) {
                    lsq.base_value = cdb_result.value;
                    lsq.base_rob_idx = 0;
                    // 重新计算地址
                    lsq.address = lsq.base_value + lsq.offset;
                    lsq.address_ready = true;
                }
            }
        }
    }
}

// 6. 提交阶段
void Processor::commit(CPU_State &cpu) {
    // 永远只看ROB的头部
    if (cpu.rob_size == 0)
        return;

    ROBEntry &rob = cpu.rob[cpu.rob_head];
    if (!rob.busy || !rob.ready)
        return;

    // 检查是否是分支指令且预测错误
    if (rob.is_branch) {
        if (rob.predicted_taken != rob.actual_taken) {
            // 分支预测错误，需要冲刷流水线
            branch_mispredictions++;
            handle_branch_misprediction(cpu, rob.actual_taken ? rob.target_pc : rob.pc + 4);
            return;
        }
    }

    // 正常提交指令
    commit_instruction(cpu, rob);

    // 更新RAT
    if (rob.dest_reg != 0 && cpu.rat[rob.dest_reg].rob_idx == cpu.rob_head) {
        cpu.rat[rob.dest_reg].busy = false;
    }

    // 释放ROB条目
    rob.busy = false;
    cpu.rob_head = (cpu.rob_head + 1) % ROB_SIZE;
    cpu.rob_size--;
}

// 辅助函数实现
bool Processor::is_alu_instruction(InstrType type) {
    return type >= InstrType::ALU_ADD && type <= InstrType::ALU_SLTIU;
}

bool Processor::is_branch_instruction(InstrType type) {
    return type >= InstrType::BRANCH_BEQ && type <= InstrType::BRANCH_BGEU;
}

bool Processor::is_load_instruction(InstrType type) {
    return type >= InstrType::LOAD_LB && type <= InstrType::LOAD_LHU;
}

bool Processor::is_store_instruction(InstrType type) {
    return type >= InstrType::STORE_SB && type <= InstrType::STORE_SW;
}

void Processor::broadcast_cdb_result(CPU_State &cpu, uint32_t rob_index, uint32_t value) {
    cdb_result.valid = true;
    cdb_result.rob_idx = rob_index;
    cdb_result.value = value;
}

bool Processor::allocate_rob_entry(CPU_State &cpu, uint32_t &rob_idx) {
    if (cpu.rob_size >= ROB_SIZE)
        return false;

    rob_idx = cpu.rob_tail;
    cpu.rob_tail = (cpu.rob_tail + 1) % ROB_SIZE;
    cpu.rob_size++;
    return true;
}

bool Processor::allocate_rs_entry(CPU_State &cpu, InstrType type, uint32_t &rs_idx) {
    RSEntry *rs_array;
    int rs_size;

    if (is_branch_instruction(type)) {
        rs_array = cpu.rs_branch;
        rs_size = RS_SIZE / 2;
    } else {
        rs_array = cpu.rs_alu;
        rs_size = RS_SIZE;
    }

    for (int i = 0; i < rs_size; i++) {
        if (!rs_array[i].busy) {
            rs_array[i].busy = true;
            rs_idx = i;
            return true;
        }
    }
    return false;
}

bool Processor::allocate_lsq_entry(CPU_State &cpu, uint32_t &lsq_idx) {
    for (int i = 0; i < LSQ_SIZE; i++) {
        if (!cpu.lsq[i].busy) {
            cpu.lsq[i].busy = true;
            lsq_idx = i;
            return true;
        }
    }
    return false;
}

bool Processor::predict_branch(CPU_State &cpu, uint32_t pc) {
    // 简单的全局历史预测器
    return cpu.branch_predictor;
}

void Processor::handle_branch_misprediction(CPU_State &cpu, uint32_t correct_pc) {
    flush_pipeline(cpu);
    cpu.pc = correct_pc;
}

void Processor::flush_pipeline(CPU_State &cpu) {
    // 清空ROB、RS、LSQ
    for (int i = 0; i < ROB_SIZE; i++) {
        cpu.rob[i].busy = false;
    }
    for (int i = 0; i < RS_SIZE; i++) {
        cpu.rs_alu[i].busy = false;
    }
    for (int i = 0; i < RS_SIZE / 2; i++) {
        cpu.rs_branch[i].busy = false;
    }
    for (int i = 0; i < LSQ_SIZE; i++) {
        cpu.lsq[i].busy = false;
    }

    // 清空RAT
    for (int i = 0; i < 32; i++) {
        cpu.rat[i].busy = false;
    }

    cpu.rob_head = 0;
    cpu.rob_tail = 0;
    cpu.rob_size = 0;
    cpu.pipeline_flushed = true;
}

// LSQ相关的关键函数实现
void Processor::execute_load(CPU_State &cpu, LSQEntry &lsq, ROBEntry &rob) {
    if (!lsq.address_ready)
        return;

    // 关键步骤：检查内存依赖
    uint32_t forwarded_value;
    if (check_memory_dependencies(cpu, lsq.address, lsq.rob_idx, forwarded_value)) {
        // 可以从Store-to-Load Forwarding获得数据
        broadcast_cdb_result(cpu, lsq.rob_idx, forwarded_value);
        lsq.busy = false;
        return;
    }

    // 检查是否可以进行Load操作
    if (!can_load_proceed(cpu, lsq.address, lsq.rob_idx)) {
        // 必须等待前面的Store地址计算完成
        return;
    }

    // 可以安全地从内存读取
    if (lsq.address < MEMORY_SIZE) {
        uint32_t loaded_value = 0;
        switch (lsq.op) {
        case InstrType::LOAD_LW:
            if (lsq.address + 3 < MEMORY_SIZE) {
                loaded_value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8) |
                               (cpu.memory[lsq.address + 2] << 16) |
                               (cpu.memory[lsq.address + 3] << 24);
            }
            break;
        case InstrType::LOAD_LH:
            if (lsq.address + 1 < MEMORY_SIZE) {
                loaded_value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8);
                // 符号扩展
                if (loaded_value & 0x8000) {
                    loaded_value |= 0xFFFF0000;
                }
            }
            break;
        case InstrType::LOAD_LB:
            loaded_value = cpu.memory[lsq.address];
            // 符号扩展
            if (loaded_value & 0x80) {
                loaded_value |= 0xFFFFFF00;
            }
            break;
        case InstrType::LOAD_LBU:
            loaded_value = cpu.memory[lsq.address];
            break;
        case InstrType::LOAD_LHU:
            if (lsq.address + 1 < MEMORY_SIZE) {
                loaded_value = cpu.memory[lsq.address] | (cpu.memory[lsq.address + 1] << 8);
            }
            break;
        default:
            // 其他指令类型不是load指令
            break;
        }

        broadcast_cdb_result(cpu, lsq.rob_idx, loaded_value);
        lsq.busy = false;
    }
}

void Processor::execute_store(CPU_State &cpu, LSQEntry &lsq, ROBEntry &rob) {
    // STORE指令只计算地址和准备数据，不实际写入内存
    // 实际写入在commit阶段进行

    if (lsq.address_ready && lsq.value_ready) {
        // 地址和数据都准备好了，标记为就绪
        rob.ready = true;
        rob.state = InstrState::Writeback;
    }
}

bool Processor::check_memory_dependencies(const CPU_State &cpu, uint32_t load_address,
                                          uint32_t load_rob_idx, uint32_t &forwarded_value) {
    // 遍历LSQ中所有在load之前的store指令
    for (int i = 0; i < LSQ_SIZE; i++) {
        const LSQEntry &store_entry = cpu.lsq[i];
        if (!store_entry.busy || !is_store_instruction(store_entry.op))
            continue;

        // 检查是否是程序顺序上更早的store
        if (store_entry.rob_idx >= load_rob_idx)
            continue;

        // 如果store的地址还没准备好，保守地阻塞load
        if (!store_entry.address_ready) {
            return false; // 需要等待
        }

        // 检查地址是否匹配
        if (store_entry.address == load_address && store_entry.value_ready) {
            // Store-to-Load Forwarding
            forwarded_value = store_entry.value;
            return true;
        }
    }

    return false; // 没有匹配的store，可以从内存读取
}

bool Processor::can_load_proceed(const CPU_State &cpu, uint32_t load_address,
                                 uint32_t load_rob_idx) {
    // 检查是否有程序顺序上更早的store的地址还未计算
    for (int i = 0; i < LSQ_SIZE; i++) {
        const LSQEntry &store_entry = cpu.lsq[i];
        if (!store_entry.busy || !is_store_instruction(store_entry.op))
            continue;

        if (store_entry.rob_idx < load_rob_idx && !store_entry.address_ready) {
            // 有更早的store地址未知，必须等待
            return false;
        }
    }

    return true;
}

void Processor::commit_instruction(CPU_State &cpu, ROBEntry &rob) {
    if (rob.dest_reg != 0) {
        // 写入架构寄存器文件
        cpu.arf.regs[rob.dest_reg] = rob.value;
    }

    // 如果是store指令，实际写入内存
    if (is_store_instruction(rob.instr_type)) {
        // 找到对应的LSQ条目
        for (int i = 0; i < LSQ_SIZE; i++) {
            LSQEntry &lsq = cpu.lsq[i];
            if (lsq.busy && lsq.rob_idx == cpu.rob_head) {
                commit_store(cpu, rob, lsq);
                lsq.busy = false;
                break;
            }
        }
    }
}

void Processor::commit_store(CPU_State &cpu, const ROBEntry &rob, const LSQEntry &lsq) {
    // 现在才真正写入内存
    if (lsq.address < MEMORY_SIZE) {
        switch (lsq.op) {
        case InstrType::STORE_SW:
            if (lsq.address + 3 < MEMORY_SIZE) {
                cpu.memory[lsq.address] = lsq.value & 0xFF;
                cpu.memory[lsq.address + 1] = (lsq.value >> 8) & 0xFF;
                cpu.memory[lsq.address + 2] = (lsq.value >> 16) & 0xFF;
                cpu.memory[lsq.address + 3] = (lsq.value >> 24) & 0xFF;
            }
            break;
        case InstrType::STORE_SH:
            if (lsq.address + 1 < MEMORY_SIZE) {
                cpu.memory[lsq.address] = lsq.value & 0xFF;
                cpu.memory[lsq.address + 1] = (lsq.value >> 8) & 0xFF;
            }
            break;
        case InstrType::STORE_SB:
            cpu.memory[lsq.address] = lsq.value & 0xFF;
            break;
        default:
            // 其他指令类型不是store指令
            break;
        }
    }
}

void Processor::execute_branch_instruction(CPU_State &cpu, RSEntry &rs, ROBEntry &rob) {
    bool taken = false;

    switch (rs.op) {
    case InstrType::BRANCH_BEQ:
        taken = (rs.Vj == rs.Vk);
        break;
    case InstrType::BRANCH_BNE:
        taken = (rs.Vj != rs.Vk);
        break;
    case InstrType::BRANCH_BLT:
        taken = ((int32_t) rs.Vj < (int32_t) rs.Vk);
        break;
    case InstrType::BRANCH_BGE:
        taken = ((int32_t) rs.Vj >= (int32_t) rs.Vk);
        break;
    case InstrType::BRANCH_BLTU:
        taken = (rs.Vj < rs.Vk);
        break;
    case InstrType::BRANCH_BGEU:
        taken = (rs.Vj >= rs.Vk);
        break;
    default:
        // 其他指令类型不是分支指令
        taken = false;
        break;
    }

    rob.actual_taken = taken;
    rob.ready = true;
    rob.state = InstrState::Writeback;

    // 更新分支预测器
    cpu.branch_predictor = taken;
}

// 临时的decode_instruction实现（需要根据实际RISC-V指令格式完善）
DecodedInstruction Processor::decode_instruction(uint32_t instruction, uint32_t pc) {
    DecodedInstruction decoded;
    // 这里需要实现完整的RISC-V指令解码
    // 暂时返回基本结构
    decoded.raw_instr = instruction;
    decoded.pc = pc;
    decoded.rd = (instruction >> 7) & 0x1F;
    decoded.rs1 = (instruction >> 15) & 0x1F;
    decoded.rs2 = (instruction >> 20) & 0x1F;
    decoded.imm = 0; // 需要根据指令类型计算
    decoded.type = get_instruction_type(instruction);
    return decoded;
}

InstrType Processor::get_instruction_type(uint32_t instruction) {
    // 根据opcode和func字段确定指令类型
    uint32_t opcode = instruction & 0x7F;

    switch (opcode) {
    case 0x33:                     // R-type
        return InstrType::ALU_ADD; // 简化实现
    case 0x13:                     // I-type immediate
        return InstrType::ALU_ADDI;
    case 0x03: // Load
        return InstrType::LOAD_LW;
    case 0x23: // Store
        return InstrType::STORE_SW;
    case 0x63: // Branch
        return InstrType::BRANCH_BEQ;
    default:
        return InstrType::ALU_ADD;
    }
}

bool Processor::rob_full(const CPU_State &cpu) { return cpu.rob_size >= ROB_SIZE; }

bool Processor::rob_empty(const CPU_State &cpu) { return cpu.rob_size == 0; }
