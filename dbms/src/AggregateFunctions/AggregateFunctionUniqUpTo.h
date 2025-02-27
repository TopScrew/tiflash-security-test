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

#pragma once

#include <AggregateFunctions/IAggregateFunction.h>
#include <AggregateFunctions/UniqVariadicHash.h>
#include <Columns/ColumnsNumber.h>
#include <Common/FieldVisitors.h>
#include <Common/typeid_cast.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypeUUID.h>
#include <DataTypes/DataTypesNumber.h>
#include <IO/ReadHelpers.h>

namespace DB
{
/** Counts the number of unique values up to no more than specified in the parameter.
  *
  * Example: uniqUpTo(3)(UserID)
  * - will count the number of unique visitors, return 1, 2, 3 or 4 if visitors > = 4.
  *
  * For strings, a non-cryptographic hash function is used, due to which the calculation may be a bit inaccurate.
  */

template <typename T>
struct __attribute__((__packed__)) AggregateFunctionUniqUpToData : AggregationCollatorsWrapper<false>
{
    /** If count == threshold + 1 - this means that it is "overflowed" (values greater than threshold).
  * In this case (for example, after calling the merge function), the `data` array does not necessarily contain the initialized values
  * - example: combine a state in which there are few values, with another state that has overflowed;
  *   then set count to `threshold + 1`, and values from another state are not copied.
  */
    UInt8 count = 0;

    T * data;


    size_t size() const
    {
        return count;
    }

    /// threshold - for how many elements there is room in a `data`.
    void insert(T x, UInt8 threshold)
    {
        /// The state is already full - nothing needs to be done.
        if (count > threshold)
            return;

        /// Linear search for the matching element.
        for (size_t i = 0; i < count; ++i)
            if (data[i] == x)
                return;

        /// Did not find the matching element. If there is room for one more element, insert it.
        if (count < threshold)
            data[count] = x;

        /// After increasing count, the state may be overflowed.
        ++count;
    }

    void merge(const AggregateFunctionUniqUpToData<T> & rhs, UInt8 threshold)
    {
        if (count > threshold)
            return;

        if (rhs.count > threshold)
        {
            /// If `rhs` is overflowed, then set `count` too also overflowed for the current state.
            count = rhs.count;
            return;
        }

        for (size_t i = 0; i < rhs.count; ++i)
            insert(rhs.data[i], threshold);
    }

    void write(WriteBuffer & wb, UInt8 threshold) const
    {
        writeBinary(count, wb);

        /// Write values only if the state is not overflowed. Otherwise, they are not needed, and only the fact that the state is overflowed is important.
        if (count <= threshold)
            wb.write(reinterpret_cast<const char *>(&data[0]), count * sizeof(data[0]));
        writeCollators(wb);
    }

    void read(ReadBuffer & rb, UInt8 threshold)
    {
        readBinary(count, rb);

        if (count <= threshold)
            rb.read(reinterpret_cast<char *>(&data[0]), count * sizeof(data[0]));
        readCollators(rb);
    }

    void add(const IColumn & column, size_t row_num, UInt8 threshold)
    {
        insert(static_cast<const ColumnVector<T> &>(column).getData()[row_num], threshold);
    }
};


/// For strings, their hashes are remembered.
template <>
struct AggregateFunctionUniqUpToData<String> : AggregateFunctionUniqUpToData<UInt64>
{
    void add(const IColumn & column, size_t row_num, UInt8 threshold)
    {
        /// Keep in mind that calculations are approximate.
        StringRef value = column.getDataAt(row_num);
        value = getUpdatedValueForCollator(value, 0);
        insert(CityHash_v1_0_2::CityHash64(value.data, value.size), threshold);
    }
};

template <>
struct AggregateFunctionUniqUpToData<UInt128> : AggregateFunctionUniqUpToData<UInt64>
{
    void add(const IColumn & column, size_t row_num, UInt8 threshold)
    {
        UInt128 value = static_cast<const ColumnVector<UInt128> &>(column).getData()[row_num];
        insert(sipHash64(value), threshold);
    }
};


template <typename T>
class AggregateFunctionUniqUpTo final : public IAggregateFunctionDataHelper<AggregateFunctionUniqUpToData<T>, AggregateFunctionUniqUpTo<T>>
{
private:
    UInt8 threshold;

public:
    explicit AggregateFunctionUniqUpTo(UInt8 threshold)
        : threshold(threshold)
    {
    }

    size_t sizeOfData() const override
    {
        return sizeof(AggregateFunctionUniqUpToData<T>) + sizeof(T) * threshold;
    }

    String getName() const override { return "uniqUpTo"; }

    DataTypePtr getReturnType() const override
    {
        return std::make_shared<DataTypeUInt64>();
    }

    void add(AggregateDataPtr __restrict place, const IColumn ** columns, size_t row_num, Arena *) const override
    {
        this->data(place).add(*columns[0], row_num, threshold);
    }

    void merge(AggregateDataPtr __restrict place, ConstAggregateDataPtr rhs, Arena *) const override
    {
        this->data(place).merge(this->data(rhs), threshold);
    }

    void serialize(ConstAggregateDataPtr __restrict place, WriteBuffer & buf) const override
    {
        this->data(place).write(buf, threshold);
    }

    void deserialize(AggregateDataPtr __restrict place, ReadBuffer & buf, Arena *) const override
    {
        this->data(place).read(buf, threshold);
    }

    void insertResultInto(ConstAggregateDataPtr __restrict place, IColumn & to, Arena *) const override
    {
        static_cast<ColumnUInt64 &>(to).getData().push_back(this->data(place).size());
    }

    const char * getHeaderFilePath() const override { return __FILE__; }
};


/** For multiple arguments. To compute, hashes them.
  * You can pass multiple arguments as is; You can also pass one argument - a tuple.
  * But (for the possibility of effective implementation), you can not pass several arguments, among which there are tuples.
  */
template <bool argument_is_tuple>
class AggregateFunctionUniqUpToVariadic final
    : public IAggregateFunctionDataHelper<AggregateFunctionUniqUpToData<UInt64>, AggregateFunctionUniqUpToVariadic<argument_is_tuple>>
{
private:
    size_t num_args = 0;
    UInt8 threshold;

public:
    AggregateFunctionUniqUpToVariadic(const DataTypes & arguments, UInt8 threshold)
        : threshold(threshold)
    {
        if (argument_is_tuple)
            num_args = typeid_cast<const DataTypeTuple &>(*arguments[0]).getElements().size();
        else
            num_args = arguments.size();
    }

    size_t sizeOfData() const override
    {
        return sizeof(AggregateFunctionUniqUpToData<UInt64>) + sizeof(UInt64) * threshold;
    }

    String getName() const override { return "uniqUpTo"; }

    DataTypePtr getReturnType() const override
    {
        return std::make_shared<DataTypeUInt64>();
    }

    void add(AggregateDataPtr __restrict place, const IColumn ** columns, size_t row_num, Arena *) const override
    {
        this->data(place).insert(UniqVariadicHash<AggregateFunctionUniqUpToData<UInt64>, false, argument_is_tuple>::apply(this->data(place), num_args, columns, row_num), threshold);
    }

    void merge(AggregateDataPtr __restrict place, ConstAggregateDataPtr rhs, Arena *) const override
    {
        this->data(place).merge(this->data(rhs), threshold);
    }

    void serialize(ConstAggregateDataPtr __restrict place, WriteBuffer & buf) const override
    {
        this->data(place).write(buf, threshold);
    }

    void deserialize(AggregateDataPtr __restrict place, ReadBuffer & buf, Arena *) const override
    {
        this->data(place).read(buf, threshold);
    }

    void insertResultInto(ConstAggregateDataPtr __restrict place, IColumn & to, Arena *) const override
    {
        static_cast<ColumnUInt64 &>(to).getData().push_back(this->data(place).size());
    }

    const char * getHeaderFilePath() const override { return __FILE__; }
};


} // namespace DB
