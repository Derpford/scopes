/*
Scopes Compiler
Copyright (c) 2016, 2017, 2018 Leonard Ritter

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef SCOPES_UNION_HPP
#define SCOPES_UNION_HPP

#include "sized_storage.hpp"
#include "argument.hpp"

namespace scopes {

//------------------------------------------------------------------------------
// UNION TYPE
//------------------------------------------------------------------------------

struct UnionType : StorageType {
    static bool classof(const Type *T);

    UnionType(const Args &_values);

    Any unpack(void *src, size_t i) const;

    const Type *type_at_index(size_t i) const;

    size_t field_index(Symbol name) const;

    Symbol field_name(size_t i) const;

    Args values;
    ArgTypes types;
    size_t largest_field;
    const Type *tuple_type;
};

const Type *MixedUnion(const Args &values);

const Type *Union(const ArgTypes &types);

} // namespace scopes

#endif // SCOPES_UNION_HPP