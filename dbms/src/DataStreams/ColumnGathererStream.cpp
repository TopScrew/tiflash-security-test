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

#include <Common/typeid_cast.h>
#include <DataStreams/ColumnGathererStream.h>
#include <common/logger_useful.h>

#include <iomanip>


namespace DB
{
namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int INCOMPATIBLE_COLUMNS;
extern const int INCORRECT_NUMBER_OF_COLUMNS;
extern const int NOT_FOUND_COLUMN_IN_BLOCK;
extern const int EMPTY_DATA_PASSED;
extern const int RECEIVED_EMPTY_DATA;
} // namespace ErrorCodes

ColumnGathererStream::ColumnGathererStream(
    const String & column_name_,
    const BlockInputStreams & source_streams,
    ReadBuffer & row_sources_buf_,
    size_t block_preferred_size_)
    : name(column_name_)
    , row_sources_buf(row_sources_buf_)
    , block_preferred_size(block_preferred_size_)
    , log(&Poco::Logger::get("ColumnGathererStream"))
{
    if (source_streams.empty())
        throw Exception("There are no streams to gather", ErrorCodes::EMPTY_DATA_PASSED);

    children.assign(source_streams.begin(), source_streams.end());
}


void ColumnGathererStream::init()
{
    sources.reserve(children.size());
    for (size_t i = 0; i < children.size(); ++i)
    {
        sources.emplace_back(children[i]->read(), name);

        Block & block = sources.back().block;

        /// Sometimes MergeTreeReader injects additional column with partitioning key
        if (block.columns() > 2)
            throw Exception(
                "Block should have 1 or 2 columns, but contains " + toString(block.columns()),
                ErrorCodes::INCORRECT_NUMBER_OF_COLUMNS);
        if (!block.has(name))
            throw Exception(
                "Not found column `" + name + "' in block.",
                ErrorCodes::NOT_FOUND_COLUMN_IN_BLOCK);

        if (i == 0)
        {
            column.name = name;
            column.type = block.getByName(name).type;
            column.column = column.type->createColumn();
        }

        if (block.getByName(name).column->getName() != column.column->getName())
            throw Exception("Column types don't match", ErrorCodes::INCOMPATIBLE_COLUMNS);
    }
}


Block ColumnGathererStream::readImpl()
{
    /// Special case: single source and there are no skipped rows
    if (children.size() == 1 && row_sources_buf.eof())
        return children[0]->read();

    /// Initialize first source blocks
    if (sources.empty())
        init();

    if (!source_to_fully_copy && row_sources_buf.eof())
        return Block();

    output_block = Block{column.cloneEmpty()};
    MutableColumnPtr output_column = output_block.getByPosition(0).column->assumeMutable();
    output_column->gather(*this);
    if (!output_column->empty())
        output_block.getByPosition(0).column = std::move(output_column);

    return output_block;
}


void ColumnGathererStream::fetchNewBlock(Source & source, size_t source_num)
{
    try
    {
        source.block = children[source_num]->read();
        source.update(name);
    }
    catch (Exception & e)
    {
        e.addMessage("Cannot fetch required block. Stream " + children[source_num]->getName() + ", part " + toString(source_num));
        throw;
    }

    if (0 == source.size)
    {
        throw Exception("Fetched block is empty. Stream " + children[source_num]->getName() + ", part " + toString(source_num),
                        ErrorCodes::RECEIVED_EMPTY_DATA);
    }
}


void ColumnGathererStream::readSuffixImpl()
{
    const BlockStreamProfileInfo & profile_info = getProfileInfo();

    /// Don't print info for small parts (< 10M rows)
    if (profile_info.rows < 10000000)
        return;

    double seconds = profile_info.total_stopwatch.elapsedSeconds();
    String speed;
    if (seconds)
        speed = fmt::format(", {:.2f} rows/sec., {:.2f} MiB/sec.", profile_info.rows / seconds, profile_info.bytes / 1048576.0 / seconds);
    LOG_TRACE(log,
              "Gathered column {} ({:.2f} bytes/elem.) in {} sec.{}",
              name,
              static_cast<double>(profile_info.bytes) / profile_info.rows,
              seconds,
              speed);
}

} // namespace DB
