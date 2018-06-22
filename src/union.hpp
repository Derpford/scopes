/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_UNION_HPP
#define SCOPES_UNION_HPP

#include "sized_storage.hpp"
#include "argument.hpp"
#include "result.hpp"

namespace scopes {

//------------------------------------------------------------------------------
// UNION TYPE
//------------------------------------------------------------------------------

struct UnionType : StorageType {
    static bool classof(const Type *T);

    void stream_name(StyledStream &ss) const;
    UnionType(const KeyedTypes &_values);

    SCOPES_RESULT(Any) unpack(void *src, size_t i) const;

    SCOPES_RESULT(const Type *) type_at_index(size_t i) const;

    size_t field_index(Symbol name) const;

    SCOPES_RESULT(Symbol) field_name(size_t i) const;

    KeyedTypes values;
    size_t largest_field;
    const Type *tuple_type;
};

SCOPES_RESULT(const Type *) KeyedUnion(const KeyedTypes &values);

SCOPES_RESULT(const Type *) Union(const ArgTypes &types);

} // namespace scopes

#endif // SCOPES_UNION_HPP