// MIT License
//
// Copyright (c) 2022 Advanced Micro Devices, Inc. All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "common/defines.h"
#include "common/delimit.hpp"
#include "common/environment.hpp"
#include "common/join.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(OMNITRACE_SETUP_LOG_NAME)
#    if defined(OMNITRACE_COMMON_LIBRARY_NAME)
#        define OMNITRACE_SETUP_LOG_NAME "[" OMNITRACE_COMMON_LIBRARY_NAME "]"
#    else
#        define OMNITRACE_SETUP_LOG_NAME
#    endif
#endif

#if !defined(OMNITRACE_SETUP_LOG_START)
#    if defined(OMNITRACE_COMMON_LIBRARY_LOG_START)
#        define OMNITRACE_SETUP_LOG_START OMNITRACE_COMMON_LIBRARY_LOG_START
#    elif defined(TIMEMORY_LOG_COLORS_AVAILABLE)
#        define OMNITRACE_SETUP_LOG_START                                                \
            fprintf(stderr, "%s", ::tim::log::color::info());
#    else
#        define OMNITRACE_SETUP_LOG_START
#    endif
#endif

#if !defined(OMNITRACE_SETUP_LOG_END)
#    if defined(OMNITRACE_COMMON_LIBRARY_LOG_END)
#        define OMNITRACE_SETUP_LOG_END OMNITRACE_COMMON_LIBRARY_LOG_END
#    elif defined(TIMEMORY_LOG_COLORS_AVAILABLE)
#        define OMNITRACE_SETUP_LOG_END fprintf(stderr, "%s", ::tim::log::color::end());
#    else
#        define OMNITRACE_SETUP_LOG_END
#    endif
#endif

#define OMNITRACE_SETUP_LOG(CONDITION, ...)                                              \
    if(CONDITION)                                                                        \
    {                                                                                    \
        fflush(stderr);                                                                  \
        OMNITRACE_SETUP_LOG_START                                                        \
        fprintf(stderr, "[omnitrace]" OMNITRACE_SETUP_LOG_NAME "[%i] ", getpid());       \
        fprintf(stderr, __VA_ARGS__);                                                    \
        OMNITRACE_SETUP_LOG_END                                                          \
        fflush(stderr);                                                                  \
    }

namespace omnitrace
{
inline namespace common
{
namespace path
{
inline std::string
find_path(const std::string& _path, int _verbose, std::string _search_paths = {})
{
    auto _exists = [](std::string_view _name) {
        struct stat _buffer;
        if(stat(_name.data(), &_buffer) == 0)
            return (S_ISREG(_buffer.st_mode) != 0 || S_ISLNK(_buffer.st_mode) != 0);
        return false;
    };

    if(_exists(_path) && !_path.empty() && _path.at(0) == '/') return _path;

    auto _default_search_paths =
        join(":", get_env("OMNITRACE_PATH", ""), get_env("PWD", ""), ".",
             get_env("LD_LIBRARY_PATH", ""), get_env("LIBRARY_PATH", ""));

    if(_search_paths.empty()) _search_paths = _default_search_paths;

    auto _paths = delimit(_search_paths, ":");

    if(_paths.empty())
    {
        _search_paths = _default_search_paths;
        _paths        = delimit(_search_paths, ":");
    }

    int _verbose_lvl = 2;
    for(const auto& itr : _paths)
    {
        auto _f = join('/', itr, _path);
        OMNITRACE_SETUP_LOG(_verbose >= _verbose_lvl, "searching for '%s' in '%s' ...\n",
                            _path.c_str(), itr.c_str());
        if(_exists(_f)) return _f;
        for(const auto* sitr : { "lib", "lib64", "../lib", "../lib64" })
        {
            _f = join('/', itr, sitr, _path);
            OMNITRACE_SETUP_LOG(_verbose >= _verbose_lvl,
                                "searching for '%s' in '%s' ...\n", _path.c_str(),
                                common::join('/', itr, sitr).c_str());
            if(_exists(_f)) return _f;
        }
    }
    return _path;
}

inline std::string
dirname(const std::string& _fname)
{
    if(_fname.find('/') != std::string::npos)
        return _fname.substr(0, _fname.find_last_of('/'));
    return std::string{};
}

inline bool
exists(const std::string& _fname)
{
    struct stat _buffer;
    if(stat(_fname.c_str(), &_buffer) == 0)
        return (S_ISREG(_buffer.st_mode) != 0 || S_ISLNK(_buffer.st_mode) != 0);
    return false;
}
}  // namespace path

inline void
setup_environ(int _verbose, const std::string& _search_paths = {},
              std::string _omnilib    = "libomnitrace.so",
              std::string _omnilib_dl = "libomnitrace-dl.so")
{
    _omnilib    = common::path::find_path(_omnilib, _verbose, _search_paths);
    _omnilib_dl = common::path::find_path(_omnilib_dl, _verbose, _search_paths);

#if defined(OMNITRACE_USE_ROCTRACER) && OMNITRACE_USE_ROCTRACER > 0
    setenv("HSA_TOOLS_LIB", _omnilib.c_str(), 0);
#endif

#if defined(OMNITRACE_USE_ROCPROFILER) && OMNITRACE_USE_ROCPROFILER > 0
#    if OMNITRACE_HIP_VERSION >= 50200
#        define ROCPROFILER_METRICS_DIR "lib/rocprofiler"
#    else
#        define ROCPROFILER_METRICS_DIR "rocprofiler/lib"
#    endif
    setenv("HSA_TOOLS_LIB", _omnilib.c_str(), 0);
    setenv("ROCP_TOOL_LIB", _omnilib.c_str(), 0);
    setenv("ROCPROFILER_LOG", "1", 0);
    setenv("ROCP_HSA_INTERCEPT", "1", 0);
    setenv("HSA_TOOLS_REPORT_LOAD_FAILURE", "1", 0);

    auto _possible_rocp_metrics = std::vector<std::string>{};
    auto _possible_rocprof_libs = std::vector<std::string>{};
    for(const auto* itr : { "OMNITRACE_ROCM_PATH", "ROCM_PATH" })
    {
        if(getenv(itr))
        {
            _possible_rocp_metrics.emplace_back(
                common::join('/', getenv(itr), "lib/rocprofiler", "metrics.xml"));
            _possible_rocprof_libs.emplace_back(
                common::join('/', getenv(itr), "lib/rocprofiler", "librocprofiler64.so"));
            _possible_rocp_metrics.emplace_back(
                common::join('/', getenv(itr), "rocprofiler/lib", "metrics.xml"));
            _possible_rocprof_libs.emplace_back(
                common::join('/', getenv(itr), "rocprofiler/lib", "librocprofiler64.so"));
        }
    }

    // default path
    _possible_rocp_metrics.emplace_back(
        common::join('/', OMNITRACE_DEFAULT_ROCM_PATH, "lib/rocprofiler", "metrics.xml"));
    _possible_rocp_metrics.emplace_back(
        common::join('/', OMNITRACE_DEFAULT_ROCM_PATH, "rocprofiler/lib", "metrics.xml"));

    for(const auto& itr : _possible_rocprof_libs)
    {
        if(path::exists(itr))
        {
            setenv("OMNITRACE_ROCPROFILER_LIBRARY", itr.c_str(), 0);
            _possible_rocp_metrics.emplace(
                _possible_rocp_metrics.begin(),
                common::join('/', path::dirname(itr),
                             "../../lib/rocprofiler/metrics.xml"));
            _possible_rocp_metrics.emplace(
                _possible_rocp_metrics.begin(),
                common::join('/', path::dirname(itr), "metrics.xml"));
        }
    }

    for(const auto& itr : _possible_rocp_metrics)
        if(path::exists(itr)) setenv("ROCP_METRICS", itr.c_str(), 0);

    // default if none of above succeeded
    setenv("ROCP_METRICS",
           common::join('/', OMNITRACE_DEFAULT_ROCM_PATH, ROCPROFILER_METRICS_DIR,
                        "metrics.xml")
               .c_str(),
           0);

#endif

#if defined(OMNITRACE_USE_OMPT) && OMNITRACE_USE_OMPT > 0
    std::string _omni_omp_libs = _omnilib_dl;
    const char* _omp_libs      = getenv("OMP_TOOL_LIBRARIES");
    if(_omp_libs != nullptr &&
       std::string_view{ _omp_libs }.find(_omnilib_dl) == std::string::npos)
        _omni_omp_libs = common::join(':', _omp_libs, _omnilib_dl);
    OMNITRACE_SETUP_LOG(_verbose >= 2, "setting OMP_TOOL_LIBRARIES to '%s'\n",
                        _omni_omp_libs.c_str());
    setenv("OMP_TOOL_LIBRARIES", _omni_omp_libs.c_str(), 1);
#endif

    (void) _omnilib;
    (void) _omnilib_dl;
}
}  // namespace common
}  // namespace omnitrace
