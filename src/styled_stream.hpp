/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_STYLED_STREAM_HPP
#define SCOPES_STYLED_STREAM_HPP

#include "symbol_enum.hpp"

#include <iostream>

namespace scopes {

//------------------------------------------------------------------------------
// ANSI COLOR FORMATTING
//------------------------------------------------------------------------------

typedef KnownSymbol Style;

// support 24-bit ANSI colors (ISO-8613-3)
// works on most bash shells as well as windows 10
void ansi_from_style(std::ostream &ost, Style style);

typedef void (*StreamStyleFunction)(std::ostream &, Style);

void stream_ansi_style(std::ostream &ost, Style style);

void stream_plain_style(std::ostream &ost, Style style);

extern StreamStyleFunction stream_default_style;

struct StyledStream {
    StreamStyleFunction _ssf;
    std::ostream &_ost;

    StyledStream(std::ostream &ost, StreamStyleFunction ssf);
    StyledStream(std::ostream &ost);

    StyledStream();

    static StyledStream plain(std::ostream &ost);
    static StyledStream plain(StyledStream &ost);

    template<typename T>
    StyledStream& operator<<(const T &o) { _ost << o; return *this; }
    template<typename T>
    StyledStream& operator<<(const T *o) { _ost << o; return *this; }
    template<typename T>
    StyledStream& operator<<(T &o) { _ost << o; return *this; }

    StyledStream& operator<<(std::ostream &(*o)(std::ostream&));

    StyledStream& operator<<(Style s);

    StyledStream& operator<<(bool s);

    StyledStream& stream_number(int8_t x);
    StyledStream& stream_number(uint8_t x);

    StyledStream& stream_number(double x);
    StyledStream& stream_number(float x);

    template<typename T>
    StyledStream& stream_number(T x) {
        _ssf(_ost, Style_Number);
        _ost << x;
        _ssf(_ost, Style_None);
        return *this;
    }
};

#define STREAM_STYLED_NUMBER(T) \
    inline StyledStream& operator<<(StyledStream& ss, T x) { \
        ss.stream_number(x); \
        return ss; \
    }
STREAM_STYLED_NUMBER(int8_t)
STREAM_STYLED_NUMBER(int16_t)
STREAM_STYLED_NUMBER(int32_t)
STREAM_STYLED_NUMBER(int64_t)
STREAM_STYLED_NUMBER(uint8_t)
STREAM_STYLED_NUMBER(uint16_t)
STREAM_STYLED_NUMBER(uint32_t)
STREAM_STYLED_NUMBER(uint64_t)
STREAM_STYLED_NUMBER(float)
STREAM_STYLED_NUMBER(double)

#undef STREAM_STYLED_NUMBER

} // namespace scopes

#endif // SCOPES_STYLED_STREAM_HPP
