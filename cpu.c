#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "cpu.h"
#include "memory.h"

#define WORD_SIZE      4
#define HALF_WORD_SIZE 2
#define BYTE_SIZE      1

#define CC_UNSET 0
#define CC_SET   1
#define CC_UNMOD 2

#define THUMB_ACTIVATED             (cpu->registers.cpsr >> 5 & 1)
#define PROCESSOR_MODE              (cpu->registers.cpsr & 0x1F)

#define SET_PROCESSOR_MODE(mode) cpu->registers.cpsr &= ~0x1F;\
                                 cpu->registers.cpsr |= mode;

typedef bool Bit;
typedef uint32_t Word;

static inline Word get_reg(uint8_t reg_id);
static inline Word get_psr_reg(void);
static inline int tick_cpu(void);

typedef enum {
    SHIFT_TYPE_LSL,
    SHIFT_TYPE_LSR,
    SHIFT_TYPE_ASR,
    SHIFT_TYPE_ROR
} ShiftType;

typedef enum {
    Branch,
    BranchExchange,
    BlockDataTransfer,
    HalfwordDataTransfer,
    SingleDataTransfer,
    DataProcessing,
    Multiply,
    SWP,
    SoftwareInterrupt,
    SingleDataSwap,
    MSR,
    MRS,

    THUMB_1,
    THUMB_2,
    THUMB_3,
    THUMB_4,
    THUMB_5,
    THUMB_6,
    THUMB_7,
    THUMB_8,
    THUMB_9,
    THUMB_10,
    THUMB_11,
    THUMB_12,
    THUMB_13,
    THUMB_14,
    THUMB_15,
    THUMB_16,
    THUMB_17,
    THUMB_18,
    THUMB_19,

    ARM_BAD_INSTR,
    THUMB_BAD_INSTR,
} InstrType;

typedef enum {
    User        = 0x10,
    FIQ         = 0x11,
    IRQ         = 0x12,
    Supervisor  = 0x13,
    Abort       = 0x17,
    Undefined   = 0x1B,
    System      = 0x1F
} Mode;

typedef enum {
    N, Z, C, V
} Flag;

typedef struct {
    Word r0;
    Word r1;
    Word r2;
    Word r3;
    Word r4;
    Word r5;
    Word r6;
    Word r7;
    Word r8;
    Word r9;
    Word r10;
    Word r11;
    Word r12;

    Word r13;  // SP (Stack pointer)
    Word r14;  // LR (Link register)
    Word r15;  // PC (Program counter)
    Word r8_fiq;
    Word r9_fiq;
    Word r10_fiq;
    Word r11_fiq;
    Word r12_fiq;
    Word r13_fiq;
    Word r14_fiq;
    Word r13_svc;
    Word r14_svc;
    Word r13_abt;
    Word r14_abt;
    Word r13_irq;
    Word r14_irq;
    Word r13_und;
    Word r14_und;

    Word cpsr;
    Word spsr_fiq;
    Word spsr_svc;
    Word spsr_abt;
    Word spsr_irq;
    Word spsr_und;
} RegisterSet;

typedef struct {
    RegisterSet registers;
    Word pipeline;
    uint8_t shifter_carry;

    Memory *mem;
} CPU;

CPU *cpu;

char* cond_to_cstr(uint8_t opcode) {
    switch (opcode) {
    case 0x0: return "EQ";
    case 0x1: return "NE";
    case 0x2: return "CS";
    case 0x3: return "CC";
    case 0x4: return "MI";
    case 0x5: return "PL";
    case 0x6: return "VS";
    case 0x7: return "VC";
    case 0x8: return "HI";
    case 0x9: return "LS";
    case 0xA: return "GE";
    case 0xB: return "LT";
    case 0xC: return "GT";
    case 0xD: return "LE";
    case 0xE: return "";
    }
    return "";
}

char* amod_to_cstr(Bit p, Bit u) {
    switch ((p << 1) | u) {
    case 0x0: return "DA";
    case 0x1: return "IA";
    case 0x2: return "DB";
    case 0x3: return "IB";
    }
    return "";
}

char* register_to_cstr(uint8_t reg_id) {
    switch (reg_id) {
    case 0x0: return "r0";
    case 0x1: return "r1";
    case 0x2: return "r2";
    case 0x3: return "r3";
    case 0x4: return "r4";
    case 0x5: return "r5";
    case 0x6: return "r6";
    case 0x7: return "r7";
    case 0x8: return "r8";
    case 0x9: return "r9";
    case 0xA: return "r10";
    case 0xB: return "r11";
    case 0xC: return "ip";
    case 0xD: return "sp";
    case 0xE: return "lr";
    case 0xF: return "pc";
    }
    return "";
}

char* processor_mode_to_cstr(Mode mode) {
    switch (mode) {
    case User:
    case System: return "fc";
    case Supervisor: return "svc";
    case FIQ: return "fiq";
    case IRQ: return "irq";
    case Abort: return "abt";
    case Undefined: return "und";
    }
}

void print_dump(void) {
    printf("\n\n==== DUMP ====\n");
    for (int i = 0; i < 16; i++) {
        printf("r%d: %08X\n", i, get_reg(i));
    }
    printf("cpsr: %08X\n", cpu->registers.cpsr);
    printf("current psr: %08X\n", get_psr_reg());
    if (cpu->pipeline) printf("pipeline (next instruction): %08X\n", cpu->pipeline);
    printf("\n");
}

static inline Word get_psr_reg(void) {
    switch (PROCESSOR_MODE) {
    case User:
    case System: return cpu->registers.cpsr;
    case FIQ: return cpu->registers.spsr_fiq;
    case IRQ: return cpu->registers.spsr_irq;
    case Supervisor: return cpu->registers.spsr_svc;
    case Abort: return cpu->registers.spsr_abt;
    case Undefined: return cpu->registers.spsr_und;
    }
}

static inline void set_psr_reg(Word val) {
    switch (PROCESSOR_MODE) {
    case User:
    case System:
        cpu->registers.cpsr = val;
        break;
    case FIQ:
        cpu->registers.spsr_fiq = val;
        break;
    case IRQ:
        cpu->registers.spsr_irq = val;
        break;
    case Supervisor:
        cpu->registers.spsr_svc = val;
        break;
    case Abort:
        cpu->registers.spsr_abt = val;
        break;
    case Undefined:
        cpu->registers.spsr_und = val;
        break;
    }
}

static inline Bit get_cc(Flag cc) {
    switch (cc) {
    case N: return (get_psr_reg() >> 31) & 1;
    case Z: return (get_psr_reg() >> 30) & 1;
    case C: return (get_psr_reg() >> 29) & 1;
    case V: return (get_psr_reg() >> 28) & 1;
    }
}

static inline void set_cc(uint8_t n, int z, int c, int v) {
    Word curr_psr = get_psr_reg();

    if (n != CC_UNMOD) curr_psr = n ? (1 << 31) | curr_psr : ~(1 << 31) & curr_psr;
    if (z != CC_UNMOD) curr_psr = z ? (1 << 30) | curr_psr : ~(1 << 30) & curr_psr;
    if (c != CC_UNMOD) curr_psr = c ? (1 << 29) | curr_psr : ~(1 << 29) & curr_psr;
    if (v != CC_UNMOD) curr_psr = v ? (1 << 28) | curr_psr : ~(1 << 28) & curr_psr;

    set_psr_reg(curr_psr);
}

static inline bool eval_cond(uint8_t opcode) {
    switch (opcode) {
    case 0x0: return get_cc(Z);
    case 0x1: return !get_cc(Z);
    case 0x2: return get_cc(C);
    case 0x3: return !get_cc(C);
    case 0x4: return get_cc(N);
    case 0x5: return !get_cc(N);
    case 0x6: return get_cc(V);
    case 0x7: return !get_cc(V);
    case 0x8: return get_cc(C) & !get_cc(Z);
    case 0x9: return !get_cc(C) | get_cc(Z);
    case 0xA: return get_cc(N) == get_cc(V);
    case 0xB: return get_cc(N) ^ get_cc(V);
    case 0xC: return !get_cc(Z) && (get_cc(N) == get_cc(V));
    case 0xD: return get_cc(Z) || (get_cc(N) ^ get_cc(V));
    case 0xE: return true;
    }
}

static inline Word get_reg(uint8_t reg_id) {
    switch (reg_id) {
    case 0x0: return cpu->registers.r0;
    case 0x1: return cpu->registers.r1;
    case 0x2: return cpu->registers.r2;
    case 0x3: return cpu->registers.r3;
    case 0x4: return cpu->registers.r4;
    case 0x5: return cpu->registers.r5;
    case 0x6: return cpu->registers.r6;
    case 0x7: return cpu->registers.r7;
    case 0x8: return cpu->registers.r8;
    case 0x9: return cpu->registers.r9;
    case 0xA: return cpu->registers.r10;
    case 0xB: return cpu->registers.r11;
    case 0xC: return cpu->registers.r12;
    case 0xD:
        switch (PROCESSOR_MODE) {
        case User:
        case System: return cpu->registers.r13;
        case FIQ: return cpu->registers.r13_fiq;
        case IRQ: return cpu->registers.r13_irq;
        case Supervisor: return cpu->registers.r13_svc;
        case Abort: return cpu->registers.r13_abt;
        case Undefined: return cpu->registers.r13_und;
        }
    case 0xE:
        switch (PROCESSOR_MODE) {
        case User:
        case System: return cpu->registers.r14;
        case FIQ: return cpu->registers.r14_fiq;
        case IRQ: return cpu->registers.r14_irq;
        case Supervisor: return cpu->registers.r14_svc;
        case Abort: return cpu->registers.r14_abt;
        case Undefined: return cpu->registers.r14_und;
        }
    case 0xF: return cpu->registers.r15;
    }
}

static inline void set_reg(uint8_t reg_id, Word val) {
    switch (reg_id) {
    case 0x0:
        cpu->registers.r0 = val;
        break;
    case 0x1:
        cpu->registers.r1 = val;
        break;
    case 0x2:
        cpu->registers.r2 = val;
        break;
    case 0x3:
        cpu->registers.r3 = val;
        break;
    case 0x4:
        cpu->registers.r4 = val;
        break;
    case 0x5:
        cpu->registers.r5 = val;
        break;
    case 0x6:
        cpu->registers.r6 = val;
        break;
    case 0x7:
        cpu->registers.r7 = val;
        break;
    case 0x8:
        cpu->registers.r8 = val;
        break;
    case 0x9:
        cpu->registers.r9 = val;
        break;
    case 0xA:
        cpu->registers.r10 = val;
        break;
    case 0xB:
        cpu->registers.r11 = val;
        break;
    case 0xC:
        cpu->registers.r12 = val;
        break;
    case 0xD:
        switch (PROCESSOR_MODE) {
        case User:
        case System:
            cpu->registers.r13 = val;
            break;
        case FIQ:
            cpu->registers.r13_fiq = val;
            break;
        case IRQ:
            cpu->registers.r13_irq = val;
            break;
        case Supervisor:
            cpu->registers.r13_svc = val;
            break;
        case Abort:
            cpu->registers.r13_abt = val;
            break;
        case Undefined:
            cpu->registers.r13_und = val;
            break;
        }
        break;
    case 0xE:
        switch (PROCESSOR_MODE) {
        case User:
        case System:
            cpu->registers.r14 = val;
            break;
        case FIQ:
            cpu->registers.r14_fiq = val;
            break;
        case IRQ:
            cpu->registers.r14_irq = val;
            break;
        case Supervisor:
            cpu->registers.r14_svc = val;
            break;
        case Abort:
            cpu->registers.r14_abt = val;
            break;
        case Undefined:
            cpu->registers.r14_und = val;
            break;
        }
        break;
    case 0xF:
        cpu->registers.r15 = val;
        break;
    }
}

static inline Word fetch() {
    Word instr;

    if (THUMB_ACTIVATED) {
        instr = read_mem(cpu->mem, cpu->registers.r15, HALF_WORD_SIZE);
        cpu->registers.r15 += HALF_WORD_SIZE;
    } else {
        instr = read_mem(cpu->mem, cpu->registers.r15, WORD_SIZE);
        cpu->registers.r15 += WORD_SIZE;
    }

    return instr;
}

static inline InstrType decode(Word instr) {
    cpu->pipeline = fetch();

    if (THUMB_ACTIVATED) {
        switch ((instr >> 13) & 0x7) {
        case 0x0:
            if (((instr >> 11) & 0x3) == 0x3) 
                return THUMB_2;
            return THUMB_1;
        case 0x1: return THUMB_3;
        case 0x2:
            switch ((instr >> 10) & 0x7) {
            case 0x0: return THUMB_4;
            case 0x1: return THUMB_5;
            case 0x2:
            case 0x3: return THUMB_6;
            }
            if ((instr >> 9) & 1)
                return THUMB_8;
            return THUMB_7;
        case 0x3: return THUMB_9;
        case 0x4:
            if ((instr >> 12) & 1) 
                return THUMB_11;
            return THUMB_10;
        case 0x5:
            if (((instr >> 12) & 1) == 0) 
                return THUMB_12;
            if (((instr >> 9) & 0x3) == 0x2)
                return THUMB_14;
            return THUMB_13;
        case 0x6:
            switch ((instr >> 12) & 0x3) {
            case 0x0:
                return THUMB_15;
            case 0x1:
                switch ((instr >> 8) & 0xFF) {
                case 0b11011111: return THUMB_17;
                case 0b10111110: 
                    fprintf(stderr, "[THUMB] BKPT not supported!\n");
                    exit(1);
                }
                return THUMB_16;
            }
        case 0x7:
            if ((instr >> 12) & 1)
                return THUMB_19;
            return THUMB_18;
        }

        return THUMB_BAD_INSTR;
    } else {
        switch ((instr >> 25) & 0x7) {
        case 0x0:
            switch ((instr >> 4) & 0xF) {
            case 0x1:
                if (((instr >> 8) & 0xF) == 0xF) return BranchExchange;
                goto is_psr_transfer_or_data_transfer;
            case 0x9:
                switch ((instr >> 23) & 0x3) {
                case 0x0:
                case 0x1: return Multiply;
                case 0x2: return SingleDataSwap;
                }
                fprintf(stderr, "invalid instruction: %08X!\n", instr);
                exit(1);
            case 0xB:
            case 0xD: return HalfwordDataTransfer;
            default: goto is_psr_transfer_or_data_transfer;
            }
        case 0x1: goto is_psr_transfer_or_data_transfer;
        case 0x2:
        case 0x3: return SingleDataTransfer;
        case 0x4: return BlockDataTransfer;
        case 0x5: return Branch;
        case 0x6:
            fprintf(stderr, "coprocessor data transfer\n");
            exit(1);
        case 0x7:
            if ((instr >> 24) & 1) return SoftwareInterrupt;
            fprintf(stderr, "[ARM] debug not supported!\n", instr);
            exit(1);
        default:
            fprintf(stderr, "[ARM] invalid instruction: #0x%08X\n", instr);
            exit(1);
        }

        return ARM_BAD_INSTR;
    }

    // if ALU instruction for (TEQ,TST,CMP,CMN) and S = 1 -> PSR transfer
    is_psr_transfer_or_data_transfer: {
        uint8_t opcode = (instr >> 21) & 0xF;
        switch (opcode) {
            case 0x8:
            case 0x9:
            case 0xA:
            case 0xB:
                if (((instr >> 20) & 1) == 0) {
                    if (opcode & 1)
                        return MSR;
                    return MRS;
                };
            default: return DataProcessing;
        }
    }
}

static inline Word barrel_shifter(ShiftType shift_type, Word operand_2, size_t shift) {
    if (!shift) // LSR#0, ASR#0, ROR#0 converted to LSL#0
        shift_type = SHIFT_TYPE_LSL;

    switch (shift_type) {
    case SHIFT_TYPE_LSL:
        if (!shift) { // LSL#0: carry unmodified as well as operand
            cpu->shifter_carry = CC_UNMOD;
            break;
        }
        cpu->shifter_carry = (operand_2 << (shift - 1)) >> 31;
        operand_2 = shift > 31 ? 0 : operand_2 << shift;
        break;
    case SHIFT_TYPE_LSR:
        if (shift > 31) {
            printf("LSR shift > 31\n");
            exit(1);
        }
        cpu->shifter_carry = (operand_2 >> (shift - 1)) & 1;
        operand_2 >>= shift;
        break;
    case SHIFT_TYPE_ASR:
        if (shift > 31) { // ASR#32: operand and C are filled by Bit 31 of Rm
            Bit msb = (operand_2 >> 31) & 1;
            cpu->shifter_carry = msb;
            operand_2 = msb ? ~0U : 0;
            break;
        }
        cpu->shifter_carry = ((int32_t)operand_2 >> (shift - 1)) & 1;
        operand_2 = (int32_t)operand_2 >> shift;
        break;
    case SHIFT_TYPE_ROR:
        if (shift > 31) {
            printf("ROR shift > 31\n");
            exit(1);
        }
        operand_2 = (operand_2 >> shift) | (operand_2 << (32 - shift));
        cpu->shifter_carry = operand_2 >> 31;
        break;
    }
    return operand_2;
}

static inline int arm_exec_instr(uint32_t instr, InstrType type) {
    uint8_t cond = (instr >> 28) & 0xF;

    if (eval_cond(cond)) {
        switch (type) {
        case Branch: {
            Bit with_link = (instr >> 24) & 1;
            int32_t offset = (instr & 0xFFFFFF) << 8;

            if (with_link & !THUMB_ACTIVATED) set_reg(0xE, cpu->registers.r15 - 4);
            cpu->registers.r15 += (offset >> 8) * 4;

            DEBUG_PRINT(("B%s%s #0x%X\n", with_link ? "L" : "", cond_to_cstr(cond), cpu->registers.r15))
            break;
        }
        case BranchExchange: {
            uint8_t opcode = (instr >> 4) & 0xF;
            uint8_t rn = instr & 0xF;
            uint32_t rn_val = get_reg(rn);

            switch (opcode) {
            case 0x1:
                DEBUG_PRINT(("BX%s %s\n", cond_to_cstr(cond), register_to_cstr(rn)))
                if (rn_val & 1) {
                    // change mode to THUMB 
                    cpu->registers.cpsr |= 0x20;
                    // align to 16-bit THUMB instructions
                    cpu->registers.r15 = rn_val & ~0x1; 
                } else {
                    // continue in ARM so align to 32-bit ARM instructions
                    cpu->registers.r15 = rn_val & ~0x3; 
                }
                break;
            case 0x3:
                DEBUG_PRINT(("BLX"))
                exit(1);
                break;
            default:
                fprintf(stderr, "CPU Error: invalid BX opcode!\n");
                exit(1);
            }
            break;
        }
        case BlockDataTransfer: {
            Bit p = (instr >> 24) & 1;
            Bit u = (instr >> 23) & 1;
            Bit s = (instr >> 22) & 1;
            Bit w = (instr >> 21) & 1;
            Bit l = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint16_t reg_list = instr & 0xFFFF;

            if (s) {
                fprintf(stderr, "FUCK THE S Bit NEEDS TO BE HANDLED\n");
                exit(1);
            }

            if (l) { // LDM (load from memory)
                DEBUG_PRINT(("LDM%s%s %s, { ", cond_to_cstr(cond), amod_to_cstr(p, u), register_to_cstr(rn)))

                int amod = (p << 1) | u;
                switch (amod) {
                case 1: // IA
                case 3: // IB
                    for (int reg = 0; reg < 16; reg++) {
                        bool should_transfer = (reg_list >> reg) & 1;

                        if (should_transfer) {
                            uint32_t base = get_reg(rn);
                            uint32_t addr = amod == 1 ? base : base + 4;
                            set_reg(reg, read_mem(cpu->mem, addr, WORD_SIZE));
                            if (w) set_reg(rn, base + 4);
                            DEBUG_PRINT(("%s ", register_to_cstr(reg), reg))
                        }
                    }
                    DEBUG_PRINT(("}\n"))
                    break;
                case 0: // DA
                case 2: // DB
                    printf("BOOP!");
                    exit(1);
                    break;
                }
            } else { // STM (store to memory)
                DEBUG_PRINT(("STM%s%s %s, { ", cond_to_cstr(cond), amod_to_cstr(p, u), register_to_cstr(rn)))

                int amod = (p << 1) | u;
                switch (amod) {
                case 1: // IA
                case 3: // IB
                    printf("GOOP!");
                    exit(1);
                    break;
                case 0: // DA
                case 2: // DB
                    for (int reg = 15; reg >= 0; reg--) {
                        bool should_transfer = (reg_list >> reg) & 1;

                        if (should_transfer) {
                            uint32_t base = get_reg(rn);
                            uint32_t addr = amod == 0 ? base : base - 4;
                            write_mem(cpu->mem, addr, get_reg(reg), WORD_SIZE);
                            if (w) set_reg(rn, base - 4);
                            DEBUG_PRINT(("%s ", register_to_cstr(reg), reg))
                        }
                    }
                    DEBUG_PRINT(("}\n"))
                    break;
                }
            }
            break;
        }
        case DataProcessing: {
            Bit i = (instr >> 25) & 1;
            Bit s = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            if (rd == 0xF && s) {
                printf("THE P OPCODES!\n");
                exit(1);
            }

            Word operand2;

            // will be set to 4 if rn = r15 because returned value should be PC + 12 (r15 holds PC + 8)
            Word rn_r15_offset = 0;

            if (i) {
                operand2 = barrel_shifter(SHIFT_TYPE_ROR, instr & 0xFF, ((instr & 0xF00) >> 8) * 2);
            } else {
                Bit r = (instr >> 4) & 1;

                uint8_t shift_type = (instr >> 5) & 0x3;
                uint8_t rm = instr & 0xF;

                if (r) {
                    // will be set to 4 if rm = r15 because returned value should be PC + 12 (r15 holds PC + 8)
                    Word rm_r15_offset = 0;
                    if (rn == 0xF) rn_r15_offset = 4;
                    if (rm == 0xF) rm_r15_offset = 4;

                    uint8_t shift_reg_val = get_reg((instr >> 8) & 0xF) & 0xFF; // r0-r14, only 0-255
                    operand2 = barrel_shifter(shift_type, get_reg(rm) + rm_r15_offset, shift_reg_val);
                } else {
                    operand2 = barrel_shifter(shift_type, get_reg(rm), (instr >> 7) & 0x1F);
                }
            }

            Word rn_val = get_reg(rn) + rn_r15_offset;

            switch ((instr >> 21) & 0xF) {
            case 0x0: {
                DEBUG_PRINT(("AND%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                Word result = rn_val & operand2;
                if (s) set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
                set_reg(rd, result);
                break;
            }
            case 0x1: {
                DEBUG_PRINT(("EOR%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                Word result = rn_val ^ operand2;
                if (s) set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
                set_reg(rd, result);
                break;
            }
            case 0x2: {
                DEBUG_PRINT(("SUB%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)rn_val + ((uint64_t)~operand2 + (uint64_t)1);
                int64_t signed_result = (int64_t)(int32_t)rn_val + ((int64_t)(int32_t)~operand2 + (int64_t)1);
                if (s) set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, rn_val >= operand2, signed_result > INT32_MAX || signed_result < INT32_MIN);
                set_reg(rd, unsigned_result);
                break;
            }
            case 0x3: {
                DEBUG_PRINT(("RSB%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))

                printf("RSB: %08X\n", cpu->registers.r15);
                print_dump();
                exit(1);

                break;
            }
            case 0x4: {
                DEBUG_PRINT(("ADD%s %s, %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rd), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)rn_val + (uint64_t)operand2;
                int64_t signed_result = (int64_t)(int32_t)rn_val + (int64_t)(int32_t)operand2;
                if (s) set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
                set_reg(rd, unsigned_result);
                break;
            }
            case 0x5: {
                DEBUG_PRINT(("ADC%s %s, %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rd), register_to_cstr(rn), operand2))
                
                printf("ADC: %08X\n", cpu->registers.r15);
                print_dump();
                exit(1);

                break;
            }
            case 0x6: {
                DEBUG_PRINT(("SBC%s %s, %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rd), register_to_cstr(rn), operand2))

                printf("SBC: %08X\n", cpu->registers.r15);
                print_dump();
                exit(1);

                break;
            }
            case 0x7: {
                DEBUG_PRINT(("RSC%s%s %s, %s, #0x%X\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand2))

                printf("RSC: %08X\n", cpu->registers.r15);
                print_dump();
                exit(1);

                break;
            }
            case 0x8: {
                DEBUG_PRINT(("TST%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                Word result = rn_val & operand2;
                set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
                break;
            }
            case 0x9: {
                DEBUG_PRINT(("TEQ%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                Word result = rn_val ^ operand2;
                set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
                break;
            }
            case 0xA: {
                DEBUG_PRINT(("CMP%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)rn_val + ((uint64_t)~operand2 + (uint64_t)1);
                int64_t signed_result = (int64_t)(int32_t)rn_val + ((int64_t)(int32_t)~operand2 + (int64_t)1);
                if (s) set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, rn_val >= operand2, signed_result > INT32_MAX || signed_result < INT32_MIN);
                break;
            }
            case 0xB: {
                DEBUG_PRINT(("CMN%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)rn_val + (uint64_t)operand2;
                int64_t signed_result = (int64_t)(int32_t)rn_val + (int64_t)(int32_t)operand2;
                if (s) set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
                break;
            }
            case 0xC: {
                DEBUG_PRINT(("ORR%s %s, %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rd), register_to_cstr(rn), operand2))
                Word result = rn_val | operand2;
                if (s) set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
                set_reg(rd, result);
                break;
            }
            case 0xD: {
                DEBUG_PRINT(("MOV%s%s %s, #0x%X\n", cond_to_cstr(cond), s ? "S" : "", register_to_cstr(rd), operand2))
                if (s) set_cc(operand2 >> 31, operand2 == 0, cpu->shifter_carry, CC_UNMOD);
                set_reg(rd, operand2);
                break;
            }
            case 0xE: {
                DEBUG_PRINT(("BIC%s%s %s, %s, #0x%X", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand2))
                Word result = rn_val & ~operand2;
                if (s) set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
                set_reg(rd, result);
                break;
            }
            case 0xF: {
                DEBUG_PRINT(("MVN%s%s %s, #0x%X\n", cond_to_cstr(cond), s ? "S" : "", register_to_cstr(rd), operand2))
                operand2 = ~operand2;
                if (s) set_cc(operand2 >> 31, operand2 == 0, cpu->shifter_carry, CC_UNMOD);
                set_reg(rd, operand2);
                break;
            }
            }
            break;
        }
        case HalfwordDataTransfer: {
            Bit p = (instr >> 24) & 1;
            Bit u = (instr >> 23) & 1; // if unset offset is negative (subtracted from base)
            Bit i = (instr >> 22) & 1;
            Bit w = (instr >> 21) & 1;
            Bit l = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            int32_t offset = i ? 
                ((((instr >> 8) & 0xF) << 4) | (instr & 0xF)) : 
                get_reg(instr & 0xF);
            if (!u) offset = -offset;

            Word addr = get_reg(rn) + offset;
            bool should_write_back = (p && w) || !p;

            if (l) {
                switch ((instr >> 5) & 0x3) {
                case 1:
                    DEBUG_PRINT(("LDR%sH ", cond_to_cstr(cond)))
                    set_reg(rd, read_mem(cpu->mem, addr - (p ? 0 : offset), HALF_WORD_SIZE));
                    break;
                case 2:
                    DEBUG_PRINT(("LDR%sSB ", cond_to_cstr(cond)))
                    exit(1);
                    break;
                case 3:
                    DEBUG_PRINT(("LDR%sSH ", cond_to_cstr(cond)))
                    exit(1);
                    break;
                }
            } else {
                switch ((instr >> 5) & 0x3) {
                case 1:
                    DEBUG_PRINT(("STR%sH ", cond_to_cstr(cond)))
                    write_mem(cpu->mem, addr - (p ? 0 : offset), get_reg(rd), HALF_WORD_SIZE);
                    break;
                case 2:
                    DEBUG_PRINT(("LDR%sD ", cond_to_cstr(cond)))
                    exit(1);
                    break;
                case 3:
                    DEBUG_PRINT(("STR%sD ", cond_to_cstr(cond)))
                    exit(1);
                    break;
                }
            }

            if (should_write_back) set_reg(rn, addr);

            DEBUG_PRINT(("%s, ", register_to_cstr(rd)))
            if (p) {
                DEBUG_PRINT(("[%s", register_to_cstr(rn)))

                if (i) {
                    if (offset) {
                        DEBUG_PRINT((", #0x%X]", !u ? -offset : offset))
                    } else {
                        DEBUG_PRINT(("]"))
                    }
                } else {
                    DEBUG_PRINT((", %s]", register_to_cstr(instr & 0xF)))
                }

                if ((p && w) || !p) {
                    DEBUG_PRINT(("!\n"));
                } else {
                    DEBUG_PRINT(("\n"))
                }
            } else {
                DEBUG_PRINT(("[%s], ", register_to_cstr(rn)))
                if (i) {
                    DEBUG_PRINT(("#0x%X\n", !u ? -offset : offset))
                } else {
                    DEBUG_PRINT(("%s\n", register_to_cstr(instr & 0xF)))
                }
            }
            break;
        }
        case SingleDataTransfer: {
            Bit i = (instr >> 25) & 1; 
            Bit p = (instr >> 24) & 1; // if set pre-index (apply offset before memory acess)
            Bit u = (instr >> 23) & 1; // if set operand is unsigned
            Bit b = (instr >> 22) & 1; // if set byte transfers are enabled otherwise word
            Bit t = (instr >> 21) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;
            uint8_t rm = instr & 0xF;

            uint8_t shift_amount = (instr >> 7) & 0x1F;

            Word operand_2 = i ? 
                barrel_shifter((instr >> 5) & 0x3, get_reg(instr & 0xF), (instr >> 7) & 0x1F) : 
                instr & 0xFFF;
            if (!u) operand_2 = ~operand_2 + 1;

            Word addr = get_reg(rn) + operand_2; // base address with offset applied
            bool should_write_back = !p || (p && t);

            if (rd == 0xF) {
                printf("POOP ON A STICK!\n");
                exit(1);
            }

            switch ((instr >> 20) & 1) {
            case 0:
                DEBUG_PRINT(("STR%s%s%s ", cond_to_cstr(cond), b ? "B" : "", t ? "T" : ""))
                write_mem(cpu->mem, addr - (p ? 0 : operand_2), get_reg(rd), b ? BYTE_SIZE : WORD_SIZE);
                break;
            case 1:
                DEBUG_PRINT(("LDR%s%s%s ", cond_to_cstr(cond), b ? "B" : "", t ? "T" : ""))
                set_reg(rd, read_mem(cpu->mem, addr - (p ? 0 : operand_2), b ? BYTE_SIZE : WORD_SIZE));
                break;
            }

            if (should_write_back) set_reg(rn, addr);

            DEBUG_PRINT(("%s, [%s", register_to_cstr(rd), register_to_cstr(rn)))
            if (p) {
                DEBUG_PRINT((", #0x%X]%s", operand_2, should_write_back ? "!" : ""))
            } else {
                DEBUG_PRINT(("], #0x%X", operand_2))
            }
            DEBUG_PRINT(("\n"))
            break;
        }
        case SoftwareInterrupt:
            DEBUG_PRINT(("SWI%s #%X\n", cond_to_cstr(cond), instr & 0xFFFFFF))
            cpu->registers.r14_svc = cpu->registers.r15 - 4; // LR set to the instruction following SWI (PC + 4) (note: r15 always PC + 8)
            cpu->registers.spsr_svc = cpu->registers.cpsr;
            SET_PROCESSOR_MODE(Supervisor)
            cpu->registers.r15 = 0x00000008;
            break;
        case Multiply: { // NOTE: no halfword multiply instructions for GBA
            Bit s = (instr >> 20) & 1;
            uint8_t rd = (instr >> 16) & 0xF;
            uint8_t rn = (instr >> 12) & 0xF;
            uint8_t rs = (instr >> 8) & 0xF;
            uint8_t rm = instr & 0xF;

            switch ((instr >> 21) & 0xF) {
            case 0x0: {
                DEBUG_PRINT(("MUL%s%s %s, %s, %s\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
                Word result = get_reg(rm) * get_reg(rs);
                if (s) set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
                set_reg(rd, result);
                break;
            }
            case 0x1: {
                DEBUG_PRINT(("MLA%s %s, %s, %s, %s\n", cond_to_cstr(cond), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs), register_to_cstr(rn)))
                Word result = get_reg(rm) * get_reg(rs) + get_reg(rn);
                if (s) set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
                set_reg(rd, result);
                break;
            }
            case 0x2:
                fprintf(stderr, "multiply opcode not implemented yet: %04X\n", (instr >> 21) & 0xF);
                exit(1);
                break;
            case 0x4: {
                DEBUG_PRINT(("UMULL%s%s %s, %s, %s, %s\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
                uint64_t result = (uint64_t)get_reg(rm) * (uint64_t)get_reg(rs);
                if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
                set_reg(rn, result & ~0U);
                set_reg(rd, (result >> 32) & ~0U);
                break;
            }
            case 0x5:
                DEBUG_PRINT(("UMLAL%s%s %s, %s, %s, %s\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
                uint64_t result = (uint64_t)get_reg(rm) * (uint64_t)get_reg(rs) + (((uint64_t)get_reg(rd) << (uint64_t)32) | (uint64_t)get_reg(rn));
                if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
                set_reg(rn, result & ~0U);
                set_reg(rd, (result >> 32) & ~0U);
                break;
            case 0x6: {
                DEBUG_PRINT(("SMULL%s%s %s, %s, %s, %s\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
                int64_t result = (int64_t)(int32_t)get_reg(rm) * (int64_t)(int32_t)get_reg(rs);
                if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
                set_reg(rn, result & ~0U);
                set_reg(rd, (result >> 32) & ~0U);
                break;
            }
            case 0x7: {
                DEBUG_PRINT(("SMLAL%s%s %s, %s, %s, %s\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
                int64_t result = (int64_t)(int32_t)get_reg(rm) * (int64_t)(int32_t)get_reg(rs) + ((int64_t)((uint64_t)get_reg(rd) << (uint64_t)32) | (int64_t)get_reg(rn));
                if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
                set_reg(rn, result & ~0U);
                set_reg(rd, (result >> 32) & ~0U);
                break;
            }
            default:
                fprintf(stderr, "CPU Error: invalid opcode\n");
                exit(1);
            }
            break;
        }
        case MSR: {
            DEBUG_PRINT(("MSR%s ", cond_to_cstr(cond)))

            Bit i = (instr >> 25) & 1;
            Bit psr = (instr >> 22) & 1;
            Bit f = (instr >> 19) & 1; // if set, modify psr cc flag bits
            Bit c = (instr >> 16) & 1; // if set, modify psr control (processor mode) bits

            Word operand = i ?
                barrel_shifter(SHIFT_TYPE_ROR, instr & 0xFF, ((instr >> 8) & 0xF) * 2) :
                get_reg(instr & 0xF);

            if (psr) {
                DEBUG_PRINT(("spsr_%s, ", processor_mode_to_cstr(PROCESSOR_MODE)))
                set_psr_reg((get_psr_reg() & 0x00FFFFFF) | (operand & 0xFF000000));
                set_psr_reg((get_psr_reg() & 0xFFFFFF00) | (operand & 0x000000FF));
            } else {
                DEBUG_PRINT(("cpsr, "))
                cpu->registers.cpsr = (cpu->registers.cpsr & 0x00FFFFFF) | (operand & 0xFF000000);
                cpu->registers.cpsr = (cpu->registers.cpsr & 0xFFFFFF00) | (operand & 0x000000FF);
            }

            if (i) { DEBUG_PRINT(("#0x%X\n", operand)) } else { DEBUG_PRINT(("%s\n", register_to_cstr(instr & 0xF))) }
            break;
        }
        case MRS: {
            DEBUG_PRINT(("MRS%s ", cond_to_cstr(instr)))

            Bit psr = (instr >> 22) & 1;
            uint8_t rd = (instr >> 12) & 0xF;

            if (psr) {
                DEBUG_PRINT(("%s, spsr_%s\n", register_to_cstr(rd), processor_mode_to_cstr(PROCESSOR_MODE)))
                set_reg(rd, get_psr_reg());
            } else {
                DEBUG_PRINT(("%s, cpsr\n", register_to_cstr(rd)))
                set_reg(rd, cpu->registers.cpsr);
            }

            break;
        }
        case SingleDataSwap: {
            Bit b = (instr >> 22) & 1; // if set, transfer byte otherwise word
            uint8_t rn = (instr >> 16) & 0xF; // r0-r14
            uint8_t rd = (instr >> 12) & 0xF; // r0-r14
            uint8_t rm = instr & 0xF; // r0-r14

            set_reg(rd, read_mem(cpu->mem, get_reg(rn), b ? BYTE_SIZE : WORD_SIZE)); // Rd=[Rn]
            write_mem(cpu->mem, get_reg(rn), get_reg(rm), b ? BYTE_SIZE : WORD_SIZE); // [Rn]=Rm

            DEBUG_PRINT(("SWP%s%s %s, %s, [%s]", cond_to_cstr(instr), b ? "B" : "", register_to_cstr(rd)))
            break;
        }
        default:
            fprintf(stderr, "decoded instruction not handled yet! %d\n", type);
            exit(1);
        }
    } else {
        DEBUG_PRINT(("\n"));
    }

    return 1;
}

static inline int thumb_exec_instr(uint16_t instr, InstrType type) {
    switch (type) {
    case THUMB_1: {
        uint8_t opcode = (instr >> 11) & 0x3;
        uint8_t offset = (instr >> 6) & 0x1F;

        uint8_t rs = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        Word operand_2 = barrel_shifter(opcode, get_reg(rs), offset);
        set_reg(rd, operand_2);
        set_cc(operand_2 >> 31, operand_2 == 0, cpu->shifter_carry, CC_UNMOD);

        switch (opcode) {
        case 0x0:
            DEBUG_PRINT(("LSLS "))
            break;
        case 0x1:
            DEBUG_PRINT(("LSRS "))
            break;
        case 0x2:
            DEBUG_PRINT(("ASRS "))
            break;
        }
        DEBUG_PRINT(("%s, %s, #0x%X\n", register_to_cstr(rd), register_to_cstr(rs), offset))
        break;
    }
    case THUMB_2: {
        uint8_t rn_or_imm = (instr >> 6) & 0x7;
        uint8_t rs = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        Word rs_val = get_reg(rs);

        switch ((instr >> 9) & 0x3) {
        case 0x0: {
            DEBUG_PRINT(("ADDS %s, %s, %s\n", register_to_cstr(rd), register_to_cstr(rs), register_to_cstr(rn_or_imm)))
            uint64_t unsigned_result = (uint64_t)rs_val + (uint64_t)get_reg(rn_or_imm);
            int64_t signed_result = (int64_t)(int32_t)rs_val + (int64_t)(int32_t)get_reg(rn_or_imm);
            set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        case 0x1: {
            DEBUG_PRINT(("SUBS %s, %s, %s\n", register_to_cstr(rd), register_to_cstr(rs), register_to_cstr(rn_or_imm)))
            uint64_t unsigned_result = (uint64_t)rs_val + ((uint64_t)~get_reg(rn_or_imm) + (uint64_t)1);
            int64_t signed_result = (int64_t)(int32_t)rs_val + ((int64_t)(int32_t)~get_reg(rn_or_imm) + (int64_t)1);
            set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, rs_val >= get_reg(rn_or_imm), signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        case 0x2: {
            DEBUG_PRINT(("ADDS %s, %s, #0x%X\n", register_to_cstr(rd), register_to_cstr(rs), rn_or_imm))
            uint64_t unsigned_result = (uint64_t)rs_val + (uint64_t)rn_or_imm;
            int64_t signed_result = (int64_t)(int32_t)rs_val + (int64_t)(int32_t)rn_or_imm;
            set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        case 0x3: {
            DEBUG_PRINT(("SUBS %s, %s, #0x%X\n", register_to_cstr(rd), register_to_cstr(rs), rn_or_imm))
            uint64_t unsigned_result = (uint64_t)rs_val + ((uint64_t)~rn_or_imm + (uint64_t)1);
            int64_t signed_result = (int64_t)(int32_t)rs_val + ((int64_t)(int32_t)~rn_or_imm + (int64_t)1);
            set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, rs_val >= rn_or_imm, signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        }
        break;
    }
    case THUMB_3: {
        uint8_t opcode = (instr >> 11) & 0x3;

        uint8_t rd = (instr >> 8) & 0x7;
        uint8_t nn = instr & 0xFF;

        Word rd_val = get_reg(rd);

        switch (opcode) {
        case 0x0:
            DEBUG_PRINT(("MOVS %s, #0x%X\n", register_to_cstr(rd), nn))
            set_reg(rd, nn);
            set_cc(CC_UNSET, nn == 0, CC_UNMOD, CC_UNMOD);
            break;
        case 0x1: {
            DEBUG_PRINT(("CMPS %s, #0x%X\n", register_to_cstr(rd), nn))
            Word result = rd_val - nn;
            set_cc(result >> 31, result == 0, rd_val >= nn, CC_UNMOD); // TODO: FIX V FLAG
            break;
        }
        case 0x2:
            DEBUG_PRINT(("ADDS %s, #0x%X\n", register_to_cstr(rd), nn))
            Word result = get_reg(rd) + nn;
            set_reg(rd, result);
            set_cc(result >> 31, result == 0, CC_UNSET, CC_UNMOD); // TODO: FIX C AND V FLAG
            break;
        case 0x3: {
            DEBUG_PRINT(("SUBS %s, #0x%X\n", register_to_cstr(rd), nn))
            Word result = rd_val - nn;
            set_cc(result >> 31, result == 0, rd_val >= nn, CC_UNMOD); // TODO: FIX V FLAG
            set_reg(rd, result);
            break;
        }
        }
        break;
    }
    case THUMB_4: {
        uint8_t rs = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        Word rd_val = get_reg(rd);
        Word rs_val = get_reg(rs);

        switch ((instr >> 6) & 0xF) {
        case 0x0: {
            DEBUG_PRINT(("AND "))
            Word result = rd_val & rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            set_reg(rd, result);
            break;
        }

        case 0x8: {
            DEBUG_PRINT(("TST "))
            Word result = rd_val & rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            break;
        }

        case 0xA: {
            DEBUG_PRINT(("CMP "))
            Word result = rd_val - rs_val;
            set_cc(result >> 31, result == 0, rd_val >= rs_val, CC_UNMOD); // TODO: FIX V FLAG            
            break;
        }

        case 0xC: {
            DEBUG_PRINT(("ORR "))
            Word result = rd_val | rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            set_reg(rd, result);
            break;
        }
        case 0xD: {
            DEBUG_PRINT(("MUL "))
            Word result = (uint64_t)rd_val * (uint64_t)rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            set_reg(rd, result);
            break;
        }

        case 0xE: {
            DEBUG_PRINT(("BICS "))
            Word result = rd_val & ~rs_val;
            set_reg(rd, result);
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            break;
        }
        default:
            fprintf(stderr, "thumb alu missing 0x%X\n", (instr >> 6) & 0xF);
            exit(1);
        }
        DEBUG_PRINT(("%s, %s\n", register_to_cstr(rd), register_to_cstr(rs)))
        break;
    }
    case THUMB_5: { // NOTE: i do not think flags are set for this
        uint8_t rs = (((instr >> 6) & 1) << 3) | ((instr >> 3) & 0x7); // MSBs 
        uint8_t rd = (((instr >> 7) & 1) << 3) | (instr & 0x7); // MSBd

        Word rs_val = get_reg(rs);

        switch ((instr >> 8) & 0x3) {
        case 0x0:
            DEBUG_PRINT(("ADD %s, %s\n", register_to_cstr(rd), register_to_cstr(rs)));
            set_reg(rd, get_reg(rd) + rs_val);
            break;
        case 0x1: {
            DEBUG_PRINT(("CMP %s, %s\n", register_to_cstr(rd), register_to_cstr(rs)));
            uint64_t unsigned_result = (uint64_t)get_reg(rd) + ((uint64_t)~rs_val + (uint64_t)1);
            int64_t signed_result = (int64_t)(int32_t)get_reg(rd) + ((int64_t)(int32_t)~rs_val + (int64_t)1);
            set_cc(signed_result < 0, (uint32_t)unsigned_result == 0, get_reg(rd) >= rs_val, signed_result > INT32_MAX || signed_result < INT32_MIN);
            break;
        }
        case 0x2: {
            if (rd == rs == 8) { // R8 = R8
                DEBUG_PRINT(("NOP\n"))
                break;
            } else {
                DEBUG_PRINT(("MOV %s, %s\n", register_to_cstr(rd), register_to_cstr(rs)))
                set_reg(rd, get_reg(rs));
                break;
            }
        }
        case 0x3:
            DEBUG_PRINT(("BX %s\n", register_to_cstr(rs)))
            if (!(rs_val & 0x1)) { // switch mode to ARM if rs bit 0 unset
                cpu->registers.cpsr =  ~(1 << 5) & cpu->registers.cpsr;
                // mask bit 1 since we need word alignment for ARM 32-bit instructions and bit 0 is already 0
                cpu->registers.r15 = rs_val & ~0x2;
            } else {
                // mask bit 0 since we need half-word alignment for THUMB 16-bit instructions
                cpu->registers.r15 = rs_val & ~0x1;
            }
            break;
        }
        break;
    }
    case THUMB_6: {
        uint8_t rd = (instr >> 8) & 0x7;
        uint16_t nn = (instr & 0xFF) * 4;
        DEBUG_PRINT(("LDR %s, [PC, #0x%X]\n", register_to_cstr(rd), nn))
        set_reg(rd, read_mem(cpu->mem, (cpu->registers.r15 & ~2) + nn, WORD_SIZE));
        break;
    }
    case THUMB_7: {
        printf("THUMB_7\n");
        exit(1);
    }
    case THUMB_9: {
        uint8_t nn = (instr >> 6) & 0x1F; // nn*4 for WORD
        uint8_t rb = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        switch ((instr >> 11) & 0x3) {
        case 0x0:
            DEBUG_PRINT(("STR "))
            write_mem(cpu->mem, get_reg(rb) + nn * 4, get_reg(rd), WORD_SIZE);
            break;
        case 0x1:
            DEBUG_PRINT(("LDR "))
            set_reg(rd, read_mem(cpu->mem, get_reg(rb) + nn * 4, WORD_SIZE));
            break;
        case 0x2:
            DEBUG_PRINT(("STRB "))
            write_mem(cpu->mem, get_reg(rb) + nn, get_reg(rd), BYTE_SIZE);
            break;
        case 0x3:
            DEBUG_PRINT(("LDRB "))
            set_reg(rd, read_mem(cpu->mem, get_reg(rb) + nn, BYTE_SIZE));
            break;
        }
        DEBUG_PRINT(("%s, [%s, #%X]\n", register_to_cstr(rd), register_to_cstr(rb), nn))
        break;
    }
    case THUMB_10: {
        uint8_t nn = ((instr >> 6) & 0x1F) * 2; // (0-62, step 2)
        uint8_t rb = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        Word addr = get_reg(rb) + nn;

        switch ((instr >> 11) & 1) {
        case 0x0:
            DEBUG_PRINT(("STRH "))
            write_mem(cpu->mem, addr, get_reg(rd), HALF_WORD_SIZE);
            break;
        case 0x1:
            DEBUG_PRINT(("LDRH "))
            set_reg(rd, read_mem(cpu->mem, addr, HALF_WORD_SIZE));
            break;
        }
        DEBUG_PRINT(("%s, [%s, #0x%X]\n", register_to_cstr(rd), register_to_cstr(rb), nn))
        break;
    }
    case THUMB_11: {
        uint8_t rd = (instr >> 8) & 0x7;
        uint16_t nn = (instr & 0xFF) * 4; // (0-1020, step 4)

        Word addr = get_reg(0xD) + nn;

        switch ((instr >> 11) & 1) {
        case 0x0:
            DEBUG_PRINT(("STR "))
            write_mem(cpu->mem, addr, get_reg(rd), WORD_SIZE);
            break;
        case 0x1:
            DEBUG_PRINT(("LDR "))
            set_reg(rd, read_mem(cpu->mem, addr, WORD_SIZE));
            break;
        }
        DEBUG_PRINT(("%s, [sp, #0x%X]\n", register_to_cstr(rd), nn))
        break;
    }
    case THUMB_12: {
        uint8_t rd = (instr >> 8) & 0x7;
        uint8_t nn = (instr & 0xFF) * 4; // (0-1020, step 4)

        switch ((instr >> 11) & 1) {
        case 0:
            DEBUG_PRINT(("ADD %s, pc, #0x%X\n", register_to_cstr(rd), nn))
            set_reg(rd, cpu->registers.r15 + nn);
            break;
        case 1:
            printf("ADD 1");
            exit(1);
        }
        break;
    }
    case THUMB_13: {
        exit(1);
        break;
    }
    case THUMB_14: {
        Bit pc_or_lr = (instr >> 8) & 1;
        uint8_t reg_list = instr & 0xFF;

        switch ((instr >> 11) & 1) {
        case 0x0: { // equivelant to STMDB
            DEBUG_PRINT(("PUSH { "))
            DEBUG_PRINT(("%s", pc_or_lr ? "lr " : ""))

            // push lr to stack
            if (pc_or_lr) {
                set_reg(0xD, get_reg(0xD) - WORD_SIZE);
                write_mem(cpu->mem, get_reg(0xD), get_reg(0xE), WORD_SIZE);
            }

            for (int i = 7; i >= 0; i--) {
                if ((reg_list >> i) & 1) {
                    DEBUG_PRINT(("%s ", register_to_cstr(i)))
                    set_reg(0xD, get_reg(0xD) - WORD_SIZE);
                    write_mem(cpu->mem, get_reg(0xD), get_reg(i), WORD_SIZE);
                }
            }

            DEBUG_PRINT(("}\n"))
            break;
        }
        case 0x1: // equivelant to LDMIA
            DEBUG_PRINT(("POP { "))
            for (int i = 0; i < 8; i++) {
                if ((reg_list >> i) & 1) {
                    DEBUG_PRINT(("%s ", register_to_cstr(i)))
                    set_reg(i, read_mem(cpu->mem, get_reg(0xD), WORD_SIZE));
                    set_reg(0xD, get_reg(0xD) + WORD_SIZE);
                }
            }

            // last popped value assigned to PC
            if (pc_or_lr) {
                cpu->registers.r15 = read_mem(cpu->mem, get_reg(0xD), WORD_SIZE);
                set_reg(0xD, get_reg(0xD) + WORD_SIZE);
            }

            DEBUG_PRINT(("%s\n", pc_or_lr ? "pc }" : "}"))
            break;
        }
        break;
    }
    case THUMB_15: {
        uint8_t rb = (instr >> 8) & 0x7;
        uint8_t r_list = instr & 0xFF;

        switch ((instr >> 11) & 1) {
        case 0:
            DEBUG_PRINT(("STMIA %s!, { ", register_to_cstr(rb)))
            for (int i = 0; i < r_list; i++) {
                if ((r_list >> i) & 1) {
                    DEBUG_PRINT(("%s ", register_to_cstr(i)))
                    Word base = get_reg(rb);
                    write_mem(cpu->mem, base, get_reg(i), WORD_SIZE);
                    set_reg(rb, base + WORD_SIZE);
                }
            }
            DEBUG_PRINT(("}"))
            break;
        case 1:
            DEBUG_PRINT(("LDMIA %s!, { ", register_to_cstr(rb)))
            for (int i = 0; i < r_list; i++) {
                if ((r_list >> i) & 1) {
                    DEBUG_PRINT(("%s ", register_to_cstr(i)))
                    Word base = get_reg(rb);
                    set_reg(i, read_mem(cpu->mem, base, WORD_SIZE));
                    set_reg(rb, base + WORD_SIZE);
                }
            }
            DEBUG_PRINT(("}"))
            break;
        }
        DEBUG_PRINT(("\n"))
        break;
    }
    case THUMB_16: {
        uint8_t cond = (instr >> 8) & 0xF;
        int32_t offset = (int32_t)(int8_t)(instr & 0xFF) * 2;
        if (eval_cond(cond)) {
            cpu->registers.r15 += offset;
            DEBUG_PRINT(("B%s #0x%X", cond_to_cstr(cond), cpu->registers.r15))
        }
        DEBUG_PRINT(("\n"))
        break;
    }
    case THUMB_17: {
        printf("SWI\n");
        exit(1);

        uint8_t comment_field = instr & 0xFF;

        switch ((instr >> 8) & 0xFF) {
        case 0b11011111:
            DEBUG_PRINT(("SWI #0x%X", comment_field))

            break;
        default:
            fprintf(stderr, "THUMB.17 Error: unhandled opcode!\n");
            exit(1);
        }

        printf("THUMB_17\n");
        exit(1);
        break;
    }
    case THUMB_18: {
        // 11 bit signed offset (step 2) and sign extension at bit 10
        int16_t offset = (int16_t)(((instr & 0x7FF) * 2) << 5) >> 5; 
        cpu->registers.r15 += offset;
        DEBUG_PRINT(("B #0x%X\n", cpu->registers.r15))
        break;
    }
    case THUMB_19: { // 2 16 bit instructions in memory
        uint32_t offset_high = instr & 0x7FF;
        uint32_t offset_low = cpu->pipeline & 0x7FF;

        // sign extension for high offset at bit 10
        offset_high = (int32_t)(offset_high << 21) >> 21; 

        cpu->registers.r14 = cpu->registers.r15 | 1;
        cpu->registers.r15 = cpu->registers.r15 + (offset_high << 12) + (offset_low << 1);

        switch ((cpu->pipeline >> 11) & 0x1F) {
        case 0b11111:
            DEBUG_PRINT(("BL #0x%X\n", cpu->registers.r15))
            break;
        case 0b11101:
            DEBUG_PRINT(("BLX #0x%X\n", cpu->registers.r15))
            exit(1);
        }

        return 2;
    }
    default:
        fprintf(stderr, "unhandled thumb instruction type\n");
        exit(1);
    }

    return 1;
}

static inline int execute(Word instr, InstrType type) {
    int32_t original_pc = cpu->registers.r15;
    int cycles = 0;

    if (THUMB_ACTIVATED) {
        DEBUG_PRINT(("[THUMB] (%08X) %04X ", cpu->registers.r15 - 4, instr))
        cycles = thumb_exec_instr(instr, type);
    } else {
        DEBUG_PRINT(("[ARM] (%08X) %08X ", cpu->registers.r15 - 8, instr))
        cycles = arm_exec_instr(instr, type);
    }

    // will force pipeline flush if r15 is modified in execute stage
    if (cpu->registers.r15 != original_pc) cpu->pipeline = 0;

    return 1;
}

static inline int tick_cpu(void) {
    Word instr = cpu->pipeline ? cpu->pipeline : fetch();
    InstrType type;

    switch ((type = decode(instr))) {
    case ARM_BAD_INSTR:
        fprintf(stderr, "[ARM] invalid opcode: #0x%08X\n", instr);
        exit(1);
    case THUMB_BAD_INSTR:
        fprintf(stderr, "[THUMB] invalid opcode: #0x%04X\n", instr);
        exit(1);
    default: return execute(instr, type);
    }
}

void init_GBA(const char *rom_file, const char *bios_file) {
    cpu = (CPU *)calloc(1, sizeof(CPU));
    if (cpu == NULL) {
        fprintf(stderr, "unable to allocate more memory!\n");
        exit (1);
    }

    // initialize memory
    cpu->mem = init_mem(bios_file, rom_file);

    // initialize stack
    cpu->registers.r13_svc = 0x03007FE0;
    cpu->registers.r13_irq = 0x03007FA0;
    cpu->registers.r13 = 0x03007F00;

    // initialize PC + default mode
    cpu->registers.r14 = 0x08000000;
    cpu->registers.r15 = 0x08000000;
    cpu->registers.cpsr |= System;
}

uint16_t* compute_frame(uint16_t key_input) {
    cpu->mem->reg_keyinput = key_input;

    int total_cycles = 0;
    while (total_cycles < 280896) {
        int cycles = tick_cpu();
        for (int j = 0; j < cycles; j++) {
            tick_ppu();
        }
        total_cycles += cycles;
    }

    return frame;
}
