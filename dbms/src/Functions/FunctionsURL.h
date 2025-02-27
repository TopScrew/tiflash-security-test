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

#include <Columns/ColumnString.h>
#include <Common/StringUtils/StringUtils.h>
#include <Common/StringView.h>
#include <Common/typeid_cast.h>
#include <DataTypes/DataTypeString.h>
#include <Functions/FunctionHelpers.h>
#include <Functions/FunctionsString.h>
#include <fmt/core.h>

#ifdef __APPLE__
#include <common/apple_memrchr.h>
#endif


namespace DB
{
/** URL processing functions.
  * All functions are not strictly follow RFC, instead they are maximally simplified for performance reasons.
  *
  * Functions for extraction parts of URL.
  * If URL has nothing like, then empty string is returned.
  *
  *  domain
  *  domainWithoutWWW
  *  topLevelDomain
  *  protocol
  *  path
  *  queryString
  *  fragment
  *  queryStringAndFragment
  *
  * Functions, removing parts from URL.
  * If URL has nothing like, then it is retured unchanged.
  *
  *  cutWWW
  *  cutFragment
  *  cutQueryString
  *  cutQueryStringAndFragment
  *
  * Extract value of parameter in query string or in fragment identifier. Return empty string, if URL has no such parameter.
  * If there are many parameters with same name - return value of first one. Value is not %-decoded.
  *
  *  extractURLParameter(URL, name)
  *
  * Extract all parameters from URL in form of array of strings name=value.
  *  extractURLParameters(URL)
  *
  * Extract names of all parameters from URL in form of array of strings.
  *  extractURLParameterNames(URL)
  *
  * Remove specified parameter from URL.
  *  cutURLParameter(URL, name)
  *
  * Get array of URL 'hierarchy' as in Yandex.Metrica tree-like reports. See docs.
  *  URLHierarchy(URL)
  */

using Pos = const char *;


/// Extracts scheme from given url.
inline StringView getURLScheme(const StringView & url)
{
    // scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
    const char * p = url.data();
    const char * end = url.data() + url.size();

    if (isAlphaASCII(*p))
    {
        for (++p; p < end; ++p)
        {
            if (!(isAlphaNumericASCII(*p) || *p == '+' || *p == '-' || *p == '.'))
            {
                break;
            }
        }

        return StringView(url.data(), p - url.data());
    }

    return StringView();
}


/// Extracts host from given url.
inline StringView getURLHost(const StringView & url)
{
    Pos pos = url.data();
    Pos end = url.data() + url.size();

    if (nullptr == (pos = strchr(pos, '/')))
        return StringView();

    if (pos != url.data())
    {
        StringView scheme = getURLScheme(url);
        Pos scheme_end = url.data() + scheme.size();

        // Colon must follows after scheme.
        if (pos - scheme_end != 1 || *scheme_end != ':')
            return StringView();
    }

    if (end - pos < 2 || *(pos) != '/' || *(pos + 1) != '/')
        return StringView();

    const char * start_of_host = (pos += 2);
    for (; pos < end; ++pos)
    {
        if (*pos == '@')
            start_of_host = pos + 1;
        else if (*pos == ':' || *pos == '/' || *pos == '?' || *pos == '#')
            break;
    }

    return (pos == start_of_host) ? StringView() : StringView(start_of_host, pos - start_of_host);
}


struct ExtractProtocol
{
    static size_t getReserveLengthForElement();

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size);
};

template <bool without_www>
struct ExtractDomain
{
    static size_t getReserveLengthForElement() { return 15; }

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        StringView host = getURLHost(StringView(data, size));

        if (host.empty())
        {
            res_data = data;
            res_size = 0;
        }
        else
        {
            if (without_www && host.size() > 4 && !strncmp(host.data(), "www.", 4))
                host = host.substr(4);

            res_data = host.data();
            res_size = host.size();
        }
    }
};

struct ExtractFirstSignificantSubdomain
{
    static size_t getReserveLengthForElement() { return 10; }

    static void execute(const Pos data, const size_t size, Pos & res_data, size_t & res_size, Pos * out_domain_end = nullptr)
    {
        res_data = data;
        res_size = 0;

        Pos tmp;
        size_t domain_length;
        ExtractDomain<true>::execute(data, size, tmp, domain_length);

        if (domain_length == 0)
            return;

        if (out_domain_end)
            *out_domain_end = tmp + domain_length;

        /// cut useless dot
        if (tmp[domain_length - 1] == '.')
            --domain_length;

        res_data = tmp;
        res_size = domain_length;

        const auto * begin = tmp;
        const auto * end = begin + domain_length;
        const char * last_3_periods[3]{};
        const auto * pos = static_cast<const char *>(memchr(begin, '.', domain_length));

        while (pos)
        {
            last_3_periods[2] = last_3_periods[1];
            last_3_periods[1] = last_3_periods[0];
            last_3_periods[0] = pos;
            pos = static_cast<const char *>(memchr(pos + 1, '.', end - pos - 1));
        }

        if (!last_3_periods[0])
            return;

        if (!last_3_periods[1])
        {
            res_size = last_3_periods[0] - begin;
            return;
        }

        if (!last_3_periods[2])
            last_3_periods[2] = begin - 1;

        if (!strncmp(last_3_periods[1] + 1, "com.", 4) /// Note that in ColumnString every value has zero byte after it.
            || !strncmp(last_3_periods[1] + 1, "net.", 4)
            || !strncmp(last_3_periods[1] + 1, "org.", 4)
            || !strncmp(last_3_periods[1] + 1, "co.", 3))
        {
            res_data += last_3_periods[2] + 1 - begin;
            res_size = last_3_periods[1] - last_3_periods[2] - 1;
            return;
        }

        res_data += last_3_periods[1] + 1 - begin;
        res_size = last_3_periods[0] - last_3_periods[1] - 1;
    }
};

struct CutToFirstSignificantSubdomain
{
    static size_t getReserveLengthForElement() { return 15; }

    static void execute(const Pos data, const size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos tmp_data;
        size_t tmp_length;
        Pos domain_end;
        ExtractFirstSignificantSubdomain::execute(data, size, tmp_data, tmp_length, &domain_end);

        if (tmp_length == 0)
            return;

        res_data = tmp_data;
        res_size = domain_end - tmp_data;
    }
};

struct ExtractTopLevelDomain
{
    static size_t getReserveLengthForElement() { return 5; }

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        StringView host = getURLHost(StringView(data, size));

        res_data = data;
        res_size = 0;

        if (!host.empty())
        {
            if (host.back() == '.')
                host = StringView(host.data(), host.size() - 1);

            Pos last_dot = reinterpret_cast<Pos>(memrchr(host.data(), '.', host.size()));

            if (!last_dot)
                return;
            /// For IPv4 addresses select nothing.
            if (last_dot[1] <= '9')
                return;

            res_data = last_dot + 1;
            res_size = (host.data() + host.size()) - res_data;
        }
    }
};

struct ExtractPath
{
    static size_t getReserveLengthForElement() { return 25; }

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos pos = data;
        Pos end = pos + size;

        if (nullptr != (pos = strchr(data, '/')) && pos[1] == '/' && nullptr != (pos = strchr(pos + 2, '/')))
        {
            Pos query_string_or_fragment = strpbrk(pos, "?#");

            res_data = pos;
            res_size = (query_string_or_fragment ? query_string_or_fragment : end) - res_data;
        }
    }
};

struct ExtractPathFull
{
    static size_t getReserveLengthForElement() { return 30; }

    static void execute(const Pos data, const size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos pos = data;
        Pos end = pos + size;

        if (nullptr != (pos = strchr(data, '/')) && pos[1] == '/' && nullptr != (pos = strchr(pos + 2, '/')))
        {
            res_data = pos;
            res_size = end - res_data;
        }
    }
};

template <bool without_leading_char>
struct ExtractQueryString
{
    static size_t getReserveLengthForElement() { return 10; }

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos pos = data;
        Pos end = pos + size;

        if (nullptr != (pos = strchr(data, '?')))
        {
            Pos fragment = strchr(pos, '#');

            res_data = pos + (without_leading_char ? 1 : 0);
            res_size = (fragment ? fragment : end) - res_data;
        }
    }
};

template <bool without_leading_char>
struct ExtractFragment
{
    static size_t getReserveLengthForElement() { return 10; }

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos pos = data;
        Pos end = pos + size;

        if (nullptr != (pos = strchr(data, '#')))
        {
            res_data = pos + (without_leading_char ? 1 : 0);
            res_size = end - res_data;
        }
    }
};

template <bool without_leading_char>
struct ExtractQueryStringAndFragment
{
    static size_t getReserveLengthForElement() { return 20; }

    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos pos = data;
        Pos end = pos + size;

        if (nullptr != (pos = strchr(data, '?')))
        {
            res_data = pos + (without_leading_char ? 1 : 0);
            res_size = end - res_data;
        }
        else if (nullptr != (pos = strchr(data, '#')))
        {
            res_data = pos;
            res_size = end - res_data;
        }
    }
};

/// With dot at the end.
struct ExtractWWW
{
    static void execute(Pos data, size_t size, Pos & res_data, size_t & res_size)
    {
        res_data = data;
        res_size = 0;

        Pos pos = data;
        Pos end = pos + size;

        if (nullptr != (pos = strchr(pos, '/')))
        {
            if (pos != data)
            {
                Pos tmp;
                size_t protocol_length;
                ExtractProtocol::execute(data, size, tmp, protocol_length);

                if (pos != data + protocol_length + 1)
                    return;
            }

            if (end - pos < 2 || *(pos) != '/' || *(pos + 1) != '/')
                return;

            const char * start_of_host = (pos += 2);
            for (; pos < end; ++pos)
            {
                if (*pos == '@')
                    start_of_host = pos + 1;
                else if (*pos == ':' || *pos == '/' || *pos == '?' || *pos == '#')
                    break;
            }

            if (start_of_host + 4 < end && !strncmp(start_of_host, "www.", 4))
            {
                res_data = start_of_host;
                res_size = 4;
            }
        }
    }
};


struct ExtractURLParameterImpl
{
    static void vector(const ColumnString::Chars_t & data,
                       const ColumnString::Offsets & offsets,
                       std::string pattern,
                       ColumnString::Chars_t & res_data,
                       ColumnString::Offsets & res_offsets)
    {
        res_data.reserve(data.size() / 5);
        res_offsets.resize(offsets.size());

        pattern += '=';
        const char * param_str = pattern.c_str();
        size_t param_len = pattern.size();

        size_t prev_offset = 0;
        size_t res_offset = 0;

        for (size_t i = 0; i < offsets.size(); ++i)
        {
            size_t cur_offset = offsets[i];

            const char * str = reinterpret_cast<const char *>(&data[prev_offset]);

            const char * pos = nullptr;
            const char * begin = strpbrk(str, "?#");
            if (begin)
            {
                pos = begin + 1;
                while (true)
                {
                    pos = strstr(pos, param_str);

                    if (pos == nullptr)
                        break;

                    if (pos[-1] != '?' && pos[-1] != '#' && pos[-1] != '&')
                    {
                        pos += param_len;
                        continue;
                    }
                    else
                    {
                        pos += param_len;
                        break;
                    }
                }
            }

            if (pos)
            {
                const char * end = strpbrk(pos, "&#");
                if (end == nullptr)
                    end = pos + strlen(pos);

                res_data.resize(res_offset + (end - pos) + 1);
                memcpySmallAllowReadWriteOverflow15(&res_data[res_offset], pos, end - pos);
                res_offset += end - pos;
            }
            else
            {
                res_data.resize(res_offset + 1);
            }

            res_data[res_offset] = 0;
            ++res_offset;
            res_offsets[i] = res_offset;

            prev_offset = cur_offset;
        }
    }
};


struct CutURLParameterImpl
{
    static void vector(const ColumnString::Chars_t & data,
                       const ColumnString::Offsets & offsets,
                       std::string pattern,
                       ColumnString::Chars_t & res_data,
                       ColumnString::Offsets & res_offsets)
    {
        res_data.reserve(data.size());
        res_offsets.resize(offsets.size());

        pattern += '=';
        const char * param_str = pattern.c_str();
        size_t param_len = pattern.size();

        size_t prev_offset = 0;
        size_t res_offset = 0;

        for (size_t i = 0; i < offsets.size(); ++i)
        {
            size_t cur_offset = offsets[i];

            const char * url_begin = reinterpret_cast<const char *>(&data[prev_offset]);
            const char * url_end = reinterpret_cast<const char *>(&data[cur_offset]) - 1;
            const char * begin_pos = url_begin;
            const char * end_pos = begin_pos;

            do
            {
                const char * begin = strpbrk(url_begin, "?#");
                if (begin == nullptr)
                    break;

                const char * pos = strstr(begin + 1, param_str);
                if (pos == nullptr)
                    break;

                if (pos[-1] != '?' && pos[-1] != '#' && pos[-1] != '&')
                {
                    pos = nullptr;
                    break;
                }

                begin_pos = pos;
                end_pos = begin_pos + param_len;

                /// Skip the value.
                while (*end_pos && *end_pos != '&' && *end_pos != '#')
                    ++end_pos;

                /// Capture '&' before or after the parameter.
                if (*end_pos == '&')
                    ++end_pos;
                else if (begin_pos[-1] == '&')
                    --begin_pos;
            } while (false);

            size_t cut_length = (url_end - url_begin) - (end_pos - begin_pos);
            res_data.resize(res_offset + cut_length + 1);
            memcpySmallAllowReadWriteOverflow15(&res_data[res_offset], url_begin, begin_pos - url_begin);
            memcpySmallAllowReadWriteOverflow15(&res_data[res_offset] + (begin_pos - url_begin), end_pos, url_end - end_pos);
            res_offset += cut_length + 1;
            res_data[res_offset - 1] = 0;
            res_offsets[i] = res_offset;

            prev_offset = cur_offset;
        }
    }
};


/** Select part of string using the Extractor.
  */
template <typename Extractor>
struct ExtractSubstringImpl
{
    static void vector(const ColumnString::Chars_t & data, const ColumnString::Offsets & offsets, ColumnString::Chars_t & res_data, ColumnString::Offsets & res_offsets)
    {
        size_t size = offsets.size();
        res_offsets.resize(size);
        res_data.reserve(size * Extractor::getReserveLengthForElement());

        size_t prev_offset = 0;
        size_t res_offset = 0;

        /// Matched part.
        Pos start;
        size_t length;

        for (size_t i = 0; i < size; ++i)
        {
            Extractor::execute(reinterpret_cast<const char *>(&data[prev_offset]), offsets[i] - prev_offset - 1, start, length);

            res_data.resize(res_data.size() + length + 1);
            memcpySmallAllowReadWriteOverflow15(&res_data[res_offset], start, length);
            res_offset += length + 1;
            res_data[res_offset - 1] = 0;

            res_offsets[i] = res_offset;
            prev_offset = offsets[i];
        }
    }

    static void constant(const std::string & data,
                         std::string & res_data)
    {
        Pos start;
        size_t length;
        Extractor::execute(data.data(), data.size(), start, length);
        res_data.assign(start, length);
    }

    static void vectorFixed(const ColumnString::Chars_t &, size_t, ColumnString::Chars_t &)
    {
        throw Exception("Column of type FixedString is not supported by URL functions", ErrorCodes::ILLEGAL_COLUMN);
    }
};


/** Delete part of string using the Extractor.
  */
template <typename Extractor>
struct CutSubstringImpl
{
    static void vector(const ColumnString::Chars_t & data, const ColumnString::Offsets & offsets, ColumnString::Chars_t & res_data, ColumnString::Offsets & res_offsets)
    {
        res_data.reserve(data.size());
        size_t size = offsets.size();
        res_offsets.resize(size);

        size_t prev_offset = 0;
        size_t res_offset = 0;

        /// Matched part.
        Pos start;
        size_t length;

        for (size_t i = 0; i < size; ++i)
        {
            const char * current = reinterpret_cast<const char *>(&data[prev_offset]);
            Extractor::execute(current, offsets[i] - prev_offset - 1, start, length);
            size_t start_index = start - reinterpret_cast<const char *>(&data[0]);

            res_data.resize(res_data.size() + offsets[i] - prev_offset - length);
            memcpySmallAllowReadWriteOverflow15(
                &res_data[res_offset],
                current,
                start - current);
            memcpySmallAllowReadWriteOverflow15(
                &res_data[res_offset + start - current],
                start + length,
                offsets[i] - start_index - length);
            res_offset += offsets[i] - prev_offset - length;

            res_offsets[i] = res_offset;
            prev_offset = offsets[i];
        }
    }

    static void constant(const std::string & data,
                         std::string & res_data)
    {
        Pos start;
        size_t length;
        Extractor::execute(data.data(), data.size(), start, length);
        res_data.reserve(data.size() - length);
        res_data.append(data.data(), start);
        res_data.append(start + length, data.data() + data.size());
    }

    static void vectorFixed(const ColumnString::Chars_t &, size_t, ColumnString::Chars_t &)
    {
        throw Exception("Column of type FixedString is not supported by URL functions", ErrorCodes::ILLEGAL_COLUMN);
    }
};


/// Percent decode of url data.
struct DecodeURLComponentImpl
{
    static void vector(const ColumnString::Chars_t & data, const ColumnString::Offsets & offsets, ColumnString::Chars_t & res_data, ColumnString::Offsets & res_offsets);

    static void constant(const std::string & data,
                         std::string & res_data);

    static void vectorFixed(const ColumnString::Chars_t & data, size_t n, ColumnString::Chars_t & res_data);
};

} // namespace DB
