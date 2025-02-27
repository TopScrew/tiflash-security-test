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

#include <Dictionaries/IDictionarySource.h>
#include <Poco/Timestamp.h>


namespace DB
{

class Context;


/// Allows loading dictionaries from a file with given format, does not support "random access"
class FileDictionarySource final : public IDictionarySource
{
public:
    FileDictionarySource(const std::string & filename, const std::string & format, Block & sample_block,
        const Context & context);

    FileDictionarySource(const FileDictionarySource & other);

    BlockInputStreamPtr loadAll() override;

    BlockInputStreamPtr loadUpdatedAll() override
    {
        throw Exception{"Method loadUpdatedAll is unsupported for FileDictionarySource", ErrorCodes::NOT_IMPLEMENTED};
    }

    BlockInputStreamPtr loadIds(const std::vector<UInt64> & /*ids*/) override
    {
        throw Exception{"Method loadIds is unsupported for FileDictionarySource", ErrorCodes::NOT_IMPLEMENTED};
    }

    BlockInputStreamPtr loadKeys(
        const Columns & /*key_columns*/, const std::vector<size_t> & /*requested_rows*/) override
    {
        throw Exception{"Method loadKeys is unsupported for FileDictionarySource", ErrorCodes::NOT_IMPLEMENTED};
    }

    bool isModified() const override { return getLastModification() > last_modification; }
    bool supportsSelectiveLoad() const override { return false; }

    ///Not supported for FileDictionarySource
    bool hasUpdateField() const override { return false; }

    DictionarySourcePtr clone() const override { return std::make_unique<FileDictionarySource>(*this); }

    std::string toString() const override;

private:
    Poco::Timestamp getLastModification() const;

    const std::string filename;
    const std::string format;
    Block sample_block;
    const Context & context;
    Poco::Timestamp last_modification;
};

}
