#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "decompressor.h"

InstrType thumb_decompress_1(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100000000100000000000000000000;

    Word shift_amount = (thumb_instr >> 6) & 0x1F;
    Word rs = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;

    ShiftType shift_type;

    switch ((thumb_instr >> 11) & 0x3) {
    case 0x0:
        shift_type = SHIFT_TYPE_LSL;
        break;
    case 0x1:
        shift_type = SHIFT_TYPE_LSR;
        break;
    case 0x2:
        shift_type = SHIFT_TYPE_ASR;
        break;
    case 0x3:
        shift_type = SHIFT_TYPE_ROR;
        break;
    }

    translation |= (0xD << 21);
    translation |= (rd << 12);
    translation |= (shift_amount << 7);
    translation |= (shift_type << 5);
    translation |= rs;

    *arm_instr = translation;
    return DataProcessing;
}

InstrType thumb_decompress_2(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100000000100000000000000000000;

    Word rn_or_nn = (thumb_instr >> 6) & 0x7;
    Word rs = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;

    Word arm_opcode;

    switch ((thumb_instr >> 9) & 0x3) {
    case 0x3:
        translation |= (0x1 << 25);
    case 0x1:
        arm_opcode = 0x2;
        break;
    case 0x2:
        translation |= (0x1 << 25);
    case 0x0:
        arm_opcode = 0x4;
    }

    translation |= (arm_opcode << 21);
    translation |= (rs << 16);
    translation |= (rd << 12);
    translation |= rn_or_nn;

    *arm_instr = translation;
    return DataProcessing;
}

InstrType thumb_decompress_3(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100010000100000000000000000000;

    Word rd = (thumb_instr >> 8) & 0x7;
    Word nn = thumb_instr & 0xFF;

    Word arm_opcode;

    switch ((thumb_instr >> 11) & 0x3) {
    case 0x0:
        arm_opcode = 0xD;
        break;
    case 0x1:
        arm_opcode = 0xA;
        break;
    case 0x2:
        arm_opcode = 0x4;
        break;
    case 0x3:
        arm_opcode = 0x2;
    }

    translation |= (arm_opcode << 21);
    translation |= (rd << 16);
    translation |= (rd << 12);
    translation |= nn;

    *arm_instr = translation;
    return DataProcessing;
}

InstrType thumb_decompress_4(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100000000100000000000000000000;

    Word rd = thumb_instr & 0x7;
    Word rs = (thumb_instr >> 3) & 0x7;
    Word arm_opcode = (thumb_instr >> 6) & 0xF;
    ShiftType shift_type = SHIFT_TYPE_LSL;

    translation |= (rd << 12);

    switch ((thumb_instr >> 6) & 0xF) {
    case 0x2:
        goto thumb_shift_instr;
    case 0x3:
        shift_type = SHIFT_TYPE_LSR;
        goto thumb_shift_instr;
    case 0x4:
        shift_type = SHIFT_TYPE_ASR;
        goto thumb_shift_instr;
    case 0x7:
        shift_type = SHIFT_TYPE_ROR;
        goto thumb_shift_instr;
    case 0x9: // [unique case #1] NEG -> RSB Rd, Rs, #0
        translation |= (0x3 << 21);
        translation |= (0x1 << 25);
        translation |= (rs << 16);

        *arm_instr = translation;
        return DataProcessing;
    case 0xD: // [unique case #2] MUL -> MULS Rd, Rs, Rd
        translation |= (rd << 16);
        translation |= rs;
        translation |= (rd << 8);

        *arm_instr = translation;
        return Multiply;
    }

    translation |= rs;

    complete_translation:
        translation |= (arm_opcode << 21);
        translation |= (rd << 16);
        translation |= (shift_type << 5);

        *arm_instr = translation;
        return DataProcessing;

    // LSL,LSR,ASR,ROR -> MOV rd, rd <SHIFT> rs
    thumb_shift_instr:
        arm_opcode = 0xD;
        translation |= (0x1 << 4);
        translation |= (rs << 8);
        translation |= rd;

        goto complete_translation;
}

InstrType thumb_decompress_5(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100000000000000000000000000000;

    Word rs = (((thumb_instr >> 6) & 1) << 3) | ((thumb_instr >> 3) & 0x7); // MSBs added (r0-r15)
    Word rd = (((thumb_instr >> 7) & 1) << 3) | (thumb_instr & 0x7); // MSBd added (r0-r15)

    Word arm_opcode;

    switch ((thumb_instr >> 8) & 0x3) {
    case 0x0:
        arm_opcode = 0x4;
        break;
    case 0x1: 
        translation |= (1 << 20); // CMP is the only instruction that will set cc here
        arm_opcode = 0xA;
        break;
    case 0x2:
        arm_opcode = 0xD;
        break;
    case 0x3: // [unique case #1] -> decompressed to BX Rs
        translation = 0b11100001001011111111111100010000;
        translation |= rs;
        
        *arm_instr = translation;
        return BranchExchange;
    }

    translation |= (arm_opcode << 21);
    translation |= (rd << 16);
    translation |= (rd << 12);
    translation |= rs;

    *arm_instr = translation;
    return DataProcessing;
}

InstrType thumb_decompress_16(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b00001010000000000000000000000000;

    Word cond = (thumb_instr >> 8) & 0xF;
    Word offset = (int32_t)(int8_t)(thumb_instr & 0xFF);

    translation |= (cond << 28);
    translation |= (offset & 0xFFFFFF);

    *arm_instr = translation;
    return Branch;
}

InstrType thumb_decompress_18(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11101010000000000000000000000000;

    Word offset = (int32_t)((int16_t)((thumb_instr & 0x7FF) << 5) >> 5);
    translation |= (offset & 0xFFFFFF);

    *arm_instr = translation;
    return Branch;
}
