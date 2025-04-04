#ifndef EXECUTION_HPP
#define EXECUTION_HPP

#include <string>
#include <map>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include "types.hpp"

using namespace riscv;

inline void initialiseRegisters(uint32_t* registers) {
    for (int i = 0; i < 32; i++) registers[i] = 0x00000000;
    registers[2] = 0x7FFFFFDC;
    registers[3] = 0x10000000;
    registers[10] = 0x00000001;
    registers[11] = 0x7FFFFFDC;
}

inline bool isValidAddress(uint32_t addr, uint32_t size) {
    if (addr + size > MEMORY_SIZE || addr + size < 0x0) {
        std::stringstream ss;
        ss << "Memory access error: Address 0x" << std::hex << addr << " with size " << std::dec << size 
           << " is outside of valid memory range (0x0-0x" << std::hex << MEMORY_SIZE << ")";
        logs[300] = ss.str();
        throw std::runtime_error(ss.str());
    }
    return true;
}

inline InstructionType classifyInstructions(uint32_t instHex) {
    uint32_t opcode = instHex & 0x7F;
    uint32_t func3 = (instHex >> 12) & 0x7;
    uint32_t func7 = (instHex >> 25) & 0x7F;

    auto rTypeEncoding = RTypeInstructions::getEncoding();
    for (const auto &[name, op] : rTypeEncoding.opcodeMap) {
        if (op == opcode && rTypeEncoding.func3Map.at(name) == func3 && rTypeEncoding.func7Map.at(name) == func7) return InstructionType::R;
    }

    auto iTypeEncoding = ITypeInstructions::getEncoding();
    for (const auto &[name, op] : iTypeEncoding.opcodeMap) {
        if (op == opcode && iTypeEncoding.func3Map.at(name) == func3) return InstructionType::I;
    }

    auto sTypeEncoding = STypeInstructions::getEncoding();
    for (const auto &[name, op] : sTypeEncoding.opcodeMap) {
        if (op == opcode && sTypeEncoding.func3Map.at(name) == func3) return InstructionType::S;
    }

    auto sbTypeEncoding = SBTypeInstructions::getEncoding();
    for (const auto &[name, op] : sbTypeEncoding.opcodeMap) {
        if (op == opcode && sbTypeEncoding.func3Map.at(name) == func3) return InstructionType::SB;
    }

    auto uTypeEncoding = UTypeInstructions::getEncoding();
    for (const auto &[name, op] : uTypeEncoding.opcodeMap) {
        if (op == opcode) return InstructionType::U;
    }

    auto ujTypeEncoding = UJTypeInstructions::getEncoding();
    for (const auto &[name, op] : ujTypeEncoding.opcodeMap) {
        if (op == opcode) return InstructionType::UJ;
    }
    
    std::stringstream ss;
    ss << "Instruction 0x" << std::hex << instHex << " could not be classified: Invalid opcode (0x" << opcode << ")";
    logs[400] = ss.str();
    throw std::runtime_error(ss.str());
}

inline void fetchInstruction(InstructionNode* node , uint32_t& PC, bool& running, std::map<uint32_t, std::pair<uint32_t, std::string>>& textMap) {
    if (!isValidAddress(PC, 4)) {
        std::ostringstream oss;
        oss << "Fetch error: Invalid PC address 0x" << std::hex << PC;
        logs[400] = oss.str();
        throw std::runtime_error(oss.str());
    }
    auto it = textMap.find(PC);
    if (it != textMap.end()) {
        node->instruction = it->second.first;
        node->instructionType = classifyInstructions(node->instruction);
        node->PC = PC;
        PC += INSTRUCTION_SIZE;
    } else {
        node->instruction = 0;
        running = false;
    }
}

inline void decodeInstruction(InstructionNode* node, InstructionRegisters& instructionRegisters, uint32_t* registers) {
    node->opcode = node->instruction & 0x7F;
    node->rd = (node->instruction >> 7) & 0x1F;
    node->func3 = (node->instruction >> 12) & 0x7;
    node->rs1 = (node->instruction >> 15) & 0x1F;
    node->rs2 = (node->instruction >> 20) & 0x1F;
    node->func7 = (node->instruction >> 25) & 0x7F;

    switch (node->instructionType) {
        case InstructionType::R:
        case InstructionType::I:
        case InstructionType::S:
        case InstructionType::SB:
            instructionRegisters.RA = registers[node->rs1];
            break;
        case InstructionType::U:
        case InstructionType::UJ:
            instructionRegisters.RA = 0;
            break;
        default:
            instructionRegisters.RA = 0;
            break;
    }
    
    switch (node->instructionType) {
        case InstructionType::R:
            instructionRegisters.RB = registers[node->rs2];
            break;

        case InstructionType::I: {
            int32_t imm = (node->instruction >> 20) & 0xFFF;
            if (imm & 0x800) imm |= 0xFFFFF000;
            instructionRegisters.RB = imm;
            break;
        }

        case InstructionType::S: {
            int32_t imm = ((node->instruction >> 25) << 5) | ((node->instruction >> 7) & 0x1F);
            if (imm & 0x800) imm |= 0xFFFFF000;
            instructionRegisters.RB = imm;
            instructionRegisters.RM = registers[node->rs2];
            break;
        }

        case InstructionType::SB: {
            int32_t imm = ((node->instruction >> 31) << 12) | (((node->instruction >> 7) & 1) << 11) | (((node->instruction >> 25) & 0x3F) << 5) | (((node->instruction >> 8) & 0xF) << 1);
            if (imm & 0x1000) imm |= 0xFFFFE000;
            instructionRegisters.RB = imm;
            break;
        }

        case InstructionType::U:
            instructionRegisters.RB = node->instruction & 0xFFFFF000;
            break;

        case InstructionType::UJ: {
            int32_t imm = ((node->instruction >> 31) << 20) | (((node->instruction >> 12) & 0xFF) << 12) | (((node->instruction >> 20) & 1) << 11) | (((node->instruction >> 21) & 0x3FF) << 1);
            if (imm & 0x100000) imm |= 0xFFE00000;
            instructionRegisters.RB = imm;
            break;
        }

        default:
            logs[400] = "The instruction is not decoded";
            throw std::runtime_error("Error: The instruction is not decoded");
    }
}

inline void decodeInstructionWithForwarding(InstructionNode* node, InstructionRegisters& regs, const uint32_t* registers, bool dataForwarding) {
    uint32_t opcode = node->instruction & 0x7F;
    uint32_t funct3 = (node->instruction >> 12) & 0x7;
    uint32_t funct7 = (node->instruction >> 25) & 0x7F;
    uint32_t rd = (node->instruction >> 7) & 0x1F;
    uint32_t rs1 = (node->instruction >> 15) & 0x1F;
    uint32_t rs2 = (node->instruction >> 20) & 0x1F;

    node->opcode = opcode;
    node->func3 = funct3;
    node->func7 = funct7;
    node->rd = rd;
    node->rs1 = rs1;
    node->rs2 = rs2;

    if (node->instructionType == InstructionType::R || 
        node->instructionType == InstructionType::I || 
        node->instructionType == InstructionType::S || 
        node->instructionType == InstructionType::SB) {
        
        if (rs1 != 0) {
            if (dataForwarding && rs1 == regs.RZ) {
                regs.RA = regs.RY;
            } else {
                regs.RA = registers[rs1];
            }
        } else {
            regs.RA = 0;
        }
    } else {
        regs.RA = 0;
    }

    if (node->instructionType == InstructionType::R) {
        if (rs2 != 0) {
            if (dataForwarding && rs2 == regs.RZ) {
                regs.RB = regs.RY;
            } else {
                regs.RB = registers[rs2];
            }
        } else {
            regs.RB = 0;
        }
    }
}

inline void executeInstruction(InstructionNode* node, InstructionRegisters& instructionRegisters, uint32_t* registers, uint32_t& PC) {
    uint32_t result = 0;

    auto rTypeEncoding = RTypeInstructions::getEncoding();
    for (const auto &[name, op] : rTypeEncoding.opcodeMap) {
        if (op == node->opcode && rTypeEncoding.func3Map.at(name) == node->func3 && rTypeEncoding.func7Map.at(name) == node->func7) {
            if (name == "add") {
                result = instructionRegisters.RA + instructionRegisters.RB;
            } else if (name == "sub") {
                result = instructionRegisters.RA - instructionRegisters.RB;
            } else if (name == "mul") {
                result = instructionRegisters.RA * instructionRegisters.RB;
            } else if (name == "div") {
                if (instructionRegisters.RB == 0) result = 0xFFFFFFFF;
                else result = static_cast<uint32_t>(static_cast<int32_t>(instructionRegisters.RA) / static_cast<int32_t>(instructionRegisters.RB));
            } else if (name == "rem") {
                if (instructionRegisters.RB == 0) result = instructionRegisters.RA;
                else result = static_cast<uint32_t>(static_cast<int32_t>(instructionRegisters.RA) % static_cast<int32_t>(instructionRegisters.RB));
            } else if (name == "and") {
                result = instructionRegisters.RA & instructionRegisters.RB;
            } else if (name == "or") {
                result = instructionRegisters.RA | instructionRegisters.RB;
            } else if (name == "xor") {
                result = instructionRegisters.RA ^ instructionRegisters.RB;
            } else if (name == "sll") {
                result = instructionRegisters.RA << (instructionRegisters.RB & 0x1F);
            } else if (name == "srl") {
                result = instructionRegisters.RA >> (instructionRegisters.RB & 0x1F);
            } else if (name == "sra") {
                result = static_cast<uint32_t>(static_cast<int32_t>(instructionRegisters.RA) >> (instructionRegisters.RB & 0x1F));
            } else if (name == "slt") {
                result = (static_cast<int32_t>(instructionRegisters.RA) < static_cast<int32_t>(instructionRegisters.RB)) ? 1 : 0;
            }
            instructionRegisters.RY = result;
            return;
        }
    }

    auto iTypeEncoding = ITypeInstructions::getEncoding();
    for (const auto &[name, op] : iTypeEncoding.opcodeMap) {
        if (op == node->opcode && iTypeEncoding.func3Map.at(name) == node->func3) {
            if (name == "addi") {
                result = instructionRegisters.RA + instructionRegisters.RB;
            } else if (name == "andi") {
                result = instructionRegisters.RA & instructionRegisters.RB;
            } else if (name == "ori") {
                result = instructionRegisters.RA | instructionRegisters.RB;
            } else if (name == "xori") {
                result = instructionRegisters.RA ^ instructionRegisters.RB;
            } else if (name == "slti") {
                result = (static_cast<int32_t>(instructionRegisters.RA) < static_cast<int32_t>(instructionRegisters.RB)) ? 1 : 0;
            } else if (name == "sltiu") {
                result = (instructionRegisters.RA < instructionRegisters.RB) ? 1 : 0;
            } else if (name == "slli") {
                result = instructionRegisters.RA << (instructionRegisters.RB & 0x1F);
            } else if (name == "srli") {
                result = instructionRegisters.RA >> (instructionRegisters.RB & 0x1F);
            } else if (name == "srai") {
                result = static_cast<uint32_t>(static_cast<int32_t>(instructionRegisters.RA) >> (instructionRegisters.RB & 0x1F));
            } else if (name == "lb" || name == "lh" || name == "lw") {
                result = instructionRegisters.RA + instructionRegisters.RB;
                instructionRegisters.RY = result;
                return;
            } else if (name == "ld") {
                logs[400] = "ld instruction not supported";
                throw std::runtime_error("Error: ld instruction not supported");
            } else if (name == "jalr") {
                result = PC;
                PC = (instructionRegisters.RA + instructionRegisters.RB) & ~1;
            }
            instructionRegisters.RY = result;
            return;
        }
    }

    auto sTypeEncoding = STypeInstructions::getEncoding();
    for (const auto &[name, op] : sTypeEncoding.opcodeMap) {
        if (op == node->opcode && sTypeEncoding.func3Map.at(name) == node->func3) {
            if (name == "sb" || name == "sh" || name == "sw" || name == "sd") {
                result = instructionRegisters.RA + instructionRegisters.RB;
                instructionRegisters.RY = result;
            }
            return;
        }
    }

    auto sbTypeEncoding = SBTypeInstructions::getEncoding();
    for (const auto &[name, op] : sbTypeEncoding.opcodeMap) {
        if (op == node->opcode && sbTypeEncoding.func3Map.at(name) == node->func3) {
            bool branchTaken = false;
            if (name == "beq") {
                branchTaken = (instructionRegisters.RA == instructionRegisters.RB);
            } else if (name == "bne") {
                branchTaken = (instructionRegisters.RA != instructionRegisters.RB);
            } else if (name == "blt") {
                branchTaken = (static_cast<int32_t>(instructionRegisters.RA) < static_cast<int32_t>(instructionRegisters.RB));
            } else if (name == "bge") {
                branchTaken = (static_cast<int32_t>(instructionRegisters.RA) >= static_cast<int32_t>(instructionRegisters.RB));
            } else if (name == "bltu") {
                branchTaken = (instructionRegisters.RA < instructionRegisters.RB);
            } else if (name == "bgeu") {
                branchTaken = (instructionRegisters.RA >= instructionRegisters.RB);
            }
            if (branchTaken) {
                PC = node->PC + instructionRegisters.RB;
            }
            instructionRegisters.RY = branchTaken;
            return;
        }
    }

    auto uTypeEncoding = UTypeInstructions::getEncoding();
    for (const auto &[name, op] : uTypeEncoding.opcodeMap) {
        if (op == node->opcode) {
            if (name == "lui") {
                result = instructionRegisters.RB;
            } else if (name == "auipc") {
                result = node->PC + instructionRegisters.RB;
            }
            instructionRegisters.RY = result;
            return;
        }
    }

    auto ujTypeEncoding = UJTypeInstructions::getEncoding();
    for (const auto &[name, op] : ujTypeEncoding.opcodeMap) {
        if (op == node->opcode) {
            if (name == "jal") {
                result = PC;
                PC = node->PC + instructionRegisters.RB;
            }
            instructionRegisters.RY = result;
            return;
        }
    }
    logs[400] = "The Instruction is not executed";
    throw std::runtime_error("Error: The Instruction is not executed");
}

inline void memoryAccess(InstructionNode* node, InstructionRegisters& instructionRegisters, uint32_t* registers, std::unordered_map<uint32_t, uint8_t>& dataMap) {
    uint32_t address = instructionRegisters.RY;
    instructionRegisters.RZ = instructionRegisters.RY;
    
    auto iTypeEncoding = ITypeInstructions::getEncoding();
    for (const auto &[name, op] : iTypeEncoding.opcodeMap) {
        if (op == node->opcode && iTypeEncoding.func3Map.at(name) == node->func3) {
            if (name == "lb") {
                isValidAddress(address, 1);
                instructionRegisters.RZ = dataMap.count(address) ? static_cast<int8_t>(dataMap[address]) : 0;
            } else if (name == "lh") {
                isValidAddress(address, 2);
                instructionRegisters.RZ = static_cast<int16_t>(
                    (dataMap.count(address + 1) ? dataMap[address + 1] : 0) << 8 |
                    (dataMap.count(address) ? dataMap[address] : 0)
                );
            } else if (name == "lw") {
                isValidAddress(address, 4);
                instructionRegisters.RZ = 
                    ((dataMap.count(address + 3) ? dataMap[address + 3] : 0) << 24) |
                    ((dataMap.count(address + 2) ? dataMap[address + 2] : 0) << 16) |
                    ((dataMap.count(address + 1) ? dataMap[address + 1] : 0) << 8)  |
                    (dataMap.count(address) ? dataMap[address] : 0);
            }
            return;
        }
    }

    auto sTypeEncoding = STypeInstructions::getEncoding();
    for (const auto &[name, op] : sTypeEncoding.opcodeMap) {
        if (op == node->opcode && sTypeEncoding.func3Map.at(name) == node->func3) {
            uint32_t valueToStore = instructionRegisters.RM;
            if (name == "sb") {
                dataMap[address] = valueToStore & 0xFF;
            } else if (name == "sh") {
                dataMap[address] = valueToStore & 0xFF;
                dataMap[address + 1] = (valueToStore >> 8) & 0xFF;
            } else if (name == "sw") {
                dataMap[address] = valueToStore & 0xFF;
                dataMap[address + 1] = (valueToStore >> 8) & 0xFF;
                dataMap[address + 2] = (valueToStore >> 16) & 0xFF;
                dataMap[address + 3] = (valueToStore >> 24) & 0xFF;
            }
            return;
        }
    }
}

inline void writeback(InstructionNode* node, InstructionRegisters& instructionRegisters, uint32_t* registers) {
    if (node->rd != 0) {
        switch (node->instructionType) {
            case InstructionType::R:
            case InstructionType::I:
            case InstructionType::U:
            case InstructionType::UJ:
                registers[node->rd] = instructionRegisters.RZ;
                break;
            case InstructionType::S:
            case InstructionType::SB:
                break;
            default:
                logs[400] = "Unknown instruction type in writeback";
                throw std::runtime_error("Error: Unknown instruction type in writeback");
        }
    }
    registers[0] = 0;
}
    
inline std::string parseInstructions(uint32_t instHex) {
    uint32_t opcode = instHex & 0x7F;
    uint32_t rd = (instHex >> 7) & 0x1F;
    uint32_t func3 = (instHex >> 12) & 0x7;
    uint32_t rs1 = (instHex >> 15) & 0x1F;
    uint32_t rs2 = (instHex >> 20) & 0x1F;
    uint32_t func7 = (instHex >> 25) & 0x7F;

    auto rTypeEncoding = RTypeInstructions::getEncoding();
    for (const auto &[name, op] : rTypeEncoding.opcodeMap) {
        if (op == opcode && rTypeEncoding.func3Map.at(name) == func3 && rTypeEncoding.func7Map.at(name) == func7) {
            std::stringstream ss;
            ss << name << " x" << rd << ", x" << rs1 << ", x" << rs2;
            return ss.str();
        }
    }

    auto iTypeEncoding = ITypeInstructions::getEncoding();
    for (const auto &[name, op] : iTypeEncoding.opcodeMap) {
        if (op == opcode && iTypeEncoding.func3Map.at(name) == func3) {
            int32_t imm = (instHex >> 20);
            if (imm & 0x800) imm |= 0xFFFFF000;
            std::stringstream ss;
            ss << name << " x" << rd << ", x" << rs1 << ", " << imm;
            return ss.str();
        }
    }

    auto sTypeEncoding = STypeInstructions::getEncoding();
    for (const auto &[name, op] : sTypeEncoding.opcodeMap) {
        if (op == opcode && sTypeEncoding.func3Map.at(name) == func3) {
            int32_t imm = ((instHex >> 25) << 5) | ((instHex >> 7) & 0x1F);
            if (imm & 0x800) imm |= 0xFFFFF000;
            std::stringstream ss;
            ss << name << " x" << rs2 << ", " << imm << "(x" << rs1 << ")";
            return ss.str();
        }
    }

    auto sbTypeEncoding = SBTypeInstructions::getEncoding();
    for (const auto &[name, op] : sbTypeEncoding.opcodeMap) {
        if (op == opcode && sbTypeEncoding.func3Map.at(name) == func3) {
            int32_t imm = ((instHex >> 31) << 12) | (((instHex >> 7) & 1) << 11) | (((instHex >> 25) & 0x3F) << 5) | (((instHex >> 8) & 0xF) << 1);
            if (imm & 0x1000) imm |= 0xFFFFE000;
            std::stringstream ss;
            ss << name << " x" << rs1 << ", x" << rs2 << ", " << imm;
            return ss.str();
        }
    }

    auto uTypeEncoding = UTypeInstructions::getEncoding();
    for (const auto &[name, op] : uTypeEncoding.opcodeMap) {
        if (op == opcode) {
            uint32_t imm = instHex & 0xFFFFF000;
            std::stringstream ss;
            ss << name << " x" << rd << ", " << (imm >> 12);
            return ss.str();
        }
    }

    auto ujTypeEncoding = UJTypeInstructions::getEncoding();
    for (const auto &[name, op] : ujTypeEncoding.opcodeMap) {
        if (op == opcode) {
            int32_t imm = ((instHex >> 31) << 20) | (((instHex >> 12) & 0xFF) << 12) | (((instHex >> 20) & 1) << 11) | (((instHex >> 21) & 0x3FF) << 1);
            if (imm & 0x100000) imm |= 0xFFE00000;
            std::stringstream ss;
            ss << name << " x" << rd << ", " << imm;
            return ss.str();
        }
    }
    logs[400] = "The Instruction is not valid";
    throw std::runtime_error("Error: The Instruction is not valid");
}

#endif