// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/PowerPC/PPCTables.h"

#include <algorithm>
#include <array>
#include <bitset>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/PowerPC/PowerPC.h"

namespace PowerPC
{
const std::array<u64, 16> m_crTable = {{
    PPCCRToInternal(0x0),
    PPCCRToInternal(0x1),
    PPCCRToInternal(0x2),
    PPCCRToInternal(0x3),
    PPCCRToInternal(0x4),
    PPCCRToInternal(0x5),
    PPCCRToInternal(0x6),
    PPCCRToInternal(0x7),
    PPCCRToInternal(0x8),
    PPCCRToInternal(0x9),
    PPCCRToInternal(0xA),
    PPCCRToInternal(0xB),
    PPCCRToInternal(0xC),
    PPCCRToInternal(0xD),
    PPCCRToInternal(0xE),
    PPCCRToInternal(0xF),
}};
}  // namespace PowerPC

namespace PPCTables
{
struct OpDispatch
{
  std::bitset<64> leaf_flags;
  std::bitset<64> subtables;
  u32 op_index_start;
  u16 dispatch_start;
  u8 subop_shift;
  u8 subop_len;
};
static std::array<OpDispatch, 30> dispatch_table = {{
#include "OpID_DecodingTable.gen.cpp"
}};

static constexpr GekkoOPInfo UNKNOWN = {"Invalid Opcode", OpType::Invalid, 0};

std::array<GekkoOPInfo, (size_t)OpId::End> opinfo = {{
    UNKNOWN,
#include "OpInfo.gen.cpp"
}};

OpId GetOpId(UGeckoInstruction instruction)
{
  int subtable = 0;
  while (true)
  {
    auto disp = dispatch_table[subtable];
    int opcode = (instruction.hex >> disp.subop_shift) & ((1 << disp.subop_len) - 1);
    auto shifted = disp.leaf_flags >> (63 - opcode);
    if (shifted[0])
    {
      int res = disp.op_index_start + shifted.count() - 1;
      return (OpId)res;
    }
    else if ((disp.subtables >> (63 - opcode))[0])
    {
      subtable = disp.dispatch_start + (disp.subtables >> (63 - opcode)).count() - 1;
    }
    else
    {
      ERROR_LOG(DYNA_REC, "subtable %d, value %d not found", subtable, opcode);
      return OpId::Invalid;
    }
  }
}

static const std::array<u8, (size_t)OpId::End> cycles = {{
    0,
#include "Cycles_Table.gen.cpp"
}};

int Cycles(OpId opid)
{
  return cycles[(int)opid];
}

const char* GetInstructionName(UGeckoInstruction inst)
{
  return opinfo[(int)GetOpId(inst)].opname;
}

}  // namespace
