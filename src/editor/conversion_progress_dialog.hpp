#pragma once
#include <gtkmm.h>

namespace dune3d {

// Modal "please wait" dialog with a Cancel button, shown while a mesh
// conversion's sewing/merging step is taking a while -- see
// EditorWaitProgressReporter (editor.cpp), which owns the decision of when
// to actually create/show one of these (not immediately, to avoid a flash
// on the common fast/instant case).
class ConversionProgressDialog : public Gtk::Window {
public:
    ConversionProgressDialog();
    void pulse();
    // `progress` is 0..1, straight from OpenCascade's own
    // Message_ProgressIndicator::GetPosition() -- real, but paces unevenly
    // against wall-clock time (see WorkContext::progress), so this can look
    // like it stalls then jumps rather than filling smoothly.
    void set_progress(double progress);
    bool cancel_requested() const
    {
        return m_cancel_requested;
    }

private:
    Gtk::ProgressBar *m_progress_bar = nullptr;
    bool m_cancel_requested = false;
};

} // namespace dune3d
