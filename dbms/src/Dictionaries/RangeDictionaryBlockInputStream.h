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
#include <Columns/ColumnVector.h>
#include <Columns/ColumnString.h>
#include <Columns/IColumn.h>
#include <DataStreams/IProfilingBlockInputStream.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeDate.h>
#include <Dictionaries/DictionaryBlockInputStreamBase.h>
#include <Dictionaries/DictionaryStructure.h>
#include <Dictionaries/IDictionary.h>
#include <Dictionaries/RangeHashedDictionary.h>
#include <ext/range.h>

namespace DB
{

/*
 * BlockInputStream implementation for external dictionaries
 * read() returns single block consisting of the in-memory contents of the dictionaries
 */
template <typename DictionaryType, typename Key>
class RangeDictionaryBlockInputStream : public DictionaryBlockInputStreamBase
{
public:
    using DictionatyPtr = std::shared_ptr<DictionaryType const>;

    RangeDictionaryBlockInputStream(
        DictionatyPtr dictionary, size_t max_block_size, const Names & column_names, PaddedPODArray<Key> && ids,
        PaddedPODArray<UInt16> && start_dates, PaddedPODArray<UInt16> && end_dates);

    String getName() const override
    {
        return "RangeDictionary";
    }

protected:
    Block getBlock(size_t start, size_t length) const override;

private:
    template <typename Type>
    using DictionaryGetter = void (DictionaryType::*)(const std::string &, const PaddedPODArray<Key> &,
                             const PaddedPODArray<UInt16> &, PaddedPODArray<Type> &) const;

    template <typename AttributeType>
    ColumnPtr getColumnFromAttribute(DictionaryGetter<AttributeType> getter,
                                     const PaddedPODArray<Key> & ids, const PaddedPODArray<UInt16> & dates,
                                     const DictionaryAttribute& attribute, const DictionaryType& dictionary) const;
    ColumnPtr getColumnFromAttributeString(const PaddedPODArray<Key> & ids, const PaddedPODArray<UInt16> & dates,
                                           const DictionaryAttribute& attribute, const DictionaryType& dictionary) const;
    template <typename T>
    ColumnPtr getColumnFromPODArray(const PaddedPODArray<T> & array) const;

    template <typename T>
    void addSpecialColumn(
        const std::optional<DictionarySpecialAttribute> & attribute, DataTypePtr type,
        const std::string & default_name, const std::unordered_set<std::string> & column_names,
        const PaddedPODArray<T> & values, ColumnsWithTypeAndName& columns) const;

    Block fillBlock(const PaddedPODArray<Key> & ids,
                    const PaddedPODArray<UInt16> & start_dates, const PaddedPODArray<UInt16> & end_dates) const;

    PaddedPODArray<UInt16> makeDateKey(
        const PaddedPODArray<UInt16> & start_dates, const PaddedPODArray<UInt16> & end_dates) const;

    DictionatyPtr dictionary;
    Names column_names;
    PaddedPODArray<Key> ids;
    PaddedPODArray<UInt16> start_dates;
    PaddedPODArray<UInt16> end_dates;
};


template <typename DictionaryType, typename Key>
RangeDictionaryBlockInputStream<DictionaryType, Key>::RangeDictionaryBlockInputStream(
    DictionatyPtr dictionary, size_t max_column_size, const Names & column_names, PaddedPODArray<Key> && ids,
    PaddedPODArray<UInt16> && start_dates, PaddedPODArray<UInt16> && end_dates)
    : DictionaryBlockInputStreamBase(ids.size(), max_column_size),
      dictionary(dictionary), column_names(column_names),
      ids(std::move(ids)), start_dates(std::move(start_dates)), end_dates(std::move(end_dates))
{
}

template <typename DictionaryType, typename Key>
Block RangeDictionaryBlockInputStream<DictionaryType, Key>::getBlock(size_t start, size_t length) const
{
    PaddedPODArray<Key> block_ids;
    PaddedPODArray<UInt16> block_start_dates;
    PaddedPODArray<UInt16> block_end_dates;
    block_ids.reserve(length);
    block_start_dates.reserve(length);
    block_end_dates.reserve(length);

    for (auto idx : ext::range(start, start + length))
    {
        block_ids.push_back(ids[idx]);
        block_start_dates.push_back(start_dates[idx]);
        block_end_dates.push_back(end_dates[idx]);
    }

    return fillBlock(block_ids, block_start_dates, block_end_dates);
}

template <typename DictionaryType, typename Key>
template <typename AttributeType>
ColumnPtr RangeDictionaryBlockInputStream<DictionaryType, Key>::getColumnFromAttribute(
    DictionaryGetter<AttributeType> getter, const PaddedPODArray<Key> & ids,
    const PaddedPODArray<UInt16> & dates, const DictionaryAttribute& attribute, const DictionaryType& dictionary) const
{
    auto column_vector = ColumnVector<AttributeType>::create(ids.size());
    (dictionary.*getter)(attribute.name, ids, dates, column_vector->getData());
    return column_vector;
}

template <typename DictionaryType, typename Key>
ColumnPtr RangeDictionaryBlockInputStream<DictionaryType, Key>::getColumnFromAttributeString(
    const PaddedPODArray<Key> & ids, const PaddedPODArray<UInt16> & dates,
    const DictionaryAttribute& attribute, const DictionaryType& dictionary) const
{
    auto column_string = ColumnString::create();
    dictionary.getString(attribute.name, ids, dates, column_string.get());
    return column_string;
}

template <typename DictionaryType, typename Key>
template <typename T>
ColumnPtr RangeDictionaryBlockInputStream<DictionaryType, Key>::getColumnFromPODArray(const PaddedPODArray<T> & array) const
{
    auto column_vector = ColumnVector<T>::create();
    column_vector->getData().reserve(array.size());
    for (T value : array)
        column_vector->insert(value);
    return column_vector;
}


template <typename DictionaryType, typename Key>
template <typename T>
void RangeDictionaryBlockInputStream<DictionaryType, Key>::addSpecialColumn(
    const std::optional<DictionarySpecialAttribute> & attribute, DataTypePtr type,
    const std::string& default_name, const std::unordered_set<std::string> & column_names,
    const PaddedPODArray<T> & values, ColumnsWithTypeAndName & columns) const
{
    std::string name = default_name;
    if (attribute)
        name = attribute->name;

    if (column_names.find(name) != column_names.end())
        columns.emplace_back(getColumnFromPODArray(values), type, name);
}

template <typename DictionaryType, typename Key>
PaddedPODArray<UInt16> RangeDictionaryBlockInputStream<DictionaryType, Key>::makeDateKey(
        const PaddedPODArray<UInt16> & start_dates, const PaddedPODArray<UInt16> & end_dates) const
{
    PaddedPODArray<UInt16> key(start_dates.size());
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (RangeHashedDictionary::Range::isCorrectDate(start_dates[i]))
            key[i] = start_dates[i];
        else
            key[i] = end_dates[i];
    }

    return key;
}


template <typename DictionaryType, typename Key>
Block RangeDictionaryBlockInputStream<DictionaryType, Key>::fillBlock(
    const PaddedPODArray<Key> & ids,
    const PaddedPODArray<UInt16> & start_dates, const PaddedPODArray<UInt16> & end_dates) const
{
    ColumnsWithTypeAndName columns;
    const DictionaryStructure& structure = dictionary->getStructure();

    std::unordered_set<std::string> names(column_names.begin(), column_names.end());

    addSpecialColumn(structure.id, std::make_shared<DataTypeUInt64>(), "ID", names, ids, columns);
    addSpecialColumn(structure.range_min, std::make_shared<DataTypeDate>(), "Range Start", names, start_dates, columns);
    addSpecialColumn(structure.range_max, std::make_shared<DataTypeDate>(), "Range End", names, end_dates, columns);

    auto date_key = makeDateKey(start_dates, end_dates);

    for (const auto idx : ext::range(0, structure.attributes.size()))
    {
        const DictionaryAttribute& attribute = structure.attributes[idx];
        if (names.find(attribute.name) != names.end())
        {
            ColumnPtr column;
#define GET_COLUMN_FORM_ATTRIBUTE(TYPE)\
            column = getColumnFromAttribute<TYPE>(&DictionaryType::get##TYPE, ids, date_key, attribute, *dictionary)
            switch (attribute.underlying_type)
            {
            case AttributeUnderlyingType::UInt8:
                GET_COLUMN_FORM_ATTRIBUTE(UInt8);
                break;
            case AttributeUnderlyingType::UInt16:
                GET_COLUMN_FORM_ATTRIBUTE(UInt16);
                break;
            case AttributeUnderlyingType::UInt32:
                GET_COLUMN_FORM_ATTRIBUTE(UInt32);
                break;
            case AttributeUnderlyingType::UInt64:
                GET_COLUMN_FORM_ATTRIBUTE(UInt64);
                break;
            case AttributeUnderlyingType::UInt128:
                GET_COLUMN_FORM_ATTRIBUTE(UInt128);
                break;
            case AttributeUnderlyingType::Int8:
                GET_COLUMN_FORM_ATTRIBUTE(Int8);
                break;
            case AttributeUnderlyingType::Int16:
                GET_COLUMN_FORM_ATTRIBUTE(Int16);
                break;
            case AttributeUnderlyingType::Int32:
                GET_COLUMN_FORM_ATTRIBUTE(Int32);
                break;
            case AttributeUnderlyingType::Int64:
                GET_COLUMN_FORM_ATTRIBUTE(Int64);
                break;
            case AttributeUnderlyingType::Float32:
                GET_COLUMN_FORM_ATTRIBUTE(Float32);
                break;
            case AttributeUnderlyingType::Float64:
                GET_COLUMN_FORM_ATTRIBUTE(Float64);
                break;
            case AttributeUnderlyingType::String:
                column = getColumnFromAttributeString(ids, date_key, attribute, *dictionary);
                break;
            }

            columns.emplace_back(column, attribute.type, attribute.name);
        }
    }
    return Block(columns);
}

}
