/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_AST_PROVER_HPP
#define SCOPES_AST_PROVER_HPP

#include "result.hpp"
#include "type.hpp"
#include "value.hpp"

#include <stdint.h>

namespace scopes {

struct Type;
struct Closure;
struct List;
struct Builtin;
struct Symbol;

enum EvalTarget {
    EvalTarget_Void,
    EvalTarget_Symbol,
    EvalTarget_Return,
};

struct ASTContext {
    ASTContext with_return_target() const;
    ASTContext with_void_target() const;
    ASTContext with_symbol_target() const;

    ASTContext with_target(EvalTarget target) const;

    ASTContext for_loop(Loop *loop) const;

    ASTContext for_try(Try *_try) const;

    ASTContext with_block(Block &_block) const;

    ASTContext with_frame(Function *frame) const;

    ASTContext();

    ASTContext(Function *_function, Function *_frame, EvalTarget _target,
        Loop *_loop, Try *xtry, Block *_block);

    Function *function;
    Function *frame;
    EvalTarget target;
    Loop *loop;
    Try *_try;
    Block *block;
};

SCOPES_RESULT(const Type *) extract_type_constant(Value *value);
SCOPES_RESULT(const Closure *) extract_closure_constant(Value *value);
SCOPES_RESULT(const List *) extract_list_constant(Value *value);
SCOPES_RESULT(const String *) extract_string_constant(Value *value);
SCOPES_RESULT(Builtin) extract_builtin_constant(Value *value);
SCOPES_RESULT(Symbol) extract_symbol_constant(Value *value);
SCOPES_RESULT(uint64_t) extract_integer_constant(Value *value);
SCOPES_RESULT(Function *) extract_function_constant(Value *value);
Value *extract_argument(const ASTContext &ctx, Value *value, int index);
Value *build_argument_list(const Anchor *anchor, const Values &values);
const Type *try_get_const_type(Value *node);
const String *try_extract_string(Value *node);
Value *rekey(const Anchor *anchor, Symbol key, Value *value);
SCOPES_RESULT(void) map_keyed_arguments(const Anchor *anchor,
    Values &outargs, const Values &values, const Symbols &symbols, bool varargs);

SCOPES_RESULT(Function *) prove(Function *frame, Template *func, const ArgTypes &types);
SCOPES_RESULT(Value *) prove(const ASTContext &ctx, Value *node);

} // namespace scopes

#endif // SCOPES_AST_PROVER_HPP
