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
#include <Columns/ColumnArray.h>
#include <Columns/ColumnVector.h>
#include <Common/FieldVisitors.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypesNumber.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <Interpreters/convertFieldToType.h>

#define AGGREGATE_FUNCTION_GROUP_ARRAY_INSERT_AT_MAX_SIZE 0xFFFFFF


namespace DB
{
namespace ErrorCodes
{
extern const int TOO_LARGE_ARRAY_SIZE;
extern const int CANNOT_CONVERT_TYPE;
extern const int ILLEGAL_TYPE_OF_ARGUMENT;
extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
} // namespace ErrorCodes


/** Aggregate function, that takes two arguments: value and position,
  *  and as a result, builds an array with values are located at corresponding positions.
  *
  * If more than one value was inserted to single position, the any value (first in case of single thread) is stored.
  * If no values was inserted to some position, then default value will be substituted.
  *
  * Aggregate function also accept optional parameters:
  * - default value to substitute;
  * - length to resize result arrays (if you want to have results of same length for all aggregation keys);
  *
  * If you want to pass length, default value should be also given.
  */


/// Generic case (inefficient).
struct AggregateFunctionGroupArrayInsertAtDataGeneric
{
    Array value; /// TODO Add MemoryTracker
};


class AggregateFunctionGroupArrayInsertAtGeneric final
    : public IAggregateFunctionDataHelper<AggregateFunctionGroupArrayInsertAtDataGeneric, AggregateFunctionGroupArrayInsertAtGeneric>
{
private:
    DataTypePtr type;
    Field default_value;
    UInt64 length_to_resize = 0; /// zero means - do not do resizing.

public:
    AggregateFunctionGroupArrayInsertAtGeneric(const DataTypes & arguments, const Array & params)
    {
        if (!params.empty())
        {
            if (params.size() > 2)
                throw Exception("Aggregate function " + getName() + " requires at most two parameters.", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

            default_value = params[0];

            if (params.size() == 2)
            {
                length_to_resize = applyVisitor(FieldVisitorConvertToNumber<UInt64>(), params[1]);
                if (length_to_resize > AGGREGATE_FUNCTION_GROUP_ARRAY_INSERT_AT_MAX_SIZE)
                    throw Exception("Too large array size", ErrorCodes::TOO_LARGE_ARRAY_SIZE);
            }
        }

        if (arguments.size() != 2)
            throw Exception("Aggregate function " + getName() + " requires two arguments.", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        if (!arguments[1]->isUnsignedInteger())
            throw Exception("Second argument of aggregate function " + getName() + " must be integer.", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        type = arguments.front();

        if (default_value.isNull())
            default_value = type->getDefault();
        else
        {
            Field converted = convertFieldToType(default_value, *type);
            if (converted.isNull())
                throw Exception("Cannot convert parameter of aggregate function " + getName() + " (" + applyVisitor(FieldVisitorToString(), default_value) + ")"
                                                                                                                                                             " to type "
                                    + type->getName() + " to be used as default value in array",
                                ErrorCodes::CANNOT_CONVERT_TYPE);

            default_value = converted;
        }
    }

    String getName() const override { return "groupArrayInsertAt"; }

    DataTypePtr getReturnType() const override
    {
        return std::make_shared<DataTypeArray>(type);
    }

    void add(AggregateDataPtr __restrict place, const IColumn ** columns, size_t row_num, Arena *) const override
    {
        /// TODO Do positions need to be 1-based for this function?
        size_t position = columns[1]->get64(row_num);

        /// If position is larger than size to which array will be cutted - simply ignore value.
        if (length_to_resize && position >= length_to_resize)
            return;

        if (position >= AGGREGATE_FUNCTION_GROUP_ARRAY_INSERT_AT_MAX_SIZE)
            throw Exception("Too large array size: position argument (" + toString(position) + ")"
                                                                                               " is greater or equals to limit ("
                                + toString(AGGREGATE_FUNCTION_GROUP_ARRAY_INSERT_AT_MAX_SIZE) + ")",
                            ErrorCodes::TOO_LARGE_ARRAY_SIZE);

        Array & arr = data(place).value;

        if (arr.size() <= position)
            arr.resize(position + 1);
        else if (!arr[position].isNull())
            return; /// Element was already inserted to the specified position.

        columns[0]->get(row_num, arr[position]);
    }

    void merge(AggregateDataPtr __restrict place, ConstAggregateDataPtr rhs, Arena *) const override
    {
        Array & arr_lhs = data(place).value;
        const Array & arr_rhs = data(rhs).value;

        if (arr_lhs.size() < arr_rhs.size())
            arr_lhs.resize(arr_rhs.size());

        for (size_t i = 0, size = arr_rhs.size(); i < size; ++i)
            if (arr_lhs[i].isNull() && !arr_rhs[i].isNull())
                arr_lhs[i] = arr_rhs[i];
    }

    void serialize(ConstAggregateDataPtr __restrict place, WriteBuffer & buf) const override
    {
        const Array & arr = data(place).value;
        size_t size = arr.size();
        writeVarUInt(size, buf);

        for (const Field & elem : arr)
        {
            if (elem.isNull())
            {
                writeBinary(UInt8(1), buf);
            }
            else
            {
                writeBinary(UInt8(0), buf);
                type->serializeBinary(elem, buf);
            }
        }
    }

    void deserialize(AggregateDataPtr __restrict place, ReadBuffer & buf, Arena *) const override
    {
        size_t size = 0;
        readVarUInt(size, buf);

        if (size > AGGREGATE_FUNCTION_GROUP_ARRAY_INSERT_AT_MAX_SIZE)
            throw Exception("Too large array size", ErrorCodes::TOO_LARGE_ARRAY_SIZE);

        Array & arr = data(place).value;

        arr.resize(size);
        for (size_t i = 0; i < size; ++i)
        {
            UInt8 is_null = 0;
            readBinary(is_null, buf);
            if (!is_null)
                type->deserializeBinary(arr[i], buf);
        }
    }

    void insertResultInto(ConstAggregateDataPtr __restrict place, IColumn & to, Arena *) const override
    {
        ColumnArray & to_array = static_cast<ColumnArray &>(to);
        IColumn & to_data = to_array.getData();
        ColumnArray::Offsets & to_offsets = to_array.getOffsets();

        const Array & arr = data(place).value;

        for (const Field & elem : arr)
        {
            if (!elem.isNull())
                to_data.insert(elem);
            else
                to_data.insert(default_value);
        }

        size_t result_array_size = length_to_resize ? length_to_resize : arr.size();

        /// Pad array if need.
        for (size_t i = arr.size(); i < result_array_size; ++i)
            to_data.insert(default_value);

        to_offsets.push_back((to_offsets.empty() ? 0 : to_offsets.back()) + result_array_size);
    }

    const char * getHeaderFilePath() const override { return __FILE__; }
};


#undef AGGREGATE_FUNCTION_GROUP_ARRAY_INSERT_AT_MAX_SIZE

} // namespace DB
