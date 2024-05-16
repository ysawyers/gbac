#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "cpu.h"
#include "memory.h"

#define CYCLES_PER_FRAME 280896

#define WORD_ACCESS       4
#define HALF_WORD_ACCESS  2
#define BYTE_ACCESS       1

#define CC_UNSET  0
#define CC_SET    1
#define CC_UNMOD  2

#define SP_REG 0xD
#define LR_REG 0xE
#define PC_REG 0xF

#define THUMB_ACTIVATED     (cpu->registers.cpsr >> 5 & 1)
#define PROCESSOR_MODE      (cpu->registers.cpsr & 0x1F)

#define SET_PROCESSOR_MODE(mode)    cpu->registers.cpsr &= ~0x1F; \
                                    cpu->registers.cpsr |= (mode);

#define ROR(operand, shift_amount) (((operand) >> ((shift_amount) & 31)) | ((operand) << ((-(shift_amount)) & 31)))

// https://problemkaputt.de/gbatek.htm#armcpuflagsconditionfieldcond
// all ARM instructions start with a 4 bit condition opcode
#define ARM_INSTR_COND(instr) ((instr >> 28) & 0xF)

// https://problemkaputt.de/gbatek.htm#armcpumemoryalignments
// compute shift amount for rotated reads (LDR/SWP)
#define ROT_READ_SHIFT_AMOUNT(addr) (((addr) & 0x3) * 8)

// used to fix pipeline flush edge case on pc updates 
// that are pointing to PC(+2 FOR THUMB)(+4 FOR ARM)
// which in the execute stage (for this implementation) r15 = PC (+2/+4 respectively)
#define PC_UPDATE(new_pc)   cpu->registers.r15 = new_pc; \ 
                            cpu->pipeline = 0; \

typedef uint32_t Word;
typedef uint16_t HalfWord;
typedef uint8_t Byte;
typedef bool Bit;

static inline Word get_reg(uint8_t reg_id);
static inline Word get_psr_reg(void);
static inline size_t tick_cpu(void);

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
    Multiply,
    SoftwareInterrupt,
    SingleDataSwap,
    ALU,
    MSR,
    MRS,
    SWP,

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
    uint8_t shifter_carry;

    Word curr_instr;
    Word pipeline;

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
    case 0xC: return "r12";
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
    if (cpu->pipeline) {
        printf("pipeline (next instruction): %08X\n", cpu->pipeline);
    } else {
        printf("PIPELINE FLUSH, RE-FILL");
    }
    printf("\n");
}

static Word get_psr_reg(void) {
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

static void set_psr_reg(Word val) {
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

static Bit get_cc(Flag cc) {
    switch (cc) {
    case N: return (get_psr_reg() >> 31) & 1;
    case Z: return (get_psr_reg() >> 30) & 1;
    case C: return (get_psr_reg() >> 29) & 1;
    case V: return (get_psr_reg() >> 28) & 1;
    }
}

static void set_cc(uint8_t n, int z, int c, int v) {
    Word curr_psr = get_psr_reg();

    if (n != CC_UNMOD) curr_psr = n ? (1 << 31) | curr_psr : ~(1 << 31) & curr_psr;
    if (z != CC_UNMOD) curr_psr = z ? (1 << 30) | curr_psr : ~(1 << 30) & curr_psr;
    if (c != CC_UNMOD) curr_psr = c ? (1 << 29) | curr_psr : ~(1 << 29) & curr_psr;
    if (v != CC_UNMOD) curr_psr = v ? (1 << 28) | curr_psr : ~(1 << 28) & curr_psr;

    set_psr_reg(curr_psr);
}

static bool eval_cond(uint8_t opcode) {
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

static __attribute__((always_inline)) Word get_reg(uint8_t reg_id) {
    switch (reg_id) {
    case 0x0: return cpu->registers.r0;
    case 0x1: return cpu->registers.r1;
    case 0x2: return cpu->registers.r2;
    case 0x3: return cpu->registers.r3;
    case 0x4: return cpu->registers.r4;
    case 0x5: return cpu->registers.r5;
    case 0x6: return cpu->registers.r6;
    case 0x7: return cpu->registers.r7;
    case 0x8:
        if (PROCESSOR_MODE == FIQ) return cpu->registers.r8_fiq;
        return cpu->registers.r8;
    case 0x9:
        if (PROCESSOR_MODE == FIQ) return cpu->registers.r9_fiq;
        return cpu->registers.r9;
    case 0xA:
        if (PROCESSOR_MODE == FIQ) return cpu->registers.r10_fiq;
        return cpu->registers.r10;
    case 0xB:
        if (PROCESSOR_MODE == FIQ) return cpu->registers.r11_fiq;
        return cpu->registers.r11;
    case 0xC:
        if (PROCESSOR_MODE == FIQ) return cpu->registers.r12_fiq;
        return cpu->registers.r12;
    case 0xD:
        switch (PROCESSOR_MODE) {
        case User:
        case System: return cpu->registers.r13;
        case FIQ: return cpu->registers.r13_fiq;
        case IRQ: return cpu->registers.r13_irq;
        case Supervisor: return cpu->registers.r13_svc;
        case Abort: return cpu->registers.r13_abt;
        case Undefined: return cpu->registers.r13_und;
        default:
            fprintf(stderr, "CPU Error: invalid\n");
            exit(1);
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
        default:
            fprintf(stderr, "CPU Error: invalid\n");
            exit(1);
        }
    case 0xF: return cpu->registers.r15;
    }
}

static __attribute__((always_inline)) void set_reg(uint8_t reg_id, Word val) {
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
        if (PROCESSOR_MODE == FIQ) {
            cpu->registers.r8_fiq = val;
        } else {
            cpu->registers.r8 = val;
        }
        break;
    case 0x9:
        if (PROCESSOR_MODE == FIQ) {
            cpu->registers.r9_fiq = val;
        } else {
            cpu->registers.r9 = val;
        }
        break;
    case 0xA:
        if (PROCESSOR_MODE == FIQ) {
            cpu->registers.r10_fiq = val;
        } else {
            cpu->registers.r10 = val;
        }
        break;
    case 0xB:
        if (PROCESSOR_MODE == FIQ) {
            cpu->registers.r11_fiq = val;
        } else {
            cpu->registers.r11 = val;
        }
        break;
    case 0xC:
        if (PROCESSOR_MODE == FIQ) {
            cpu->registers.r12_fiq = val;
        } else {
            cpu->registers.r12 = val;
        }
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
        default:
            fprintf(stderr, "CPU Error: invalid\n");
            exit(1);
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
        default:
            fprintf(stderr, "CPU Error: invalid\n");
            exit(1);
        }
        break;
    case 0xF:
        PC_UPDATE(val);
        break;
    }
}

static Word fetch() {
    Word instr;

    if (THUMB_ACTIVATED) {
        instr = read_mem(cpu->mem, cpu->registers.r15, HALF_WORD_ACCESS);
        cpu->registers.r15 += HALF_WORD_ACCESS;
    } else {
        instr = read_mem(cpu->mem, cpu->registers.r15, WORD_ACCESS);
        cpu->registers.r15 += WORD_ACCESS;
    }

    return instr;
}

static InstrType decode(Word instr) {
    cpu->curr_instr = instr;
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
                    fprintf(stderr, "CPU Error [THUMB]: debugging not supported!\n");
                    exit(1);
                }
                return THUMB_16;
            }
        case 0x7:
            if ((instr >> 12) & 1)
                return THUMB_19;
            return THUMB_18;
        default: return THUMB_BAD_INSTR;
        }
    } else {
        switch ((instr >> 25) & 0x7) {
        case 0x0:
            switch ((instr >> 4) & 0xF) {
            case 0x1:
                if (((instr >> 8) & 0xF) == 0xF) return BranchExchange;
                goto psr_transfer_or_alu_op;
            case 0x9:
                switch ((instr >> 23) & 0x3) {
                case 0x0:
                case 0x1: return Multiply;
                case 0x2: return SingleDataSwap;
                default: return ARM_BAD_INSTR;
                }
            case 0xB:
            case 0xD: return HalfwordDataTransfer;
            default: goto psr_transfer_or_alu_op;
            }
        case 0x1: goto psr_transfer_or_alu_op;
        case 0x2:
        case 0x3: return SingleDataTransfer;
        case 0x4: return BlockDataTransfer;
        case 0x5: return Branch;
        case 0x6:
            fprintf(stderr, "CPU Error [ARM]: coprocessor instructions not supported on GBA!\n");
            exit(1);
        case 0x7:
            if ((instr >> 24) & 1) 
                return SoftwareInterrupt;
            fprintf(stderr, "CPU Error [ARM]: debugging not supported!\n", instr);
            exit(1);
        default: return ARM_BAD_INSTR;
        }
    }

    // if ALU instruction for (TEQ,TST,CMP,CMN) and S = 1 -> PSR transfer
    psr_transfer_or_alu_op: {
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
            default: return ALU;
        }
    }
}

static Word barrel_shifter(ShiftType shift_type, Word operand_2, size_t shift, bool reg_shift_by_immediate) {
    // EDGE CASE: Rs=00h carry flag not affected
    if (!reg_shift_by_immediate && (shift == 0))
        return operand_2;

    switch (shift_type) {
    case SHIFT_TYPE_LSL:
        switch (shift) {
        case 0: // LSL#0: No shift performed, ie. directly Op2=Rm, the C flag is NOT affected.
            cpu->shifter_carry = CC_UNMOD;
            break;
        case 32: // LSL#32: LSL by 32 has result zero, carry out equal to bit 0 of Rm.
            cpu->shifter_carry = operand_2 & 1;
            operand_2 = 0;
            break;
        default: // LSL by more than 32 has result zero, carry out zero.
            cpu->shifter_carry = shift > 32 ? 0
                : (operand_2 << (shift - 1)) >> 31;
            operand_2 = shift > 32 ? 0
                : operand_2 << shift;
        }
        break;
    case SHIFT_TYPE_LSR:
        switch (shift) {
        case 0: // LSR#0 (shift by immediate): Interpreted as LSR#32, ie. Op2 becomes zero, C becomes Bit 31 of Rm.
            if (reg_shift_by_immediate) {
                cpu->shifter_carry = operand_2 >> 31;
                operand_2 = 0;
            }
            break;
        case 32: // LSR#32: LSR by 32 has result zero, carry out equal to bit 31 of Rm.
            cpu->shifter_carry = operand_2 >> 31;
            operand_2 = 0;
            break;
        default: // LSR by more than 32 has result zero, carry out zero.
            cpu->shifter_carry = shift > 32 ? 0 
                : (operand_2 >> (shift - 1)) & 1;
            operand_2 = shift > 32 ? 0 
                : operand_2 >> shift;
        }
        break;
    case SHIFT_TYPE_ASR:
        // ASR#0 (shift by immediate): Interpreted as ASR#32, ie. Op2 and C are filled by Bit 31 of Rm.
        if (reg_shift_by_immediate && (shift == 0)) {
            Bit msb = operand_2 >> 31;
            cpu->shifter_carry = msb;
            operand_2 = msb ? ~0 : 0;
            break;
        }

        // ASR by 32 or more has result filled with and carry out equal to bit 31 of Rm.
        cpu->shifter_carry = shift > 31 ? operand_2 >> 31
            : ((int32_t)operand_2 >> (shift - 1)) & 1;
        operand_2 = shift > 31 ? (operand_2 >> 31 ? ~0 : 0)
            : (int32_t)operand_2 >> shift;
        break;
    case SHIFT_TYPE_ROR:
        // ROR#0 (shift by immediate): Interpreted as RRX#1 (RCR), like ROR#1, but Op2 Bit 31 set to old C.
        if (reg_shift_by_immediate && (shift == 0)) {
            cpu->shifter_carry = operand_2 & 1;
            operand_2 = ((uint32_t)get_cc(C) << 31) | (operand_2 >> 1);
            break;
        }

        // ROR by n where n is greater than 32 will give the same result and carry out as ROR by n-32 repeated until shift in the range of 1-32
        operand_2 = (operand_2 >> (shift & 31)) | (operand_2 << ((-shift) & 31));
        cpu->shifter_carry = operand_2 >> 31;
        break;
    default:
        fprintf(stderr, "CPU Error: invalid shift opcode\n");
        exit(1);
    }

    return operand_2;
}

static int arm_branch() {
    Bit with_link = (cpu->curr_instr >> 24) & 1;
    int32_t offset = ((int32_t)((cpu->curr_instr & 0xFFFFFF) << 8) >> 8) << 2; // sign extended 24-bit offset shifted left by 2

    if (with_link)
        set_reg(LR_REG, cpu->registers.r15 - 4);
    cpu->registers.r15 = PC_UPDATE(cpu->registers.r15 + offset);

    DEBUG_PRINT(("B%s%s #0x%X\n", with_link ? "L" : "", cond_to_cstr(ARM_INSTR_COND(cpu->curr_instr)), cpu->registers.r15))
    return 1;
}

static int arm_branch_exchange() {
    uint8_t rn = cpu->curr_instr & 0xF;
    Word rn_val = get_reg(rn);

    switch ((cpu->curr_instr >> 4) & 0xF) {
    case 0x1:
        if (rn_val & 1) {
            cpu->registers.cpsr |= 0x20; // set thumb state indicator
            cpu->registers.r15 = PC_UPDATE(rn_val & ~0x1); // aligns to halfword boundary
        } else {
            cpu->registers.r15 = PC_UPDATE(rn_val & ~0x3); // aligns to word boundary
        }

        DEBUG_PRINT(("BX%s %s\n", cond_to_cstr(ARM_INSTR_COND(cpu->curr_instr)), register_to_cstr(rn)))
        break;
    case 0x3:
        DEBUG_PRINT(("BLX"))
        exit(1);
        break;
    default:
        fprintf(stderr, "CPU Error: invalid BX opcode!\n");
        exit(1);
    }

    return 1;
}

static int arm_block_data_transfer(Bit l) {
    Bit p = (cpu->curr_instr >> 24) & 1;
    Bit u = (cpu->curr_instr >> 23) & 1;
    Bit s = (cpu->curr_instr >> 22) & 1; // if set, instruction is assumed to be executing in privileged mode
    Bit w = (cpu->curr_instr >> 21) & 1;

    uint8_t rn = (cpu->curr_instr >> 16) & 0xF;
    uint16_t reg_list = cpu->curr_instr & 0xFFFF;

    DEBUG_PRINT(("%s%s%s %s, { ", l ? "LDM" : "STM", amod_to_cstr(p, u), cond_to_cstr(ARM_INSTR_COND(cpu->curr_instr)), register_to_cstr(rn)))

    Word base_addr = get_reg(rn);
    bool r15_transferred = (reg_list >> 0xF) & 1; // works in conjunction with s bit for special cases (see below)

    // in the case of a user bank transfer this will store the old cpsr value
    // and will switch modes for this instruction alone (the effect technically last
    // 
    Word user_bank_transfer = 0;

    if (s) {
        if (l && r15_transferred) {
            cpu->registers.cpsr = get_psr_reg();
        } else {
            user_bank_transfer = cpu->registers.cpsr;
            cpu->registers.cpsr = (cpu->registers.cpsr & ~0xFF) | User;
        }
    }

    // (?) calculate the final writeback value and write back to base register before transfers occur
    int total_transfers = __builtin_popcount(reg_list);
    if (w) {
        Word writeback_val = u ? base_addr + (total_transfers * WORD_ACCESS) : base_addr - (total_transfers * WORD_ACCESS);
        set_reg(rn, writeback_val);
    }

    // bit of a hack due to this implementation: internally, transfers will always happen in order from r0-r15
    // although for simplicity I'm iterating in the direction of the addressing mode (incrementing/decrementing)
    // and manually checking the STM edge case condition (see below) and create a copy of the base registers value
    uint8_t first_transferred_reg = __builtin_ffs(reg_list) - 1;
    Word base_addr_copy = base_addr;

    // r0 (or the first transferred register from the file) should be transferred 
    // to/from the lowest address location out of the entire register file
    for (int reg = u ? 0x0 : 0xF; reg != (u ? 0xF : -1); u ? reg++ : reg--) {
        bool should_transfer = (reg_list >> reg) & 1;

        if (should_transfer) {
            Word addr = p ? u ? base_addr + WORD_ACCESS : base_addr - WORD_ACCESS : base_addr;

            if (l) {
                set_reg(reg, read_mem(cpu->mem, addr, WORD_ACCESS)); // LDM
            } else {
                // if r15 is used in register list, stored value is PC + 12 (r15 + 4)
                Word stored_value = get_reg(reg) + (reg == 0xF ? 4 : 0);
                // A STM which includes storing the base, with the base as the first register to be stored, 
                // will therefore store the unchanged value, whereas with the base second or later 
                // in the transfer order, will store the modified value.
                write_mem(cpu->mem, addr, (reg == rn && rn == first_transferred_reg) ? base_addr_copy : stored_value, WORD_ACCESS); // STM
            }

            if (w)
                base_addr = u ? base_addr + WORD_ACCESS : base_addr - WORD_ACCESS;

            DEBUG_PRINT(("%s ", register_to_cstr(reg)))
        }
    }
    DEBUG_PRINT(("}\n"))

    if (user_bank_transfer) 
        cpu->registers.cpsr = user_bank_transfer;

    return 1;
}

static int arm_exec_instr(Word instr, InstrType type) {
    uint8_t cond = ARM_INSTR_COND(instr);

    if (eval_cond(cond)) {
        switch (type) {
        case Branch: return arm_branch();
        case BranchExchange: return arm_branch_exchange();
        case BlockDataTransfer: return arm_block_data_transfer((cpu->curr_instr >> 20) & 1); // LDM instruction if set otherwise STM

        case ALU: {
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
                uint8_t shift_amount = ((instr & 0xF00) >> 8) * 2;
                operand2 = barrel_shifter(SHIFT_TYPE_ROR, instr & 0xFF, shift_amount, false);
            } else {
                Bit r = (instr >> 4) & 1;

                uint8_t shift_type = (instr >> 5) & 0x3;
                uint8_t rm = instr & 0xF;

                if (r) {
                    // will be set to 4 if rm = r15 because returned value should be PC + 12 (r15 holds PC + 8)
                    Word rm_r15_offset = 0;
                    if (rn == 0xF) rn_r15_offset = 4;
                    if (rm == 0xF) rm_r15_offset = 4;

                    uint8_t shift_amount = get_reg((instr >> 8) & 0xF) & 0xFF;
                    operand2 = barrel_shifter(shift_type, get_reg(rm) + rm_r15_offset, shift_amount, false);
                } else {
                    uint8_t shift_amount = (instr >> 7) & 0x1F;
                    operand2 = barrel_shifter(shift_type, get_reg(rm), shift_amount, true);
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
                if (s) set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, rn_val >= operand2, signed_result > INT32_MAX || signed_result < INT32_MIN);
                set_reg(rd, unsigned_result);
                break;
            }
            case 0x3: { // TODO: BROKEN
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
                if (s) set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
                set_reg(rd, unsigned_result);
                break;
            }
            case 0x5: {
                DEBUG_PRINT(("ADC%s %s, %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rd), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)rn_val + (uint64_t)operand2 + (uint64_t)get_cc(C);
                int64_t signed_result = (int64_t)(int32_t)rn_val + (int64_t)(int32_t)operand2 + (int64_t)get_cc(C);
                if (s) set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
                set_reg(rd, unsigned_result);
                break;
            }
            case 0x6: { // FIX: REVISIT
                DEBUG_PRINT(("SBC%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                uint32_t result = rn_val - operand2 - !get_cc(C);
                if (s) set_cc(result >> 31, result == 0, (uint64_t)rn_val >= (uint64_t)operand2 + (uint64_t)!get_cc(C), ((rn_val>>31) != (operand2>>31)) && ((rn_val>>31) != (result>>31)));
                set_reg(rd, result);
                break;
            }
            case 0x7: { // TODO: BROKEN
                DEBUG_PRINT(("RSC%s%s %s, %s, #0x%X\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)operand2 + ((uint64_t)~rn_val + (uint64_t)1) + ((uint64_t)get_cc(C) - 1);
                int64_t signed_result = (int64_t)(int32_t)operand2 + ((int64_t)(int32_t)~rn_val + (int64_t)1) + ((int64_t)get_cc(C) - 1);
                if (s) set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, operand2 >= rn_val, signed_result > INT32_MAX || signed_result < INT32_MIN);
                set_reg(rd, unsigned_result);
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
                if (s) set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, rn_val >= operand2, signed_result > INT32_MAX || signed_result < INT32_MIN);
                break;
            }
            case 0xB: {
                DEBUG_PRINT(("CMN%s %s, #0x%X\n", cond_to_cstr(cond), register_to_cstr(rn), operand2))
                uint64_t unsigned_result = (uint64_t)rn_val + (uint64_t)operand2;
                int64_t signed_result = (int64_t)(int32_t)rn_val + (int64_t)(int32_t)operand2;
                if (s) set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
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
                DEBUG_PRINT(("BIC%s%s %s, %s, #0x%X\n", cond_to_cstr(instr), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand2))
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
            Bit u = (instr >> 23) & 1;
            Bit i = (instr >> 22) & 1;
            Bit w = (instr >> 21) & 1;
            Bit l = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            int32_t offset = i ? 
                ((((instr >> 8) & 0xF) << 4) | (instr & 0xF)) : 
                get_reg(instr & 0xF);
            if (!u) offset = -offset;

            Word addr = get_reg(rn) + (p ? offset : 0); // apply offset initially if pre-indexing is set
            bool should_write_back = (p && w) || !p;

            if (l) {
                switch ((instr >> 5) & 0x3) {
                case 1:
                    DEBUG_PRINT(("LDR%sH ", cond_to_cstr(cond)))
                    set_reg(rd, read_mem(cpu->mem, addr, HALF_WORD_ACCESS));
                    break;
                case 2:
                    DEBUG_PRINT(("LDR%sSB ", cond_to_cstr(cond)))
                    set_reg(rd, (int32_t)(int8_t)read_mem(cpu->mem, addr, BYTE_ACCESS));
                    break;
                case 3:
                    DEBUG_PRINT(("LDR%sSH ", cond_to_cstr(cond)))
                    set_reg(rd, (int32_t)(int16_t)read_mem(cpu->mem, addr, HALF_WORD_ACCESS));
                    break;
                }
            } else {
                switch ((instr >> 5) & 0x3) {
                case 1:
                    DEBUG_PRINT(("STR%sH ", cond_to_cstr(cond)))
                    write_mem(cpu->mem, addr, get_reg(rd), HALF_WORD_ACCESS);
                    break;
                case 2:
                    DEBUG_PRINT(("LDR%sD ", cond_to_cstr(cond)))
                    printf("IMPL LDRD");
                    exit(1);
                    break;
                case 3:
                    DEBUG_PRINT(("STR%sD ", cond_to_cstr(cond)))
                    printf("IMPL STRD");
                    exit(1);
                    break;
                }
            }

            if (should_write_back && rn != 0xF) 
                if (rd != rn)
                    set_reg(rn, get_reg(rn) + offset);

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
                DEBUG_PRINT(("%s", should_write_back ? "!" : ""))
            } else {
                DEBUG_PRINT(("[%s], ", register_to_cstr(rn)))
                if (i) {
                    DEBUG_PRINT(("#0x%X", !u ? -offset : offset))
                } else {
                    DEBUG_PRINT(("%s", register_to_cstr(instr & 0xF)))
                }
            }
            DEBUG_PRINT(("\n"))
            break;
        }
        case SingleDataTransfer: {
            Bit i = (instr >> 25) & 1; 
            Bit p = (instr >> 24) & 1; // if set pre-index (apply offset before memory acess)
            Bit u = (instr >> 23) & 1; // if set operand is unsigned
            Bit b = (instr >> 22) & 1; // if set byte transfers are enabled otherwise word
            Bit t = (instr >> 21) & 1;
            Bit l = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;
            uint8_t rm = instr & 0xF;

            uint8_t shift_amount = (instr >> 7) & 0x1F;
            Word operand_2 = i ? barrel_shifter((instr >> 5) & 0x3, get_reg(instr & 0xF), shift_amount, true) 
                : instr & 0xFFF;

            if (!u) operand_2 = -operand_2;

            Word addr = get_reg(rn) + (p ? operand_2 : 0); // base address with offset applied initially if pre-indexing is set
            bool should_write_back = !p || (p && t);

            if (p && b && !t && l && rd == 0xF && cond == 0xF) {
                printf("PLD INSTRUCTION!\n");
                exit(1);
            }

            switch (l) {
            case 0:
                DEBUG_PRINT(("STR%s%s%s ", cond_to_cstr(cond), b ? "B" : "", t && !p ? "T" : ""))
                Word stored_value = get_reg(rd) + (rd == 0xF ? 4 : 0); // when rd is used for STR instruction PC + 12 is used
                write_mem(cpu->mem, addr, stored_value, b ? BYTE_ACCESS : WORD_ACCESS);             
                break;
            case 1:
                DEBUG_PRINT(("LDR%s%s%s ", cond_to_cstr(cond), b ? "B" : "", t && !p ? "T" : ""))
                // LDR CASE: rotated read for misaligned word transfers (shift by (addr AND 3) * 8)
                Word read_value = b ? read_mem(cpu->mem, addr, BYTE_ACCESS)
                    : ROR(read_mem(cpu->mem, addr, WORD_ACCESS), ROT_READ_SHIFT_AMOUNT(addr));
                set_reg(rd, read_value);
                break;
            }

            if (should_write_back && rn != 0xF)
                if (rd != rn)
                    set_reg(rn, get_reg(rn) + operand_2);

            DEBUG_PRINT(("%s, ", register_to_cstr(rd)))
            if (p) {
                DEBUG_PRINT(("[%s", register_to_cstr(rn)))
                if (i) {
                    if (operand_2) {
                        DEBUG_PRINT((", #0x%X]", !u ? -operand_2 : operand_2))
                    } else {
                        DEBUG_PRINT(("]"))
                    }
                } else {
                    DEBUG_PRINT((", %s]", register_to_cstr(instr & 0xF)))
                }
                DEBUG_PRINT(("%s", should_write_back ? "!" : ""))
            } else {
                DEBUG_PRINT(("[%s], ", register_to_cstr(rn)))
                if (i) {
                    DEBUG_PRINT(("#0x%X", !u ? -operand_2 : operand_2))
                } else {
                    DEBUG_PRINT(("%s", register_to_cstr(instr & 0xF)))
                }
            }
            DEBUG_PRINT(("\n"))
            break;
        }
        case SoftwareInterrupt:
            DEBUG_PRINT(("SWI%s #%X\n", cond_to_cstr(cond), instr & 0xFFFFFF))
            cpu->registers.r14_svc = cpu->registers.r15 - 4; // LR set to the instruction following SWI (PC + 4) (note: r15 always PC + 8)
            cpu->registers.spsr_svc = cpu->registers.cpsr;
            SET_PROCESSOR_MODE(Supervisor)
            cpu->registers.r15 = PC_UPDATE(0x00000008);
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

            Word operand = i ? ROR(instr & 0xFF, ((instr >> 8) & 0xF) * 2)
                : get_reg(instr & 0xF);

            if (psr) {
                DEBUG_PRINT(("spsr_%s, ", processor_mode_to_cstr(PROCESSOR_MODE)))
                if (f) set_psr_reg((get_psr_reg() & 0x00FFFFFF) | (operand & 0xFF000000));
                if (c) set_psr_reg((get_psr_reg() & 0xFFFFFF00) | (operand & 0x000000FF));
            } else {
                DEBUG_PRINT(("cpsr, "))
                if (f) cpu->registers.cpsr = (cpu->registers.cpsr & 0x00FFFFFF) | (operand & 0xFF000000);
                if (c) cpu->registers.cpsr = (cpu->registers.cpsr & 0xFFFFFF00) | (operand & 0x000000FF);
            }

            if (i) { DEBUG_PRINT(("#0x%X\n", operand)) } else { DEBUG_PRINT(("%s\n", register_to_cstr(instr & 0xF))) }
            break;
        }
        case MRS: {
            DEBUG_PRINT(("MRS%s ", cond_to_cstr(cond)))

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
            Bit b = (instr >> 22) & 1;
            uint8_t rn = (instr >> 16) & 0xF; // r0-r14
            uint8_t rd = (instr >> 12) & 0xF; // r0-r14
            uint8_t rm = instr & 0xF; // r0-r14

            Word addr = get_reg(rn);

            if (b) {
                uint8_t temp = read_mem(cpu->mem, addr, BYTE_ACCESS);
                write_mem(cpu->mem, addr, get_reg(rm), BYTE_ACCESS);
                set_reg(rd, temp);
            } else {
                // SWP CASE: rotated read for misaligned word transfers (shift by (addr AND 3) * 8)
                Word temp = ROR(read_mem(cpu->mem, addr, WORD_ACCESS), (addr & 3) * 8);
                write_mem(cpu->mem, get_reg(rn), get_reg(rm), WORD_ACCESS);
                set_reg(rd, temp);
            }

            DEBUG_PRINT(("SWP%s%s %s, %s, [%s]\n", cond_to_cstr(cond), b ? "B" : "", register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rn)))
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

static int thumb_exec_instr(uint16_t instr, InstrType type) {
    switch (type) {
    case THUMB_1: {
        uint8_t opcode = (instr >> 11) & 0x3;
        uint8_t rs = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        uint8_t shift_amount = (instr >> 6) & 0x1F;
        Word operand_2 = barrel_shifter(opcode, get_reg(rs), shift_amount, true);

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
        DEBUG_PRINT(("%s, %s, #0x%X\n", register_to_cstr(rd), register_to_cstr(rs), shift_amount))
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
            uint64_t unsigned_result = (uint64_t)get_reg(rs) + (uint64_t)get_reg(rn_or_imm);
            int64_t signed_result = (int64_t)(int32_t)get_reg(rs) + (int64_t)(int32_t)get_reg(rn_or_imm);
            set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        case 0x1: {
            DEBUG_PRINT(("SUBS %s, %s, %s\n", register_to_cstr(rd), register_to_cstr(rs), register_to_cstr(rn_or_imm)))
            uint64_t unsigned_result = (uint64_t)rs_val + ((uint64_t)~get_reg(rn_or_imm) + (uint64_t)1);
            int64_t signed_result = (int64_t)(int32_t)rs_val + ((int64_t)(int32_t)~get_reg(rn_or_imm) + (int64_t)1);
            set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, rs_val >= get_reg(rn_or_imm), signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        case 0x2: {
            DEBUG_PRINT(("ADDS %s, %s, #0x%X\n", register_to_cstr(rd), register_to_cstr(rs), rn_or_imm))
            uint64_t unsigned_result = (uint64_t)get_reg(rs) + (uint64_t)rn_or_imm;
            int64_t signed_result = (int64_t)(int32_t)get_reg(rs) + (int64_t)(int32_t)rn_or_imm;
            set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
        case 0x3: {
            DEBUG_PRINT(("SUBS %s, %s, #0x%X\n", register_to_cstr(rd), register_to_cstr(rs), rn_or_imm))
            uint64_t unsigned_result = (uint64_t)rs_val + ((uint64_t)~rn_or_imm + (uint64_t)1);
            int64_t signed_result = (int64_t)(int32_t)rs_val + ((int64_t)(int32_t)~rn_or_imm + (int64_t)1);
            set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, rs_val >= rn_or_imm, signed_result > INT32_MAX || signed_result < INT32_MIN);
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
        case 0x2: {
            DEBUG_PRINT(("ADDS %s, #0x%X\n", register_to_cstr(rd), nn))
            uint64_t unsigned_result = (uint64_t)get_reg(rd) + (uint64_t)nn;
            int64_t signed_result = (int64_t)(int32_t)get_reg(rd) + (int64_t)(int32_t)nn;
            set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, unsigned_result > UINT32_MAX, signed_result > INT32_MAX || signed_result < INT32_MIN);
            set_reg(rd, unsigned_result);
            break;
        }
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
        case 0x1: {
            DEBUG_PRINT(("EOR "))
            Word result = rd_val ^ rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            set_reg(rd, result);
            break;
        }
        case 0x2: {
            DEBUG_PRINT(("LSL "))
            uint8_t shift_amount = rs_val & 0xFF;
            Word result = shift_amount > 31 ? 0 : rd_val << shift_amount;
            set_cc((result >> 31) & 1, result == 0, shift_amount == 0 ? CC_UNMOD : ((rd_val << (shift_amount - 1)) >> 31), CC_UNMOD);
            set_reg(rd, result);
            break;
        }
        case 0x7: {
            DEBUG_PRINT(("ROR "))
            uint8_t shift_amount = rs_val & 0xFF;
            Word result = ROR(rd_val, shift_amount);
            set_cc((result >> 31) & 1, result == 0, shift_amount == 0 ? CC_UNMOD : (result >> 31) & 1, CC_UNMOD);
            set_reg(rd, result);
            break;
        }
        case 0x8: {
            DEBUG_PRINT(("TST "))
            Word result = rd_val & rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            break;
        }
        case 0x9: {
            DEBUG_PRINT(("NEG "))
            Word result = -rs_val;
            set_cc(result >> 31, result == 0, rs_val <= 0, CC_UNMOD); // TODO: FIX V FLAG
            set_reg(rd, result);
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
        case 0xF: {
            DEBUG_PRINT(("MVN "))
            Word result = ~rs_val;
            set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
            set_reg(rd, result);
            break;
        }
        default:
            fprintf(stderr, "thumb alu missing 0x%X\n", (instr >> 6) & 0xF);
            exit(1);
        }
        DEBUG_PRINT(("%s, %s\n", register_to_cstr(rd), register_to_cstr(rs)))
        break;
    }
    case THUMB_5: {
        uint8_t rs = (((instr >> 6) & 1) << 3) | ((instr >> 3) & 0x7); // MSBs added (r0-r15)
        uint8_t rd = (((instr >> 7) & 1) << 3) | (instr & 0x7); // MSBd added (r0-r15)

        Word rs_val = get_reg(rs);

        switch ((instr >> 8) & 0x3) {
        case 0x0:
            DEBUG_PRINT(("ADD %s, %s\n", register_to_cstr(rd), register_to_cstr(rs)));
            set_reg(rd, get_reg(rd) + rs_val);
            break;
        case 0x1: { // only CMP affects CPSR condition flags for this instruction
            DEBUG_PRINT(("CMP %s, %s\n", register_to_cstr(rd), register_to_cstr(rs)));
            uint64_t unsigned_result = (uint64_t)get_reg(rd) + ((uint64_t)~rs_val + (uint64_t)1);
            int64_t signed_result = (int64_t)(int32_t)get_reg(rd) + ((int64_t)(int32_t)~rs_val + (int64_t)1);
            set_cc((unsigned_result >> 31) & 1, (uint32_t)unsigned_result == 0, get_reg(rd) >= rs_val, signed_result > INT32_MAX || signed_result < INT32_MIN);
            break;
        }
        case 0x2:
            DEBUG_PRINT(("MOV %s, %s\n", register_to_cstr(rd), register_to_cstr(rs)))
            set_reg(rd, get_reg(rs));
            break;
        case 0x3:
            DEBUG_PRINT(("BX %s\n", register_to_cstr(rs)))
            if (!(rs_val & 0x1)) { // switch mode to ARM if rs bit 0 unset
                cpu->registers.cpsr =  ~(1 << 5) & cpu->registers.cpsr;
                // mask bit 1 since we need word alignment for ARM 32-bit instructions and bit 0 is already 0
                cpu->registers.r15 = PC_UPDATE(rs_val & ~0x2);
            } else {
                // mask bit 0 since we need half-word alignment for THUMB 16-bit instructions
                cpu->registers.r15 = PC_UPDATE(rs_val & ~0x1);
            }
            break;
        }
        break;
    }
    case THUMB_6: {
        uint8_t rd = (instr >> 8) & 0x7;
        uint16_t nn = (instr & 0xFF) << 2; // 10-bit unsigned immediate offset
        DEBUG_PRINT(("LDR %s, [PC, #0x%X]\n", register_to_cstr(rd), nn))
        set_reg(rd, read_mem(cpu->mem, (cpu->registers.r15 & ~0x2) + nn, WORD_ACCESS));
        break;
    }
    case THUMB_7: {
        uint8_t ro = (instr >> 6) & 0x7;
        uint8_t rb = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        Word addr = get_reg(rb) + get_reg(ro);

        switch ((instr >> 10) & 0x3) {
        case 0x0:
            printf("0\n");
            exit(1);
            break;
        case 0x1:
            printf("1\n");
            exit(1);
            break;
        case 0x2:
            DEBUG_PRINT(("LDR "))
            set_reg(rd, ROR(read_mem(cpu->mem, addr, WORD_ACCESS), ROT_READ_SHIFT_AMOUNT(addr)));
            break;
        case 0x3:
            DEBUG_PRINT(("LDRB "))
            set_reg(rd, read_mem(cpu->mem, addr, BYTE_ACCESS));
            break;
        }
        DEBUG_PRINT(("%s, [%s, %s]\n", rd, rb, ro))
        break;
    }
    case THUMB_8: {
        uint8_t ro = (instr >> 6) & 0x7;
        uint8_t rb = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        Word addr = get_reg(rb) + get_reg(ro);

        switch ((instr >> 10) & 0x3) {
        case 0x0:
            printf("0\n");
            exit(1);
            break;
        case 0x1: // load sign-extended 8bit
            DEBUG_PRINT(("LDSB "))
            set_reg(rd, (int32_t)(int8_t)read_mem(cpu->mem, addr, BYTE_ACCESS));
            break;
        case 0x2: // load zero-extended 16bit
            DEBUG_PRINT(("LDRH "))
            set_reg(rd, read_mem(cpu->mem, addr, HALF_WORD_ACCESS));
            break;
        case 0x3: // load sign-extended 16bit
            DEBUG_PRINT(("LDSH "))
            set_reg(rd, (int32_t)(int16_t)read_mem(cpu->mem, addr, HALF_WORD_ACCESS));
            break;
        }
        DEBUG_PRINT(("%s, [%s, %s]\n", register_to_cstr(rd), register_to_cstr(rb), register_to_cstr(ro)))
        break;
    }
    case THUMB_9: {
        uint8_t nn = (instr >> 6) & 0x1F; // unsigned offset will be shifted left by 2 for WORD (7-bit immediate)
        uint8_t rb = (instr >> 3) & 0x7;
        uint8_t rd = instr & 0x7;

        switch ((instr >> 11) & 0x3) {
        case 0x0:
            DEBUG_PRINT(("STR "))
            write_mem(cpu->mem, get_reg(rb) + (nn << 2), get_reg(rd), WORD_ACCESS);
            break;
        case 0x1: {
            DEBUG_PRINT(("LDR "))
            Word addr = get_reg(rb) + (nn << 2);
            set_reg(rd, ROR(read_mem(cpu->mem, addr, WORD_ACCESS), ROT_READ_SHIFT_AMOUNT(addr)));
            break;
        }
        case 0x2:
            DEBUG_PRINT(("STRB "))
            write_mem(cpu->mem, get_reg(rb) + nn, get_reg(rd), BYTE_ACCESS);
            break;
        case 0x3:
            DEBUG_PRINT(("LDRB "))
            set_reg(rd, read_mem(cpu->mem, get_reg(rb) + nn, BYTE_ACCESS));
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
            write_mem(cpu->mem, addr, get_reg(rd), HALF_WORD_ACCESS);
            break;
        case 0x1:
            DEBUG_PRINT(("LDRH "))
            set_reg(rd, read_mem(cpu->mem, addr, HALF_WORD_ACCESS));
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
            write_mem(cpu->mem, addr, get_reg(rd), WORD_ACCESS);
            break;
        case 0x1:
            DEBUG_PRINT(("LDR "))
            set_reg(rd, read_mem(cpu->mem, addr, WORD_ACCESS));
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
            set_reg(rd, (cpu->registers.r15 & ~0x2) + nn);
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
                set_reg(0xD, get_reg(0xD) - WORD_ACCESS);
                write_mem(cpu->mem, get_reg(0xD), get_reg(0xE), WORD_ACCESS);
            }

            for (int i = 7; i >= 0; i--) {
                if ((reg_list >> i) & 1) {
                    DEBUG_PRINT(("%s ", register_to_cstr(i)))
                    set_reg(0xD, get_reg(0xD) - WORD_ACCESS);
                    write_mem(cpu->mem, get_reg(0xD), get_reg(i), WORD_ACCESS);
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
                    set_reg(i, read_mem(cpu->mem, get_reg(0xD), WORD_ACCESS));
                    set_reg(0xD, get_reg(0xD) + WORD_ACCESS);
                }
            }

            // last popped value assigned to PC
            if (pc_or_lr) {
                cpu->registers.r15 = PC_UPDATE(read_mem(cpu->mem, get_reg(0xD), WORD_ACCESS) & ~0x1);
                set_reg(0xD, get_reg(0xD) + WORD_ACCESS);
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
                    write_mem(cpu->mem, base, get_reg(i), WORD_ACCESS);
                    set_reg(rb, base + WORD_ACCESS);
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
                    set_reg(i, read_mem(cpu->mem, base, WORD_ACCESS));
                    set_reg(rb, base + WORD_ACCESS);
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
            cpu->registers.r15 = PC_UPDATE(cpu->registers.r15 + offset);
            DEBUG_PRINT(("B%s #0x%X", cond_to_cstr(cond), cpu->registers.r15))
        }
        DEBUG_PRINT(("\n"))
        break;
    }
    case THUMB_17:
        printf("THUMB.17\n");
        exit(1);
        break;
    case THUMB_18: {
        // 11 bit signed offset (step 2) and sign extension at bit 10
        int16_t offset = (int16_t)(((instr & 0x7FF) * 2) << 5) >> 5; 
        cpu->registers.r15 = PC_UPDATE(cpu->registers.r15 + offset);
        DEBUG_PRINT(("B #0x%X\n", cpu->registers.r15))
        break;
    }
    case THUMB_19: {
        uint32_t offset_high = instr & 0x7FF;
        // instruction is split between two 16-bit THUMB instructions
        uint32_t offset_low = cpu->pipeline & 0x7FF;

        // sign extension for high offset at bit 10
        offset_high = (int32_t)(offset_high << 21) >> 21; 

        cpu->registers.r14 = cpu->registers.r15 | 1;
        cpu->registers.r15 = PC_UPDATE(cpu->registers.r15 + (offset_high << 12) + (offset_low << 1));

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

static size_t execute(Word instr, InstrType type) {
    size_t cpi = 0;

    if (THUMB_ACTIVATED) {
        DEBUG_PRINT(("[THUMB] (%08X) %04X ", cpu->registers.r15 - 4, instr))
        cpi = thumb_exec_instr(instr, type);
    } else {
        DEBUG_PRINT(("[ARM] (%08X) %08X ", cpu->registers.r15 - 8, instr))
        cpi = arm_exec_instr(instr, type);
    }

    return 1;
}

static size_t tick_cpu(void) {
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

    size_t cycles = 0;
    while (cycles < CYCLES_PER_FRAME) {
        size_t cpi = tick_cpu();
        for (int j = 0; j < cpi; j++) {
            tick_ppu();
        }
        cycles += cpi;
    }

    return frame;
}
