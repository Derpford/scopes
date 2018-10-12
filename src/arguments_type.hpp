/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_ARGUMENTS_HPP
#define SCOPES_ARGUMENTS_HPP

#include "type.hpp"
#include "symbol.hpp"

namespace scopes {

//------------------------------------------------------------------------------
// ARGUMENTS TYPE
//------------------------------------------------------------------------------

struct TupleType;

struct ArgumentsType : Type {
    static bool classof(const Type *T);

    void stream_name(StyledStream &ss) const;
    ArgumentsType(const ArgTypes &_values);
    const TupleType *to_tuple_type() const;

    ArgTypes values;
};

const Type *arguments_type(const ArgTypes &values);
const Type *empty_arguments_type();

bool is_arguments_type(const Type *T);

} // namespace scopes

#endif // SCOPES_ARGUMENTS_HPP