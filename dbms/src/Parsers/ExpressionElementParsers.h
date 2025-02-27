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


class ParserArray : public IParserBase
{
protected:
    const char * getName() const { return "array"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** If in parenthesis an expression from one element - returns this element in `node`;
  *  or if there is a SELECT subquery in parenthesis, then this subquery returned in `node`;
  *  otherwise returns `tuple` function from the contents of brackets.
  */
class ParserParenthesisExpression : public IParserBase
{
protected:
    const char * getName() const { return "parenthesized expression"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** The SELECT subquery is in parenthesis.
  */
class ParserSubquery : public IParserBase
{
protected:
    const char * getName() const { return "SELECT subquery"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** An identifier, for example, x_yz123 or `something special`
  */
class ParserIdentifier : public IParserBase
{
protected:
    const char * getName() const { return "identifier"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** An identifier, possibly containing a dot, for example, x_yz123 or `something special` or Hits.EventTime
  */
class ParserCompoundIdentifier : public IParserBase
{
protected:
    const char * getName() const { return "compound identifier"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/// Just *
class ParserAsterisk : public IParserBase
{
protected:
    const char * getName() const { return "asterisk"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** Something like t.* or db.table.*
  */
class ParserQualifiedAsterisk : public IParserBase
{
protected:
    const char * getName() const { return "qualified asterisk"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** A function, for example, f(x, y + 1, g(z)).
  * Or an aggregate function: sum(x + f(y)), corr(x, y). The syntax is the same as the usual function.
  * Or a parametric aggregate function: quantile(0.9)(x + y).
  *  Syntax - two pairs of parentheses instead of one. The first is for parameters, the second for arguments.
  * For functions, the DISTINCT modifier can be specified, for example, count(DISTINCT x, y).
  */
class ParserFunction : public IParserBase
{
protected:
    const char * getName() const { return "function"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};

class ParserCastExpression : public IParserBase
{
    /// this name is used for identifying CAST expression among other function calls
    static constexpr auto name = "CAST";

protected:
    const char * getName() const override { return name; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected) override;
};

class ParserExtractExpression : public IParserBase
{
    static constexpr auto name = "EXTRACT";

protected:
    const char * getName() const override { return name; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected) override;
};


/** NULL literal.
  */
class ParserNull : public IParserBase
{
protected:
    const char * getName() const { return "NULL"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** Numeric literal.
  */
class ParserNumber : public IParserBase
{
protected:
    const char * getName() const { return "number"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};

/** Unsigned integer, used in right hand side of tuple access operator (x.1).
  */
class ParserUnsignedInteger : public IParserBase
{
protected:
    const char * getName() const { return "unsigned integer"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** String in single quotes.
  */
class ParserStringLiteral : public IParserBase
{
protected:
    const char * getName() const { return "string literal"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** An array of literals.
  * Arrays can also be parsed as an application of [] operator.
  * But parsing the whole array as a whole constant seriously speeds up the analysis of expressions in the case of very large arrays.
  * We try to parse the array as an array of literals first (fast path),
  *  and if it did not work out (when the array consists of complex expressions) - parse as an application of [] operator (slow path).
  */
class ParserArrayOfLiterals : public IParserBase
{
protected:
    const char * getName() const { return "array"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** The literal is one of: NULL, UInt64, Int64, Float64, String.
  */
class ParserLiteral : public IParserBase
{
protected:
    const char * getName() const { return "literal"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** The alias is the identifier before which `AS` comes. For example: AS x_yz123.
  */
struct ParserAliasBase
{
    static const char * restricted_keywords[];
};

template <typename ParserIdentifier>
class ParserAliasImpl : public IParserBase, ParserAliasBase
{
public:
    ParserAliasImpl(bool allow_alias_without_as_keyword_)
        : allow_alias_without_as_keyword(allow_alias_without_as_keyword_) {}
protected:
    bool allow_alias_without_as_keyword;

    const char * getName() const { return "alias"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


class ParserTypeInCastExpression;

extern template class ParserAliasImpl<ParserIdentifier>;
extern template class ParserAliasImpl<ParserTypeInCastExpression>;

using ParserAlias = ParserAliasImpl<ParserIdentifier>;
using ParserCastExpressionAlias = ParserAliasImpl<ParserTypeInCastExpression>;


/** The expression element is one of: an expression in parentheses, an array, a literal, a function, an identifier, an asterisk.
  */
class ParserExpressionElement : public IParserBase
{
protected:
    const char * getName() const { return "element of expression"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};


/** An expression element, possibly with an alias, if appropriate.
  */
template <typename ParserAlias>
class ParserWithOptionalAliasImpl : public IParserBase
{
public:
    ParserWithOptionalAliasImpl(ParserPtr && elem_parser_, bool allow_alias_without_as_keyword_, bool prefer_alias_to_column_name_ = false)
    : elem_parser(std::move(elem_parser_)), allow_alias_without_as_keyword(allow_alias_without_as_keyword_),
      prefer_alias_to_column_name(prefer_alias_to_column_name_) {}
protected:
    ParserPtr elem_parser;
    bool allow_alias_without_as_keyword;
    bool prefer_alias_to_column_name;

    const char * getName() const { return "element of expression with optional alias"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};

extern template class ParserWithOptionalAliasImpl<ParserAlias>;
extern template class ParserWithOptionalAliasImpl<ParserCastExpressionAlias>;

using ParserWithOptionalAlias = ParserWithOptionalAliasImpl<ParserAlias>;
using ParserCastExpressionWithOptionalAlias = ParserWithOptionalAliasImpl<ParserCastExpressionAlias>;


/** Element of ORDER BY expression - same as expression element, but in addition, ASC[ENDING] | DESC[ENDING] could be specified
  *  and optionally, NULLS LAST|FIRST
  *  and optionally, COLLATE 'locale'.
  */
class ParserOrderByElement : public IParserBase
{
protected:
    const char * getName() const { return "element of ORDER BY expression"; }
    bool parseImpl(Pos & pos, ASTPtr & node, Expected & expected);
};

}
