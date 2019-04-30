/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_GEN_JIT_HPP
#define SCOPES_GEN_JIT_HPP

#include "result.hpp"
#include "valueref.inc"

#include <stdint.h>

namespace scopes {

SCOPES_RESULT(ConstPointerRef) compile_jit(const FunctionRef &fn, uint64_t flags);

} // namespace scopes

#endif // SCOPES_GEN_JIT_HPP