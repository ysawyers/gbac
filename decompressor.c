#include <stdbool.h>
#include "decompressor.h"

InstrType thumb_decompress_4(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100000000100000000000000000000;

    uint8_t rd = thumb_instr & 0x7;
    uint8_t rs = (thumb_instr >> 3) & 0x7;
    uint8_t opcode = (thumb_instr >> 6) & 0xF;
    ShiftType shift_type = SHIFT_TYPE_LSL;

    bool shift_by_rs = false;

    translation |= ((Word)rd << 12);

    switch ((thumb_instr >> 6) & 0xF) {
    case 0x2:
        shift_by_rs = true;
        break;
    case 0x3:
        shift_type = SHIFT_TYPE_LSR;
        shift_by_rs = true;
        break;
    case 0x4:
        shift_type = SHIFT_TYPE_ASR;
        shift_by_rs = true;
        break;
    case 0x7:
        shift_type = SHIFT_TYPE_ROR;
        shift_by_rs = true;
        break;
    case 0x9: // [unique case #1] NEG -> RSB Rd, Rs, #0
        translation |= (0x3 << 21);
        translation |= (0x1 << 25);
        translation |= ((Word)rs << 16);

        *arm_instr = translation;
        return DataProcessing;
    case 0xD: // [unique case #2] MUL -> MULS Rd, Rs, Rd
        translation |= ((Word)rd << 16);
        translation |= rs;
        translation |= ((Word)rd << 8);

        *arm_instr = translation;
        return Multiply;
    }

    translation |= ((Word)rd << 16);

    // LSL,LSR,ASR,ROR -> MOV Rd, Rd <SHIFT> Rs
    if (shift_by_rs) {
        opcode = 0xD;
        translation |= (0x1 << 4);
        translation |= ((Word)rs << 8);
        translation |= rd;
    } else {
        translation |= rs;
    }

    translation |= ((Word)opcode << 21);
    translation |= ((Word)shift_type << 5);

    *arm_instr = translation;
    return DataProcessing;
}
