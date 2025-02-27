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

#include <DataStreams/ExpressionBlockInputStream.h>
#include <Interpreters/ExpressionActions.h>

namespace DB
{
ExpressionBlockInputStream::ExpressionBlockInputStream(
    const BlockInputStreamPtr & input,
    const ExpressionActionsPtr & expression_,
    const String & req_id)
    : expression(expression_)
    , log(Logger::get(req_id))
{
    children.push_back(input);
}

Block ExpressionBlockInputStream::getTotals()
{
    if (auto * child = dynamic_cast<IProfilingBlockInputStream *>(&*children.back()))
    {
        totals = child->getTotals();
        expression->executeOnTotals(totals);
    }

    return totals;
}

Block ExpressionBlockInputStream::getHeader() const
{
    Block res = children.back()->getHeader();
    expression->execute(res);
    return res;
}

Block ExpressionBlockInputStream::readImpl()
{
    Block res = children.back()->read();
    if (!res)
        return res;
    expression->execute(res);
    return res;
}

} // namespace DB
