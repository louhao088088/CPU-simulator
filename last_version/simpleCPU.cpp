#include <bitset>
#include <cstdint>
#include <iostream>
#include <string>
#include <sys/types.h>

const int MEMORY_SIZE = 1024 * 1024;
const uint32_t HALT_INSTRUCTION = 0x0ff00513;

using std::cerr;

struct CPU_State {
    uint32_t pc;
    uint32_t regs[32];
    uint8_t memory[MEMORY_SIZE];

    CPU_State() {
        pc = 0;
        for (int i = 0; i < 32; ++i) {
            regs[i] = 0;
        }
        for (int i = 0; i < MEMORY_SIZE; ++i) {
            memory[i] = 0;
        }
    }
};
int cnt = 0;
class RISCV_Simulator {
  private:
    CPU_State cpu;
    bool is_halted = false;

  public:
    RISCV_Simulator() = default;

    // load the instruction
    void load_program() {
        std::string line;
        unsigned int current_address = 0;

        char at_symbol;
        while (std::cin >> at_symbol) {
            if (at_symbol == '@') {
                std::cin >> std::hex >> current_address;
            } else {
                std::cin.putback(at_symbol);
                unsigned int byte_value;
                std::cin >> std::hex >> byte_value;
                if (current_address < MEMORY_SIZE) {
                    cpu.memory[current_address++] = static_cast<uint8_t>(byte_value);
                }
            }
        }
    }

    void run() {
        while (!is_halted) {
            tick();
        }
        print_result();
    }

  private:
    void tick() {

        uint32_t instruction = fetch_instruction();

        if (instruction == HALT_INSTRUCTION) {
            is_halted = true;
            return;
        }

        // DECODE & EXECUTE
        if (cnt == 126) {
            std::cout << std::bitset<32>(instruction) << " " << std::hex << " " << cpu.pc
                      << std::dec << std::endl;
        }

        uint32_t next_pc = cpu.pc + 4;

        if (cnt > 145069)
            exit(0);
        decode_and_execute(instruction, next_pc);

        cpu.pc = next_pc;

        cpu.regs[0] = 0;
        cnt++;

        std::cout << cnt << " "
                  << "\n";
        for (int i = 0; i < 32; i++)
            std::cout << cpu.regs[i] << " ";
        std::cout << "\n";
    }

    uint32_t fetch_instruction() {

        if (cpu.pc >= MEMORY_SIZE - 3) {
            std::cerr << "Error: Program Counter out of bounds!" << std::endl;
            is_halted = true;
            return 0;
        }

        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 0]);
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 1]) << 8;
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 2]) << 16;
        inst |= static_cast<uint32_t>(cpu.memory[cpu.pc + 3]) << 24;
        return inst;
    }

    void decode_and_execute(uint32_t instruction, uint32_t &next_pc) {

        uint32_t opcode = instruction & 0x7F;

        switch (opcode) {

        case 0b0110111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t imm = (instruction >> 12) & 0xFFFFF;
            if (rd != 0)
                cpu.regs[rd] = (imm << 12);

            break;
        } // U-TYPE lui

        case 0b0010111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t imm = (instruction >> 12) & 0xFFFFF;
            if (rd != 0)
                cpu.regs[rd] = (imm << 12) + cpu.pc;

            // std::cerr<< "auipc"
            //   << " " << rd << " " << imm;

            break;
        } // U-TYPE auipc

        case 0b1101111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t imm_20 = (instruction >> 31) & 1;
            uint32_t imm_10_1 = (instruction >> 21) & 0x3FF;
            uint32_t imm_11 = (instruction >> 20) & 1;
            uint32_t imm_19_12 = (instruction >> 12) & 0xFF;

            uint32_t imm = ((imm_20 << 20) | (imm_19_12 << 12) | (imm_11 << 11) | (imm_10_1 << 1));
            int32_t offset;
            if (imm_20 == 1) {
                offset = static_cast<int32_t>(imm | 0xFFF00000);
            } else {
                offset = static_cast<int32_t>(imm);
            }
            if (rd != 0) {
                cpu.regs[rd] = cpu.pc + 4;
            }
            next_pc = cpu.pc + offset;

            // cerr<< "jal" << std::dec << " " << offset << " " << next_pc << " " << rd << "\n";

            break;
        } // J-TYPE

        case 0b1100111: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            int32_t imm = static_cast<int32_t>(instruction) >> 20;

            if (funct3 == 0b000) {
                next_pc =
                    (static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + imm)) & (~1U);
                if (rd != 0) {
                    cpu.regs[rd] = cpu.pc + 4;
                }
            } else {
                std::cerr << "Error: Unknown J-type instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }

            // cerr<< "jalr" << std::dec << " " << rd << " " << rs1 << " " << imm << "\n";
            break;
        } // I-TYPE JALR

        case 0b1100011: {
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            uint32_t rs2 = (instruction >> 20) & 0x1F;

            uint32_t imm_12 = (instruction >> 31) & 0x1;
            uint32_t imm_10_5 = (instruction >> 25) & 0x3F;
            uint32_t imm_4_1 = (instruction >> 8) & 0xF;
            uint32_t imm_11 = (instruction >> 7) & 0x1;

            uint32_t imm = (imm_12 << 12) | (imm_11 << 11) | (imm_10_5 << 5) | (imm_4_1 << 1);

            int32_t offset = static_cast<int32_t>(imm << 19) >> 19;

            if (funct3 == 0b000) {
                if (cpu.regs[rs1] == cpu.regs[rs2]) {
                    next_pc = cpu.pc + offset;
                }
                // cerr<< "beq ";
            } // Branch if Equal
            else if (funct3 == 0b001) {
                if (cpu.regs[rs1] != cpu.regs[rs2]) {
                    next_pc = cpu.pc + offset;
                }
                // cerr<< "bne ";
            } // Branch if Not Equal
            else if (funct3 == 0b100) {
                if (static_cast<int32_t>(cpu.regs[rs1]) < static_cast<int32_t>(cpu.regs[rs2])) {
                    next_pc = cpu.pc + offset;
                }
                // cerr<< "blt ";
            } // Branch if Less Than
            else if (funct3 == 0b101) {
                if (static_cast<int32_t>(cpu.regs[rs1]) >= static_cast<int32_t>(cpu.regs[rs2])) {
                    next_pc = cpu.pc + offset;
                }
                // cerr<< "bge ";
            } // Branch if Greater Than or Equal
            else if (funct3 == 0b110) {
                if (cpu.regs[rs1] < cpu.regs[rs2]) {
                    next_pc = cpu.pc + offset;
                }
                // cerr<< "bltu ";

            } // Branch if Less Than, Unsigned
            else if (funct3 == 0b111) {
                if (cpu.regs[rs1] >= cpu.regs[rs2]) {
                    next_pc = cpu.pc + offset;
                }
                // cerr<< "bgeu ";
            } // Branch if Greater Than or Equal, Unsigned
            else {
                std::cerr << "Error: Unknown branch instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }
            // cerr<< std::dec << " " << funct3 << " " << rs1 << " " << rs2 << " " << offset <<
            // "\n";
            break;
        } // B-TYPE

        case 0b0000011: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            int32_t offset = static_cast<int32_t>(instruction) >> 20;

            if (funct3 == 0b000) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = static_cast<int32_t>(static_cast<int8_t>(cpu.memory[address]));

                // cerr<< "lb ";

            } // Load Byte
            else if (funct3 == 0b001) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE - 1) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address + 1 << std::endl;
                    is_halted = true;
                    return;
                }
                if (address % 2 != 0) {
                    std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = static_cast<int32_t>(static_cast<int16_t>(
                        static_cast<uint16_t>(cpu.memory[address]) |
                        (static_cast<uint16_t>(cpu.memory[address + 1]) << 8)));

                // cerr<< "lh ";
            } // Load Halfword
            else if (funct3 == 0b010) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE - 3) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address + 3 << std::endl;
                    is_halted = true;
                    return;
                }

                if (address % 4 != 0) {
                    std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = static_cast<uint32_t>(cpu.memory[address]) |
                                   (static_cast<uint32_t>(cpu.memory[address + 1]) << 8) |
                                   (static_cast<uint32_t>(cpu.memory[address + 2]) << 16) |
                                   (static_cast<uint32_t>(cpu.memory[address + 3]) << 24);

                ////cerr<< "lw " << std::dec << " " << address << " " << rs1 << " " << offset << " "
                // << cpu.regs[rs1] << " " << cpu.regs[rd] << "\n";
            } // Load Word
            else if (funct3 == 0b100) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }

                if (rd != 0)
                    cpu.regs[rd] = cpu.memory[address];

                // cerr<< "lbu ";
            } // Load Byte, Unsigned
            else if (funct3 == 0b101) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE - 1) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address + 1 << std::endl;
                    is_halted = true;
                    return;
                }
                if (address % 2 != 0) {
                    std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }
                if (rd != 0)
                    cpu.regs[rd] = static_cast<uint32_t>(cpu.memory[address]) |
                                   (static_cast<uint32_t>(cpu.memory[address + 1]) << 8);

                // cerr<< "lhu ";

            } // Load Halfword, Unsigned
            else {
                std::cerr << "Error: Unknown load instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }

            // cerr<< std::dec << " " << rd << " " << rs1 << " " << offset << "\n";
            break;
        } // I-TYPE LOAD

        case 0b0100011: {
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            uint32_t rs2 = (instruction >> 20) & 0x1F;
            int32_t offset = static_cast<int32_t>((((instruction >> 25) & 0x7F) << 5) |
                                                  ((instruction >> 7) & 0x1F));
            if (offset & 0x800) {
                offset |= 0xFFFFF000;
            }
            if (funct3 == 0b000) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }
                cpu.memory[address] = static_cast<uint8_t>(cpu.regs[rs2]);

                // cerr<< "sb ";
            } // Store Byte
            else if (funct3 == 0b001) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address + 1 >= MEMORY_SIZE) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address + 1 << std::endl;
                    is_halted = true;
                    return;
                }
                if (address % 2 != 0) {
                    std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;
                    is_halted = true;
                    return;
                }

                cpu.memory[address] = static_cast<uint8_t>(cpu.regs[rs2]);
                cpu.memory[address + 1] = static_cast<uint8_t>(cpu.regs[rs2] >> 8);
                // cerr<< "sh ";
            } // Store Halfword
            else if (funct3 == 0b010) {
                uint32_t address =
                    static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + offset);
                if (address >= MEMORY_SIZE - 3) {
                    std::cerr << "Error: Memory access out of bounds at pc " << std::hex << cpu.pc
                              << ", trying to access " << address + 3 << std::endl;
                    is_halted = true;
                    return;
                }
                if (address % 4 != 0) {
                    std::cerr << "Error: Misaligned memory access at pc " << std::hex << cpu.pc
                              << ", trying to access " << address << std::endl;

                    is_halted = true;
                    return;
                }

                cpu.memory[address] = static_cast<uint8_t>(cpu.regs[rs2]);
                cpu.memory[address + 1] = static_cast<uint8_t>(cpu.regs[rs2] >> 8);
                cpu.memory[address + 2] = static_cast<uint8_t>(cpu.regs[rs2] >> 16);
                cpu.memory[address + 3] = static_cast<uint8_t>(cpu.regs[rs2] >> 24);
                // cerr<< "sw ";
            } // Store Word
            else {
                std::cerr << "Error: Unknown store instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }
            // cerr<< std::dec << " " << rs1 << " " << rs2 << " " << offset << "\n";
            break;
        } // S-TYPE STORE
        case 0b0010011: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            int32_t imm = static_cast<int32_t>(instruction) >> 20;

            if (funct3 == 0b000) {
                if (rd != 0)
                    cpu.regs[rd] = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) + imm);
                // cerr<< "addi ";
            } // Add Immediate
            else if (funct3 == 0b010) {
                if (rd != 0)
                    cpu.regs[rd] = (static_cast<int32_t>(cpu.regs[rs1]) < (imm)) ? 1 : 0;
                // cerr<< "slti ";
            } // Set if Less Than Immediate
            else if (funct3 == 0b011) {
                if (rd != 0)
                    cpu.regs[rd] = (cpu.regs[rs1] < static_cast<uint32_t>(imm)) ? 1 : 0;
                // cerr<< "sltiu ";
            } // Set if Less Than Immediate, Unsigned
            else if (funct3 == 0b100) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] ^ imm;
                // cerr<< "xori ";
            } // XORI
            else if (funct3 == 0b110) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] | imm;
                // cerr<< "ori ";
            } // ORI
            else if (funct3 == 0b111) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] & imm;
                // cerr<< "andi ";
            } // ANDI
            else if (funct3 == 0b001) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] << (imm & 0x1F);
                // cerr<< "slli ";
            } // Shift Left Logical Immediate
            else if (funct3 == 0b101 && ((instruction >> 30) & 1) == 0) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] >> (imm & 0x1F);
                // cerr<< "srli ";
            } // Shift Right Logical Immediate
            else if (funct3 == 0b101 && ((instruction >> 30) & 1) == 1) {
                if (rd != 0)
                    cpu.regs[rd] =
                        static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) >> (imm & 0x1F));
                // cerr<< "srai ";
            } // Shift Right Arithmetic Immediate
            else {
                std::cerr << "Error: Unknown I-type instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;

                is_halted = true;
            }

            // cerr<< std::dec << std::dec << " " << rd << " " << rs1 << " " << imm << "\n";
            break;
        } // I-TYPE Immediate

        case 0b0110011: {
            uint32_t rd = (instruction >> 7) & 0x1F;
            uint32_t funct3 = (instruction >> 12) & 0x7;
            uint32_t rs1 = (instruction >> 15) & 0x1F;
            uint32_t rs2 = (instruction >> 20) & 0x1F;
            uint32_t funct7 = (instruction >> 25) & 0x7F;

            if (funct3 == 0 && funct7 == 0) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] + cpu.regs[rs2];
                // cerr<< "add ";
            } // add
            else if (funct3 == 0 && funct7 == 0b0100000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] - cpu.regs[rs2];
                // cerr<< "sub ";
            } // sub
            else if (funct3 == 0b001 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] << (cpu.regs[rs2] & 0x1F);
                // cerr<< "sll ";

            } // Shift Left Logical
            else if (funct3 == 0b010 && funct7 == 0b0000000) {
                if (static_cast<int32_t>(cpu.regs[rs1]) < static_cast<int32_t>(cpu.regs[rs2])) {
                    if (rd != 0)
                        cpu.regs[rd] = 1;
                } else {
                    if (rd != 0)
                        cpu.regs[rd] = 0;
                }
                // cerr<< "slt ";

            } // Set if Less Than
            else if (funct3 == 0b011 && funct7 == 0b0000000) {
                if (cpu.regs[rs1] < cpu.regs[rs2]) {
                    if (rd != 0)
                        cpu.regs[rd] = 1;
                } else {
                    if (rd != 0)
                        cpu.regs[rd] = 0;
                }
                // cerr<< "sltu ";
            } // Set if Less Than, Unsigned
            else if (funct3 == 0b100 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] ^ cpu.regs[rs2];
                // cerr<< "xor ";
            } // XOR
            else if (funct3 == 0b101 && funct7 == 0b0100000) {
                if (rd != 0)
                    cpu.regs[rd] = static_cast<uint32_t>(static_cast<int32_t>(cpu.regs[rs1]) >>
                                                         (cpu.regs[rs2] & 0x1F));

                // cerr<< "sra ";
            } // Shift Right Arithmetic
            else if (funct3 == 0b101 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] >> (cpu.regs[rs2] & 0x1F);

                // cerr<< "srl ";

            } // Shift Right Logical
            else if (funct3 == 0b110 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] | cpu.regs[rs2];
                // cerr<< "or ";
            } // OR
            else if (funct3 == 0b111 && funct7 == 0b0000000) {
                if (rd != 0)
                    cpu.regs[rd] = cpu.regs[rs1] & cpu.regs[rs2];
                // cerr<< "and ";
            } // AND
            else {
                std::cerr << "Error: Unknown R-type instruction " << std::hex << instruction
                          << " at pc " << cpu.pc << std::endl;
                is_halted = true;
            }
            // cerr<< " " << rd << " " << rs1 << " " << rs2 << " " << funct3 << " " << funct7 <<
            // "\n";

            break;

        } // R-TYPE
        default: {
            std::cerr << std::dec << "Error: Unknown instruction " << std::hex << instruction
                      << " at pc " << cpu.pc << std::endl;
            is_halted = true;
            break;
        }
        }
    }

    void print_result() {
        uint32_t result = cpu.regs[10] & 0xFF;
        std::cout << result << std::endl;
    }
};

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    RISCV_Simulator simulator;

    simulator.load_program();

    simulator.run();

    return 0;
}