#pragma once
#include "msd.hpp"

namespace dune3d {
class MSDAnimator {
public:
    bool step(double time);
    double get_s() const;
    double get_s_delta();
    void start(double init = 0);
    void stop();
    // Immediately jump to value and stop: unlike stop() alone, this also
    // resyncs the underlying simulated position, so a tick callback that
    // reads get_s() after a direct (non-animated) set can't still see the
    // animator's stale in-flight value and clobber it on the next frame.
    void set(double value);
    float target = 0;
    bool is_running() const;

    void set_params(const MSD::Params &p);
    const MSD::Params &get_params() const;

private:
    MSD msd;
    bool running = false;
    double start_time = 0;
    double m_last_s = 0;
};
} // namespace dune3d
