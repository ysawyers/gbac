#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "cpu.h"
#include "ppu.h"
#include "memory.h"

#define CC_UNSET 0
#define CC_SET   1
#define CC_UNMOD 2

#define THUMB_ACTIVATED (registers.cspr >> 5 & 1)
#define PROCESSOR_MODE  (registers.cspr & 0x1F)

#define SIGN_FLAG     (registers.cspr >> 31 & 1)
#define ZERO_FLAG     (registers.cspr >> 30 & 1)
#define CARRY_FLAG    (registers.cspr >> 29 & 1)
#define OVERFLOW_FLAG (registers.cspr >> 28 & 1)

#define ROR(n, shift) ((n) >> (shift) | (n) << (32 - (shift)))

typedef bool bit;

typedef enum {
    Branch,
    BranchExchange,
    BlockDataTransfer,
    HalfwordDataTransfer,
    SingleDataTransfer,
    DataProcessing,
    MUL,
    MULL,
    SWP,
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

typedef struct {
    int32_t r0;
    int32_t r1;
    int32_t r2;
    int32_t r3;
    int32_t r4;
    int32_t r5;
    int32_t r6;
    int32_t r7;
    int32_t r8;
    int32_t r9;
    int32_t r10;
    int32_t r11;
    int32_t r12;

    int32_t r13;  // SP (Stack pointer)
    int32_t r14;  // LR (Link register)
    int32_t r15;  // PC (Program counter)
    int32_t r8_fiq;
    int32_t r9_fiq;
    int32_t r10_fiq;
    int32_t r11_fiq;
    int32_t r12_fiq;
    int32_t r13_fiq;
    int32_t r14_fiq;
    int32_t r13_svc;
    int32_t r14_svc;
    int32_t r13_abt;
    int32_t r14_abt;
    int32_t r13_irq;
    int32_t r14_irq;
    int32_t r13_und;
    int32_t r14_und;

    int32_t cspr;
    int32_t spsr_fiq;
    int32_t spsr_svc;
    int32_t spsr_abt;
    int32_t spsr_irq;
    int32_t spsr_und;
} RegisterSet;

uint32_t pipeline = 0;

RegisterSet registers = {0};

static inline char* cond_to_cstr(uint32_t instr) {
    switch (instr >> 28) {
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
}

static inline char* amod_to_cstr(bit p, bit u) {
    switch ((p << 1) | u) {
    case 0: return "DA";
    case 1: return "IA";
    case 2: return "DB";
    case 3: return "IB";
    }
}

static inline char* register_to_cstr(uint8_t r) {
    switch (r) {
    case 0: return "r0";
    case 1: return "r1";
    case 2: return "r2";
    case 3: return "r3";
    case 4: return "r4";
    case 5: return "r5";
    case 6: return "r6";
    case 7: return "r7";
    case 8: return "r8";
    case 9: return "r9";
    case 10: return "r10";
    case 11: return "r11";
    case 12: return "r12";
    case 13: return "r13";
    case 14: return "r14";
    case 15: return "r15";
    }
}

static inline bit cond(uint32_t instr) {
    switch (instr >> 28) {
    case 0x0: return ZERO_FLAG;
    case 0x1: return !ZERO_FLAG;
    case 0x2: return CARRY_FLAG;
    case 0x3: return !CARRY_FLAG;
    case 0x4: return SIGN_FLAG;
    case 0x5: return !SIGN_FLAG;
    case 0x6: return OVERFLOW_FLAG;
    case 0x7: return !OVERFLOW_FLAG;
    case 0x8: return CARRY_FLAG & !ZERO_FLAG;
    case 0x9: return !CARRY_FLAG | ZERO_FLAG;
    case 0xA: return SIGN_FLAG == OVERFLOW_FLAG;
    case 0xB: return SIGN_FLAG ^ OVERFLOW_FLAG;
    case 0xC: return !ZERO_FLAG && (SIGN_FLAG == OVERFLOW_FLAG);
    case 0xD: return ZERO_FLAG || (SIGN_FLAG ^ OVERFLOW_FLAG);
    case 0xE: return true;
    }
}

static inline int32_t get_register_val(uint8_t r) {
    switch (r) {
    case 0x0: return registers.r0;
    case 0x1: return registers.r1;
    case 0x2: return registers.r2;
    case 0x3: return registers.r3;
    case 0x4: return registers.r4;
    case 0x5: return registers.r5;
    case 0x6: return registers.r6;
    case 0x7: return registers.r7;
    case 0x8: return registers.r8;
    case 0x9: return registers.r9;
    case 0xA: return registers.r10;
    case 0xB: return registers.r11;
    case 0xC: return registers.r12;
    case 0xD:
        switch (PROCESSOR_MODE) {
        case User:
        case System:
            return registers.r13;
        case FIQ: return registers.r13_fiq;
        case IRQ: return registers.r13_irq;
        case Supervisor: return registers.r13_svc;
        case Abort: return registers.r13_abt;
        case Undefined: return registers.r13_und;
        }
    case 0xE:
        switch (PROCESSOR_MODE) {
        case User:
        case System:
            return registers.r14;
        case FIQ: return registers.r14_fiq;
        case IRQ: return registers.r14_irq;
        case Supervisor: return registers.r14_svc;
        case Abort: return registers.r14_abt;
        case Undefined: return registers.r14_und;
        }
    case 0xF: return registers.r15;
    }
}

static inline void set_register(uint8_t r, int32_t v) {
    switch (r) {
    case 0x0:
        registers.r0 = v;
        break;
    case 0x1:
        registers.r1 = v;
        break;
    case 0x2:
        registers.r2 = v;
        break;
    case 0x3:
        registers.r3 = v;
        break;
    case 0x4:
        registers.r4 = v;
        break;
    case 0x5:
        registers.r5 = v;
        break;
    case 0x6:
        registers.r6 = v;
        break;
    case 0x7:
        registers.r7 = v;
        break;
    case 0x8:
        registers.r8 = v;
        break;
    case 0x9:
        registers.r9 = v;
        break;
    case 0xA:
        registers.r10 = v;
        break;
    case 0xB:
        registers.r11 = v;
        break;
    case 0xC:
        registers.r12 = v;
        break;
    case 0xD:
        switch (PROCESSOR_MODE) {
        case User:
        case System:
            registers.r13 = v;
            break;
        case FIQ:
            registers.r13_fiq = v;
            break;
        case IRQ:
            registers.r13_irq = v;
            break;
        case Supervisor:
            registers.r13_svc = v;
            break;
        case Abort:
            registers.r13_abt = v;
            break;
        case Undefined:
            registers.r13_und = v;
            break;
        }
    case 0xE:
        switch (PROCESSOR_MODE) {
        case User:
        case System:
            registers.r14 = v;
            break;
        case FIQ:
            registers.r14_fiq = v;
            break;
        case IRQ:
            registers.r14_irq = v;
            break;
        case Supervisor:
            registers.r14_svc = v;
            break;
        case Abort:
            registers.r14_abt = v;
            break;
        case Undefined:
            registers.r14_und = v;
            break;
        }
    case 0xF:
        registers.r15 = v;
        break;
    }
}

static inline void set_cc(uint8_t n, uint8_t z, uint8_t c, uint8_t v) {
    switch (PROCESSOR_MODE) {
    case User:
    case System:
        if (n != CC_UNMOD) registers.cspr = n ? (1 << 31) | registers.cspr : ~(1 << 31) & registers.cspr;
        if (z != CC_UNMOD) registers.cspr = z ? (1 << 30) | registers.cspr : ~(1 << 30) & registers.cspr;
        if (c != CC_UNMOD) registers.cspr = c ? (1 << 29) | registers.cspr : ~(1 << 29) & registers.cspr;
        if (v != CC_UNMOD) registers.cspr = v ? (1 << 28) | registers.cspr : ~(1 << 28) & registers.cspr;
        break;
    case FIQ:
        if (n != CC_UNMOD) registers.spsr_fiq = n ? (1 << 31) | registers.spsr_fiq : ~(1 << 31) & registers.spsr_fiq;
        if (z != CC_UNMOD) registers.spsr_fiq = z ? (1 << 30) | registers.spsr_fiq : ~(1 << 30) & registers.spsr_fiq;
        if (c != CC_UNMOD) registers.spsr_fiq = c ? (1 << 29) | registers.spsr_fiq : ~(1 << 29) & registers.spsr_fiq;
        if (v != CC_UNMOD) registers.spsr_fiq = v ? (1 << 28) | registers.spsr_fiq : ~(1 << 28) & registers.spsr_fiq;
        break;
    case IRQ:
        if (n != CC_UNMOD) registers.spsr_irq = n ? (1 << 31) | registers.spsr_irq : ~(1 << 31) & registers.spsr_irq;
        if (z != CC_UNMOD) registers.spsr_irq = z ? (1 << 30) | registers.spsr_irq : ~(1 << 30) & registers.spsr_irq;
        if (c != CC_UNMOD) registers.spsr_irq = c ? (1 << 29) | registers.spsr_irq : ~(1 << 29) & registers.spsr_irq;
        if (v != CC_UNMOD) registers.spsr_irq = v ? (1 << 28) | registers.spsr_irq : ~(1 << 28) & registers.spsr_irq;
        break;
    case Supervisor:
        if (n != CC_UNMOD) registers.spsr_svc = n ? (1 << 31) | registers.spsr_svc : ~(1 << 31) & registers.spsr_svc;
        if (z != CC_UNMOD) registers.spsr_svc = z ? (1 << 30) | registers.spsr_svc : ~(1 << 30) & registers.spsr_svc;
        if (c != CC_UNMOD) registers.spsr_svc = c ? (1 << 29) | registers.spsr_svc : ~(1 << 29) & registers.spsr_svc;
        if (v != CC_UNMOD) registers.spsr_svc = v ? (1 << 28) | registers.spsr_svc : ~(1 << 28) & registers.spsr_svc;
        break;
    case Abort:
        if (n != CC_UNMOD) registers.spsr_abt = n ? (1 << 31) | registers.spsr_abt : ~(1 << 31) & registers.spsr_abt;
        if (z != CC_UNMOD) registers.spsr_abt = z ? (1 << 30) | registers.spsr_abt : ~(1 << 30) & registers.spsr_abt;
        if (c != CC_UNMOD) registers.spsr_abt = c ? (1 << 29) | registers.spsr_abt : ~(1 << 29) & registers.spsr_abt;
        if (v != CC_UNMOD) registers.spsr_abt = v ? (1 << 28) | registers.spsr_abt : ~(1 << 28) & registers.spsr_abt;
        break;
    case Undefined:
        if (n != CC_UNMOD) registers.spsr_und = n ? (1 << 31) | registers.spsr_und : ~(1 << 31) & registers.spsr_und;
        if (z != CC_UNMOD) registers.spsr_und = z ? (1 << 30) | registers.spsr_und : ~(1 << 30) & registers.spsr_und;
        if (c != CC_UNMOD) registers.spsr_und = c ? (1 << 29) | registers.spsr_und : ~(1 << 29) & registers.spsr_und;
        if (v != CC_UNMOD) registers.spsr_und = v ? (1 << 28) | registers.spsr_und : ~(1 << 28) & registers.spsr_und;
        break;
    }
}

static inline uint32_t fetch() {
    uint32_t instr;

    if (THUMB_ACTIVATED) {
        instr = read_half_word(registers.r15);
        registers.r15 += 2;
    } else {
        instr = read_word(registers.r15);
        registers.r15 += 4;
    }

    return instr;
}

static inline InstrType decode_arm(uint32_t instr) {
    pipeline = fetch();

    switch ((instr >> 25) & 0x7) {
    case 0x0:
        switch ((instr >> 4) & 0xF) {
        case 0x1:
            if (((instr >> 8) & 0xF) == 0xF) return BranchExchange;
            return DataProcessing;
        case 0x9:
            fprintf(stderr, "unhandled decoding: %08X\n", instr);
            exit(1);
        case 0xB:
        case 0xD: return HalfwordDataTransfer;
        default: return DataProcessing;
        }
    case 0x1: return DataProcessing;
    case 0x2:
    case 0x3: return SingleDataTransfer;
    case 0x4: return BlockDataTransfer;
    case 0x5: return Branch;
    case 0x6:
        fprintf(stderr, "coprocessor data transfer\n");
        exit(1);
    case 0x7:
        fprintf(stderr, "unhandled decoding: %08X\n", instr);
        exit(1);
    }
}

static inline int execute(uint32_t instr, InstrType type) {
    bool flush_pipeline = false;
    int cycles = 0;

    DEBUG_PRINT(("[%s] (%08X) %08X ", THUMB_ACTIVATED ? "THUMB" : "ARM", registers.r15 - 8, instr))

    if (cond(instr)) {
        switch (type) {
        case Branch: {
            bit with_link = (instr >> 24) & 1;
            if (with_link & !THUMB_ACTIVATED) {
                fprintf(stderr, "branch not implemented\n");
                exit(1);
            } else {
                int32_t offset = (instr & 0xFFFFFF) << 8;
                registers.r15 += (offset >> 8) * 4;
            }
            flush_pipeline = true;
            DEBUG_PRINT(("B%s #0x%X\n", cond_to_cstr(instr), registers.r15))
            break;
        }
        case BlockDataTransfer: {
            bit p = (instr >> 24) & 1;
            bit u = (instr >> 23) & 1;
            bit s = (instr >> 22) & 1;
            bit w = (instr >> 21) & 1;
            bit l = (instr >> 21) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint16_t reg_list = instr & 0xFFFF;

            if (l) {
                
            } else {

            }

            DEBUG_PRINT(("%s%s%s %s, <reglist>$%04X\n", l ? "LDM" : "STM", cond_to_cstr(instr), amod_to_cstr(p, u), register_to_cstr(rn), reg_list))
            exit(1);
            break;
        }
        case DataProcessing: {
            uint8_t opcode = (instr >> 21) & 0xF;
            bit i = (instr >> 25) & 1;
            bit s = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            uint32_t operand2;

            if (i) {
                operand2 = ROR(instr & 0xFF, ((instr & 0xF00) >> 8) * 2);
            } else {
                uint8_t shift_type = (instr >> 5) & 0x3;
                bit r = (instr >> 4) & 1;
                uint8_t rm = instr & 0xF;

                uint8_t shift_amount;

                if (r) {
                    DEBUG_PRINT(("i = 0 r = 1 branch case\n"))
                    exit(1);
                } else {
                    shift_amount = (instr >> 7) & 0x1F;
                }

                switch (shift_type) {
                case 0:
                    operand2 = shift_amount > 31 ? 0 : get_register_val(rm) << shift_amount;
                    break;
                case 1:
                    exit(1);
                case 2: 
                    exit(1);
                case 3:
                    exit(1);
                }
            }

            switch (opcode) {
            case 0x0:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x1:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x2:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x3:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x4: {
                int32_t result = get_register_val(rn) + operand2;
                if (s) {
                    DEBUG_PRINT(("TODO set flags for ADD ALU\n"))
                    exit(1);
                }
                set_register(rd, result);
                DEBUG_PRINT(("ADD%s %s, %s, #0x%X\n", cond_to_cstr(instr), register_to_cstr(rd), register_to_cstr(rn), operand2))
                break;
            }
            case 0x5:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x6:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x7:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x8:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0x9:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xA: {
                int32_t result = get_register_val(rn) - operand2;
                set_cc(result < 0, result == 0, result > 0, operand2 < 0 && get_register_val(rn) < 0 && result > 0);
                DEBUG_PRINT(("CMP%s %s, #0x%X\n", cond_to_cstr(instr), register_to_cstr(rn), operand2))
                break;
            }
            case 0xB:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xC:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xD:
                set_register(rd, operand2);
                if (s) {
                    fprintf(stderr, "TODO set flags for MOV ALU\n");
                    exit(1);
                };
                DEBUG_PRINT(("MOV%s %s, #0x%X\n", cond_to_cstr(instr), register_to_cstr(rd), operand2))
                break;
            case 0xE:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xF:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            }
            break;
        }
        case HalfwordDataTransfer: {
            bit p = (instr >> 24) & 1;
            bit u = (instr >> 23) & 1;
            bit i = (instr >> 22) & 1;
            bit w = (instr >> 21) & 1;
            bit l = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            int32_t offset = i ? ((((instr >> 8) & 0xF) << 4) | (instr & 0xF)) : get_register_val(instr & 0xF);
            if (u) offset = -offset;

            uint32_t address = get_register_val(rn) + offset;

            if (l) {
                switch ((instr >> 5) & 0x3) {
                case 1:
                    if (p) {
                        set_register(rd, read_half_word(address));
                    } else {
                        set_register(rd, read_half_word(get_register_val(rn)));
                    }
                    DEBUG_PRINT(("LDR%sH ", cond_to_cstr(instr)))
                    break;
                case 2:
                    DEBUG_PRINT(("LDR%sSB ", cond_to_cstr(instr)))
                    break;
                case 3:
                    DEBUG_PRINT(("LDR%sSH ", cond_to_cstr(instr)))
                    break;
                }
            } else {
                switch ((instr >> 5) & 0x3) {
                case 1:
                    if (p) {
                        write_half_word(address, get_register_val(rd));
                    } else {
                        write_half_word(get_register_val(rn), get_register_val(rd));
                    }
                    DEBUG_PRINT(("STR%sH ", cond_to_cstr(instr)))
                    break;
                case 2:
                    DEBUG_PRINT(("LDR%sD ", cond_to_cstr(instr)))
                    break;
                case 3:
                    DEBUG_PRINT(("STR%sD ", cond_to_cstr(instr)))
                    break;
                }
            }

            // post indexing implies writeback
            if ((p && w) || !p) set_register(rn, address);

            DEBUG_PRINT(("%s, ", register_to_cstr(rd)))
            if (p) {
                if (i) {
                    DEBUG_PRINT(("[%s", register_to_cstr(rn)))
                    if (offset) {
                        DEBUG_PRINT((", #0x%X]", u ? -offset : offset))
                    } else {
                        DEBUG_PRINT(("]"))
                    }
                } else {
                    DEBUG_PRINT((", %s]", register_to_cstr(instr & 0xF)))
                }

                if (w) {
                    DEBUG_PRINT(("\n"));
                } else {
                    DEBUG_PRINT(("!\n"))
                }
            } else {
                DEBUG_PRINT(("[%s], ", register_to_cstr(rn)))
                if (i) {
                    DEBUG_PRINT(("#0x%X\n", u ? -offset : offset))
                } else {
                    DEBUG_PRINT((", %s\n", register_to_cstr(instr & 0xF)))
                }
            }
            break;
        }
        default:
            fprintf(stderr, "decoded instruction not handled yet!\n");
            exit(1);
        }

        if (flush_pipeline) pipeline = 0;
    } else {
        cycles = 1;
        DEBUG_PRINT(("\n"));
    }

    return 1;
}

static inline int tick_cpu() {
    if (!pipeline) {
        uint32_t instr = fetch();
        return execute(instr, decode_arm(instr));
    } else {
        return execute(pipeline, decode_arm(pipeline));
    }
}

void start(char *rom_file, char *bios_file) {
    load_bios(bios_file);
    load_rom(rom_file);

    // initialize call stack
    registers.r13_svc = 0x03007FE0;
    registers.r13_irq = 0x03007FA0;
    registers.r13 = 0x03007F00;

    // initialize PC and CPU mode
    registers.r15 = 0x08000000;
    registers.cspr |= System;

    for (int i = 0; i < 0xFFFFFF; i++) {
        int cycles = tick_cpu();
        for (int j = 0; j < cycles; j++) {
            tick_ppu();
        }
    }
}
