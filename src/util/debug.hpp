#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <string>

namespace dune3d {

enum class DebugCategory {
    UI,
    TREE,
    RENDER,
    MODEL,
    EXTRUDE,
    SELECTION,
    IMPORT,
    TIMELINE,
    INPUT,
    DOCUMENT,
};

inline bool debug_ui = false;
inline bool debug_render = false;
inline bool debug_all = false;

inline bool &debug_category_flag(DebugCategory category)
{
    static bool flags[10] = {};
    return flags[static_cast<unsigned int>(category)];
}

inline const char *debug_category_name(DebugCategory category)
{
    switch (category) {
    case DebugCategory::UI: return "ui";
    case DebugCategory::TREE: return "tree";
    case DebugCategory::RENDER: return "render";
    case DebugCategory::MODEL: return "model";
    case DebugCategory::EXTRUDE: return "extrude";
    case DebugCategory::SELECTION: return "selection";
    case DebugCategory::IMPORT: return "import";
    case DebugCategory::TIMELINE: return "timeline";
    case DebugCategory::INPUT: return "input";
    case DebugCategory::DOCUMENT: return "document";
    }
    return "unknown";
}

inline bool debug_enabled(DebugCategory category)
{
    if (debug_all)
        return true;
    if (category == DebugCategory::UI && debug_ui)
        return true;
    if (category == DebugCategory::RENDER && debug_render)
        return true;
    return debug_category_flag(category);
}

inline void debug_log(DebugCategory category, const std::string &message)
{
    if (!debug_enabled(category))
        return;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    const auto filename = std::string("/tmp/dune3d-debug-") + debug_category_name(category) + ".log";
    std::ofstream log(filename, std::ios::app);
    log << message << '\n';
}

class DebugTrace {
public:
    DebugTrace(DebugCategory category, const char *function, const char *file, int line)
        : m_category(category), m_function(function), m_file(file), m_line(line), m_enabled(debug_enabled(category))
    {
        if (m_enabled)
            debug_log(m_category, "ENTER " + std::string(m_function) + " (" + m_file + ":"
                                         + std::to_string(m_line) + ")");
    }

    ~DebugTrace()
    {
        if (m_enabled)
            debug_log(m_category, "EXIT " + std::string(m_function));
    }

private:
    DebugCategory m_category;
    const char *m_function;
    const char *m_file;
    int m_line;
    bool m_enabled;
};

#define DUNE3D_DEBUG_JOIN_IMPL(a, b) a##b
#define DUNE3D_DEBUG_JOIN(a, b) DUNE3D_DEBUG_JOIN_IMPL(a, b)
#define DUNE3D_TRACE(category) \
    ::dune3d::DebugTrace DUNE3D_DEBUG_JOIN(dune3d_debug_trace_, __LINE__)(category, __func__, __FILE__, __LINE__)

inline void configure_debug_flags(int &argc, char *argv[])
{
    int write_index = 1;
    for (int read_index = 1; read_index < argc; read_index++) {
        const std::string_view arg(argv[read_index]);
        if (arg == "-debug_ui" || arg == "--debug_ui" || arg == "--debug-ui")
            debug_ui = true;
        else if (arg == "-debug_render" || arg == "--debug_render" || arg == "--debug-render")
            debug_render = true;
        else if (arg == "-debug_all" || arg == "--debug_all" || arg == "--debug-all")
            debug_all = true;
        else if (arg.starts_with("-debug_") || arg.starts_with("--debug-")) {
            auto name = arg.starts_with("-debug_") ? arg.substr(7) : arg.substr(8);
            if (name == "tree")
                debug_category_flag(DebugCategory::TREE) = true;
            else if (name == "model")
                debug_category_flag(DebugCategory::MODEL) = true;
            else if (name == "extrude")
                debug_category_flag(DebugCategory::EXTRUDE) = true;
            else if (name == "selection")
                debug_category_flag(DebugCategory::SELECTION) = true;
            else if (name == "import")
                debug_category_flag(DebugCategory::IMPORT) = true;
            else if (name == "timeline")
                debug_category_flag(DebugCategory::TIMELINE) = true;
            else if (name == "input")
                debug_category_flag(DebugCategory::INPUT) = true;
            else if (name == "document")
                debug_category_flag(DebugCategory::DOCUMENT) = true;
        }
        else
            argv[write_index++] = argv[read_index];
    }
    argc = write_index;
    argv[argc] = nullptr;
}

} // namespace dune3d
