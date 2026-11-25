// ZEBU Slicer: time spent in each stage of slicing and G-code export.
#include "ZebuTiming.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>

#include <tbb/global_control.h>

namespace Slic3r { namespace ZebuTiming {

namespace {
struct Counter
{
    std::string name;
    double      seconds = 0.;
    long        calls   = 0;
};

std::mutex           g_mutex;
std::vector<Counter> g_counters; // In order of first use; few entries, so a linear search is enough.
} // namespace

void add(const char *name, double seconds)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Counter &c : g_counters)
        if (c.name == name) {
            c.seconds += seconds;
            ++c.calls;
            return;
        }
    g_counters.push_back({name, seconds, 1});
}

void reset()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_counters.clear();
}

void report(const std::string &title)
{
    std::vector<Counter> counters;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        counters = g_counters;
    }
    for (const Counter &c : counters) {
        char line[256];
        std::snprintf(line, sizeof(line), "ZEBU timing | %-48s | %9.3f s | %ld", c.name.c_str(), c.seconds, c.calls);
        BOOST_LOG_TRIVIAL(info) << line;
    }

    const char *path = std::getenv("ZEBU_TIMINGS");
    if (path == nullptr || *path == 0)
        return;
    if (FILE *f = boost::nowide::fopen(path, "ab")) {
        std::fprintf(f, "# %s\n", title.c_str());
        for (const Counter &c : counters)
            std::fprintf(f, "%s;%.4f;%ld\n", c.name.c_str(), c.seconds, c.calls);
        std::fclose(f);
    }
}

void apply_thread_limit_from_env()
{
    const char *value = std::getenv("ZEBU_MAX_THREADS");
    if (value == nullptr || *value == 0)
        return;
    char *end = nullptr;
    const long n = std::strtol(value, &end, 10);
    if (end == value || n < 1 || n > 4096) {
        BOOST_LOG_TRIVIAL(warning) << "ZEBU_MAX_THREADS ignored: \"" << value << "\"";
        return;
    }
    // Lives until the application ends: TBB honours the limit for as long as the object exists.
    static std::unique_ptr<tbb::global_control> limit;
    limit = std::make_unique<tbb::global_control>(tbb::global_control::max_allowed_parallelism, size_t(n));
    BOOST_LOG_TRIVIAL(warning) << "ZEBU_MAX_THREADS: worker threads limited to " << n;
}

}} // namespace Slic3r::ZebuTiming
