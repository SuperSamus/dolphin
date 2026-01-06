#include "Common/BitSet.h"
#include "Core/PowerPC/Jit64/Jit.h"

#include <cstdint>
#include <ios>
#include <iostream>
#include <variant>
#include "Common/Assert.h"
#include "Common/Contains.h"
#include "Core/PowerPC/Jit64Common/Jit64Constants.h"
#include "Core/PowerPC/Jit64Common/Jit64PowerPCState.h"

struct RegsUsed
{
  BitSet32 regsIn;
  BitSet32 regsOut;
  BitSet32 fregsIn;
  BitSet32 fregsOut;
};

static RegsUsed CombineBranchInfo(const RegsUsed& bs, const PPCAnalyst::BranchInfo& bi)
{
  RegsUsed result;
  result.regsIn = bs.regsIn | bi.regsIn;
  result.regsOut = bs.regsOut | bi.regsOut;
  result.fregsIn = bs.fregsIn | bi.fregsIn;
  result.fregsOut = bs.fregsOut | bi.fregsOut;
  return result;
}

static size_t GetStart_i(const PPCAnalyst::BranchInfo& bi)
{
  return std::min(bi.address_i, bi.branchTo_i);
}

static size_t GetEnd_i(const PPCAnalyst::BranchInfo& bi)
{
  // Backwards branches properly end *after* the branch instruction.
  if (bi.direction == PPCAnalyst::BranchDirection::Forward)
    return bi.branchTo_i;
  else
    return bi.address_i + 1;
}

bool Jit64::IsInBlockBranchActive()
{
  return js.inBlockBranchStatus.ends_at.has_value();
}

void Jit64::EndInBlockBranch()
{
  // End the optimized branches.
  js.inBlockBranchStatus = {};

  // TODO, bad idea, Analyzer in use, I wrote this all over the place.
  gpr.UnfixHostRegisters();
  fpr.UnfixHostRegisters();
}

void Jit64::ForcePreloadRegisters()
{
  // TODO: Some registers do not need to be preloaded, if it involves forwards branches.
  // For instance, in a single forward branch, registers could be safely flushed or discarded
  // inside it. No need to preload in this case.

  // RSCRATCH_EXTRA may be needed by some instructions, leave it clean.
  RCX64Reg scratch_guard = gpr.Scratch(RSCRATCH_EXTRA);

  gpr.InBlockBranchPreloadRegisters(js.inBlockBranchStatus.regsIn | js.inBlockBranchStatus.regsOut);
  // Don't preload immediates
  // & ~(gpr.GetImmSet() & ~js.inBlockBranchStatus.regsOut));
  fpr.InBlockBranchPreloadRegisters(js.inBlockBranchStatus.fregsIn |
                                    js.inBlockBranchStatus.fregsOut);
}

bool Jit64::TryPrepareInBlockBranches(const PPCAnalyst::CodeOp& op)
{
  const auto AreThereEnoughHostRegs = [&](RegsUsed regs) {
    return (regs.regsIn | regs.regsOut).Count() <= gpr.GetMaxPreloadableRegisters() &&
           (regs.fregsIn | regs.fregsOut).Count() <= fpr.GetMaxPreloadableRegisters();
  };
  // Downcount management right now is kinda dumb: it's subtracted whenever a branch or a barrier is
  // encountered. Example:
  // # Subtract here (1.)
  // b ---------------
  // ...             |
  // # And here (2.) |
  // <----------------
  // # And at the end of the block.
  // That's why it's inefficient, 1 and 3 are could be merged into one.
  // TODO: Fix it.
  const auto DecreaseDowncount = [&]() {
    SUB(32, PPCSTATE(downcount), Gen::Imm32(js.downcountAmount - op.opinfo->num_cycles));
    js.downcountAmount = op.opinfo->num_cycles;
  };
  // A "barrier" is a branch target that is part of the optimized branches.
  const auto HandleBarriers = [&]() {
    bool first_barrier = true;
    for (const PPCAnalyst::BranchInfo& bi : code_block.m_branch_infos)
    {
      if (!(bi.branchTo_i == op.i &&
            Common::Contains(js.inBlockBranchStatus.optimized_branches_i, bi.address_i)))
        continue;
      if (first_barrier)
      {
        first_barrier = false;
        ForcePreloadRegisters();  // TODO: Bad idea, again, in use for the Analyzer would have been
                                  // better.
        DecreaseDowncount();
        // See comments on respective fields.
        js.inBlockBranchStatus.gpr_guard.EndFork();
        js.inBlockBranchStatus.fpr_guard.EndFork();
        // TODO: Which registers are dirtied could be smarter.
        gpr.ForceDirty(js.inBlockBranchStatus.regsOut);
        fpr.ForceDirty(js.inBlockBranchStatus.fregsOut);
      }
      if (bi.direction == PPCAnalyst::BranchDirection::Forward)
      {
        // std::cout << "Set target for branch " << std::dec << bi.address_i
        //           << ", completed at instruction " << op.i << std::endl;
        if (!js.inBlockBranchStatus.forward_fixups.contains(bi.address_i))
        {
          // TODO: Disabled because not all of these are implemented yet (e.g.
          // DoMergedBranchImmediate) ASSERT_MSG(DYNA_REC, true, "An optimized forward branch was
          // supposed to generate a fixup, but didn't do it.");
          continue;
        }
        SetJumpTarget(js.inBlockBranchStatus.forward_fixups.at(bi.address_i));
      }
      else if (bi.direction == PPCAnalyst::BranchDirection::Backward)
        js.inBlockBranchStatus.backwards_addresses[bi.branchTo_i] = GetCodePtr();
      // TODO: Set pc?
    }
  };
  const auto FlushUnusedRegisters = [&]() {
    gpr.Flush(~(js.inBlockBranchStatus.regsIn | js.inBlockBranchStatus.regsOut));
    fpr.Flush(~(js.inBlockBranchStatus.fregsIn | js.inBlockBranchStatus.fregsOut));
  };

  if (js.inBlockBranchStatus.ends_at.has_value())
  {
    HandleBarriers();
    if (op.i >= js.inBlockBranchStatus.ends_at.value())
    {
      ASSERT_MSG(DYNA_REC, op.i == js.inBlockBranchStatus.ends_at.value(),
                 "Address {:#x}, op i {}, in-branch end i {}", op.address, op.i,
                 js.inBlockBranchStatus.ends_at.value());
      EndInBlockBranch();
      // TODO: This is too hard to handle for now. (Maybe register analysis should be done by
      // the Analyzer...)
      // gpr.Discard(op.gprDiscardable);
      // fpr.Discard(op.gprDiscardable);
      gpr.Flush(~op.gprInUse, RegCache::IgnoreDiscardedRegisters::Yes);
      fpr.Flush(~op.fprInUse, RegCache::IgnoreDiscardedRegisters::Yes);

      // Just as it ends, another set of optimized branches may begin.
      return TryPrepareInBlockBranches(op);
    }
    else
    {
      return true;
    }
  }
  else
  {
    if (op.branchTo == UINT32_MAX && !op.isBranchTarget)
      return false;
    else
    {
      // Note that the branch infos are sorted by `min(address_i, branchTo_i)`.
      // (See `PPCAnalyzer::Analyze()`.)
      // We want to start from the shortest branch that is or targets the current op.
      auto it =
          std::ranges::find_if(code_block.m_branch_infos, [&](const PPCAnalyst::BranchInfo& bi) {
            return bi.direction != PPCAnalyst::BranchDirection::Outside && GetStart_i(bi) == op.i;
          });
      if (it == code_block.m_branch_infos.cend())
        return false;
      RegsUsed ru = CombineBranchInfo({}, *it);
      if (!AreThereEnoughHostRegs(ru))
      {
        // Can't start from here.
        return false;
      }
      bool should_flush_everything_else = it->contains_flush_and_continue;

      // std::cout << "Starting inline branches at address with branch at address " << std::hex
      //           << it->address << " (end " << it->branchTo << ")" << std::endl;
      // std::cout << "Which starts at index " << std::dec << it->address_i << " and ends at index "
      //           << it->branchTo_i << std::endl;
      size_t end_i = GetEnd_i(*it);
      std::vector<size_t> optimized_branches_i{it->address_i};
      for (it++; it < code_block.m_branch_infos.cend(); it++)
      {
        // Only go on until the branches intersect with the current run.
        if (GetStart_i(*it) >= end_i)
          break;
        if (it->direction == PPCAnalyst::BranchDirection::Outside)
          continue;

        RegsUsed possible_ru = CombineBranchInfo(ru, *it);
        if (AreThereEnoughHostRegs(possible_ru))
        {
          // std::cout << "Adding branch " << std::hex << it->address << std::endl;
          ru = possible_ru;
          optimized_branches_i.push_back(it->address_i);
          end_i = std::max(end_i, GetEnd_i(*it));
          should_flush_everything_else |= it->contains_flush_and_continue;
        }
      }
      for (size_t i = op.i; i < end_i; i++)
      {
        should_flush_everything_else |= IsFallbackToInterpreter(m_code_buffer[i].inst);
      }

      js.inBlockBranchStatus.ends_at = end_i;
      js.inBlockBranchStatus.optimized_branches_i = optimized_branches_i;
      js.inBlockBranchStatus.regsIn = ru.regsIn;
      js.inBlockBranchStatus.regsOut = ru.regsOut;
      js.inBlockBranchStatus.fregsIn = ru.fregsIn;
      js.inBlockBranchStatus.fregsOut = ru.fregsOut;

      if (should_flush_everything_else)
        FlushUnusedRegisters();
      ForcePreloadRegisters();

      // In retrospective, this was a bad idea, and making registers in use in the Analyzer would
      // have been a better idea...
      gpr.FixHostRegisters(js.inBlockBranchStatus.regsIn | js.inBlockBranchStatus.regsOut);
      // & ~(gpr.GetImmSet() & ~js.inBlockBranchStatus.regsOut));
      fpr.FixHostRegisters(js.inBlockBranchStatus.fregsIn | js.inBlockBranchStatus.fregsOut);

      js.inBlockBranchStatus.gpr_guard = gpr.Fork();
      js.inBlockBranchStatus.fpr_guard = fpr.Fork();

      // std::cout << "Start: " << std::hex << op.address << " " << op.i << " "
      //           << js.inBlockBranchStatus.ends_at.value() << std::endl
      //           << "GPR: " << ru.regsIn.m_val << " " << ru.regsOut.m_val << std::endl
      //           << "FPR: " << ru.fregsIn.m_val << " " << ru.fregsOut.m_val << std::endl;

      HandleBarriers();

      return true;
    }
  }
}

std::variant<std::monostate, FixupBranch*, const u8*>
Jit64::TryInBlockBranch(const PPCAnalyst::CodeOp& op)
{
  // ASSERT(op.branchTo != UINT32_MAX);

  // This is called here too because `DoJit` skips merged instructions, which means that we can't
  // rely on it if in-block branches should be started by a forward branch.
  if (TryPrepareInBlockBranches(op))
  {
    if (!Common::Contains(js.inBlockBranchStatus.optimized_branches_i, op.i))
      return {};

    // TODO: Not on Outside
    ForcePreloadRegisters();  // TODO: Again, Analyzer, registers in use.
    auto it = std::ranges::find_if(code_block.m_branch_infos,
                                   [&](const auto& bi) { return bi.address_i == op.i; });
    switch (it->direction)
    {
    case PPCAnalyst::BranchDirection::Outside:
      return {};
    case PPCAnalyst::BranchDirection::Forward:
      // Insert a FixupBranch, and return a pointer of it for the branch instruction to fill.
      // std::cout << "Add fixup branch " << it->address_i << std::endl;
      js.inBlockBranchStatus.forward_fixups[it->address_i] = {};
      return &js.inBlockBranchStatus.forward_fixups.at(it->address_i);
    case PPCAnalyst::BranchDirection::Backward:
      return js.inBlockBranchStatus.backwards_addresses.at(it->branchTo_i);
    }
  }
  return {};
}
