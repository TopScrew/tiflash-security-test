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

#include <IO/ReadHelpers.h>
#include <IO/Operators.h>

#include <DataStreams/verbosePrintString.h>
#include <DataStreams/CSVRowInputStream.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int INCORRECT_DATA;
    extern const int LOGICAL_ERROR;
}


CSVRowInputStream::CSVRowInputStream(ReadBuffer & istr_, const Block & header_, const char delimiter_, bool with_names_, bool with_types_)
    : istr(istr_), header(header_), delimiter(delimiter_), with_names(with_names_), with_types(with_types_)
{
    size_t num_columns = header.columns();
    data_types.resize(num_columns);
    for (size_t i = 0; i < num_columns; ++i)
        data_types[i] = header.safeGetByPosition(i).type;
}


static void skipEndOfLine(ReadBuffer & istr)
{
    /// \n (Unix) or \r\n (DOS/Windows) or \n\r (Mac OS Classic)

    if (*istr.position() == '\n')
    {
        ++istr.position();
        if (!istr.eof() && *istr.position() == '\r')
            ++istr.position();
    }
    else if (*istr.position() == '\r')
    {
        ++istr.position();
        if (!istr.eof() && *istr.position() == '\n')
            ++istr.position();
        else
            throw Exception("Cannot parse CSV format: found \\r (CR) not followed by \\n (LF)."
                " Line must end by \\n (LF) or \\r\\n (CR LF) or \\n\\r.", ErrorCodes::INCORRECT_DATA);
    }
    else if (!istr.eof())
        throw Exception("Expected end of line", ErrorCodes::INCORRECT_DATA);
}


static void skipDelimiter(ReadBuffer & istr, const char delimiter, bool is_last_column)
{
    if (is_last_column)
    {
        if (istr.eof())
            return;

        /// we support the extra delimiter at the end of the line
        if (*istr.position() == delimiter)
        {
            ++istr.position();
            if (istr.eof())
                return;
        }

        skipEndOfLine(istr);
    }
    else
        assertChar(delimiter, istr);
}


/// Skip `whitespace` symbols allowed in CSV.
static inline void skipWhitespacesAndTabs(ReadBuffer & buf)
{
    while (!buf.eof()
            && (*buf.position() == ' '
                || *buf.position() == '\t'))
        ++buf.position();
}


static void skipRow(ReadBuffer & istr, const char delimiter, size_t num_columns)
{
    String tmp;
    for (size_t i = 0; i < num_columns; ++i)
    {
        skipWhitespacesAndTabs(istr);
        readCSVString(tmp, istr);
        skipWhitespacesAndTabs(istr);

        skipDelimiter(istr, delimiter, i + 1 == num_columns);
    }
}


void CSVRowInputStream::readPrefix()
{
    /// In this format, we assume, that if first string field contain BOM as value, it will be written in quotes,
    ///  so BOM at beginning of stream cannot be confused with BOM in first string value, and it is safe to skip it.
    skipBOMIfExists(istr);

    size_t num_columns = data_types.size();
    String tmp;

    if (with_names)
        skipRow(istr, delimiter, num_columns);

    if (with_types)
        skipRow(istr, delimiter, num_columns);
}


bool CSVRowInputStream::read(MutableColumns & columns)
{
    if (istr.eof())
        return false;

    updateDiagnosticInfo();

    size_t size = data_types.size();

    for (size_t i = 0; i < size; ++i)
    {
        skipWhitespacesAndTabs(istr);
        data_types[i]->deserializeTextCSV(*columns[i], istr, delimiter);
        skipWhitespacesAndTabs(istr);

        skipDelimiter(istr, delimiter, i + 1 == size);
    }

    return true;
}


String CSVRowInputStream::getDiagnosticInfo()
{
    if (istr.eof())        /// Buffer has gone, cannot extract information about what has been parsed.
        return {};

    WriteBufferFromOwnString out;

    MutableColumns columns = header.cloneEmptyColumns();

    /// It is possible to display detailed diagnostics only if the last and next to last rows are still in the read buffer.
    size_t bytes_read_at_start_of_buffer = istr.count() - istr.offset();
    if (bytes_read_at_start_of_buffer != bytes_read_at_start_of_buffer_on_prev_row)
    {
        out << "Could not print diagnostic info because two last rows aren't in buffer (rare case)\n";
        return out.str();
    }

    size_t max_length_of_column_name = 0;
    for (size_t i = 0; i < header.columns(); ++i)
        if (header.safeGetByPosition(i).name.size() > max_length_of_column_name)
            max_length_of_column_name = header.safeGetByPosition(i).name.size();

    size_t max_length_of_data_type_name = 0;
    for (size_t i = 0; i < header.columns(); ++i)
        if (header.safeGetByPosition(i).type->getName().size() > max_length_of_data_type_name)
            max_length_of_data_type_name = header.safeGetByPosition(i).type->getName().size();

    /// Roll back the cursor to the beginning of the previous or current row and parse all over again. But now we derive detailed information.

    if (pos_of_prev_row)
    {
        istr.position() = pos_of_prev_row;

        out << "\nRow " << (row_num - 1) << ":\n";
        if (!parseRowAndPrintDiagnosticInfo(columns, out, max_length_of_column_name, max_length_of_data_type_name))
            return out.str();
    }
    else
    {
        if (!pos_of_current_row)
        {
            out << "Could not print diagnostic info because parsing of data hasn't started.\n";
            return out.str();
        }

        istr.position() = pos_of_current_row;
    }

    out << "\nRow " << row_num << ":\n";
    parseRowAndPrintDiagnosticInfo(columns, out, max_length_of_column_name, max_length_of_data_type_name);
    out << "\n";

    return out.str();
}


bool CSVRowInputStream::parseRowAndPrintDiagnosticInfo(MutableColumns & columns,
    WriteBuffer & out, size_t max_length_of_column_name, size_t max_length_of_data_type_name)
{
    size_t size = data_types.size();
    for (size_t i = 0; i < size; ++i)
    {
        if (i == 0 && istr.eof())
        {
            out << "<End of stream>\n";
            return false;
        }

        out << "Column " << i << ", " << std::string((i < 10 ? 2 : i < 100 ? 1 : 0), ' ')
            << "name: " << header.safeGetByPosition(i).name << ", " << std::string(max_length_of_column_name - header.safeGetByPosition(i).name.size(), ' ')
            << "type: " << data_types[i]->getName() << ", " << std::string(max_length_of_data_type_name - data_types[i]->getName().size(), ' ');

        BufferBase::Position prev_position = istr.position();
        BufferBase::Position curr_position = istr.position();
        std::exception_ptr exception;

        try
        {
            skipWhitespacesAndTabs(istr);
            prev_position = istr.position();
            data_types[i]->deserializeTextCSV(*columns[i], istr, delimiter);
            curr_position = istr.position();
            skipWhitespacesAndTabs(istr);
        }
        catch (...)
        {
            exception = std::current_exception();
        }

        if (curr_position < prev_position)
            throw Exception("Logical error: parsing is non-deterministic.", ErrorCodes::LOGICAL_ERROR);

        if (data_types[i]->isNumber() || data_types[i]->isDateOrDateTime())
        {
            /// An empty string instead of a value.
            if (curr_position == prev_position)
            {
                out << "ERROR: text ";
                verbosePrintString(prev_position, std::min(prev_position + 10, istr.buffer().end()), out);
                out << " is not like " << data_types[i]->getName() << "\n";
                return false;
            }
        }

        out << "parsed text: ";
        verbosePrintString(prev_position, curr_position, out);

        if (exception)
        {
            if (data_types[i]->getName() == "DateTime")
                out << "ERROR: DateTime must be in YYYY-MM-DD hh:mm:ss or NNNNNNNNNN (unix timestamp, exactly 10 digits) format.\n";
            else if (data_types[i]->getName() == "Date")
                out << "ERROR: Date must be in YYYY-MM-DD format.\n";
            else
                out << "ERROR\n";
            return false;
        }

        out << "\n";

        if (data_types[i]->haveMaximumSizeOfValue())
        {
            if (*curr_position != '\n' && *curr_position != '\r' && *curr_position != delimiter)
            {
                out << "ERROR: garbage after " << data_types[i]->getName() << ": ";
                verbosePrintString(curr_position, std::min(curr_position + 10, istr.buffer().end()), out);
                out << "\n";

                if (data_types[i]->getName() == "DateTime")
                    out << "ERROR: DateTime must be in YYYY-MM-DD hh:mm:ss or NNNNNNNNNN (unix timestamp, exactly 10 digits) format.\n";
                else if (data_types[i]->getName() == "Date")
                    out << "ERROR: Date must be in YYYY-MM-DD format.\n";

                return false;
            }
        }

        /// Delimiters
        if (i + 1 == size)
        {
            if (istr.eof())
                return false;

            /// we support the extra delimiter at the end of the line
            if (*istr.position() == delimiter)
            {
                ++istr.position();
                if (istr.eof())
                    break;
            }

            if (!istr.eof() && *istr.position() != '\n' && *istr.position() != '\r')
            {
                out << "ERROR: There is no line feed. ";
                verbosePrintString(istr.position(), istr.position() + 1, out);
                out << " found instead.\n"
                    " It's like your file has more columns than expected.\n"
                    "And if your file have right number of columns, maybe it have unquoted string value with comma.\n";

                return false;
            }

            skipEndOfLine(istr);
        }
        else
        {
            try
            {
                assertChar(delimiter, istr);
            }
            catch (const DB::Exception &)
            {
                if (*istr.position() == '\n' || *istr.position() == '\r')
                {
                    out << "ERROR: Line feed found where delimiter (" << delimiter << ") is expected."
                        " It's like your file has less columns than expected.\n"
                        "And if your file have right number of columns, maybe it have unescaped quotes in values.\n";
                }
                else
                {
                    out << "ERROR: There is no delimiter (" << delimiter << "). ";
                    verbosePrintString(istr.position(), istr.position() + 1, out);
                    out << " found instead.\n";
                }
                return false;
            }
        }
    }

    return true;
}


void CSVRowInputStream::syncAfterError()
{
    skipToNextLineOrEOF(istr);
}

void CSVRowInputStream::updateDiagnosticInfo()
{
    ++row_num;

    bytes_read_at_start_of_buffer_on_prev_row = bytes_read_at_start_of_buffer_on_current_row;
    bytes_read_at_start_of_buffer_on_current_row = istr.count() - istr.offset();

    pos_of_prev_row = pos_of_current_row;
    pos_of_current_row = istr.position();
}

}
