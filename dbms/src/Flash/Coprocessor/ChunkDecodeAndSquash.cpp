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

#include <Flash/Coprocessor/ChunkDecodeAndSquash.h>
#include <IO/ReadBufferFromString.h>

namespace DB
{
CHBlockChunkDecodeAndSquash::CHBlockChunkDecodeAndSquash(
    const Block & header,
    size_t rows_limit_)
    : codec(header)
    , rows_limit(rows_limit_)
{
}

std::optional<Block> CHBlockChunkDecodeAndSquash::decodeAndSquash(const String & str)
{
    std::optional<Block> res;
    ReadBufferFromString istr(str);
    if (istr.eof())
    {
        if (accumulated_block)
            res.swap(accumulated_block);
        return res;
    }

    if (!accumulated_block)
    {
        /// hard-code 1.5 here, since final column size will be more than rows_limit in most situations,
        /// so it should be larger than 1.0, just use 1.5 here, no special meaning
        Block block = codec.decodeImpl(istr, static_cast<size_t>(rows_limit * 1.5));
        if (block)
            accumulated_block.emplace(std::move(block));
    }
    else
    {
        /// Dimensions
        size_t columns = 0;
        size_t rows = 0;
        codec.readBlockMeta(istr, columns, rows);

        if (rows)
        {
            auto mutable_columns = accumulated_block->mutateColumns();
            for (size_t i = 0; i < columns; ++i)
            {
                ColumnWithTypeAndName column;
                codec.readColumnMeta(i, istr, column);
                CHBlockChunkCodec::readData(*column.type, *(mutable_columns[i]), istr, rows);
            }
            accumulated_block->setColumns(std::move(mutable_columns));
        }
    }

    if (accumulated_block && accumulated_block->rows() >= rows_limit)
    {
        /// Return accumulated data and reset accumulated_block
        res.swap(accumulated_block);
        return res;
    }
    return res;
}

std::optional<Block> CHBlockChunkDecodeAndSquash::flush()
{
    if (!accumulated_block)
        return accumulated_block;
    std::optional<Block> res;
    accumulated_block.swap(res);
    return res;
}

} // namespace DB
