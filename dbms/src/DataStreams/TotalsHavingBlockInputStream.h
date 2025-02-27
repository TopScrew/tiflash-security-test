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

#include <DataStreams/IProfilingBlockInputStream.h>


namespace DB
{

class ExpressionActions;


/** Takes blocks after grouping, with non-finalized aggregate functions.
  * Calculates total values according to totals_mode.
  * If necessary, evaluates the expression from HAVING and filters rows. Returns the finalized and filtered blocks.
  */
class TotalsHavingBlockInputStream : public IProfilingBlockInputStream
{
private:
    using ExpressionActionsPtr = std::shared_ptr<ExpressionActions>;

public:
    /// expression may be nullptr
    TotalsHavingBlockInputStream(
        const BlockInputStreamPtr & input_,
        bool overflow_row_, const ExpressionActionsPtr & expression_,
        const std::string & filter_column_, TotalsMode totals_mode_, double auto_include_threshold_);

    String getName() const override { return "TotalsHaving"; }

    Block getTotals() override;

    Block getHeader() const override;

protected:
    Block readImpl() override;

private:
    bool overflow_row;
    ExpressionActionsPtr expression;
    String filter_column_name;
    TotalsMode totals_mode;
    double auto_include_threshold;
    size_t passed_keys = 0;
    size_t total_keys = 0;

    /** Here are the values that did not pass max_rows_to_group_by.
      * They are added or not added to the current_totals, depending on the totals_mode.
      */
    Block overflow_aggregates;

    /// Here, total values are accumulated. After the work is finished, they will be placed in IProfilingBlockInputStream::totals.
    MutableColumns current_totals;
    /// Arena for aggregate function states in totals.
    ArenaPtr arena;

    /// If filter == nullptr - add all rows. Otherwise, only the rows that pass the filter (HAVING).
    void addToTotals(const Block & block, const IColumn::Filter * filter);
};

}
