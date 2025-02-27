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

#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTTablesInSelectQuery.h>
#include <Parsers/CommonParsers.h>
#include <Parsers/ExpressionElementParsers.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/ParserSampleRatio.h>
#include <Parsers/ParserSelectQuery.h>
#include <Parsers/ParserTablesInSelectQuery.h>


namespace DB
{

namespace ErrorCodes
{
extern const int SYNTAX_ERROR;
}


bool ParserTableExpression::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    auto res = std::make_shared<ASTTableExpression>();

    if (!ParserWithOptionalAlias(std::make_unique<ParserSubquery>(), true).parse(pos, res->subquery, expected)
        && !ParserWithOptionalAlias(std::make_unique<ParserFunction>(), true).parse(pos, res->table_function, expected)
        && !ParserWithOptionalAlias(std::make_unique<ParserCompoundIdentifier>(), true).parse(pos, res->database_and_table_name, expected))
        return false;

    /// FINAL
    if (ParserKeyword("FINAL").ignore(pos, expected))
        res->final = true;

    /// SAMPLE number
    if (ParserKeyword("SAMPLE").ignore(pos, expected))
    {
        ParserSampleRatio ratio;

        if (!ratio.parse(pos, res->sample_size, expected))
            return false;

        /// OFFSET number
        if (ParserKeyword("OFFSET").ignore(pos, expected))
        {
            if (!ratio.parse(pos, res->sample_offset, expected))
                return false;
        }
    }

    if (res->database_and_table_name)
        res->children.emplace_back(res->database_and_table_name);
    if (res->table_function)
        res->children.emplace_back(res->table_function);
    if (res->subquery)
        res->children.emplace_back(res->subquery);
    if (res->sample_size)
        res->children.emplace_back(res->sample_size);
    if (res->sample_offset)
        res->children.emplace_back(res->sample_offset);

    node = res;
    return true;
}


bool ParserTablesInSelectQueryElement::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    auto res = std::make_shared<ASTTablesInSelectQueryElement>();

    if (is_first)
    {
        if (!ParserTableExpression().parse(pos, res->table_expression, expected))
            return false;
    }
    else
    {
        auto table_join = std::make_shared<ASTTableJoin>();

        if (pos->type == TokenType::Comma)
        {
            ++pos;
            table_join->kind = ASTTableJoin::Kind::Comma;
        }
        else
        {
            if (ParserKeyword("GLOBAL").ignore(pos))
                table_join->locality = ASTTableJoin::Locality::Global;
            else if (ParserKeyword("LOCAL").ignore(pos))
                table_join->locality = ASTTableJoin::Locality::Local;

            if (ParserKeyword("ANY").ignore(pos))
                table_join->strictness = ASTTableJoin::Strictness::Any;
            else if (ParserKeyword("ALL").ignore(pos))
                table_join->strictness = ASTTableJoin::Strictness::All;

            if (ParserKeyword("INNER").ignore(pos))
                table_join->kind = ASTTableJoin::Kind::Inner;
            else if (ParserKeyword("LEFT").ignore(pos))
                table_join->kind = ASTTableJoin::Kind::Left;
            else if (ParserKeyword("RIGHT").ignore(pos))
                table_join->kind = ASTTableJoin::Kind::Right;
            else if (ParserKeyword("FULL").ignore(pos))
                table_join->kind = ASTTableJoin::Kind::Full;
            else if (ParserKeyword("CROSS").ignore(pos))
                table_join->kind = ASTTableJoin::Kind::Cross;
            else
            {
                /// Maybe need use INNER by default as in another DBMS.
                return false;
            }

            if (table_join->strictness != ASTTableJoin::Strictness::Unspecified
                && table_join->kind == ASTTableJoin::Kind::Cross)
                throw Exception("You must not specify ANY or ALL for CROSS JOIN.", ErrorCodes::SYNTAX_ERROR);

            /// Optional OUTER keyword for outer joins.
            if (table_join->kind == ASTTableJoin::Kind::Left
                || table_join->kind == ASTTableJoin::Kind::Right
                || table_join->kind == ASTTableJoin::Kind::Full)
            {
                ParserKeyword("OUTER").ignore(pos);
            }

            if (!ParserKeyword("JOIN").ignore(pos, expected))
                return false;
        }

        if (!ParserTableExpression().parse(pos, res->table_expression, expected))
            return false;

        if (table_join->kind != ASTTableJoin::Kind::Comma
            && table_join->kind != ASTTableJoin::Kind::Cross)
        {
            if (ParserKeyword("USING").ignore(pos, expected))
            {
                /// Expression for USING could be in parentheses or not.
                bool in_parens = pos->type == TokenType::OpeningRoundBracket;
                if (in_parens)
                    ++pos;

                if (!ParserExpressionList(false).parse(pos, table_join->using_expression_list, expected))
                    return false;

                if (in_parens)
                {
                    if (pos->type != TokenType::ClosingRoundBracket)
                        return false;
                    ++pos;
                }
            }
            else if (ParserKeyword("ON").ignore(pos, expected))
            {
                /// OR is operator with lowest priority, so start parsing from it.
                if (!ParserLogicalOrExpression().parse(pos, table_join->on_expression, expected))
                    return false;
            }
            else
            {
                return false;
            }
        }

        if (table_join->using_expression_list)
            table_join->children.emplace_back(table_join->using_expression_list);
        if (table_join->on_expression)
            table_join->children.emplace_back(table_join->on_expression);

        res->table_join = table_join;
    }

    if (res->table_expression)
        res->children.emplace_back(res->table_expression);
    if (res->table_join)
        res->children.emplace_back(res->table_join);

    node = res;
    return true;
}


bool ParserTablesInSelectQuery::parseImpl(Pos & pos, ASTPtr & node, Expected & expected)
{
    auto res = std::make_shared<ASTTablesInSelectQuery>();

    ASTPtr child;

    if (ParserTablesInSelectQueryElement(true).parse(pos, child, expected))
        res->children.emplace_back(child);
    else
        return false;

    while (ParserTablesInSelectQueryElement(false).parse(pos, child, expected))
        res->children.emplace_back(child);

    node = res;
    return true;
}

} // namespace DB
