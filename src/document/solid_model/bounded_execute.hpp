#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

namespace dune3d {

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
template <typename F> auto run_with_timeout(F fn, std::chrono::milliseconds timeout)
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

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!shared->done.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline)
            return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return std::move(shared->value);
}

} // namespace dune3d
