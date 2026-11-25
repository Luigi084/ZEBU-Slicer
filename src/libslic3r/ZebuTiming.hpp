// ZEBU Slicer: time spent in each stage of slicing and G-code export.
//
// Each Scope adds its duration to a named counter (thread-safe; a stage run on several threads adds up
// the time of every thread, so "busy" time above the wall time of its section means it ran in parallel).
// report() writes the counters to the log (info level, so the GUI log has them) and, when the environment
// variable ZEBU_TIMINGS names a file, appends them to that file for benchmarks run from the command line.
#ifndef slic3r_ZebuTiming_hpp_
#define slic3r_ZebuTiming_hpp_

#include <chrono>
#include <string>

namespace Slic3r { namespace ZebuTiming {

void add(const char *name, double seconds);
void reset();
void report(const std::string &title);

class Scope
{
public:
    explicit Scope(const char *name) : m_name(name), m_start(std::chrono::steady_clock::now()) {}
    ~Scope() { add(m_name, std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count()); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char                           *m_name;
    std::chrono::steady_clock::time_point m_start;
};

// ZEBU_MAX_THREADS=<n> limits the worker threads of the whole application (for scaling measurements).
// Call once, early; does nothing when the variable is absent or invalid.
void apply_thread_limit_from_env();

}} // namespace Slic3r::ZebuTiming

#endif
