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

#include <Parsers/IParserBase.h>

namespace DB
{


class ParserSelectWithUnionQuery : public IParserBase
{
protected:
    const char * getName() const override { return "SELECT query, possibly with UNION"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected) override;
};

}
