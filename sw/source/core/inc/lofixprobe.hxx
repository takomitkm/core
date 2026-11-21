/* LOFIX PROBE ONLY - NOT FOR UPSTREAM. v4.2.
 *
 * Diagnostic instrumentation for the non-converging Writer table layout
 * observed with an oversized as-character image in a table cell.
 *
 * Enabled at runtime via LOFIX_TRACE_SPLIT.
 *
 * v4.2 = v4.1 counters plus the decision-point data:
 *   - SwTextFrame::WouldFit logs, for the fly-only paragraph only, the input
 *     and output maximum height, line height, IsInside() result,
 *     IsKeepAlways() and the return value, on every return path;
 *   - SwTabFrame::Split logs the row/space inputs, every intermediate
 *     bSplitRowAllowed value, the keep-next/omit-headline flags, and which of
 *     the three branches it took;
 *   - lcl_InsertNewFollowFlowLine logs master/follow tab, source row and
 *     row-span flag, so the structural relation is recorded at creation;
 *   - IsAnFollow counts are only bumped for real calls, and its follow-chain
 *     steps are measured for calls made from the target scan only.
 *
 * Scope model: MakeAllScope (one per SwTabFrame::MakeAll) and SplitScope
 * (one per SwTabFrame::Split) carry the counters.
 */
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace lofixprobe
{
struct SplitScope;
struct MakeAllScope;

inline SplitScope* currentSplit();
inline MakeAllScope* currentMakeAll();

struct State
{
    bool enabled = nullptr != std::getenv("LOFIX_TRACE_SPLIT");

    long nextTabId = 0;
    long nextRowId = 0;
    long nextSplitId = 0;
    long nextMakeAllId = 0;
    long nextTextId = 0;

    long tabCreated = 0;
    long tabDestroyed = 0;
    long tabLive = 0;
    long tabPeakLive = 0;

    long rowCreated = 0;
    long rowDestroyed = 0;
    long rowLive = 0;
    long rowPeakLive = 0;

    long collapseCalls = 0;
    int maxCollapseDepth = 0;

    long anFollowCallsTotal = 0;
    long anFollowStepsTotal = 0;
    long anFollowMaxSteps = 0;
    long anFollowTrueTotal = 0;
    int anFollowTraceDepth = 0;

    std::unordered_map<void const*, long> tabIds;
    std::unordered_map<void const*, long> rowIds;
    std::unordered_map<void const*, long> textIds;

    std::vector<SplitScope*> splitStack;
    std::vector<MakeAllScope*> makeAllStack;
};

inline State& state()
{
    static State aState;
    return aState;
}

inline bool on() { return state().enabled; }

struct MakeAllScope
{
    long makeAllId = 0;
    long tabId = 0;
    int pageNo = 0;
    bool follow = false;
    long splitCalls = 0;
    long iteratorVisits = 0;
    long isAnFollowCalls = 0;
    long isAnFollowSteps = 0;
    long maxIsAnFollowSteps = 0;
    long isAnFollowTrue = 0;
    long rowCreatedAtEnter = 0;
    long rowDestroyedAtEnter = 0;
    long tabCreatedAtEnter = 0;
    long tabDestroyedAtEnter = 0;
    bool traced = false;

    MakeAllScope(void const* pTab, bool bFollow, int nPage);
    ~MakeAllScope();

    MakeAllScope(MakeAllScope const&) = delete;
    MakeAllScope& operator=(MakeAllScope const&) = delete;
};

struct SplitScope
{
    long splitId = 0;
    long tabId = 0;
    long makeAllId = 0;
    int pageNo = 0;
    bool follow = false;
    int upperType = 0;
    long shrinkInclusive = 0;
    long shrinkExclusive = 0;
    long iteratorVisits = 0;
    long isAnFollowCalls = 0;
    long isAnFollowSteps = 0;
    long maxIsAnFollowSteps = 0;
    long isAnFollowTrue = 0;
    int maxShrinkDepth = 0;
    int maxCollapseDepth = 0;
    long rowCreated = 0;
    bool traced = false;

    SplitScope(void const* pTab, bool bFollow, int nPage, int nUpperType);
    ~SplitScope();

    SplitScope(SplitScope const&) = delete;
    SplitScope& operator=(SplitScope const&) = delete;
};

/// RAII helper for recursion-depth tracking in the caller's translation unit.
struct DepthGuard
{
    int& mrDepth;
    explicit DepthGuard(int& rDepth)
        : mrDepth(rDepth)
    {
        ++mrDepth;
    }
    ~DepthGuard() { --mrDepth; }
    DepthGuard(DepthGuard const&) = delete;
    DepthGuard& operator=(DepthGuard const&) = delete;
};

/// Marks IsAnFollow() calls that belong to the target scan, so chain steps are
/// only attributed to that hotspot.
struct IsAnFollowTraceScope
{
    bool traced;
    explicit IsAnFollowTraceScope()
        : traced(state().enabled)
    {
        if (traced)
            ++state().anFollowTraceDepth;
    }
    ~IsAnFollowTraceScope()
    {
        if (traced)
            --state().anFollowTraceDepth;
    }
    IsAnFollowTraceScope(IsAnFollowTraceScope const&) = delete;
    IsAnFollowTraceScope& operator=(IsAnFollowTraceScope const&) = delete;
};

inline bool isAnFollowTraceActive() { return state().anFollowTraceDepth > 0; }

inline long tabIdFor(void const* p)
{
    State& rS = state();
    auto it = rS.tabIds.find(p);
    if (it != rS.tabIds.end())
        return it->second;
    long nId = ++rS.nextTabId;
    rS.tabIds.emplace(p, nId);
    return nId;
}

inline long textIdFor(void const* p)
{
    State& rS = state();
    auto it = rS.textIds.find(p);
    if (it != rS.textIds.end())
        return it->second;
    long nId = ++rS.nextTextId;
    rS.textIds.emplace(p, nId);
    return nId;
}

inline void noteTabCreated(void const* p)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    long nId = tabIdFor(p);
    ++rS.tabCreated;
    ++rS.tabLive;
    rS.tabPeakLive = std::max(rS.tabPeakLive, rS.tabLive);
    std::fprintf(stderr, "LOFIX Tab Created id=%ld tab_live=%ld\n", nId, rS.tabLive);
    std::fflush(stderr);
}

inline void noteTabDestroyed(void const* p)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    long nId = 0;
    auto it = rS.tabIds.find(p);
    if (it != rS.tabIds.end())
        nId = it->second;
    ++rS.tabDestroyed;
    if (rS.tabLive > 0)
        --rS.tabLive;
    std::fprintf(stderr, "LOFIX Tab Destroyed id=%ld tab_live=%ld\n", nId, rS.tabLive);
    std::fflush(stderr);
    rS.tabIds.erase(p);
}

inline long newRowId(void const* p)
{
    State& rS = state();
    long nId = ++rS.nextRowId;
    rS.rowIds.emplace(p, nId);
    ++rS.rowCreated;
    ++rS.rowLive;
    rS.rowPeakLive = std::max(rS.rowPeakLive, rS.rowLive);
    return nId;
}

inline long rowIdOf(void const* p)
{
    State& rS = state();
    auto it = rS.rowIds.find(p);
    return it == rS.rowIds.end() ? 0 : it->second;
}

/// Registers a new row frame instance. The caller prints the concrete follow
/// state (type-specific accessors) with the returned id.
inline long noteRowCreated(void const* p)
{
    State& rS = state();
    if (!rS.enabled)
        return 0;
    long nId = newRowId(p);
    if (SplitScope* pS = currentSplit())
        ++pS->rowCreated;
    return nId;
}

inline void noteRowDestroyed(void const* p)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    ++rS.rowDestroyed;
    if (rS.rowLive > 0)
        --rS.rowLive;
    rS.rowIds.erase(p);
}

inline SplitScope* currentSplit()
{
    State& rS = state();
    return rS.splitStack.empty() ? nullptr : rS.splitStack.back();
}

inline long currentSplitId()
{
    SplitScope* p = currentSplit();
    return p ? p->splitId : 0;
}

inline MakeAllScope* currentMakeAll()
{
    State& rS = state();
    return rS.makeAllStack.empty() ? nullptr : rS.makeAllStack.back();
}

inline SplitScope::SplitScope(void const* pTab, bool bFollow, int nPage, int nUpperType)
    : tabId(tabIdFor(pTab))
    , pageNo(nPage)
    , follow(bFollow)
    , upperType(nUpperType)
{
    State& rS = state();
    traced = rS.enabled;
    if (!traced)
        return;
    splitId = ++rS.nextSplitId;
    if (MakeAllScope* pM = currentMakeAll())
    {
        makeAllId = pM->makeAllId;
        ++pM->splitCalls;
    }
    rS.splitStack.push_back(this);
    std::fprintf(stderr,
        "LOFIX Split Enter id=%ld tab_id=%ld makeall=%ld follow=%d page=%d upper=%d "
        "row_live=%ld tab_live=%ld\n",
        splitId, tabId, makeAllId, follow ? 1 : 0, pageNo, upperType, rS.rowLive, rS.tabLive);
    std::fflush(stderr);
}

inline SplitScope::~SplitScope()
{
    if (!traced)
        return;
    State& rS = state();
    std::fprintf(stderr,
        "LOFIX Split Exit id=%ld tab_id=%ld makeall=%ld shrink_incl=%ld shrink_excl=%ld "
        "iter=%ld isanfollow=%ld isanfollow_steps=%ld max_isanfollow_steps=%ld "
        "isanfollow_true=%ld max_shrink_depth=%d max_collapse_depth=%d row_created=%ld "
        "row_live=%ld row_peak=%ld tab_live=%ld tab_peak=%ld\n",
        splitId, tabId, makeAllId, shrinkInclusive, shrinkExclusive, iteratorVisits,
        isAnFollowCalls, isAnFollowSteps, maxIsAnFollowSteps, isAnFollowTrue, maxShrinkDepth,
        maxCollapseDepth, rowCreated, rS.rowLive, rS.rowPeakLive, rS.tabLive, rS.tabPeakLive);
    std::fflush(stderr);
    if (!rS.splitStack.empty() && rS.splitStack.back() == this)
        rS.splitStack.pop_back();
}

inline MakeAllScope::MakeAllScope(void const* pTab, bool bFollow, int nPage)
    : tabId(tabIdFor(pTab))
    , pageNo(nPage)
    , follow(bFollow)
{
    State& rS = state();
    traced = rS.enabled;
    if (!traced)
        return;
    makeAllId = ++rS.nextMakeAllId;
    rowCreatedAtEnter = rS.rowCreated;
    rowDestroyedAtEnter = rS.rowDestroyed;
    tabCreatedAtEnter = rS.tabCreated;
    tabDestroyedAtEnter = rS.tabDestroyed;
    rS.makeAllStack.push_back(this);
    std::fprintf(stderr, "LOFIX MakeAll Enter id=%ld tab_id=%ld follow=%d page=%d\n", makeAllId,
                 tabId, follow ? 1 : 0, pageNo);
    std::fflush(stderr);
}

inline MakeAllScope::~MakeAllScope()
{
    if (!traced)
        return;
    State& rS = state();
    std::fprintf(stderr,
        "LOFIX MakeAll Exit id=%ld tab_id=%ld split_calls=%ld iter=%ld isanfollow=%ld "
        "isanfollow_steps=%ld max_isanfollow_steps=%ld isanfollow_true=%ld "
        "row_created=%ld row_destroyed=%ld tab_created=%ld tab_destroyed=%ld "
        "row_live=%ld row_peak=%ld tab_live=%ld tab_peak=%ld\n",
        makeAllId, tabId, splitCalls, iteratorVisits, isAnFollowCalls, isAnFollowSteps,
        maxIsAnFollowSteps, isAnFollowTrue, rS.rowCreated - rowCreatedAtEnter,
        rS.rowDestroyed - rowDestroyedAtEnter, rS.tabCreated - tabCreatedAtEnter,
        rS.tabDestroyed - tabDestroyedAtEnter, rS.rowLive, rS.rowPeakLive, rS.tabLive,
        rS.tabPeakLive);
    std::fflush(stderr);
    if (!rS.makeAllStack.empty() && rS.makeAllStack.back() == this)
        rS.makeAllStack.pop_back();
}

inline void noteShrink(int nShrinkDepth)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    for (SplitScope* p : rS.splitStack)
        ++p->shrinkInclusive;
    SplitScope* p = currentSplit();
    if (!p)
        return;
    ++p->shrinkExclusive;
    p->maxShrinkDepth = std::max(p->maxShrinkDepth, nShrinkDepth);
    if (0 == (p->shrinkExclusive % 10000))
    {
        std::fprintf(stderr,
            "LOFIX Split Progress id=%ld shrink_incl=%ld shrink_excl=%ld depth=%d "
            "row_live=%ld\n",
            p->splitId, p->shrinkInclusive, p->shrinkExclusive, nShrinkDepth, rS.rowLive);
        std::fflush(stderr);
    }
}

inline void noteCollapse(int nCollapseDepth)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    ++rS.collapseCalls;
    rS.maxCollapseDepth = std::max(rS.maxCollapseDepth, nCollapseDepth);
    if (SplitScope* p = currentSplit())
        p->maxCollapseDepth = std::max(p->maxCollapseDepth, nCollapseDepth);
    if (0 == (rS.collapseCalls % 50000))
    {
        std::fprintf(stderr, "LOFIX Collapse calls=%ld depth=%d max_collapse_depth=%d\n",
                     rS.collapseCalls, nCollapseDepth, rS.maxCollapseDepth);
        std::fflush(stderr);
    }
}

/// One row frame visited by lcl_calcHeightOfRowBeforeThisFrame()'s SwIterator
/// loop. Iterator cost only; not an IsAnFollow() call.
inline void noteIteratorVisit()
{
    State& rS = state();
    if (!rS.enabled)
        return;
    if (SplitScope* p = currentSplit())
        ++p->iteratorVisits;
    if (MakeAllScope* p = currentMakeAll())
        ++p->iteratorVisits;
}

/// One real IsAnFollow() call (the callee is non-null).
inline void noteIsAnFollow()
{
    State& rS = state();
    if (!rS.enabled)
        return;
    ++rS.anFollowCallsTotal;
    if (SplitScope* p = currentSplit())
        ++p->isAnFollowCalls;
    if (MakeAllScope* p = currentMakeAll())
        ++p->isAnFollowCalls;
}

/// One traced IsAnFollow() call: number of GetFollow() hops walked and result.
inline void noteIsAnFollowWalk(long nSteps, bool bFound)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    ++rS.anFollowStepsTotal;
    if (bFound)
        ++rS.anFollowTrueTotal;
    rS.anFollowMaxSteps = std::max(rS.anFollowMaxSteps, nSteps);
    if (SplitScope* p = currentSplit())
    {
        p->isAnFollowSteps += nSteps;
        p->isAnFollowTrue += bFound ? 1 : 0;
        p->maxIsAnFollowSteps = std::max(p->maxIsAnFollowSteps, nSteps);
    }
    if (MakeAllScope* p = currentMakeAll())
    {
        p->isAnFollowSteps += nSteps;
        p->isAnFollowTrue += bFound ? 1 : 0;
        p->maxIsAnFollowSteps = std::max(p->maxIsAnFollowSteps, nSteps);
    }
}

} // namespace lofixprobe