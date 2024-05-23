#include <stdint.h>

typedef uint32_t Word;
typedef uint16_t HalfWord;
typedef uint8_t Byte;
typedef bool Bit;

typedef enum {
    SHIFT_TYPE_LSL,
    SHIFT_TYPE_LSR,
    SHIFT_TYPE_ASR,
    SHIFT_TYPE_ROR
} ShiftType;

typedef enum {
    BRANCH,
    BRANCH_X,
    BlockDataTransfer,
    HalfwordDataTransfer,
    SingleDataTransfer,
    Multiply,
    SWI,
    ALU,
    MSR,
    MRS,
    SWP,
    NOP,

    THUMB_LOAD_PC_RELATIVE,
    THUMB_RELATIVE_ADDRESS,
    THUMB_LONG_BRANCH_1,
    THUMB_LONG_BRANCH_2,

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
