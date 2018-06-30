
/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#include "value.hpp"
#include "error.hpp"
#include "scope.hpp"
#include "types.hpp"
#include "stream_ast.hpp"
#include "dyn_cast.inc"

#include <assert.h>

namespace scopes {

//------------------------------------------------------------------------------

Keyed::Keyed(const Anchor *anchor, Symbol _key, Value *node)
    : Value(VK_Keyed, anchor), key(_key), value(node)
{}

Keyed *Keyed::from(const Anchor *anchor, Symbol key, Value *node) {
    return new Keyed(anchor, key, node);
}

//------------------------------------------------------------------------------

ArgumentList::ArgumentList(const Anchor *anchor, const Values &_values)
    : Value(VK_ArgumentList, anchor), values(_values) {
}

void ArgumentList::append(Value *node) {
    values.push_back(node);
}

void ArgumentList::append(Symbol key, Value *node) {
    assert(false); // todo: store key
    values.push_back(node);
}

ArgumentList *ArgumentList::from(const Anchor *anchor, const Values &values) {
    return new ArgumentList(anchor, values);
}

//------------------------------------------------------------------------------

ExtractArgument::ExtractArgument(const Anchor *anchor, Value *_value, int _index)
    : Value(VK_ExtractArgument, anchor), value(_value), index(_index) {
}

ExtractArgument *ExtractArgument::from(const Anchor *anchor, Value *value, int index) {
    return new ExtractArgument(anchor, value, index);
}

//------------------------------------------------------------------------------

Template::Template(const Anchor *anchor, Symbol _name, const SymbolValues &_params, Value *_value)
    : Value(VK_Template, anchor),
        name(_name), params(_params), value(_value),
        _inline(false), docstring(nullptr), scope(nullptr) {
}

bool Template::is_forward_decl() const {
    return !value;
}

void Template::set_inline() {
    _inline = true;
}

bool Template::is_inline() const {
    return _inline;
}

void Template::append_param(SymbolValue *sym) {
    params.push_back(sym);
}

Template *Template::from(
    const Anchor *anchor, Symbol name,
    const SymbolValues &params, Value *value) {
    return new Template(anchor, name, params, value);
}

//------------------------------------------------------------------------------

Function::Function(const Anchor *anchor, Symbol _name, const SymbolValues &_params, Value *_value)
    : Value(VK_Function, anchor),
        name(_name), params(_params), value(_value),
        docstring(nullptr), return_type(nullptr), frame(nullptr), original(nullptr), complete(false) {
}

void Function::append_param(SymbolValue *sym) {
    // verify that the symbol is typed
    assert(sym->is_typed());
    params.push_back(sym);
}

Value *Function::resolve(Value *node) {
    auto it = map.find(node);
    if (it == map.end())
        return nullptr;
    return it->second;
}

Function *Function::find_frame(Template *scope) {
    Function *frame = this;
    while (frame) {
        if (scope == frame->original)
            return frame;
        frame = frame->frame;
    }
    return nullptr;
}

void Function::bind(Value *oldnode, Value *newnode) {
    map.insert({oldnode, newnode});
}

Function *Function::from(
    const Anchor *anchor, Symbol name,
    const SymbolValues &params, Value *value) {
    return new Function(anchor, name, params, value);
}

//------------------------------------------------------------------------------

Extern::Extern(const Anchor *anchor, const Type *type, Symbol _name, size_t _flags, Symbol _storage_class, int _location, int _binding)
    : Value(VK_Extern, anchor), name(_name), flags(_flags), storage_class(_storage_class), location(_location), binding(_binding) {
    if ((storage_class == SYM_SPIRV_StorageClassUniform)
        && !(flags & EF_BufferBlock)) {
        flags |= EF_Block;
    }
    size_t ptrflags = required_flags_for_storage_class(storage_class);
    if (flags & EF_NonWritable)
        ptrflags |= PTF_NonWritable;
    else if (flags & EF_NonReadable)
        ptrflags |= PTF_NonReadable;
    set_type(pointer_type(type, ptrflags, storage_class));
}

Extern *Extern::from(const Anchor *anchor, const Type *type, Symbol name, size_t flags, Symbol storage_class, int location, int binding) {
    return new Extern(anchor, type, name, flags, storage_class, location, binding);
}

//------------------------------------------------------------------------------

Block::Block(const Anchor *anchor, const Values &_body, Value *_value)
    : Value(VK_Block, anchor), body(_body), value(_value) {
}

Block *Block::from(const Anchor *anchor, const Values &nodes, Value *value) {
    return new Block(anchor, nodes, value);
}

Value *Block::canonicalize() {
    if (!value) {
        if (!body.empty()) {
            value = body.back();
            body.pop_back();
        } else {
            value = ArgumentList::from(anchor());
        }
    }
    strip_constants();
    // can strip block if no side effects
    if (body.empty())
        return value;
    else
        return this;
}

void Block::strip_constants() {
    int i = (int)body.size();
    while (i > 0) {
        i--;
        auto arg = body[i];
        if (arg->is_symbolic()) {
            body.erase(body.begin() + i);
        }
    }
}

void Block::append(Value *node) {
    assert(!value);
    body.push_back(node);
}

//------------------------------------------------------------------------------

If::If(const Anchor *anchor, const Clauses &_clauses)
    : Value(VK_If, anchor), clauses(_clauses) {
}

If *If::from(const Anchor *anchor, const Clauses &_clauses) {
    return new If(anchor, _clauses);
}

void If::append(const Anchor *anchor, Value *cond, Value *value) {
    clauses.push_back({anchor, cond, value});
}

void If::append(const Anchor *anchor, Value *value) {
    assert(!else_clause.value);
    else_clause = Clause(anchor, nullptr, value);
}

Value *If::canonicalize() {
    if (!else_clause.value) {
        else_clause = Clause(anchor(), ArgumentList::from(anchor()));
    }
    return this;
}

//------------------------------------------------------------------------------

SymbolValue::SymbolValue(const Anchor *anchor, Symbol _name, const Type *_type, bool _variadic)
    : Value(VK_Symbol, anchor), name(_name), variadic(_variadic) {
    if (_type) set_type(_type);
}

SymbolValue *SymbolValue::from(const Anchor *anchor, Symbol name, const Type *type) {
    return new SymbolValue(anchor, name, type, false);
}

SymbolValue *SymbolValue::variadic_from(const Anchor *anchor, Symbol name, const Type *type) {
    return new SymbolValue(anchor, name, type, true);
}

bool SymbolValue::is_variadic() const {
    return variadic;
}

//------------------------------------------------------------------------------

Call::Call(const Anchor *anchor, Value *_callee, const Values &_args)
    : Value(VK_Call, anchor), callee(_callee), args(_args), flags(0) {
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

Call *Call::from(const Anchor *anchor, Value *callee, const Values &args) {
    return new Call(anchor, callee, args);
}

//------------------------------------------------------------------------------

Let::Let(const Anchor *anchor, const SymbolValues &_params, const Values &_args)
    : Value(VK_Let, anchor), params(_params), args(_args) {
}
Let *Let::from(const Anchor *anchor, const SymbolValues &params, const Values &args) {
    return new Let(anchor, params, args);
}

//------------------------------------------------------------------------------

Loop::Loop(const Anchor *anchor, const SymbolValues &_params, const Values &_args, Value *_value)
    : Value(VK_Loop, anchor), params(_params), args(_args), value(_value) {
}

Loop *Loop::from(const Anchor *anchor, const SymbolValues &params, const Values &args, Value *value) {
    return new Loop(anchor, params, args, value);
}

//------------------------------------------------------------------------------

bool Const::classof(const Value *T) {
    switch(T->kind()) {
    case VK_ConstInt:
    case VK_ConstReal:
    case VK_ConstTuple:
    case VK_ConstArray:
    case VK_ConstVector:
    case VK_ConstPointer:
        return true;
    default: return false;
    }
}

Const::Const(ValueKind _kind, const Anchor *anchor, const Type *type)
    : Value(_kind, anchor) {
    set_type(type);
}

//------------------------------------------------------------------------------

ConstInt::ConstInt(const Anchor *anchor, const Type *type, uint64_t _value)
    : Const(VK_ConstInt, anchor, type), value(_value) {
}

ConstInt *ConstInt::from(const Anchor *anchor, const Type *type, uint64_t value) {
    return new ConstInt(anchor, type, value);
}

ConstInt *ConstInt::symbol_from(const Anchor *anchor, Symbol value) {
    return new ConstInt(anchor, TYPE_Symbol, value.value());
}

ConstInt *ConstInt::builtin_from(const Anchor *anchor, Builtin value) {
    return new ConstInt(anchor, TYPE_Builtin, value.value());
}

//------------------------------------------------------------------------------

ConstReal::ConstReal(const Anchor *anchor, const Type *type, double _value)
    : Const(VK_ConstReal, anchor, type), value(_value) {}

ConstReal *ConstReal::from(const Anchor *anchor, const Type *type, double value) {
    return new ConstReal(anchor, type, value);
}

//------------------------------------------------------------------------------

ConstTuple::ConstTuple(const Anchor *anchor, const Type *type, const Constants &_fields)
    : Const(VK_ConstTuple, anchor, type), values(_fields) {
}

ConstTuple *ConstTuple::from(const Anchor *anchor, const Type *type, const Constants &fields) {
    return new ConstTuple(anchor, type, fields);
}

ConstTuple *ConstTuple::none_from(const Anchor *anchor) {
    return from(anchor, TYPE_Nothing, {});
}

//------------------------------------------------------------------------------

ConstArray::ConstArray(const Anchor *anchor, const Type *type, const Constants &_fields)
    : Const(VK_ConstTuple, anchor, type), values(_fields) {
}

ConstArray *ConstArray::from(const Anchor *anchor, const Type *type, const Constants &fields) {
    return new ConstArray(anchor, type, fields);
}

//------------------------------------------------------------------------------

ConstVector::ConstVector(const Anchor *anchor, const Type *type, const Constants &_fields)
    : Const(VK_ConstTuple, anchor, type), values(_fields) {
}

ConstVector *ConstVector::from(const Anchor *anchor, const Type *type, const Constants &fields) {
    return new ConstVector(anchor, type, fields);
}

//------------------------------------------------------------------------------

ConstPointer::ConstPointer(const Anchor *anchor, const Type *type, const void *_pointer)
    : Const(VK_ConstPointer, anchor, type), value(_pointer) {}

ConstPointer *ConstPointer::from(const Anchor *anchor, const Type *type, const void *pointer) {
    return new ConstPointer(anchor, type, pointer);
}

ConstPointer *ConstPointer::type_from(const Anchor *anchor, const Type *type) {
    return from(anchor, TYPE_Type, type);
}

ConstPointer *ConstPointer::closure_from(const Anchor *anchor, const Closure *closure) {
    return from(anchor, TYPE_Closure, closure);
}

ConstPointer *ConstPointer::string_from(const Anchor *anchor, const String *str) {
    return from(anchor, TYPE_String, str);
}

ConstPointer *ConstPointer::ast_from(const Anchor *anchor, Value *node) {
    return from(anchor, TYPE_Value, node);
}

ConstPointer *ConstPointer::list_from(const Anchor *anchor, const List *list) {
    return from(anchor, TYPE_List, list);
}

//------------------------------------------------------------------------------

Break::Break(const Anchor *anchor, Value *_value)
    : Value(VK_Break, anchor), value(_value) {
}

Break *Break::from(const Anchor *anchor, Value *value) {
    return new Break(anchor, value);
}

//------------------------------------------------------------------------------

Repeat::Repeat(const Anchor *anchor, const Values &_args)
    : Value(VK_Repeat, anchor), args(_args) {}

Repeat *Repeat::from(const Anchor *anchor, const Values &args) {
    return new Repeat(anchor, args);
}

//------------------------------------------------------------------------------

Return::Return(const Anchor *anchor, Value *_value)
    : Value(VK_Return, anchor), value(_value) {}

Return *Return::from(const Anchor *anchor, Value *value) {
    return new Return(anchor, value);
}

//------------------------------------------------------------------------------

SyntaxExtend::SyntaxExtend(const Anchor *anchor, Template *_func, const List *_next, Scope *_env)
    : Value(VK_SyntaxExtend, anchor), func(_func), next(_next), env(_env) {
}

SyntaxExtend *SyntaxExtend::from(const Anchor *anchor, Template *func, const List *next, Scope *env) {
    return new SyntaxExtend(anchor, func, next, env);
}

//------------------------------------------------------------------------------

ValueKind Value::kind() const { return _kind; }

Value::Value(ValueKind kind, const Anchor *anchor)
    : _kind(kind),_type(nullptr),_anchor(anchor) {
    assert(_anchor);
}

bool Value::is_symbolic() const {
    switch(kind()) {
    case VK_Template:
    case VK_Function:
    case VK_Symbol:
    case VK_Extern:
        return true;
    default: break;
    }
    return isa<Const>(this);
}

bool Value::is_typed() const {
    return _type != nullptr;
}
void Value::set_type(const Type *type) {
    assert(!is_typed());
    _type = type;
}

void Value::change_type(const Type *type) {
    assert(is_typed());
    _type = type;
}

const Type *Value::get_type() const {
    assert(_type);
    return _type;
}

const Anchor *Value::anchor() const {
    return _anchor;
}

#define T(NAME, BNAME, CLASS) \
    bool CLASS::classof(const Value *T) { \
        return T->kind() == NAME; \
    }
SCOPES_VALUE_KIND()
#undef T

//------------------------------------------------------------------------------

StyledStream& operator<<(StyledStream& ost, Value *node) {
    stream_ast(ost, node, StreamASTFormat());
    return ost;
}

StyledStream& operator<<(StyledStream& ost, const Value *node) {
    stream_ast(ost, node, StreamASTFormat());
    return ost;
}

} // namespace scopes
