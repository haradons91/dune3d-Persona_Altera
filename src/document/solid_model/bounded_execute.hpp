#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

namespace dune3d {

// Lets a GTK-free layer (this one) offer wait-progress UI and cancellation
// without depending on GTK itself: the Editor registers a concrete
// implementation once (see EditorWaitProgressReporter in editor.cpp) that
// shows a progress dialog and pumps the GTK event loop; with none
// registered, run_with_timeout() just sleep-polls as before (e.g. the
// standalone test harnesses used throughout this feature's development have
// no reporter and don't need one).
class WaitProgressReporter {
public:
    virtual ~WaitProgressReporter() = default;
    virtual void begin() = 0;
    // Called roughly every 20ms while waiting with the work's self-reported
    // progress (0..1, or left at 0 by work that doesn't report any -- see
    // WorkContext); return true to cancel.
    virtual bool tick(double progress) = 0;
    virtual void end() = 0;
};
extern WaitProgressReporter *g_wait_progress_reporter;

// Passed to `fn` by reference so it can cooperate with cancellation and
// report progress back, both optionally.
struct WorkContext {
    std::atomic<bool> cancel_requested{false};
    // 0..1. OpenCascade's own Message_ProgressIndicator::GetPosition() is a
    // real, non-fabricated signal (confirmed empirically against a real
    // mesh) but paces unevenly against wall-clock time -- it can sit near 0
    // for a while then leap most of the way to 1 almost instantly, since
    // it reflects internal step count, not per-step cost. Left at 0 by work
    // that has no progress hook at all (e.g. the merge step, see
    // solid_model_convert_mesh.cpp), which the reporter should treat as
    // "unknown" rather than "0%".
    std::atomic<double> progress{0.0};
};

// Runs `fn(context)` on a detached background thread and waits up to
// `timeout` for it to finish, returning its result or std::nullopt.
//
// This exists because OpenCascade's own algorithms
// (BRepBuilderAPI_Sewing::Perform, ShapeFix_Solid::Perform,
// ShapeUpgrade_UnifySameDomain::Build, ...) have no *guaranteed*
// cancellation, and BRepBuilderAPI_Sewing::Perform() has been confirmed to
// hang indefinitely on some real inputs. Some of them DO support real,
// responsive interruption via OpenCascade's own
// Message_ProgressIndicator::UserBreak() mechanism, though -- confirmed by
// direct testing against a real mesh: an in-progress ~14s
// BRepBuilderAPI_Sewing::Perform() stopped within ~45ms of a break request,
// and ShapeFix_Solid::Perform() accepts the same mechanism. See
// solid_model_convert_mesh.cpp for how those two calls wire
// `context.cancel_requested`/`context.progress` into it (checking
// cancellation after each call and returning early without touching the
// now-unsafe-to-query object further -- confirmed calling SewedShape() after
// a break segfaults, so the contract here is strictly "stop and return,"
// never "give me the partial result").
//
// `fn` receives `context` and MAY cooperate with either field (as the
// sewing path does with both) but isn't required to (the merge path
// currently doesn't touch either, since ShapeUpgrade_UnifySameDomain::Build()
// has no such hook at all). When `fn` does cooperate with cancellation,
// cancelling ends the wait cleanly, indistinguishable from any other return.
// When it doesn't -- or doesn't respond within `grace_period` of being
// asked -- this falls back to the old behavior: give up waiting and leave
// the worker thread running unobserved until it finishes on its own or the
// process exits. Either way, `fn` must still own everything it touches
// (plain value copies in, a plain value out) rather than referencing
// anything with a shorter lifetime than the call, since the orphaning
// fallback is very much still in play.
//
// `was_cancelled` (if non-null) is set to true iff the wait was ended by
// WaitProgressReporter::tick() returning true rather than by hitting
// `timeout` -- both otherwise behave identically as far as this function is
// concerned, but callers may want different messaging for the two.
template <typename F>
auto run_with_timeout(F fn, std::chrono::milliseconds timeout, bool *was_cancelled = nullptr,
                      std::chrono::milliseconds grace_period = std::chrono::milliseconds(2000))
        -> std::optional<std::invoke_result_t<F, WorkContext &>>
{
    using Result = std::invoke_result_t<F, WorkContext &>;

    struct Shared {
        std::atomic<bool> done{false};
        WorkContext context;
        std::optional<Result> value;
    };
    auto shared = std::make_shared<Shared>();

    std::thread worker([shared, fn = std::move(fn)] {
        auto result = fn(shared->context);
        shared->value = std::move(result);
        shared->done.store(true, std::memory_order_release);
    });
    worker.detach();

    if (g_wait_progress_reporter)
        g_wait_progress_reporter->begin();
    struct EndGuard {
        ~EndGuard()
        {
            if (g_wait_progress_reporter)
                g_wait_progress_reporter->end();
        }
    } end_guard;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool cancel_signalled = false;
    std::chrono::steady_clock::time_point grace_deadline{};

    while (!shared->done.load(std::memory_order_acquire)) {
        const bool timed_out = std::chrono::steady_clock::now() >= deadline;
        // Keep pumping/ticking every iteration, even after a cancel/timeout
        // has already been signalled, so the progress dialog stays
        // responsive during the (short) grace period too.
        const bool ticked_cancel =
                g_wait_progress_reporter && g_wait_progress_reporter->tick(shared->context.progress.load());

        if (!cancel_signalled && (timed_out || ticked_cancel)) {
            if (ticked_cancel && was_cancelled)
                *was_cancelled = true;
            shared->context.cancel_requested.store(true, std::memory_order_release);
            cancel_signalled = true;
            grace_deadline = std::chrono::steady_clock::now() + grace_period;
        }

        if (cancel_signalled && std::chrono::steady_clock::now() >= grace_deadline)
            return std::nullopt; // fn didn't stop promptly (or can't) -- give up on it

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return std::move(shared->value);
}

} // namespace dune3d
