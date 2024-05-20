#include "cpu_utils.h"

InstrType thumb_decompress_1(HalfWord thumb_instr, Word *arm_instr);
InstrType thumb_decompress_2(HalfWord thumb_instr, Word *arm_instr);
InstrType thumb_decompress_3(HalfWord thumb_instr, Word *arm_instr);
InstrType thumb_decompress_4(HalfWord thumb_instr, Word *arm_instr);
InstrType thumb_decompress_5(HalfWord thumb_instr, Word *arm_instr);
InstrType thumb_decompress_16(HalfWord thumb_instr, Word *arm_instr);
InstrType thumb_decompress_18(HalfWord thumb_instr, Word *arm_instr);
