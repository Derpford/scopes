
/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#include "ast.hpp"
#include "error.hpp"
#include "scope.hpp"
#include "types.hpp"
#include "dyn_cast.inc"

#include <assert.h>

namespace scopes {

//------------------------------------------------------------------------------

Keyed::Keyed(const Anchor *anchor, Symbol _key, ASTNode *node)
    : ASTNode(ASTK_Keyed, anchor), key(_key), value(node)
{}

Keyed *Keyed::from(const Anchor *anchor, Symbol key, ASTNode *node) {
    return new Keyed(anchor, key, node);
}

//------------------------------------------------------------------------------

ASTArgumentList::ASTArgumentList(const Anchor *anchor, const ASTNodes &_values)
    : ASTValue(ASTK_ArgumentList, anchor), values(_values) {
}

void ASTArgumentList::append(ASTNode *node) {
    values.push_back(node);
}

void ASTArgumentList::append(Symbol key, ASTNode *node) {
    assert(false); // todo: store key
    values.push_back(node);
}

void ASTArgumentList::flatten() {
    if (values.empty())
        return;
    int i = (int)values.size() - 1;
    while (i > 0) {
        i--;
        auto nl = dyn_cast<ASTArgumentList>(values[i]);
        if (nl) {
            // nodelist not in last place
            if (nl->values.empty()) {
                // node list empty: replace with none
                values[i] = Const::from(nl->anchor(), none);
            } else {
                // take only the first value
                values[i] = nl->values[0];
            }
        }
    }
    // nodelist in last place: expand to full argument list
    auto nl = dyn_cast<ASTArgumentList>(values.back());
    if (nl) {
        values.pop_back();
        for (auto &&arg : nl->values) {
            values.push_back(arg);
        }
    }
}

ASTArgumentList *ASTArgumentList::from(const Anchor *anchor, const ASTNodes &values) {
    return new ASTArgumentList(anchor, values);
}

//------------------------------------------------------------------------------

Template::Template(const Anchor *anchor, Symbol _name, const ASTSymbols &_params, Block *_body)
    : ASTValue(ASTK_Template, anchor),
        name(_name), params(_params), body(_body),
        _inline(false), docstring(nullptr), scope(nullptr) {
}

bool Template::is_forward_decl() const {
    return !body;
}

void Template::set_inline() {
    _inline = true;
}

bool Template::is_inline() const {
    return _inline;
}

void Template::append_param(ASTSymbol *sym) {
    params.push_back(sym);
}

Block *Template::ensure_body() {
    if (!body) {
        body = Block::from(anchor());
    }
    return body;
}

Template *Template::from(
    const Anchor *anchor, Symbol name,
    const ASTSymbols &params, Block *block) {
    return new Template(anchor, name, params, block);
}

//------------------------------------------------------------------------------

ASTFunction::ASTFunction(const Anchor *anchor, Symbol _name, const ASTSymbols &_params, Block *_body)
    : ASTValue(ASTK_Function, anchor),
        name(_name), params(_params), body(_body),
        docstring(nullptr) {
    assert(body);
}

Template *ASTFunction::from(
    const Anchor *anchor, Symbol name,
    const ASTSymbols &params, Block *block) {
    return new Template(anchor, name, params, block);
}

//------------------------------------------------------------------------------

Block::Block(const Anchor *anchor, const ASTNodes &_body)
    : ASTNode(ASTK_Block, anchor), body(_body) {
}

Block *Block::from(const Anchor *anchor, const ASTNodes &nodes) {
    return new Block(anchor, nodes);
}

void Block::strip_constants() {
    int i = (int)body.size() - 1;
    while (i > 0) {
        i--;
        auto arg = body[i];
        if (isa<ASTValue>(arg)) {
            body.erase(body.begin() + i);
        }
    }
}

void Block::flatten() {
    // if the last entry is a block, merge its contents
    if (body.empty())
        return;
    auto block = dyn_cast<Block>(body.back());
    if (!block)
        return;
    body.pop_back();
    for (int i = 0; i < block->body.size(); ++i) {
        body.push_back(block->body[i]);
    }
}

void Block::append(ASTNode *node) {
    if (node->is_empty())
        return;
    body.push_back(node);
}

//------------------------------------------------------------------------------

If::If(const Anchor *anchor, const Clauses &_clauses)
    : ASTNode(ASTK_If, anchor), clauses(_clauses) {
}

If *If::from(const Anchor *anchor, const Clauses &_clauses) {
    return new If(anchor, _clauses);
}

ASTNode *If::get_else_clause() const {
    if (!clauses.empty()) {
        const Clause &last = clauses.back();
        if (!last.cond)
            return last.body;
    }
    return nullptr;
}

void If::append(ASTNode *cond, Block *expr) {
    assert(!get_else_clause());
    clauses.push_back({cond, expr});
}

void If::append(Block *expr) {
    assert(!get_else_clause());
    clauses.push_back({nullptr, expr});
}

//------------------------------------------------------------------------------

ASTValue::ASTValue(ASTKind _kind, const Anchor *anchor)
    : ASTNode(_kind, anchor) {
}

bool ASTValue::classof(const ASTNode *T) {
    switch(T->kind()) {
    case ASTK_Template:
    case ASTK_Const:
    case ASTK_Symbol:
        return true;
    default: return false;
    }
}

//------------------------------------------------------------------------------

ASTSymbol::ASTSymbol(const Anchor *anchor, Symbol _name, const Type *_type, bool _variadic)
    : ASTValue(ASTK_Symbol, anchor), name(_name), type(_type), variadic(_variadic) {
    if (!type)
        type = TYPE_Unknown;
}

ASTSymbol *ASTSymbol::from(const Anchor *anchor, Symbol name, const Type *type) {
    return new ASTSymbol(anchor, name, type, false);
}

ASTSymbol *ASTSymbol::variadic_from(const Anchor *anchor, Symbol name, const Type *type) {
    return new ASTSymbol(anchor, name, type, true);
}

bool ASTSymbol::is_variadic() const {
    return variadic;
}

//------------------------------------------------------------------------------

Call::Call(const Anchor *anchor, ASTNode *_callee, ASTArgumentList *_args)
    : ASTNode(ASTK_Call, anchor), callee(_callee), args(_args), flags(0) {
}

bool Call::is_rawcall() const {
    return flags & CF_RawCall;
}

void Call::set_rawcall() {
    flags |= CF_RawCall;
}

bool Call::is_trycall() const {
    return flags & CF_TryCall;
}

void Call::set_trycall() {
    flags |= CF_TryCall;
}

ASTArgumentList *Call::ensure_args() {
    if (!args) {
        args = ASTArgumentList::from(anchor());
    }
    return args;
}

Call *Call::from(const Anchor *anchor, ASTNode *callee, ASTArgumentList *_args) {
    return new Call(anchor, callee, _args);
}

//------------------------------------------------------------------------------

#if 0
        if (loop->is_variadic()) {
            // accepts maximum number of arguments
            numparams = (size_t)-1;
        }

        if (numvalues > numparams) {
            set_active_anchor(loop->exprs[numparams]->get_anchor());
            StyledString ss;
            ss.out << "number of arguments exceeds number of defined names ("
                << numvalues << " > " << numparams << ")";
            SCOPES_LOCATION_ERROR(ss.str());
        }
#endif

LetLike::LetLike(ASTKind _kind, const Anchor *anchor, const ASTBindings &_bindings, Block *_body)
    : ASTNode(_kind, anchor), bindings(_bindings), body(_body) {
}

void LetLike::append(const ASTBinding &bind) {
    assert(bind.sym);
    assert(bind.expr);
    bindings.push_back(bind);
}

void LetLike::append(ASTSymbol *sym, ASTNode *expr) {
    assert(sym);
    assert(expr);
    bindings.push_back({sym, expr});
}

bool LetLike::has_variadic_section() const {
    return variadic.expr;
}

bool LetLike::has_assignments() const {
    return has_variadic_section() || !bindings.empty();
}

SCOPES_RESULT(void) LetLike::map(const ASTSymbols &syms, const ASTNodes &nodes) {
    SCOPES_RESULT_TYPE(void);
    int argcount = (int)nodes.size();
    int symcount = (int)syms.size();
    // verify that all but the last syms aren't variadic
    for (int i = 0; (i + 1) < symcount; ++i) {
        ASTSymbol *sym = syms[i];
        if (sym->is_variadic()) {
            set_active_anchor(sym->anchor());
            SCOPES_LOCATION_ERROR(String::from("a variadic symbol can only be in last place"));
        }
    }
    for (int i = 0; i < argcount; ++i) {
        ASTNode *expr = nodes[i];
        if (i >= symcount) {
            set_active_anchor(expr->anchor());
            SCOPES_LOCATION_ERROR(String::from("overhanging expression is not going to be bound to a name"));
        }
        ASTSymbol *sym = syms[i];
        if ((i + 1) == argcount) {
            // last expression
            if (((i + 1) == symcount) && (!sym->is_variadic())) {
                // only one symbol left, but it's not variadic
                bindings.push_back({sym, expr});
            } else {
                // unpacking section
                assert(!has_variadic_section());
                // the remainder goes to the variadic section
                for (int j = i; j < symcount; ++j) {
                    variadic.syms.push_back(syms[j]);
                }
                variadic.expr = expr;
            }
        } else if (sym->is_variadic()) {
            // grouping multiple arguments under one variadic symbol
            // reaching last symbol
            assert(!has_variadic_section());
            auto vals = ASTArgumentList::from(expr->anchor());
            // store remaining arguments in single node
            for (int j = i; j < argcount; ++j) {
                vals->values.push_back(nodes[i]);
            }
            variadic.expr = vals;
            variadic.syms.push_back(sym);
            break;
        } else {
            bindings.push_back({sym, expr});
        }
    }
    return true;
}

Block *LetLike::ensure_body() {
    if (!body) {
        body = Block::from(anchor());
    }
    return body;
}

//------------------------------------------------------------------------------

Let::Let(const Anchor *anchor, const ASTBindings &bindings, Block *body)
    : LetLike(ASTK_Let, anchor, bindings, body) {
}
Let *Let::from(const Anchor *anchor, const ASTBindings &bindings, Block *body) {
    return new Let(anchor, bindings, body);
}

void Let::move_constants_to_scope(Scope *scope) {
    // turn all non-computing assignments into scope bindings
    int i = (int)bindings.size();
    while (i > 0) {
        i--;
        auto &&bind = bindings[i];
        if (isa<ASTValue>(bind.expr)
            && (bind.sym->name != SYM_Unnamed)) {
            assert(!bind.sym->is_variadic());
            scope->bind(bind.sym->name, cast<ASTValue>(bind.expr));
            bindings.erase(bindings.begin() + i);
        }
    }
}

void Let::flatten() {
    /*
    if:
        1. the body has only a single entry, and that entry is a Let
        2. all assigned expressions are constants
    then:
        merge all arguments into this let, and use this lets body
    */
    assert(body);
    if (body->body.size() != 1)
        return;
    auto let = dyn_cast<Let>(body->body[0]);
    if (!let) return;
    if (let->has_variadic_section()) return;
    for (auto &&bind : let->bindings) {
        if (!isa<Const>(bind.expr))
            return;
    }
    for (auto &&bind : let->bindings) {
        bindings.push_back(bind);
    }
    body = let->body;
}

//------------------------------------------------------------------------------

Loop::Loop(const Anchor *anchor, const ASTBindings &bindings, Block *body)
    : LetLike(ASTK_Loop, anchor, bindings, body) {
}

Loop *Loop::from(const Anchor *anchor, const ASTBindings &bindings, Block *body) {
    return new Loop(anchor, bindings, body);
}

//------------------------------------------------------------------------------

Const::Const(const Anchor *anchor, Any _value) :
    ASTValue(ASTK_Const, anchor), value(_value) {
}

Const *Const::from(const Anchor *anchor, Any value) {
    return new Const(anchor, value);
}

//------------------------------------------------------------------------------

Break::Break(const Anchor *anchor, ASTNode *_value)
    : ASTNode(ASTK_Break, anchor), value(_value) {
}

Break *Break::from(const Anchor *anchor, ASTNode *value) {
    return new Break(anchor, value);
}

//------------------------------------------------------------------------------

Repeat::Repeat(const Anchor *anchor, ASTArgumentList *_args)
    : ASTNode(ASTK_Repeat, anchor), args(_args) {}

ASTArgumentList *Repeat::ensure_args() {
    if (!args) {
        args = ASTArgumentList::from(anchor());
    }
    return args;
}

Repeat *Repeat::from(const Anchor *anchor, ASTArgumentList *args) {
    return new Repeat(anchor, args);
}

//------------------------------------------------------------------------------

ASTReturn::ASTReturn(const Anchor *anchor, ASTNode *_value)
    : ASTNode(ASTK_Return, anchor), value(_value) {}

ASTReturn *ASTReturn::from(const Anchor *anchor, ASTNode *value) {
    return new ASTReturn(anchor, value);
}

//------------------------------------------------------------------------------

SyntaxExtend::SyntaxExtend(const Anchor *anchor, Template *_func, const List *_next, Scope *_env)
    : ASTNode(ASTK_SyntaxExtend, anchor), func(_func), next(_next), env(_env) {
}

SyntaxExtend *SyntaxExtend::from(const Anchor *anchor, Template *func, const List *next, Scope *env) {
    return new SyntaxExtend(anchor, func, next, env);
}

//------------------------------------------------------------------------------

ASTKind ASTNode::kind() const { return _kind; }

ASTNode::ASTNode(ASTKind kind, const Anchor *anchor)
    : _kind(kind),_anchor(anchor) {
    assert(_anchor);
}

bool ASTNode::is_typed() const {
    return _type != nullptr;
}
void ASTNode::set_type(const ReturnType *type) {
    assert(!is_typed());
    _type = type;
}
const ReturnType *ASTNode::get_type() const {
    assert(_type);
    return _type;
}

bool ASTNode::is_empty() const {
    const Block *block = dyn_cast<Block>(this);
    if (!block) return false;
    return block->body.empty();
}

const Anchor *ASTNode::anchor() const {
    return _anchor;
}

#define T(NAME, BNAME, CLASS) \
    bool CLASS::classof(const ASTNode *T) { \
        return T->kind() == NAME; \
    }
SCOPES_AST_KIND()
#undef T

//------------------------------------------------------------------------------


} // namespace scopes
