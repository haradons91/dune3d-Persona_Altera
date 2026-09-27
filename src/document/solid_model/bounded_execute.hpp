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
    // Called roughly every 20ms while waiting; return true to cancel.
    virtual bool tick() = 0;
    virtual void end() = 0;
};
extern WaitProgressReporter *g_wait_progress_reporter;

// Runs `fn` on a detached background thread and waits up to `timeout` for it
// to finish, returning its result or std::nullopt on timeout.
//
// This exists because OpenCascade's own algorithms (BRepBuilderAPI_Sewing,
// ShapeUpgrade_UnifySameDomain, ...) have no cancellation API and can hang
// indefinitely on certain inputs -- confirmed on a real mesh where
// BRepBuilderAPI_Sewing::Perform() never returned, insensitive to every
// tolerance tried. There is no way to make OCCT itself safe here, only to
// stop it from freezing the whole app (everything in this codebase runs
// solid-model computation synchronously on the GTK main thread -- see the
// Convert Mesh to Body plan for why this is a bounded, local mitigation
// rather than a full async rewrite).
//
// On timeout the worker thread is simply abandoned (detached) -- there is no
// way to safely kill a mid-flight OCCT call, so it keeps consuming a core in
// the background until it eventually finishes or the process exits. This is
// safe with respect to `fn`'s captures only if `fn` owns everything it
// touches (plain value copies in, a plain value out) rather than referencing
// anything with a shorter lifetime than the call -- callers must pass a
// self-contained callable, never one that closes over a caller-local
// reference or a Document/Group.
// `was_cancelled` (if non-null) is set to true iff the wait was ended by
// WaitProgressReporter::tick() returning true rather than by hitting
// `timeout` -- both otherwise behave identically (give up, return
// nullopt), but callers may want different messaging for the two.
template <typename F>
auto run_with_timeout(F fn, std::chrono::milliseconds timeout, bool *was_cancelled = nullptr)
        -> std::optional<std::invoke_result_t<F>>
{
    using Result = std::invoke_result_t<F>;

    struct Shared {
        std::atomic<bool> done{false};
        std::optional<Result> value;
    };
    auto shared = std::make_shared<Shared>();

    std::thread worker([shared, fn = std::move(fn)] {
        auto result = fn();
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
    while (!shared->done.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline)
            return std::nullopt;
        if (g_wait_progress_reporter && g_wait_progress_reporter->tick()) {
            if (was_cancelled)
                *was_cancelled = true;
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return std::move(shared->value);
}

} // namespace dune3d
