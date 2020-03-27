/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#include "str_type.hpp"
#include "../error.hpp"
#include "../hash.hpp"
#include "../styled_stream.hpp"
#include "../string.hpp"
#include "typename_type.hpp"
#include "array_type.hpp"

#include <unordered_set>

namespace scopes {

static std::unordered_map<size_t, const Type *> strtypes;

//------------------------------------------------------------------------------

const Type *str_type(size_t count) {
    auto it = strtypes.find(count);
    if (it != strtypes.end()) {
        return it->second;
    }
    auto ss = StyledString::plain();
    ss.out << "str@";
    if (count == UNSIZED_COUNT)
        ss.out << "?";
    else
        ss.out << count;
    auto ST = array_type(TYPE_I8, count).assert_ok();
    auto T = plain_typename_type(ss.cppstr(), TYPE_Str, ST).assert_ok();
    strtypes.insert({count, T});
    return T;
}

} // namespace scopes
