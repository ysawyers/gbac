#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "cpu.h"
#include "memory.h"

#define THUMB_ACTIVATED (registers.cspr >> 5 & 1)
#define PROCESSOR_MODE  (registers.cspr & 0x1F)

#define SIGN_FLAG     (registers.cspr >> 31 & 1)
#define ZERO_FLAG     (registers.cspr >> 30 & 1)
#define CARRY_FLAG    (registers.cspr >> 29 & 1)
#define OVERFLOW_FLAG (registers.cspr >> 28 & 1)

#define ROR(nn, Is) ((nn) >> (Is) | (nn) << (32 - (Is)))

typedef uint32_t Instr;
typedef bool Bit;

typedef enum {
    Branch,
    BlockDataTransfer,
    ALU
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

// r0-r7 (Lo registers) r8-r12 (Hi registers)
typedef struct {
    // general registers
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

    // banked registers
    int32_t r13;  // SP (Stack pointer)
    int32_t r14;  // LR (Link register)
    uint32_t r15; // PC (Program counter)
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

    // status registers
    int32_t cspr;
    int32_t spsr_fiq;
    int32_t spsr_svc;
    int32_t spsr_abt;
    int32_t spsr_irq;
    int32_t spsr_und;
} RegisterSet;

RegisterSet registers = {0};

Instr pipeline = 0;

static inline Bit cond(Instr instr) {
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
    case 0xB: return SIGN_FLAG != OVERFLOW_FLAG;
    case 0xC: return !ZERO_FLAG & (SIGN_FLAG == OVERFLOW_FLAG);
    case 0xD: return ZERO_FLAG | (SIGN_FLAG != OVERFLOW_FLAG);
    case 0xE: return true;
    }
}

static inline char* cond_to_cstr(Instr instr) {
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
    case 0xE: return "AL";
    }
}

static inline char* amod_to_cstr(Bit p, Bit u) {
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

static inline Instr fetch() {
    Instr instr;

    if (THUMB_ACTIVATED) {
        instr = read_half_word(registers.r15);
        registers.r15 += 2;
    } else {
        instr = read_word(registers.r15);
        registers.r15 += 4;
    }

    return instr;
}

static inline InstrType decode(Instr instr) {
    pipeline = fetch();

    switch ((instr >> 25) & 0x7) {
    case 0x0:
        if ((instr >> 7) & 0x1 == 0) exit(1); // BX

        

        printf("INSTR: %08X\n", instr);
        exit(1);
    case 0x1: return ALU;
    case 0x2:
        fprintf(stderr, "unhandled encoding for 0x%04X???\n", instr);
        exit(1);
    case 0x3:
        fprintf(stderr, "unhandled encoding for 0x%04X\n", instr);
        exit(1);
    case 0x4: return BlockDataTransfer;
    case 0x5: return Branch;
    case 0x6:
        fprintf(stderr, "unhandled encoding for 0x%04X\n", instr);
        exit(1);
    case 0x7:
        fprintf(stderr, "unhandled encoding for 0x%04X\n", instr);
        exit(1);
    }
}

static inline void execute(Instr instr, InstrType type) {
    bool flush_pipeline = false;

    printf("[%s] (%08X) %08X ", THUMB_ACTIVATED ? "THUMB" : "ARM", registers.r15, instr);

    if (cond(instr)) {
        switch (type) {
        case Branch: {
            Bit with_link = (instr >> 24) & 1;
            if (with_link & !THUMB_ACTIVATED) {
                fprintf(stderr, "branch not implemented\n");
                exit(1);
            } else {
                int32_t offset = instr & 0xFFFFFF;
                registers.r15 += offset * 4;
            }
            flush_pipeline = true;

            printf("B%s #%08X\n", cond_to_cstr(instr), registers.r15);

            break;
        }
        case BlockDataTransfer: {
            Bit p = (instr >> 24) & 1;
            Bit u = (instr >> 23) & 1;
            Bit s = (instr >> 22) & 1;
            Bit w = (instr >> 21) & 1;
            Bit l = (instr >> 21) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint16_t reg_list = instr & 0xFFFF;

            if (l) {
                
            } else {

            }

            printf("%s%s%s %s, <reglist>$%04X\n", l ? "LDM" : "STM", cond_to_cstr(instr), amod_to_cstr(p, u), register_to_cstr(rn), reg_list);
            exit(1);
        }
        case ALU: {
            uint8_t opcode = (instr >> 21) & 0xF;
            Bit i = (instr >> 25) & 1;
            Bit s = (instr >> 20) & 1;

            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            uint32_t operand2 = instr & 0xFFF;

            if (i) {
                operand2 = ROR(operand2 & 0xFF, ((operand2 & 0xF00) >> 8) * 2);
            } else {
                fprintf(stderr, "MOV with i not set not implemented 0x%01X\n", opcode);
                exit(1);
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
                    printf("TODO cspr flags for here\n");
                    exit(1);
                }
                set_register(rd, result);
                printf("ADD%s %s, %s, #%08X\n", cond_to_cstr(instr), register_to_cstr(rd), register_to_cstr(rn), operand2);
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
            case 0xA:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xB:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xC:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xD:
                set_register(rd, operand2);
                printf("MOV%s %s, #%08X\n", cond_to_cstr(instr), register_to_cstr(rd), operand2);
                break;
            case 0xE:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            case 0xF:
                fprintf(stderr, "ALU instruction not implemented: 0x%01X\n", opcode);
                exit(1);
            }
        }
        }

        if (flush_pipeline) pipeline = 0;
    } else {
        printf("\n");
    }
}

static inline void tick_cpu() {
    if (!pipeline) {
        Instr instr = fetch();
        execute(instr, decode(instr));
    } else {
        execute(pipeline, decode(pipeline));
    }
}

void start(char *rom_file, char *bios_file) {
    load_bios(bios_file);
    load_rom(rom_file);

    registers.r13_svc = 0x03007FE0;
    registers.r13_irq = 0x03007FA0;
    registers.r13 = 0x03007F00;

    registers.r15 = 0x08000000;

    tick_cpu();
    tick_cpu();
    tick_cpu();
    tick_cpu();
    tick_cpu();
}
