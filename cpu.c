#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "cpu.h"
#include "decompressor.h"
#include "memory.h"

#define CYCLES_PER_FRAME 280896

#define WORD_ACCESS       4
#define HALFWORD_ACCESS  2
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

// https://problemkaputt.de/gbatek.htm#armcpuflagsconditionfieldcond
// all ARM instructions start with a 4 bit condition opcode
#define INSTR_COND_FIELD(instr) ((instr >> 28) & 0xF)

#define ROR(operand, shift_amount) (((operand) >> ((shift_amount) & 31)) | ((operand) << ((-(shift_amount)) & 31)))

// https://problemkaputt.de/gbatek.htm#armcpumemoryalignments
// compute shift amount for rotated reads (LDR/SWP)
#define ROT_READ_SHIFT_AMOUNT(addr) (((addr) & 0x3) * 8)

// certain instructions will be aware of the stored value of r15 being
// two instructions ahead of the currently executed instruction
// and the returned value of r15 will be + 12 or + 6 respective of the current mode
#define PC_VALUE (THUMB_ACTIVATED ? cpu->registers.r15 + HALFWORD_ACCESS : cpu->registers.r15 + WORD_ACCESS)

// used to fix pipeline flush edge case on pc updates
// that are pointing to PC(+2 FOR THUMB)(+4 FOR ARM)
// which in the execute stage (for this implementation) r15 = PC (+2/+4 respectively)
// so just checking before and after execute if PC has changed will not suffice
#define PC_UPDATE(new_pc)   cpu->registers.r15 = new_pc; \ 
                            cpu->pipeline = 0; \

static Word get_reg(uint8_t reg_id);
static Word get_psr_reg(void);

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

static Word get_reg(uint8_t reg_id) {
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

static void set_reg(uint8_t reg_id, Word val) {
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
        PC_UPDATE(THUMB_ACTIVATED ? val & ~0x1 : val & ~0x3);
        break;
    }
}

static Word fetch() {
    Word instr;

    if (THUMB_ACTIVATED) {
        instr = read_mem(cpu->mem, cpu->registers.r15, HALFWORD_ACCESS);
        cpu->registers.r15 += HALFWORD_ACCESS;
    } else {
        instr = read_mem(cpu->mem, cpu->registers.r15, WORD_ACCESS);
        cpu->registers.r15 += WORD_ACCESS;
    }

    return instr;
}

// NOTE: might benefit from a LUT here in the future 
static InstrType decode(Word instr) {
    cpu->curr_instr = instr;
    cpu->pipeline = fetch();

    if (THUMB_ACTIVATED) {
        switch ((instr >> 13) & 0x7) {
        case 0x0:
            if (((instr >> 11) & 0x3) == 0x3) 
                return thumb_decompress_2(instr, &cpu->curr_instr);
            return thumb_decompress_1(instr, &cpu->curr_instr);
        case 0x1: return thumb_decompress_3(instr, &cpu->curr_instr);
        case 0x2:
            switch ((instr >> 10) & 0x7) {
            case 0x0: return thumb_decompress_4(instr, &cpu->curr_instr);
            case 0x1: return thumb_decompress_5(instr, &cpu->curr_instr);
            case 0x2:
            case 0x3: return THUMB_LOAD_PC_RELATIVE;
            }
            if ((instr >> 9) & 1)
                return thumb_decompress_8(instr, &cpu->curr_instr);
            return thumb_decompress_7(instr, &cpu->curr_instr);
        case 0x3: return thumb_decompress_9(instr, &cpu->curr_instr);
        case 0x4:
            if ((instr >> 12) & 1)
                return thumb_decompress_11(instr, &cpu->curr_instr);
            return thumb_decompress_10(instr, &cpu->curr_instr);
        case 0x5:
            if (((instr >> 12) & 1) == 0) 
                return THUMB_RELATIVE_ADDRESS;
            if (((instr >> 9) & 0x3) == 0x2)
                return thumb_decompress_14(instr, &cpu->curr_instr);
            return thumb_decompress_13(instr, &cpu->curr_instr);
        case 0x6:
            switch ((instr >> 12) & 0x3) {
            case 0x0:
                return thumb_decompress_15(instr, &cpu->curr_instr);
            case 0x1:
                switch ((instr >> 8) & 0xFF) {
                case 0b11011111: return thumb_decompress_17(instr, &cpu->curr_instr);
                case 0b10111110:
                    fprintf(stderr, "CPU Error [THUMB]: debugging not supported!\n");
                    exit(1);
                }
                return thumb_decompress_16(instr, &cpu->curr_instr);
            }
            return THUMB_BAD_INSTR;
        case 0x7:
            switch ((instr >> 11) & 0x3) {
            case 0x0: return thumb_decompress_18(instr, &cpu->curr_instr);
            case 0x2: return THUMB_LONG_BRANCH_1;
            case 0x3:
            case 0x1: return THUMB_LONG_BRANCH_2;
            }
            return THUMB_BAD_INSTR;
        }
        return THUMB_BAD_INSTR;
    } else {
        switch ((instr >> 25) & 0x7) {
        case 0x0:
            switch ((instr >> 4) & 0xF) {
            case 0x1:
                if (((instr >> 8) & 0xF) == 0xF) 
                    return BRANCH_X;
                goto psr_transfer_or_alu_op;
            case 0x9:
                switch ((instr >> 23) & 0x3) {
                case 0x0:
                case 0x1: return Multiply;
                case 0x2: return SWP;
                default: return ARM_BAD_INSTR;
                }
            case 0xB:
            case 0xD: 
            case 0xF: return HalfwordDataTransfer;
            default: goto psr_transfer_or_alu_op;
            }
        case 0x1: goto psr_transfer_or_alu_op;
        case 0x2:
        case 0x3: return SingleDataTransfer;
        case 0x4: return BlockDataTransfer;
        case 0x5: return BRANCH;
        case 0x6:
            fprintf(stderr, "CPU Error [ARM]: coprocessor instructions not supported on GBA!\n");
            exit(1);
        case 0x7:
            if ((instr >> 24) & 1) 
                return SWI;
            fprintf(stderr, "CPU Error [ARM]: debugging not supported!\n", instr);
            exit(1);
        default: return ARM_BAD_INSTR;
        }
    }

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
    if (!reg_shift_by_immediate && (shift == 0)) {
        cpu->shifter_carry = CC_UNMOD;
        return operand_2;
    }

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

static int arm_branch(void) {
    Bit with_link = (cpu->curr_instr >> 24) & 1;
    int32_t offset = ((int32_t)((cpu->curr_instr & 0xFFFFFF) << 8) >> 8) << 2; // sign extended 24-bit offset shifted left by 2

    // adjust for step by 2 instead of 4 for translated THUMB immediates
    if (THUMB_ACTIVATED) 
        offset >>= 1;

    if (with_link) 
        set_reg(LR_REG, cpu->registers.r15 - 4);

    cpu->registers.r15 = PC_UPDATE(cpu->registers.r15 + offset);

    DEBUG_PRINT(("B%s%s #0x%X\n", with_link ? "L" : "", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), cpu->registers.r15))
    return 3;
}

static int arm_branch_exchange(void) {
    uint8_t rn = cpu->curr_instr & 0xF;
    Word rn_val = get_reg(rn);

    switch ((cpu->curr_instr >> 4) & 0xF) {
    case 0x1:
        if (rn_val & 1) {
            cpu->registers.cpsr |= 0x20; // toggle THUMB
            cpu->registers.r15 = PC_UPDATE(rn_val & ~0x1); // aligns to halfword boundary
        } else {
            cpu->registers.cpsr &= ~(1 << 5); // toggle ARM
            cpu->registers.r15 = PC_UPDATE(rn_val & ~0x3); // aligns to word boundary
        }

        DEBUG_PRINT(("BX%s %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rn)))
        break;
    case 0x3:
        DEBUG_PRINT(("BLX"))
        exit(1);
        break;
    default:
        fprintf(stderr, "CPU Error: invalid BX opcode!\n");
        exit(1);
    }

    return 3;
}

static int arm_alu(void) {
    Bit i = (cpu->curr_instr >> 25) & 1;
    Bit s = (cpu->curr_instr >> 20) & 1;
    
    uint8_t rn = (cpu->curr_instr >> 16) & 0xF;
    uint8_t rd = (cpu->curr_instr >> 12) & 0xF;

    Word operand_1 = get_reg(rn);
    Word operand_2;

    // used to help calculate number of cycles for this instruction
    bool reg_shift = false;
    bool r15_transferred = rd == 0xF;

    if (i) {
        uint8_t shift_amount = ((cpu->curr_instr & 0xF00) >> 8) * 2;
        operand_2 = barrel_shifter(SHIFT_TYPE_ROR, cpu->curr_instr & 0xFF, shift_amount, false);
    } else {
        Bit r = (cpu->curr_instr >> 4) & 1;
        uint8_t shift_type = (cpu->curr_instr >> 5) & 0x3;
        uint8_t rm = cpu->curr_instr & 0xF;
        Word rm_val = get_reg(rm);

        if (r) {
            if (rn == 0xF) operand_1 = PC_VALUE;
            if (rm == 0xF) rm_val = PC_VALUE;
            uint8_t shift_amount = get_reg((cpu->curr_instr >> 8) & 0xF) & 0xFF;
            operand_2 = barrel_shifter(shift_type, rm_val, shift_amount, false);
            reg_shift = true;
        } else {
            uint8_t shift_amount = (cpu->curr_instr >> 7) & 0x1F;
            operand_2 = barrel_shifter(shift_type, rm_val, shift_amount, true);
        }
    }

    switch ((cpu->curr_instr >> 21) & 0xF) {
    case 0x0: {
        DEBUG_PRINT(("AND%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 & operand_2;
        if (s)
            set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
        set_reg(rd, result);
        break;
    }
    case 0x1: {
        DEBUG_PRINT(("EOR%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 ^ operand_2;
        if (s)
            set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
        set_reg(rd, result);
        break;
    }
    case 0x3: { // RSB
        Word temp = operand_1;
        operand_1 = operand_2;
        operand_2 = temp;
    }
    case 0x2: {
        DEBUG_PRINT(("SUB%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 - operand_2;
        if (s)
            set_cc(result >> 31, result == 0, operand_1 >= operand_2, ((operand_1 >> 31) != (operand_2 >> 31)) && ((operand_1 >> 31) != (result >> 31)));
        set_reg(rd, result);
        break;
    }
    case 0x4: {
        DEBUG_PRINT(("ADD%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 + operand_2;
        if (s)
            set_cc(result >> 31, result == 0, ((operand_1 >> 31) + (operand_2 >> 31) > (result >> 31)), ((operand_1 >> 31) == (operand_2 >> 31)) && ((operand_1 >> 31) != (result >> 31)));
        set_reg(rd, result);
        break;
    }
    case 0x5: {
        DEBUG_PRINT(("ADC%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 + operand_2 + get_cc(C);
        if (s)
            set_cc(result >> 31, result == 0, ((operand_1 >> 31) + (operand_2 >> 31) > (result >> 31)), ((operand_1 >> 31) == (operand_2 >> 31)) && ((operand_1 >> 31) != (result >> 31)));
        set_reg(rd, result);
        break;
    }
    case 0x7: { // RSC
        Word temp = operand_1;
        operand_1 = operand_2;
        operand_2 = temp;
    }
    case 0x6: {
        DEBUG_PRINT(("SBC%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 - operand_2 - !get_cc(C);
        if (s)
            set_cc(result >> 31, result == 0, (uint64_t)operand_1 >= ((uint64_t)operand_2 + !get_cc(C)), ((operand_1 >> 31) != (operand_2 >> 31)) && ((operand_1 >> 31) != (result >> 31)));
        set_reg(rd, result);
        break;
    }
    case 0x8: {
        DEBUG_PRINT(("TST%s %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rn), operand_2))
        Word result = operand_1 & operand_2;
        set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
        break;
    }
    case 0x9: {
        DEBUG_PRINT(("TEQ%s %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rn), operand_2))
        Word result = operand_1 ^ operand_2;
        set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
        break;
    }
    case 0xA: {
        DEBUG_PRINT(("CMP%s %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rn), operand_2))
        Word result = operand_1 - operand_2;
        set_cc(result >> 31, result == 0, operand_1 >= operand_2, ((operand_1 >> 31) != (operand_2 >> 31)) && ((operand_1 >> 31) != (result >> 31)));
        break;
    }
    case 0xB: {
        DEBUG_PRINT(("CMN%s %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rn), operand_2))
        Word result = operand_1 + operand_2;
        set_cc(result >> 31, result == 0, ((operand_1 >> 31) + (operand_2 >> 31) > (result >> 31)), ((operand_1 >> 31) == (operand_2 >> 31)) && ((operand_1 >> 31) != (result >> 31)));
        break;
    }
    case 0xC: {
        DEBUG_PRINT(("ORR%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 | operand_2;
        if (s)
            set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
        set_reg(rd, result);
        break;
    }
    case 0xF: // MVN
        operand_2 = ~operand_2;
    case 0xD:
        DEBUG_PRINT(("MOV%s%s %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), operand_2))
        if (s)
            set_cc(operand_2 >> 31, operand_2 == 0, cpu->shifter_carry, CC_UNMOD);
        set_reg(rd, operand_2);
        break;
    case 0xE: {
        DEBUG_PRINT(("BIC%s%s %s, %s, #0x%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rn), operand_2))
        Word result = operand_1 & ~operand_2;
        if (s) 
            set_cc(result >> 31, result == 0, cpu->shifter_carry, CC_UNMOD);
        set_reg(rd, result);
        break;
    }
    }

    if (s && r15_transferred)
        cpu->registers.cpsr = get_psr_reg();

    return (1 + r15_transferred) + reg_shift + r15_transferred;
}

static int arm_multiply(void) {
    Bit s = (cpu->curr_instr >> 20) & 1;
    uint8_t rd = (cpu->curr_instr >> 16) & 0xF;
    uint8_t rn = (cpu->curr_instr >> 12) & 0xF;
    uint8_t rs = (cpu->curr_instr >> 8) & 0xF;
    uint8_t rm = cpu->curr_instr & 0xF;

    Word rs_val = get_reg(rs);

    int m = 4 - (((__builtin_clz(rs_val ^ ((int32_t)rs_val >> 31)) + 0x7) & ~0x7) >> 3);
    if (m == 0) m = 4;

    switch ((cpu->curr_instr >> 21) & 0xF) {
    case 0x0: {
        DEBUG_PRINT(("MUL%s%s %s, %s, %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
        Word result = get_reg(rm) * get_reg(rs);
        if (s) set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
        set_reg(rd, result);
        return 1 + m;
    }
    case 0x1: {
        DEBUG_PRINT(("MLA%s %s, %s, %s, %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs), register_to_cstr(rn)))
        Word result = get_reg(rm) * get_reg(rs) + get_reg(rn);
        if (s) set_cc(result >> 31, result == 0, CC_UNMOD, CC_UNMOD);
        set_reg(rd, result);
        return 2 + m;
    }
    case 0x2:
        fprintf(stderr, "multiply opcode not implemented yet: %04X\n", (cpu->curr_instr >> 21) & 0xF);
        exit(1);
        break;
    case 0x4: {
        DEBUG_PRINT(("UMULL%s%s %s, %s, %s, %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
        uint64_t result = (uint64_t)get_reg(rm) * (uint64_t)get_reg(rs);
        if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
        set_reg(rn, result & ~0);
        set_reg(rd, (result >> 32) & ~0);
        return 2 + m;
    }
    case 0x5:
        DEBUG_PRINT(("UMLAL%s%s %s, %s, %s, %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
        uint64_t result = (uint64_t)get_reg(rm) * (uint64_t)get_reg(rs) + (((uint64_t)get_reg(rd) << (uint64_t)32) | (uint64_t)get_reg(rn));
        if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
        set_reg(rn, result & ~0);
        set_reg(rd, (result >> 32) & ~0);
        return 3 + m;
    case 0x6: {
        DEBUG_PRINT(("SMULL%s%s %s, %s, %s, %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
        int64_t result = (int64_t)(int32_t)get_reg(rm) * (int64_t)(int32_t)get_reg(rs);
        if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
        set_reg(rn, result & ~0);
        set_reg(rd, (result >> 32) & ~0);
        return 2 + m;
    }
    case 0x7: {
        DEBUG_PRINT(("SMLAL%s%s %s, %s, %s, %s\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), s ? "S" : "", register_to_cstr(rn), register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rs)))
        int64_t result = (int64_t)(int32_t)get_reg(rm) * (int64_t)(int32_t)get_reg(rs) + ((int64_t)((uint64_t)get_reg(rd) << (uint64_t)32) | (int64_t)get_reg(rn));
        if (s) set_cc(result >> 63, result == 0, CC_UNMOD, CC_UNMOD);
        set_reg(rn, result & ~0);
        set_reg(rd, (result >> 32) & ~0);
        return 3 + m;
    }
    default:
        fprintf(stderr, "CPU Error: invalid opcode\n");
        exit(1);
    }
}

// FRAGILE!!
static int arm_block_data_transfer(void) {
    Bit p = (cpu->curr_instr >> 24) & 1;
    Bit u = (cpu->curr_instr >> 23) & 1;
    Bit s = (cpu->curr_instr >> 22) & 1; // if set, instruction is assumed to be executing in privileged mode
    Bit w = (cpu->curr_instr >> 21) & 1;
    Bit l = (cpu->curr_instr >> 20) & 1;

    uint8_t rn = (cpu->curr_instr >> 16) & 0xF;
    uint16_t reg_list = cpu->curr_instr & 0xFFFF;

    DEBUG_PRINT(("%s%s%s %s, { ", l ? "LDM" : "STM", amod_to_cstr(p, u), cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), register_to_cstr(rn)))

    // in the case of a user bank transfer this will store the old cpsr value
    // and will switch modes for this instruction alone (the effect should technically last for the following cpu instruction/cycle?? not sure)
    Word user_bank_transfer = 0;
    bool r15_transferred = (reg_list >> 0xF) & 1;

    if (s) {
        if (l && r15_transferred) {
            cpu->registers.cpsr = get_psr_reg();
        } else {
            user_bank_transfer = cpu->registers.cpsr;
            cpu->registers.cpsr = (cpu->registers.cpsr & ~0xFF) | User;
        }
    }

    int total_transfers = __builtin_popcount(reg_list);
    bool empty_reg_list = total_transfers == 0;

    Word base_addr = get_reg(rn);
    Word base_addr_offset = u ? WORD_ACCESS : -WORD_ACCESS;

    // (ARMv4 edge case): for an empty register list r15 is loaded/stored, and base register
    // is written back to +/-40h since the register count is 16 (even though only 1 transfer occurs)
    if (empty_reg_list) {
        reg_list |= (1 << 0xF);
        set_reg(rn, base_addr + (16 * base_addr_offset));
        r15_transferred = true;
    } else if (w) {
        set_reg(rn, base_addr + (total_transfers * base_addr_offset));
    }

    uint8_t first_transferred_reg = __builtin_ffs(reg_list) - 1;

    // a clone of the base address has to be made (covering STM edge case below) because while to the CPU
    // no matter the addressing mode, it will transfer registers in order from r0-r15
    // however this is not the case with this implementation
    Word base_addr_copy = base_addr;

    // r0 (or the first transferred register from the file) should be transferred 
    // to/from the lowest address location out of the entire register file however
    // this is reversed in the empty register case
    int reg_start, reg_end, step;
    if (u ^ empty_reg_list) {
        reg_start = 0;
        reg_end = 0x10;
        step = 1;
    } else {
        reg_start = 0xF;
        reg_end = -1;
        step = -1;
    }

    for (int reg = reg_start; reg != reg_end; reg += step) {
        bool should_transfer = (reg_list >> reg) & 1;

        if (should_transfer) {
            Word transfer_addr = p ? base_addr + base_addr_offset : base_addr;

            if (l) {
                set_reg(reg, read_mem(cpu->mem, transfer_addr, WORD_ACCESS)); // LDM
            } else {
                Word stored_value = reg == 0xF ? PC_VALUE : get_reg(reg);
                // A STM which includes storing the base, with the base as the first register to be stored, 
                // will therefore store the unchanged base address value, whereas with the base second or later 
                // in the transfer order, will store the register value (stored_value)
                write_mem(cpu->mem, transfer_addr, (reg == rn && rn == first_transferred_reg) ? base_addr_copy : stored_value, WORD_ACCESS); // STM
            }

            DEBUG_PRINT(("%s ", register_to_cstr(reg)))
            base_addr += base_addr_offset;
        }

        // when the register list is empty the register count will be 16
        // and the base (not the actual register) should be incremented
        if (empty_reg_list) base_addr += base_addr_offset;
    }
    DEBUG_PRINT(("}\n"))

    if (user_bank_transfer) 
        cpu->registers.cpsr = user_bank_transfer;

    if (l)
        return (total_transfers + r15_transferred) + (1 + r15_transferred) + 1;
    return (total_transfers - 1) + 2;
}

static int arm_halfword_data_transfer(void) {
    Bit p = (cpu->curr_instr >> 24) & 1;
    Bit u = (cpu->curr_instr >> 23) & 1;
    Bit i = (cpu->curr_instr >> 22) & 1;
    Bit w = (cpu->curr_instr >> 21) & 1;
    Bit l = (cpu->curr_instr >> 20) & 1;

    uint8_t rn = (cpu->curr_instr >> 16) & 0xF;
    uint8_t rd = (cpu->curr_instr >> 12) & 0xF;

    int32_t offset = i ?
        ((((cpu->curr_instr >> 8) & 0xF) << 4) | (cpu->curr_instr & 0xF)) : 
            get_reg(cpu->curr_instr & 0xF);
    if (!u) offset = -offset;

    Word addr = get_reg(rn) + (p ? offset : 0);
    bool should_write_back = (p && w) || !p;

    if (l) {
        // https://problemkaputt.de/gbatek.htm#armcpumemoryalignments
        // LDRH and LDRSH have unique handling for misaligned accesses

        switch ((cpu->curr_instr >> 5) & 0x3) {
        case 0x1: {
            DEBUG_PRINT(("LDR%sH ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))
            bool is_misaligned = addr & 1;
            Word read_value = is_misaligned ? ROR(read_mem(cpu->mem, addr - 1, HALFWORD_ACCESS), 8) 
                : read_mem(cpu->mem, addr, HALFWORD_ACCESS); 
            set_reg(rd, read_value);
            break;
        }
        case 0x2:
            DEBUG_PRINT(("LDR%sSB ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))
            set_reg(rd, (int32_t)(int8_t)read_mem(cpu->mem, addr, BYTE_ACCESS));
            break;
        case 0x3: {
            DEBUG_PRINT(("LDR%sSH ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))
            bool is_misaligned = addr & 1;
            Word read_value = is_misaligned ? (int32_t)(int8_t)read_mem(cpu->mem, addr, BYTE_ACCESS)
                : (int32_t)(int16_t)read_mem(cpu->mem, addr, HALFWORD_ACCESS);
            set_reg(rd, read_value);
            break;
        }
        }
    } else {
        switch ((cpu->curr_instr >> 5) & 0x3) {
        case 0x1:
            DEBUG_PRINT(("STR%sH ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))
            write_mem(cpu->mem, addr, get_reg(rd), HALFWORD_ACCESS);
            break;
        case 0x2:
            DEBUG_PRINT(("LDR%sD ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))
            printf("IMPL LDRD");
            exit(1);
            break;
        case 0x3:
            DEBUG_PRINT(("STR%sD ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))
            printf("IMPL STRD");
            exit(1);
            break;
        }
    }

    // writebacks handled the same way as single data transfer (see below)
    if (should_write_back) 
        if (!l || !(rn == rd))
            set_reg(rn, get_reg(rn) + ((rn == 0xF) << 2) + offset);

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
            DEBUG_PRINT((", %s]", register_to_cstr(cpu->curr_instr & 0xF)))
        }
        DEBUG_PRINT(("%s", should_write_back ? "!" : ""))
    } else {
        DEBUG_PRINT(("[%s], ", register_to_cstr(rn)))
        if (i) {
            DEBUG_PRINT(("#0x%X", !u ? -offset : offset))
        } else {
            DEBUG_PRINT(("%s", register_to_cstr(cpu->curr_instr & 0xF)))
        }
    }
    DEBUG_PRINT(("\n"))

    bool r15_transferred = rd == 0xF;

    if (l)
        return 3 + (2 * r15_transferred);
    return 2;
}

static int arm_single_data_transfer(void) {
    Bit i = (cpu->curr_instr >> 25) & 1;
    Bit p = (cpu->curr_instr >> 24) & 1;
    Bit u = (cpu->curr_instr >> 23) & 1;
    Bit b = (cpu->curr_instr >> 22) & 1;
    Bit t = (cpu->curr_instr >> 21) & 1;
    Bit l = (cpu->curr_instr >> 20) & 1;

    uint8_t rn = (cpu->curr_instr >> 16) & 0xF;
    uint8_t rd = (cpu->curr_instr >> 12) & 0xF;
    uint8_t rm = cpu->curr_instr & 0xF;

    uint8_t shift_amount = (cpu->curr_instr >> 7) & 0x1F;
    Word offset = i ? barrel_shifter((cpu->curr_instr >> 5) & 0x3, get_reg(cpu->curr_instr & 0xF), shift_amount, true) 
        : cpu->curr_instr & 0xFFF;
    if (!u) offset = -offset;

    Word addr = get_reg(rn) + (p ? offset : 0);
    bool should_write_back = !p || (p && t);

    if (p && b && l && !t && (rd == 0xF) && (((cpu->curr_instr >> 28) & 0xF) == 0xF)) {
        printf("PLD INSTRUCTION!\n");
        exit(1);
    }

    if (!p && t) {
        printf("memory manage bit is set\n");
        exit(1);
    }

    if (l) {
        // https://problemkaputt.de/gbatek.htm#armcpumemoryalignments
        // LDR has unique handling for misaligned accesses
        DEBUG_PRINT(("LDR%s%s%s ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), b ? "B" : "", t && !p ? "T" : ""))
        Word read_value = b ? read_mem(cpu->mem, addr, BYTE_ACCESS)
            : ROR(read_mem(cpu->mem, addr, WORD_ACCESS), ROT_READ_SHIFT_AMOUNT(addr));
        set_reg(rd, read_value);
    } else {
        DEBUG_PRINT(("STR%s%s%s ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), b ? "B" : "", t && !p ? "T" : ""))
        // THUMB will not decompress to an instruction that will specify a rd of 15
        // so only the PC + 12 case has to be handled here
        Word stored_value = get_reg(rd) + ((rd == 0xF) << 2);
        write_mem(cpu->mem, addr, stored_value, b ? BYTE_ACCESS : WORD_ACCESS);
    }

    if (should_write_back)
        // writeback should not occur with a load with a base register that is the same as destination
        if (!l || !(rn == rd))
            set_reg(rn, get_reg(rn) + ((rn == 0xF) << 2) + offset);

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
            DEBUG_PRINT((", %s]", register_to_cstr(cpu->curr_instr & 0xF)))
        }
        DEBUG_PRINT(("%s", should_write_back ? "!" : ""))
    } else {
        DEBUG_PRINT(("[%s], ", register_to_cstr(rn)))
        if (i) {
            DEBUG_PRINT(("#0x%X", !u ? -offset : offset))
        } else {
            DEBUG_PRINT(("%s", register_to_cstr(cpu->curr_instr & 0xF)))
        }
    }
    DEBUG_PRINT(("\n"))

    bool r15_transferred = rd == 0xF;

    if (l)
        return 3 + (2 * r15_transferred);
    return 2;
}

static int arm_single_data_swap(void) {
    Bit b = (cpu->curr_instr >> 22) & 1;
    uint8_t rn = (cpu->curr_instr >> 16) & 0xF;
    uint8_t rd = (cpu->curr_instr >> 12) & 0xF;
    uint8_t rm = cpu->curr_instr & 0xF;
    Word addr = get_reg(rn);

    if (b) {
        Word temp_val = read_mem(cpu->mem, addr, BYTE_ACCESS);
        write_mem(cpu->mem, addr, get_reg(rm), BYTE_ACCESS);
        set_reg(rd, temp_val);
    } else {
        Word temp_val = ROR(read_mem(cpu->mem, addr, WORD_ACCESS), ROT_READ_SHIFT_AMOUNT(addr));
        write_mem(cpu->mem, get_reg(rn), get_reg(rm), WORD_ACCESS);
        set_reg(rd, temp_val);
    }

    DEBUG_PRINT(("SWP%s%s %s, %s, [%s]\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), b ? "B" : "", register_to_cstr(rd), register_to_cstr(rm), register_to_cstr(rn)))
    return 4;
}

static int arm_msr(void) {
    DEBUG_PRINT(("MSR%s ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))

    Bit i = (cpu->curr_instr >> 25) & 1;
    Bit psr = (cpu->curr_instr >> 22) & 1;
    Bit f = (cpu->curr_instr >> 19) & 1; // if set, modify psr cc flag bits
    Bit c = (cpu->curr_instr >> 16) & 1; // if set, modify psr control (processor mode) bits

    Word operand = i ? ROR(cpu->curr_instr & 0xFF, ((cpu->curr_instr >> 8) & 0xF) * 2)
        : get_reg(cpu->curr_instr & 0xF);

    // bits 8-23 of cpsr are reserved and cannot be modified.
    if (psr) {
        DEBUG_PRINT(("spsr_%s, ", processor_mode_to_cstr(PROCESSOR_MODE)))
        if (f) set_psr_reg((get_psr_reg() & 0x00FFFFFF) | (operand & 0xFF000000));
        if (c) set_psr_reg((get_psr_reg() & 0xFFFFFF00) | (operand & 0x000000FF));
    } else {
        DEBUG_PRINT(("cpsr, "))
        if (f) cpu->registers.cpsr = (cpu->registers.cpsr & 0x00FFFFFF) | (operand & 0xFF000000);
        if (c) cpu->registers.cpsr = (cpu->registers.cpsr & 0xFFFFFF00) | (operand & 0x000000FF);
    }

    if (i) { DEBUG_PRINT(("#0x%X\n", operand)) } else { DEBUG_PRINT(("%s\n", register_to_cstr(cpu->curr_instr & 0xF))) }

    return 1;
}

static int arm_mrs(void) {
    DEBUG_PRINT(("MRS%s ", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr))))

    Bit psr = (cpu->curr_instr >> 22) & 1;
    uint8_t rd = (cpu->curr_instr >> 12) & 0xF;

    if (psr) {
        DEBUG_PRINT(("%s, spsr_%s\n", register_to_cstr(rd), processor_mode_to_cstr(PROCESSOR_MODE)))
        set_reg(rd, get_psr_reg());
    } else {
        DEBUG_PRINT(("%s, cpsr\n", register_to_cstr(rd)))
        set_reg(rd, cpu->registers.cpsr);
    }

    return 1;
}

static int arm_software_interrupt(void) {
    DEBUG_PRINT(("SWI%s #%X\n", cond_to_cstr(INSTR_COND_FIELD(cpu->curr_instr)), cpu->curr_instr & 0xFFFFFF))
    cpu->registers.r14_svc = cpu->registers.r15 - 4; // LR set to the instruction following SWI (PC + 4) (note: r15 always PC + 8)
    cpu->registers.spsr_svc = cpu->registers.cpsr;
    SET_PROCESSOR_MODE(Supervisor)
    cpu->registers.r15 = PC_UPDATE(0x00000008);
    return 3;
}

// handles THUMB formats that cannot be decompressed to an ARM instruction (or at least "trivially")
static int thumb_handler(InstrType type) {
    switch (type) {
    case THUMB_LOAD_PC_RELATIVE: { // format 6
        uint8_t rd = (cpu->curr_instr >> 8) & 0x7;
        uint16_t nn = (cpu->curr_instr & 0xFF) << 2; // 10-bit unsigned immediate offset
        DEBUG_PRINT(("LDR %s, [pc, #0x%X]\n", register_to_cstr(rd), nn))
        set_reg(rd, read_mem(cpu->mem, (cpu->registers.r15 & ~0x2) + nn, WORD_ACCESS));
        return 3;
    }
    case THUMB_RELATIVE_ADDRESS: { // format 12
        uint8_t rd = (cpu->curr_instr >> 8) & 0x7;
        uint16_t nn = (cpu->curr_instr & 0xFF) << 2; // 10-bit unsigned immediate offset

        switch ((cpu->curr_instr >> 11) & 1) {
        case 0:
            DEBUG_PRINT(("ADD %s, pc, #0x%X\n", register_to_cstr(rd), nn))
            set_reg(rd, (cpu->registers.r15 & ~0x2) + nn);
            break;
        case 1:
            DEBUG_PRINT(("ADD %s, sp, #0x%X\n", register_to_cstr(rd), nn))
            set_reg(rd, get_reg(SP_REG) + nn);
            break;
        }
        return 1;
    }
    case THUMB_LONG_BRANCH_1: { // format 19 (H = 0)
        Word upper_half_offset = (int32_t)((cpu->curr_instr & 0x7FF) << 21) >> 21;
        set_reg(LR_REG, cpu->registers.r15 + (upper_half_offset << 12));
        DEBUG_PRINT(("MOV lr, #0x%08X [BL 1]\n", cpu->registers.r15 + (upper_half_offset << 12)));
        return 1;
    }
    case THUMB_LONG_BRANCH_2: { // format 19 (H = 1)
        Word lower_half_offset = cpu->curr_instr & 0x7FF;
        Word curr_pc = cpu->registers.r15;

        switch ((cpu->curr_instr >> 11) & 0x1F) {
        case 0b11111:
            cpu->registers.r15 = PC_UPDATE(get_reg(LR_REG) + (lower_half_offset << 1));
            break;
        case 0b11101:
            printf("BLX THUMB\n");
            exit(1);
        default:
            fprintf(stderr, "CPU Error [THUMB]: invalid long branch opcode!\n");
            exit(1);
        }
        set_reg(LR_REG, (curr_pc - 2) | 1);

        DEBUG_PRINT(("MOV pc, #0x%08X | lr, #0x%08X [BL 2]\n", cpu->registers.r15, get_reg(LR_REG)))
        return 3;
    }
    default:
        fprintf(stderr, "unhandled thumb instruction type\n");
        exit(1);
    }
}

static int arm_handler(InstrType type) {
    if (!eval_cond(INSTR_COND_FIELD(cpu->curr_instr))) {
        DEBUG_PRINT(("\n"));
        return 1;
    }

    switch (type) {
    case NOP: return 1;
    case BRANCH: return arm_branch();
    case BRANCH_X: return arm_branch_exchange();
    case BlockDataTransfer: return arm_block_data_transfer();
    case ALU: return arm_alu();
    case HalfwordDataTransfer: return arm_halfword_data_transfer();
    case SingleDataTransfer: return arm_single_data_transfer();
    case SWI: return arm_software_interrupt();
    case Multiply: return arm_multiply();
    case MSR: return arm_msr();
    case MRS: return arm_mrs();
    case SWP: return arm_single_data_swap();
    }
}

static int execute(void) {
    Word instr = cpu->pipeline ? cpu->pipeline : fetch();
    InstrType type = decode(instr);

    int cycles_consumed = 0;

    switch (type) {
    case ARM_BAD_INSTR:
        fprintf(stderr, "[ARM] invalid opcode: #0x%08X\n", instr);
        exit(1);
    case THUMB_BAD_INSTR:
        fprintf(stderr, "[THUMB] invalid opcode: #0x%04X\n", instr);
        exit(1);
    case THUMB_LOAD_PC_RELATIVE:
    case THUMB_RELATIVE_ADDRESS:
    case THUMB_LONG_BRANCH_1:
    case THUMB_LONG_BRANCH_2:
        DEBUG_PRINT(("[THUMB] (%08X) %08X ", cpu->registers.r15 - 4, cpu->curr_instr))
        cycles_consumed = thumb_handler(type);
        break;
    default:
        DEBUG_PRINT(("[%s] (%08X) %08X ", THUMB_ACTIVATED ? "THUMB" : "ARM", cpu->registers.r15 - (THUMB_ACTIVATED ? 4 : 8), cpu->curr_instr))
        cycles_consumed = arm_handler(type);
    }

    return cycles_consumed;
}

// NOTE: change in the future so CPU is not a malloc'd object
// no plans for any sort of communication betweeen multiple GBA instances
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
    while (total_cycles < CYCLES_PER_FRAME) {
        int cycles_passed = execute();
        for (int j = 0; j < cycles_passed; j++) {
            tick_ppu();
        }
        total_cycles += cycles_passed;
    }

    return frame;
}
