// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Common/Exception.h>
#include <Common/Stopwatch.h>
#include <IO/ReadHelpers.h>
#include <common/ThreadPool.h>

#include <iomanip>
#include <iostream>


int x = 0;

void f()
{
    ++x;
}

/*void f()
{
    std::vector<std::string> vec;
    for (size_t i = 0; i < 100; ++i)
        vec.push_back(std::string(rand() % 10, ' '));
}*/

void * g(void *)
{
    f();
    return {};
}


template <typename F>
void test(size_t n, const char * name, F && kernel)
{
    x = 0;

    Stopwatch watch;
    Stopwatch watch_one;
    double max_seconds = 0;

    std::cerr << name << ":\n";

    for (size_t i = 0; i < n; ++i)
    {
        watch_one.restart();

        kernel();

        watch_one.stop();
        if (watch_one.elapsedSeconds() > max_seconds)
            max_seconds = watch_one.elapsedSeconds();
    }

    watch.stop();

    std::cerr
        << std::fixed << std::setprecision(2)
        << n << " ops in "
        << watch.elapsedSeconds() << " sec., "
        << n / watch.elapsedSeconds() << " ops/sec., "
        << "avg latency: " << watch.elapsedSeconds() / n * 1000000 << " μs, "
        << "max latency: " << max_seconds * 1000000 << " μs "
        << "(res = " << x << ")"
        << std::endl;
}


int main(int argc, char ** argv)
{
    size_t n = argc == 2 ? DB::parse<UInt64>(argv[1]) : 100000;

    /*    test(n, "Create and destroy boost::threadpool each iteration", []
    {
        boost::threadpool::pool tp(1);
        tp.schedule(f);
        tp.wait();
    });*/

    test(n, "Create and destroy ThreadPool each iteration", [] {
        ThreadPool tp(1);
        tp.schedule(f);
        tp.wait();
    });

    test(n, "pthread_create, pthread_join each iteration", [] {
        pthread_t thread;
        if (pthread_create(&thread, nullptr, g, nullptr))
            DB::throwFromErrno("Cannot create thread.");
        if (pthread_join(thread, nullptr))
            DB::throwFromErrno("Cannot join thread.");
    });

    test(n, "Create and destroy std::thread each iteration", [] {
        std::thread thread(f);
        thread.join();
    });

    /*    {
        boost::threadpool::pool tp(1);

        test(n, "Schedule job for boost::threadpool each iteration", [&tp]
        {
            tp.schedule(f);
            tp.wait();
        });
    }*/

    {
        ThreadPool tp(1);

        test(n, "Schedule job for Threadpool each iteration", [&tp] {
            tp.schedule(f);
            tp.wait();
        });
    }

    /*    {
        boost::threadpool::pool tp(128);

        test(n, "Schedule job for boost::threadpool with 128 threads each iteration", [&tp]
        {
            tp.schedule(f);
            tp.wait();
        });
    }*/

    {
        ThreadPool tp(128);

        test(n, "Schedule job for Threadpool with 128 threads each iteration", [&tp] {
            tp.schedule(f);
            tp.wait();
        });
    }

    return 0;
}
