/* LOFIX PROBE ONLY - NOT FOR UPSTREAM.
 *
 * Diagnostic instrumentation for the non-converging Writer table layout
 * observed with an oversized as-character image in a table cell.
 *
 * Enabled at runtime via LOFIX_TRACE_SPLIT. Header-only (inline functions with
 * function-local statics) so no build-system change is needed; the inline
 * statics are a single entity across translation units.
 *
 * Scope model: each SwTabFrame::Split() entry pushes a Scope (RAII). Nested
 * Split() calls push nested scopes, so each scope keeps its parent id.
 *   - every SwRowFrame::ShrinkFrame() bumps the exclusive counter of the
 *     innermost split and the inclusive counter of all enclosing splits;
 *   - lcl_ShrinkCellsAndAllContent() reports its own recursion depth;
 *   - SwRowFrame ctor/dtor report created/destroyed/live/peak.
 */
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace lofixprobe
{
struct Scope;

struct State
{
    bool enabled = nullptr != std::getenv("LOFIX_TRACE_SPLIT");
    long nextCallId = 0;
    int splitDepth = 0;
    long rowCreated = 0;
    long rowDestroyed = 0;
    long rowLive = 0;
    long rowPeakLive = 0;
    long collapseCalls = 0;
    int maxCollapseDepth = 0;
    std::vector<Scope*> stack;
};

inline State& state()
{
    static State aState;
    return aState;
}

inline bool on() { return state().enabled; }

struct Scope
{
    long callId = 0;
    long parentId = 0;
    int depth = 0;
    long shrinkInclusive = 0;
    long shrinkExclusive = 0;
    int maxShrinkDepth = 0;
    int maxCollapseDepth = 0;
    long rowCreated = 0;
    long rowDestroyed = 0;
    long rowLiveAtEnter = 0;
    const void* tab = nullptr;
    bool traced = false;

    explicit Scope(const void* pTab)
        : tab(pTab)
    {
        State& rS = state();
        traced = rS.enabled;
        if (!traced)
            return;
        callId = ++rS.nextCallId;
        parentId = rS.stack.empty() ? 0 : rS.stack.back()->callId;
        depth = ++rS.splitDepth;
        rowLiveAtEnter = rS.rowLive;
        rS.stack.push_back(this);
        std::fprintf(stderr,
            "LOFIX Split Enter call=%ld parent=%ld depth=%d tab=%p row_live=%ld"
            "\n",
            callId, parentId, depth, tab, rowLiveAtEnter);
        std::fflush(stderr);
    }

    ~Scope()
    {
        if (!traced)
            return;
        State& rS = state();
        std::fprintf(stderr,
            "LOFIX Split Exit call=%ld parent=%ld depth=%d shrink_inclusive=%ld "
            "shrink_exclusive=%ld max_shrink_depth=%d max_collapse_depth=%d "
            "row_created=%ld row_destroyed=%ld row_live=%ld\n",
            callId, parentId, depth, shrinkInclusive, shrinkExclusive, maxShrinkDepth,
            maxCollapseDepth, rowCreated, rowDestroyed, rS.rowLive);
        std::fflush(stderr);
        if (!rS.stack.empty() && rS.stack.back() == this)
            rS.stack.pop_back();
        --rS.splitDepth;
    }

    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
};

inline Scope* current()
{
    State& rS = state();
    return rS.stack.empty() ? nullptr : rS.stack.back();
}

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

inline void noteShrink(int nShrinkDepth)
{
    State& rS = state();
    if (!rS.enabled)
        return;
    for (Scope* p : rS.stack)
        ++p->shrinkInclusive;
    Scope* p = current();
    if (!p)
        return;
    ++p->shrinkExclusive;
    p->maxShrinkDepth = std::max(p->maxShrinkDepth, nShrinkDepth);
    if (0 == (p->shrinkExclusive % 10000))
    {
        std::fprintf(stderr,
            "LOFIX Split Progress call=%ld parent=%ld shrink_inclusive=%ld "
            "shrink_exclusive=%ld shrink_depth=%d collapse_depth=%d "
            "row_created=%ld row_destroyed=%ld row_live=%ld\n",
            p->callId, p->parentId, p->shrinkInclusive, p->shrinkExclusive, nShrinkDepth,
            p->maxCollapseDepth, p->rowCreated, p->rowDestroyed, rS.rowLive);
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
    if (Scope* p = current())
        p->maxCollapseDepth = std::max(p->maxCollapseDepth, nCollapseDepth);
    if (0 == (rS.collapseCalls % 10000))
    {
        std::fprintf(stderr,
            "LOFIX Collapse calls=%ld depth=%d max_collapse_depth=%d call=%ld"
            "\n",
            rS.collapseCalls, nCollapseDepth, rS.maxCollapseDepth, current() ? current()->callId : 0);
        std::fflush(stderr);
    }
}

inline void noteRowCreated()
{
    State& rS = state();
    if (!rS.enabled)
        return;
    ++rS.rowCreated;
    ++rS.rowLive;
    rS.rowPeakLive = std::max(rS.rowPeakLive, rS.rowLive);
    if (Scope* p = current())
        ++p->rowCreated;
}

inline void noteRowDestroyed()
{
    State& rS = state();
    if (!rS.enabled)
        return;
    ++rS.rowDestroyed;
    if (rS.rowLive > 0)
        --rS.rowLive;
    if (Scope* p = current())
        ++p->rowDestroyed;
}

} // namespace lofixprobe