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

#include <iostream>
#include <iomanip>
#include <vector>

#include <unordered_map>

#include <sparsehash/dense_hash_map>
#include <sparsehash/sparse_hash_map>

#include <Common/Stopwatch.h>

//#define DBMS_HASH_MAP_COUNT_COLLISIONS
#define DBMS_HASH_MAP_DEBUG_RESIZES

#include <Core/Types.h>
#include <IO/ReadBufferFromFile.h>
#include <IO/ReadHelpers.h>
#include <IO/CompressedReadBuffer.h>
#include <common/StringRef.h>
#include <Common/HashTable/HashMap.h>
#include <Interpreters/AggregationCommon.h>


struct SmallStringRef
{
    UInt32 size;

    union
    {
        const char * data_big;
        char data_small[12];
    };

    bool isSmall() const { return size <= 12; }

    const char * data() const
    {
        return isSmall() ? data_small : data_big;
    }

    SmallStringRef(const char * data_, size_t size_)
    {
        size = size_;

        if (isSmall())
            memcpy(data_small, data_, size_);
        else
            data_big = data_;
    }

    SmallStringRef(const unsigned char * data_, size_t size_) : SmallStringRef(reinterpret_cast<const char *>(data_), size_) {}
    explicit SmallStringRef(const std::string & s) : SmallStringRef(s.data(), s.size()) {}
    SmallStringRef() {}

    std::string toString() const { return std::string(data(), size); }
};


inline bool operator==(SmallStringRef lhs, SmallStringRef rhs)
{
    if (lhs.size != rhs.size)
        return false;

    if (lhs.size == 0)
        return true;

#if __SSE2__
    return memequalSSE2Wide(lhs.data(), rhs.data(), lhs.size);
#else
    return false;
#endif
}


namespace ZeroTraits
{
    template <>
    inline bool check<SmallStringRef>(SmallStringRef x) { return x.size == 0; }

    template <>
    inline void set<SmallStringRef>(SmallStringRef & x) { x.size = 0; }
};

template <>
struct DefaultHash<SmallStringRef>
{
    size_t operator() (SmallStringRef x) const
    {
        return DefaultHash<StringRef>()(StringRef(x.data(), x.size));
    }
};


using Value = UInt64;


int main(int argc, char ** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: program n m\n";
        return 1;
    }

    size_t n = atoi(argv[1]);
    size_t m = atoi(argv[2]);

    DB::Arena pool;
    std::vector<StringRef> data(n);

    std::cerr << "sizeof(Key) = " << sizeof(SmallStringRef) << ", sizeof(Value) = " << sizeof(Value) << std::endl;

    {
        Stopwatch watch;
        DB::ReadBufferFromFileDescriptor in1(STDIN_FILENO);
        DB::CompressedReadBuffer in2(in1);

        std::string tmp;
        for (size_t i = 0; i < n && !in2.eof(); ++i)
        {
            DB::readStringBinary(tmp, in2);
            data[i] = StringRef(pool.insert(tmp.data(), tmp.size()), tmp.size());
        }

        watch.stop();
        std::cerr << std::fixed << std::setprecision(2)
            << "Vector. Size: " << n
            << ", elapsed: " << watch.elapsedSeconds()
            << " (" << n / watch.elapsedSeconds() << " elem/sec.)"
            << std::endl;
    }

    if (!m || m == 1)
    {
        Stopwatch watch;

        using Map = HashMapWithSavedHash<StringRef, Value>;

        Map map;
        Map::iterator it;
        bool inserted;

        for (size_t i = 0; i < n; ++i)
        {
            map.emplace(data[i], it, inserted);
            if (inserted)
                it->second = 0;
            ++it->second;
        }

        watch.stop();
        std::cerr << std::fixed << std::setprecision(2)
            << "HashMap (StringRef). Size: " << map.size()
            << ", elapsed: " << watch.elapsedSeconds()
            << " (" << n / watch.elapsedSeconds() << " elem/sec.)"
#ifdef DBMS_HASH_MAP_COUNT_COLLISIONS
            << ", collisions: " << map.getCollisions()
#endif
            << std::endl;
    }

    if (!m || m == 2)
    {
        Stopwatch watch;

        using Map = HashMapWithSavedHash<SmallStringRef, Value>;

        Map map;
        Map::iterator it;
        bool inserted;

        for (size_t i = 0; i < n; ++i)
        {
            map.emplace(SmallStringRef(data[i].data, data[i].size), it, inserted);
            if (inserted)
                it->second = 0;
            ++it->second;
        }

        watch.stop();
        std::cerr << std::fixed << std::setprecision(2)
            << "HashMap (SmallStringRef). Size: " << map.size()
            << ", elapsed: " << watch.elapsedSeconds()
            << " (" << n / watch.elapsedSeconds() << " elem/sec.)"
#ifdef DBMS_HASH_MAP_COUNT_COLLISIONS
            << ", collisions: " << map.getCollisions()
#endif
            << std::endl;
    }

    return 0;
}
