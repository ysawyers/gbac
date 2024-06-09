#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "decompressor.h"

InstrType thumb_decompress_1(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100001101100000000000000000000;

    Word shift_amount = (thumb_instr >> 6) & 0x1F;
    Word rs = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;
    ShiftType shift_type = (thumb_instr >> 11) & 0x3;

    translation |= (rd << 12);
    translation |= (shift_amount << 7);
    translation |= (shift_type << 5);
    translation |= rs;

    *arm_instr = translation;
    return ALU;
}

InstrType thumb_decompress_2(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100000000100000000000000000000;

    Word rn_or_nn = (thumb_instr >> 6) & 0x7;
    Word rs = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;
    Word i = (thumb_instr >> 10) & 1;
    Word arm_opcode = 0x2 << !((thumb_instr >> 9) & 1);

    translation |= (i << 25);
    translation |= (arm_opcode << 21);
    translation |= (rs << 16);
    translation |= (rd << 12);
    translation |= rn_or_nn;

    *arm_instr = translation;
    return ALU;
}

InstrType thumb_decompress_3(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100010000100000000000000000000;

    Word rd = (thumb_instr >> 8) & 0x7;
    Word nn = thumb_instr & 0xFF;

    Word thumb_opcode = (thumb_instr >> 11) & 0x3;
    Word arm_opcode = 0b1101;

    arm_opcode = (arm_opcode << thumb_opcode) & 0xF;
    arm_opcode >>= (!(~thumb_opcode & 0x3) << 1);

    translation |= (arm_opcode << 21);
    translation |= (rd << 16);
    translation |= (rd << 12);
    translation |= nn;

    *arm_instr = translation;
    return ALU;
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
        return ALU;
    case 0xD: // [unique case #2] MUL -> MULS Rd, Rs, Rd
        translation |= (rd << 16);
        translation |= rs;
        translation |= (rd << 8);

        *arm_instr = translation;
        return MULTIPLY;
    }

    translation |= rs;

    complete_translation:
        translation |= (arm_opcode << 21);
        translation |= (rd << 16);
        translation |= (shift_type << 5);

        *arm_instr = translation;
        return ALU;

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
    Word thumb_opcode = (thumb_instr >> 8) & 0x3;
    Word arm_opcode = 0b110100;

    if (thumb_opcode == 0x3) {
        translation = 0b11100001001011111111111100010000;
        translation |= rs;
        
        *arm_instr = translation;
        return BRANCH_X;
    }

    arm_opcode = (arm_opcode >> thumb_opcode) & 0xF;
    
    translation |= ((thumb_opcode == 0x1) << 20);
    translation |= (arm_opcode << 21);
    translation |= (rd << 16);
    translation |= (rd << 12);
    translation |= rs;

    *arm_instr = translation;
    return ALU;
}

InstrType thumb_decompress_7(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100111100000000000000000000000;

    Word ro = (thumb_instr >> 6) & 0x7;
    Word rb = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;
    Word b = (thumb_instr >> 10) & 1;
    Word l = (thumb_instr >> 11) & 1;

    translation |= (b << 22);
    translation |= (l << 20);
    translation |= (rb << 16);
    translation |= (rd << 12);
    translation |= ro;

    *arm_instr = translation;
    return SINGLE_TRANSFER;
}

InstrType thumb_decompress_8(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100001100000000000000010010000;

    Word ro = (thumb_instr >> 6) & 0x7;
    Word rb = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;
    Word thumb_opcode = (thumb_instr >> 10) & 0x3;
    Word arm_opcode = (((thumb_opcode << 1) & 0x3) | ((thumb_opcode >> 1) & 1)) | !thumb_opcode;
    Word l = !!thumb_opcode;

    translation |= (l << 20);
    translation |= (rb << 16);
    translation |= (rd << 12);
    translation |= (arm_opcode << 5);
    translation |= ro;

    *arm_instr = translation;
    return HALFWORD_TRANSFER;
}

InstrType thumb_decompress_9(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100101100000000000000000000000;
    
    Word rb = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;
    Word b = (thumb_instr >> 12) & 1;
    Word l = (thumb_instr >> 11) & 1;
    Word nn = ((thumb_instr >> 6) & 0x1F) << (!b << 1); // step by 4 for WORD access

    translation |= (b << 22);
    translation |= (l << 20);
    translation |= (rb << 16);
    translation |= (rd << 12);
    translation |= nn;

    *arm_instr = translation;
    return SINGLE_TRANSFER;
}

InstrType thumb_decompress_10(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100001110000000000000010110000;

    Word rb = (thumb_instr >> 3) & 0x7;
    Word rd = thumb_instr & 0x7;
    Word nn = ((thumb_instr >> 6) & 0x1F) << 1;
    Word l = ((thumb_instr >> 11) & 1);

    translation |= (l << 20);
    translation |= (rd << 12);
    translation |= (rb << 16);
    translation |= (((nn & 0xF0) >> 4) << 8);
    translation |= (nn & 0xF);

    *arm_instr = translation;
    return HALFWORD_TRANSFER;
}

InstrType thumb_decompress_11(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100101100011010000000000000000;

    Word l = (thumb_instr >> 11) & 1;
    Word rd = (thumb_instr >> 8) & 0x7;
    Word nn = (thumb_instr & 0xFF) << 2;

    translation |= (l << 20);
    translation |= (rd << 12);
    translation |= nn;

    *arm_instr = translation;
    return SINGLE_TRANSFER;
}

InstrType thumb_decompress_13(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11100010000011011101111100000000;

    Word nn = (thumb_instr & 0x7F);
    bool is_signed = (thumb_instr >> 7) & 1;

    translation |= (0x2 << (21 + is_signed));
    translation |= nn;

    *arm_instr = translation;
    return ALU;
}

InstrType thumb_decompress_14(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11101000001011010000000000000000;

    Word opcode = (thumb_instr >> 11) & 1;
    Word pc_or_lr = (thumb_instr >> 8) & 1;
    Word reg_list = thumb_instr & 0xFF;

    translation |= (!opcode << 24);
    translation |= (opcode << 23);
    translation |= (opcode << 20);
    translation |= reg_list;
    translation |= ((pc_or_lr & 1) << (0xE | opcode));

    *arm_instr = translation;
    return BLOCK_TRANSFER;
}

InstrType thumb_decompress_15(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11101000101000000000000000000000;

    Word opcode = (thumb_instr >> 11) & 1;
    Word rb = (thumb_instr >> 8) & 0x7;
    Word reg_list = thumb_instr & 0xFF;

    translation |= (opcode << 20);
    translation |= (rb << 16);
    translation |= reg_list;

    *arm_instr = translation;
    return BLOCK_TRANSFER;
}

InstrType thumb_decompress_16(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b00001010000000000000000000000000;

    Word cond = (thumb_instr >> 8) & 0xF;
    Word offset = (int32_t)(int8_t)(thumb_instr & 0xFF);

    translation |= (cond << 28);
    translation |= (offset & 0xFFFFFF);

    *arm_instr = translation;
    return BRANCH;
}

InstrType thumb_decompress_17(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11101111000000000000000000000000;

    translation |= (thumb_instr & 0xFF);

    *arm_instr = translation;
    return translation;
}

InstrType thumb_decompress_18(HalfWord thumb_instr, Word *arm_instr) {
    Word translation = 0b11101010000000000000000000000000;

    Word offset = (int32_t)((int16_t)((thumb_instr & 0x7FF) << 5) >> 5);
    translation |= (offset & 0xFFFFFF);

    *arm_instr = translation;
    return BRANCH;
}
