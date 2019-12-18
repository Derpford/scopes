/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_STRING_HPP
#define SCOPES_STRING_HPP

#include <stddef.h>

#include "styled_stream.hpp"
#include "scopes/config.h"

#include <sstream>
#include <vector>

namespace scopes {

const char STRING_ESCAPE_CHARS[] = "\"";

// small strings on the stack, big strings on the heap
#define SCOPES_BEGIN_TEMP_STRING(NAME, SIZE) \
    bool NAME ## _use_stack = ((SIZE) < 1024); \
    char stack ## NAME[NAME ## _use_stack?((SIZE)+1):1]; \
    char *NAME = (NAME ## _use_stack?stack ## NAME:((char *)malloc(sizeof(char) * ((SIZE)+1))));

#define SCOPES_END_TEMP_STRING(NAME) \
    if (!NAME ## _use_stack) free(NAME);

//------------------------------------------------------------------------------
// STRING
//------------------------------------------------------------------------------

#if SCOPES_USE_WCHAR
typedef std::wstring CppString;
typedef std::wstringstream StringStream;
#else
typedef std::string CppString;
typedef std::stringstream StringStream;
#endif

/*
struct String {
protected:
    String(const char *_data, size_t _count);

public:
    static const String *from(const char *s, size_t count);
    static const String *from_cstr(const char *s);
    static const String *join(const String *a, const String *b);

    template<unsigned N>
    static const String *from(const char (&s)[N]) {
        return from(s, N - 1);
    }

    static const String *from_stdstring(const std::string &s);
    static const String *from_stdstring(const std::wstring &ws);
    StyledStream& stream(StyledStream& ost, const char *escape_chars) const;
    const String *substr(int64_t i0, int64_t i1) const;

    std::size_t hash() const;

    std::string to_stdstring() const;

    struct Hash {
        std::size_t operator()(const String *s) const;
    };

    struct KeyEqual {
        bool operator()( const String *lhs, const String *rhs ) const;
    };

    const char *data;
    size_t count;
};

typedef std::vector<const String *> Strings;
*/

struct StyledString {
    StringStream _ss;
    StyledStream out;

    StyledString();
    StyledString(StreamStyleFunction ssf);

    static StyledString plain();
    std::string str() const;
    CppString cppstr() const;
};

std::string vformat( const char *fmt, va_list va );

std::string format( const char *fmt, ...);

// computes the levenshtein distance between two strings
size_t distance(const std::string &_s, const std::string &_t);

int unescape_string(char *buf);
int escape_string(char *buf, const char *str, int strcount, const char *quote_chars);

StyledStream& stream_escaped(StyledStream& ost, const std::string &str, const char *escape_chars);

uint64_t hash_string(const std::string &s);

} // namespace scopes

#endif // SCOPES_STRING_HPP