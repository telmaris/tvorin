#ifndef PROFILER_H
#define PROFILER_H

#include <chrono>
#include <vector>
#include <map>
#include <string_view>
#include <thread>

struct ProfileSample
{
    const char* name;

    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;

    std::thread::id threadId;
};

class Profiler
{
public:
    ~Profiler() = default;

    static void AddSample(ProfileSample sample)
    {
        static Profiler ins;
        ins.samples.push_back(sample);
    }

private:
    Profiler() = default;

    std::vector<ProfileSample> samples;
};

class ProfileScope
{
public:
    ProfileScope(const char *n) : name(n), start(std::chrono::steady_clock::now()) {}

    ~ProfileScope()
    {
        Profiler::AddSample({
        name,
        start,
        std::chrono::steady_clock::now(),
        std::this_thread::get_id()
        });
    }

private:
    const char *name;
    std::chrono::steady_clock::time_point start;
};

#ifdef PROFILING_ON
#define PROFILE(name) \
                ProfileScope scope##__LINE__(name)
#else

#define PROFILE(name)

#endif
#endif PROFILER_H