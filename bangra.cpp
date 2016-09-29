#ifndef BANGRA_CPP
#define BANGRA_CPP

//------------------------------------------------------------------------------
// C HEADER
//------------------------------------------------------------------------------

#if defined __cplusplus
extern "C" {
#endif

enum {
    // semver style versioning
    BANGRA_VERSION_MAJOR = 0,
    BANGRA_VERSION_MINOR = 3,
    BANGRA_VERSION_PATCH = 0,
};

extern int bang_argc;
extern char **bang_argv;
extern char *bang_executable_path;

int bangra_main(int argc, char ** argv);

#if defined __cplusplus
}
#endif

#endif // BANGRA_CPP
#ifdef BANGRA_CPP_IMPL

#define BANGRA_HEADER "bangra"
//#define BANGRA_DEBUG_IL

//------------------------------------------------------------------------------
// SHARED LIBRARY IMPLEMENTATION
//------------------------------------------------------------------------------

#undef NDEBUG
#include <sys/types.h>
#ifdef _WIN32
#include "mman.h"
#include "stdlib_ex.h"
#else
// for backtrace
#include <execinfo.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libgen.h>

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/ErrorHandling.h>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
//#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Casting.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/MultiplexConsumer.h"

namespace bangra {

//------------------------------------------------------------------------------
// UTILITIES
//------------------------------------------------------------------------------

template<typename ... Args>
std::string format( const std::string& format, Args ... args ) {
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... );
    std::string str;
    str.resize(size);
    snprintf( &str[0], size + 1, format.c_str(), args ... );
    return str;
}

template <typename R, typename... Args>
std::function<R (Args...)> memo(R (*fn)(Args...)) {
    std::map<std::tuple<Args...>, R> list;
    return [fn, list](Args... args) mutable -> R {
        auto argt = std::make_tuple(args...);
        auto memoized = list.find(argt);
        if(memoized == list.end()) {
            auto result = fn(args...);
            list[argt] = result;
            return result;
        } else {
            return memoized->second;
        }
    };
}

static char parse_hexchar(char c) {
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    } else if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    } else if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    return -1;
}

static char *extension(char *path) {
    char *dot = path + strlen(path);
    while (dot > path) {
        switch(*dot) {
            case '.': return dot; break;
            case '/': case '\\': {
                return NULL;
            } break;
            default: break;
        }
        dot--;
    }
    return NULL;
}

static size_t inplace_unescape(char *buf) {
    char *dst = buf;
    char *src = buf;
    while (*src) {
        if (*src == '\\') {
            src++;
            if (*src == 0) {
                break;
            } if (*src == 'n') {
                *dst = '\n';
            } else if (*src == 't') {
                *dst = '\t';
            } else if (*src == 'r') {
                *dst = '\r';
            } else if (*src == 'x') {
                char c0 = parse_hexchar(*(src + 1));
                char c1 = parse_hexchar(*(src + 2));
                if ((c0 >= 0) && (c1 >= 0)) {
                    *dst = (c0 << 4) | c1;
                    src += 2;
                } else {
                    src--;
                    *dst = *src;
                }
            } else {
                *dst = *src;
            }
        } else {
            *dst = *src;
        }
        src++;
        dst++;
    }
    // terminate
    *dst = 0;
    return dst - buf;
}

template<typename T>
static void streamString(T &stream, const std::string &a, const char *quote_chars = nullptr) {
    for (size_t i = 0; i < a.size(); i++) {
        char c = a[i];
        switch(c) {
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if ((c < 32) || (c >= 127)) {
                unsigned char uc = c;
                stream << format("\\x%02x", uc);
            } else {
                if ((c == '\\') || (quote_chars && strchr(quote_chars, c)))
                    stream << '\\';
                stream << c;
            }
            break;
        }
    }
}

static std::string quoteString(const std::string &a, const char *quote_chars = nullptr) {
    std::stringstream ss;
    streamString(ss, a, quote_chars);
    return ss.str();
}

#define ANSI_RESET "\033[0m"
#define ANSI_COLOR_BLACK "\033[30m"
#define ANSI_COLOR_RED "\033[31m"
#define ANSI_COLOR_GREEN "\033[32m"
#define ANSI_COLOR_YELLOW "\033[33m"
#define ANSI_COLOR_BLUE "\033[34m"
#define ANSI_COLOR_MAGENTA "\033[35m"
#define ANSI_COLOR_CYAN "\033[36m"
#define ANSI_COLOR_GRAY60 "\033[37m"

#define ANSI_COLOR_GRAY30 "\033[30;1m"
#define ANSI_COLOR_XRED "\033[31;1m"
#define ANSI_COLOR_XGREEN "\033[32;1m"
#define ANSI_COLOR_XYELLOW "\033[33;1m"
#define ANSI_COLOR_XBLUE "\033[34;1m"
#define ANSI_COLOR_XMAGENTA "\033[35;1m"
#define ANSI_COLOR_XCYAN "\033[36;1m"
#define ANSI_COLOR_WHITE "\033[37;1m"

#define ANSI_STYLE_STRING ANSI_COLOR_XMAGENTA
#define ANSI_STYLE_NUMBER ANSI_COLOR_XGREEN
#define ANSI_STYLE_KEYWORD ANSI_COLOR_XBLUE
#define ANSI_STYLE_OPERATOR ANSI_COLOR_XCYAN
#define ANSI_STYLE_INSTRUCTION ANSI_COLOR_YELLOW
#define ANSI_STYLE_TYPE ANSI_COLOR_XYELLOW
#define ANSI_STYLE_COMMENT ANSI_COLOR_GRAY30
#define ANSI_STYLE_ERROR ANSI_COLOR_XRED
#define ANSI_STYLE_LOCATION ANSI_COLOR_XCYAN

static bool support_ansi = false;
static std::string ansi(const std::string &code, const std::string &content) {
    if (support_ansi) {
        return code + content + ANSI_RESET;
    } else {
        return content;
    }
}

std::string quoteReprString(const std::string &value) {
    return ansi(ANSI_STYLE_STRING,
        format("\"%s\"",
            quoteString(value, "\"").c_str()));
}

std::string quoteReprInteger(int64_t value) {
    return ansi(ANSI_STYLE_NUMBER, format("%" PRIi64, value));
}

//------------------------------------------------------------------------------
// FILE I/O
//------------------------------------------------------------------------------

struct MappedFile {
protected:
    int fd;
    off_t length;
    void *ptr;

    MappedFile() :
        fd(-1),
        length(0),
        ptr(NULL)
        {}

public:
    ~MappedFile() {
        if (ptr)
            munmap(ptr, length);
        if (fd >= 0)
            close(fd);
    }

    const char *strptr() const {
        return (const char *)ptr;
    }

    size_t size() const {
        return length;
    }

    static std::unique_ptr<MappedFile> open(const char *path) {
        std::unique_ptr<MappedFile> file(new MappedFile());
        file->fd = ::open(path, O_RDONLY);
        if (file->fd < 0)
            return nullptr;
        file->length = lseek(file->fd, 0, SEEK_END);
        file->ptr = mmap(NULL, file->length, PROT_READ, MAP_PRIVATE, file->fd, 0);
        if (file->ptr == MAP_FAILED) {
            return nullptr;
        }
        return file;
    }

    void dumpLine(size_t offset) {
        if (offset >= (size_t)length) {
            return;
        }
        const char *str = strptr();
        size_t start = offset;
        size_t end = offset;
        while (start > 0) {
            if (str[start-1] == '\n')
                break;
            start--;
        }
        while (end < (size_t)length) {
            if (str[end] == '\n')
                break;
            end++;
        }
        printf("%.*s\n", (int)(end - start), str + start);
        size_t column = offset - start;
        for (size_t i = 0; i < column; ++i) {
            putchar(' ');
        }
        puts(ansi(ANSI_STYLE_OPERATOR, "^").c_str());
    }
};

static void dumpFileLine(const char *path, int offset) {
    auto file = MappedFile::open(path);
    if (file) {
        file->dumpLine((size_t)offset);
    }
}

//------------------------------------------------------------------------------
// ANCHORS
//------------------------------------------------------------------------------

struct Anchor {
    const char *path;
    int lineno;
    int column;
    int offset;

    Anchor() {
        path = NULL;
        lineno = 0;
        column = 0;
        offset = 0;
    }

    bool isValid() const {
        return (path != NULL) && (lineno != 0) && (column != 0);
    }

    bool operator ==(const Anchor &other) const {
        return
            path == other.path
                && lineno == other.lineno
                && column == other.column
                && offset == other.offset;
    }

    void printMessage (const std::string &msg) const {
        printf("%s:%i:%i: %s\n", path, lineno, column, msg.c_str());
        dumpFileLine(path, offset);
    }

    static void printMessageV (const Anchor *anchor, const char *fmt, va_list args) {
        if (anchor) {
            std::cout
                << ansi(ANSI_STYLE_LOCATION, anchor->path)
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->lineno))
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->column))
                << " ";
        }
        vprintf (fmt, args);
        putchar('\n');
        if (anchor) {
            dumpFileLine(anchor->path, anchor->offset);
        }
    }

    static void printErrorV (const Anchor *anchor, const char *fmt, va_list args) {
        if (anchor) {
            std::cout
                << ansi(ANSI_STYLE_LOCATION, anchor->path)
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->lineno))
                << ansi(ANSI_STYLE_OPERATOR, ":")
                << ansi(ANSI_STYLE_NUMBER, format("%i", anchor->column))
                << " "
                << ansi(ANSI_STYLE_ERROR, "error:")
                << " ";
        } else {
            std::cout << ansi(ANSI_STYLE_ERROR, "error:") << " ";
        }
        vprintf (fmt, args);
        putchar('\n');
        if (anchor) {
            dumpFileLine(anchor->path, anchor->offset);
        }
        exit(1);
    }

};

//------------------------------------------------------------------------------
// TYPE SYSTEM
//------------------------------------------------------------------------------

#define TYPE_ENUM_KINDS() \
    TYPE_KIND(Any) \
    TYPE_KIND(Void) \
    TYPE_KIND(Null) \
    TYPE_KIND(Integer) \
    TYPE_KIND(Real) \
    TYPE_KIND(Pointer) \
    TYPE_KIND(Array) \
    TYPE_KIND(Vector) \
    TYPE_KIND(Tuple) \
    TYPE_KIND(Struct) \
    TYPE_KIND(Enum) \
    TYPE_KIND(CFunction) \
    TYPE_KIND(Continuation)

enum TypeKind {
#define TYPE_KIND(NAME) T_ ## NAME,
    TYPE_ENUM_KINDS()
#undef TYPE_KIND
};

//------------------------------------------------------------------------------

struct Type;
typedef std::vector<Type *> TypeArray;
typedef std::vector<std::string> NameArray;

struct Type {
private:
    const TypeKind kind;

    static Type *newIntegerType(int _width, bool _signed);
    static Type *newRealType(int _width);
    static Type *newPointerType(Type *_element);
    static Type *newArrayType(Type *_element, unsigned _size);
    static Type *newVectorType(Type *_element, unsigned _size);
    static Type *newTupleType(TypeArray _elements);
    static Type *newCFunctionType(
        Type *_returntype, TypeArray _parameters, bool vararg);
    static Type *newContinuationType(TypeArray _parameters);

protected:
    Type(TypeKind kind_) :
        kind(kind_)
        {}

public:
    static Type *TypePointer;
    static Type *ValuePointer;
    static Type *Void;
    static Type *Null;
    static Type *Bool;
    static Type *Empty;
    static Type *Any;

    static Type *Opaque;
    static Type *OpaquePointer;

    static Type *Int8;
    static Type *Int16;
    static Type *Int32;
    static Type *Int64;

    static Type *UInt8;
    static Type *UInt16;
    static Type *UInt32;
    static Type *UInt64;

    static Type *Half;
    static Type *Float;
    static Type *Double;

    static Type *Rawstring;

    TypeKind getKind() const {
        return kind;
    }

    static void initTypes();

    static std::function<Type * (int, bool)> Integer;
    static std::function<Type * (int)> Real;
    static std::function<Type * (Type *)> Pointer;
    static std::function<Type * (Type *, unsigned)> Array;
    static std::function<Type * (Type *, unsigned)> Vector;
    static std::function<Type * (TypeArray)> Tuple;
    static Type *Struct(
        const std::string &name, bool builtin = false, bool union_ = false);
    static Type *Enum(const std::string &name);
    static std::function<Type * (Type *, TypeArray, bool)> CFunction;
    static std::function<Type * (TypeArray)> Continuation;
};

static std::string getRepr(Type *type);

Type *Type::TypePointer;
Type *Type::ValuePointer;
Type *Type::Void;
Type *Type::Null;
Type *Type::Bool;
Type *Type::Int8;
Type *Type::Int16;
Type *Type::Int32;
Type *Type::Int64;
Type *Type::UInt8;
Type *Type::UInt16;
Type *Type::UInt32;
Type *Type::UInt64;
Type *Type::Half;
Type *Type::Float;
Type *Type::Double;
Type *Type::Rawstring;
Type *Type::Empty;
Type *Type::Any;
Type *Type::Opaque;
Type *Type::OpaquePointer;
std::function<Type * (int, bool)> Type::Integer = memo(Type::newIntegerType);
std::function<Type * (int)> Type::Real = memo(Type::newRealType);
std::function<Type * (Type *)> Type::Pointer = memo(Type::newPointerType);
std::function<Type * (Type *, unsigned)> Type::Array = memo(Type::newArrayType);
std::function<Type * (Type *, unsigned)> Type::Vector = memo(Type::newVectorType);
std::function<Type * (TypeArray)> Type::Tuple = memo(Type::newTupleType);
std::function<Type * (Type *, TypeArray, bool)> Type::CFunction = memo(Type::newCFunctionType);
std::function<Type * (TypeArray)> Type::Continuation = memo(Type::newContinuationType);

//------------------------------------------------------------------------------

template<class T, TypeKind KindT>
struct TypeImpl : Type {
    TypeImpl() :
        Type(KindT)
        {}

    static bool classof(const Type *node) {
        return node->getKind() == KindT;
    }
};

//------------------------------------------------------------------------------

struct VoidType : TypeImpl<VoidType, T_Void> {
    std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, "void");
    }
};

//------------------------------------------------------------------------------

struct NullType : TypeImpl<NullType, T_Null> {
    std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, "null");
    }
};

//------------------------------------------------------------------------------

struct AnyType : TypeImpl<AnyType, T_Any> {
    std::string getRepr() {
        return ansi(ANSI_STYLE_ERROR, "any");
    }
};

//------------------------------------------------------------------------------

struct IntegerType : TypeImpl<IntegerType, T_Integer> {
protected:
    int width;
    bool is_signed;

public:
    int getWidth() const { return width; }
    bool isSigned() const { return is_signed; }

    IntegerType(int _width, bool _signed) :
        width(_width),
        is_signed(_signed)
        {}

    std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, format("%sint%i", is_signed?"":"u", width));
    }

};

Type *Type::newIntegerType(int _width, bool _signed) {
    return new IntegerType(_width, _signed);
}

//------------------------------------------------------------------------------

struct RealType : TypeImpl<RealType, T_Real> {
protected:
    int width;

public:
    int getWidth() const { return width; }

    RealType(int _width) :
        width(_width)
        {}

    std::string getRepr() {
        return ansi(ANSI_STYLE_TYPE, format("real%i", width));
    }
};

Type *Type::newRealType(int _width) {
    return new RealType(_width);
}

//------------------------------------------------------------------------------

struct PointerType : TypeImpl<PointerType, T_Pointer> {
protected:
    Type *element;

public:
    PointerType(Type *_element) :
        element(_element)
        {}

    Type *getElement() {
        return element;
    }

    std::string getRepr() {
        return format("(%s %s)",
                ansi(ANSI_STYLE_KEYWORD, "&").c_str(),
                bangra::getRepr(element).c_str());
    }

};

Type *Type::newPointerType(Type *_element) {
    return new PointerType(_element);
}

//------------------------------------------------------------------------------

struct ArrayType : TypeImpl<ArrayType, T_Array> {
protected:
    Type *element;
    unsigned size;

public:
    Type *getElement() {
        return element;
    }

    unsigned getSize() {
        return size;
    }

    ArrayType(Type *_element, unsigned _size) :
        element(_element),
        size(_size)
        {}

    std::string getRepr() {
        return format("(%s %s %i)",
                ansi(ANSI_STYLE_KEYWORD, "array").c_str(),
                bangra::getRepr(element).c_str(),
                size);
    }

};

Type *Type::newArrayType(Type *_element, unsigned _size) {
    return new ArrayType(_element, _size);
}

//------------------------------------------------------------------------------

struct VectorType : TypeImpl<VectorType, T_Vector> {
protected:
    Type *element;
    unsigned size;

public:
    Type *getElement() {
        return element;
    }

    unsigned getSize() {
        return size;
    }

    VectorType(Type *_element, unsigned _size) :
        element(_element),
        size(_size)
        {}

    std::string getRepr() {
        return format("(%s %s %i)",
                ansi(ANSI_STYLE_KEYWORD, "vector").c_str(),
                bangra::getRepr(element).c_str(),
                size);
    }

};

Type *Type::newVectorType(Type *_element, unsigned _size) {
    return new VectorType(_element, _size);
}

//------------------------------------------------------------------------------

struct TupleType : TypeImpl<TupleType, T_Tuple> {
protected:
    TypeArray elements;

public:
    static std::string getSpecRepr(const TypeArray &elements) {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "tuple");
        for (size_t i = 0; i < elements.size(); ++i) {
            ss << " ";
            ss << bangra::getRepr(elements[i]);
        }
        ss << ")";
        return ss.str();
    }

    TupleType(const TypeArray &_elements) :
        elements(_elements)
        {}

    std::string getRepr() {
        return getSpecRepr(elements);
    }

};

Type *Type::newTupleType(TypeArray _elements) {
    return new TupleType(_elements);
}

//------------------------------------------------------------------------------

struct StructType : TypeImpl<StructType, T_Struct> {
public:
    struct Field {
    protected:
        std::string name;
        Type *type;
        Anchor anchor;

    public:
        Field() :
            type(nullptr) {}

        Field(const std::string &name_, Type *type_) :
            name(name_),
            type(type_)
            {}

        Field(const std::string &name_, Type *type_, const Anchor &anchor_) :
            name(name_),
            type(type_),
            anchor(anchor_)
            {}

        Type *getType() const {
            return type;
        }

        std::string getName() const {
            return name;
        }

        bool isUnnamed() const {
            return name.size() == 0;
        }

        std::string getRepr() const {
            return format("(%s %s)",
                    quoteReprString(name).c_str(),
                    bangra::getRepr(type).c_str());
        }
    };

protected:
    std::string name;
    std::vector<Field> fields;
    std::unordered_map<std::string, size_t> byname;
    bool builtin;
    bool isunion;
    Anchor anchor;

public:

    StructType(const std::string &name_, bool builtin_, bool union_) :
        name(name_),
        builtin(builtin_),
        isunion(union_)
        {}

    const Anchor &getAnchor() {
        return anchor;
    }

    void setAnchor(const Anchor &anchor_) {
        anchor = anchor_;
    }

    void addField(const Field &field) {
        if (!field.isUnnamed()) {
            byname[field.getName()] = fields.size();
        }
        fields.push_back(field);
    }

    void addFields(const std::vector<Field> &fields_) {
        for (auto &field : fields_) {
            addField(field);
        }
    }

    size_t getFieldIndex(const std::string &name) {
        auto it = byname.find(name);
        if (it != byname.end()) {
            return it->second;
        } else {
            return (size_t)-1;
        }
    }

    size_t getFieldCount() {
        return fields.size();
    }

    const Field &getField(size_t i) {
        assert(i < fields.size());
        return fields[i];
    }

    std::string getName() {
        return name;
    }

    bool isUnion() {
        return isunion;
    }

    bool isBuiltin() {
        return builtin;
    }

    bool isUnnamed() {
        return name.size() == 0;
    }

    std::string getNameRepr() {
        std::stringstream ss;
        if (isUnnamed()) {
            ss << this;
        } else {
            ss << quoteReprString(name);
        }
        return ss.str();
    }

    std::string getFullRepr() {
        if (builtin) {
            return ansi(ANSI_STYLE_TYPE, name).c_str();
        } else {
            std::stringstream ss;
            ss << "(" << ansi(ANSI_STYLE_KEYWORD,
                isUnion()?"union":"struct") << " ";
            ss << getNameRepr();
            for (size_t i = 0; i < fields.size(); ++i) {
                ss << " " << fields[i].getRepr();
            }
            ss << ")";
            return ss.str();
        }
    }

    std::string getRepr() {
        if (builtin) {
            return ansi(ANSI_STYLE_TYPE, name).c_str();
        } else {
            std::stringstream ss;
            ss << "(" << ansi(ANSI_STYLE_KEYWORD,
                isUnion()?"union":"struct") << " ";
            ss << getNameRepr();
            ss << ")";
            return ss.str();
        }
    }

};

Type *Type::Struct(const std::string &name, bool builtin, bool union_) {
    return new StructType(name, builtin, union_);
}

//------------------------------------------------------------------------------

struct EnumType : TypeImpl<EnumType, T_Enum> {
public:
    struct Field {
    protected:
        std::string name;
        int64_t value;
        Anchor anchor;

    public:
        Field() {}

        Field(const std::string &name_, int64_t value_) :
            name(name_),
            value(value_)
            { assert(name.size() != 0); }

        Field(const std::string &name_, int64_t value_, const Anchor &anchor_) :
            name(name_),
            value(value_),
            anchor(anchor_)
            {}

        std::string getName() const {
            return name;
        }

        std::string getRepr() const {
            return format("(%s %s)",
                    quoteReprString(name).c_str(),
                    quoteReprInteger(value).c_str());
        }
    };

protected:
    std::string name;
    std::vector<Field> fields;
    std::unordered_map<std::string, size_t> byname;
    IntegerType *enum_type;
    Anchor anchor;

public:

    EnumType(const std::string &name_) :
        name(name_),
        enum_type(nullptr)
        {}

    void setType(Type *type) {
        enum_type = llvm::cast<IntegerType>(type);
    }

    Type *getType() {
        return enum_type;
    }

    const Anchor &getAnchor() {
        return anchor;
    }

    void setAnchor(const Anchor &anchor_) {
        anchor = anchor_;
    }

    void addField(const Field &field) {
        byname[field.getName()] = fields.size();
        fields.push_back(field);
    }

    void addFields(const std::vector<Field> &fields_) {
        for (auto &field : fields_) {
            addField(field);
        }
    }

    size_t getFieldIndex(const std::string &name) {
        auto it = byname.find(name);
        if (it != byname.end()) {
            return it->second;
        } else {
            return (size_t)-1;
        }
    }

    size_t getFieldCount() {
        return fields.size();
    }

    const Field &getField(size_t i) {
        assert(i < fields.size());
        return fields[i];
    }

    std::string getName() {
        return name;
    }

    bool isUnnamed() {
        return name.size() == 0;
    }

    std::string getNameRepr() {
        std::stringstream ss;
        if (isUnnamed()) {
            ss << this;
        } else {
            ss << quoteReprString(name);
        }
        return ss.str();
    }

    std::string getRepr() {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "enum") << " ";
        ss << getNameRepr();
        ss << " " << ansi(ANSI_STYLE_OPERATOR, ":") << " ";
        if (enum_type) {
            ss << enum_type->getRepr();
        } else {
            ss << bangra::getRepr(Type::Any);
        }
        for (size_t i = 0; i < fields.size(); ++i) {
            ss << " " << fields[i].getRepr();
        }
        ss << ")";
        return ss.str();
    }

};

Type *Type::Enum(const std::string &name) {
    return new EnumType(name);
}

//------------------------------------------------------------------------------

struct ContinuationType : TypeImpl<ContinuationType, T_Continuation> {
protected:
    TypeArray parameters;

public:
    static std::string getSpecRepr(const TypeArray &parameters) {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "fn") << " ";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0)
                ss << " ";
            ss << bangra::getRepr(parameters[i]);
        }
        ss << ")";
        return ss.str();
    }

    size_t getParameterCount() const {
        return parameters.size();
    }

    Type *getParameter(int index) const {
        if (index >= (int)parameters.size())
            return NULL;
        else
            return parameters[index];
    }

    ContinuationType(const TypeArray &_parameters) :
        parameters(_parameters)
        {}

    std::string getRepr() {
        return getSpecRepr(parameters);
    }

};

Type *Type::newContinuationType(TypeArray _parameters) {
    return new ContinuationType(_parameters);
}

//------------------------------------------------------------------------------

struct CFunctionType : TypeImpl<CFunctionType, T_CFunction> {
protected:
    Type *result;
    TypeArray parameters;
    bool isvararg;

public:
    static std::string getSpecRepr(Type *result,
        const TypeArray &parameters, bool isvararg) {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "cdecl") << " ";
        ss << bangra::getRepr(result);
        ss << " (";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0)
                ss << " ";
            ss << bangra::getRepr(parameters[i]);
        }
        if (isvararg) {
            if (parameters.size())
                ss << " ";
            ss << ansi(ANSI_STYLE_KEYWORD, "...");
        }
        ss << "))";
        return ss.str();
    }

    size_t getParameterCount() const {
        return parameters.size();
    }

    Type *getParameter(int index) const {
        if (index >= (int)parameters.size())
            return NULL;
        else
            return parameters[index];
    }

    Type *getResult() const {
        return result;
    }

    bool isVarArg() const { return isvararg; }

    CFunctionType(Type *result_, const TypeArray &_parameters, bool _isvararg) :
        result(result_),
        parameters(_parameters),
        isvararg(_isvararg)
        {}

    std::string getRepr() {
        return getSpecRepr(result, parameters, isvararg);
    }

};

Type *Type::newCFunctionType(Type *_returntype, TypeArray _parameters, bool _isvararg) {
    return new CFunctionType(_returntype, _parameters, _isvararg);
}

//------------------------------------------------------------------------------

std::string getRepr(Type *type) {
#define TYPE_KIND(NAME) \
    case T_ ## NAME: {\
        auto spec = llvm::cast<NAME ## Type>(type); \
        return spec->getRepr(); \
    } break;
    switch(type->getKind()) {
    TYPE_ENUM_KINDS()
    }
#undef TYPE_KIND
}

//------------------------------------------------------------------------------

void Type::initTypes() {
    TypePointer = Pointer(Struct("Type", true));
    ValuePointer = Pointer(Struct("SymEx", true));
    //BasicBlock = Struct("BasicBlock", true);

    Empty = Tuple({});

    Any = new AnyType();
    Void = new VoidType();
    Null = new NullType();

    Opaque = Struct("opaque", true);
    OpaquePointer = Pointer(Opaque);

    Bool = Integer(1, false);

    Int8 = Integer(8, true);
    Int16 = Integer(16, true);
    Int32 = Integer(32, true);
    Int64 = Integer(64, true);

    UInt8 = Integer(8, false);
    UInt16 = Integer(16, false);
    UInt32 = Integer(32, false);
    UInt64 = Integer(64, false);

    Half = Real(16);
    Float = Real(32);
    Double = Real(64);

    Rawstring = Pointer(Int8);

}

//------------------------------------------------------------------------------
// MID-LEVEL IL
//------------------------------------------------------------------------------

// CFF form implemented after
// Leissa et al., Graph-Based Higher-Order Intermediate Representation
// http://compilers.cs.uni-saarland.de/papers/lkh15_cgo.pdf

// some parts of the paper use hindley-milner notation
// https://en.wikipedia.org/wiki/Hindley%E2%80%93Milner_type_system

// more reading material:
// Simple and Effective Type Check Removal through Lazy Basic Block Versioning
// https://arxiv.org/pdf/1411.0352v2.pdf
// Julia: A Fast Dynamic Language for Technical Computing
// http://arxiv.org/pdf/1209.5145v1.pdf

struct Value;
struct FlowValue;
struct ParameterValue;
struct PrimitiveValue;
struct ILBuilder;
struct IntrinsicValue;
struct ConstantValue;

//------------------------------------------------------------------------------

#define ILVALUE_ENUM_KINDS() \
    ILVALUE_KIND(Intrinsic) \
    ILVALUE_KIND_ABSTRACT(Primitive) \
        ILVALUE_KIND_ABSTRACT(Constant) \
            ILVALUE_KIND(String) \
            ILVALUE_KIND(Symbol) \
            ILVALUE_KIND(Integer) \
            ILVALUE_KIND(Real) \
            ILVALUE_KIND(Pointer) \
            ILVALUE_KIND(Tuple) \
            ILVALUE_KIND(Struct) \
            ILVALUE_KIND(Closure) \
            ILVALUE_KIND(External) \
            ILVALUE_KIND(Builtin) \
            ILVALUE_KIND_EOK(ConstantEnd) \
        ILVALUE_KIND(Parameter) \
        ILVALUE_KIND_EOK(PrimitiveEnd) \
    ILVALUE_KIND(Flow)

//------------------------------------------------------------------------------

struct Value {
#define ILVALUE_KIND(NAME) NAME,
#define ILVALUE_KIND_ABSTRACT(NAME) NAME,
#define ILVALUE_KIND_EOK(NAME) NAME,
    enum Kind {
        ILVALUE_ENUM_KINDS()
    };
#undef ILVALUE_KIND
#undef ILVALUE_KIND_ABSTRACT
#undef ILVALUE_KIND_EOK

public:

    const Kind kind;
    Anchor anchor;

    Value(Kind kind_) :
        kind(kind_)
        {}
};

static std::string getRepr (const Value *value);
static std::string getRefRepr (const Value *value);
static Type *getType(const Value *value);
static const Anchor *find_valid_anchor(const Value *expr);

static void ilMessage (const Value *value, const char *format, ...) {
    const Anchor *anchor = NULL;
    if (value) {
        std::cout << "at\n  " << getRepr(value) << "\n";
        anchor = find_valid_anchor(value);
    }
    va_list args;
    va_start (args, format);
    Anchor::printMessageV(anchor, format, args);
    va_end (args);
}

static void ilError (const Value *value, const char *format, ...) {
    const Anchor *anchor = NULL;
    if (value) {
        std::cout << "at\n  " << getRepr(value) << "\n";
        anchor = find_valid_anchor(value);
    }
    va_list args;
    va_start (args, format);
    Anchor::printErrorV(anchor, format, args);
    va_end (args);
}

//------------------------------------------------------------------------------

template<typename SelfT, Value::Kind KindT, typename BaseT>
struct ValueImpl : BaseT {
    typedef ValueImpl<SelfT, KindT, BaseT> ValueImplType;

    ValueImpl() :
        BaseT(KindT)
    {}

    static Value::Kind classkind() {
        return KindT;
    }

    static bool classof(const Value *value) {
        return value->kind == KindT;
    }
};

//------------------------------------------------------------------------------

struct PrimitiveValue : Value {
    PrimitiveValue(Kind kind_) :
        Value(kind_)
        {}

    static bool classof(const Value *value) {
        return (value->kind >= Primitive) && (value->kind < PrimitiveEnd);
    }
};

//------------------------------------------------------------------------------

struct ConstantValue : PrimitiveValue {
    ConstantValue(Kind kind_) :
        PrimitiveValue(kind_)
        {}

    static bool classof(const Value *value) {
        return (value->kind >= Constant) && (value->kind < ConstantEnd);
    }
};

//------------------------------------------------------------------------------

struct ParameterValue :
    ValueImpl<ParameterValue, Value::Parameter, PrimitiveValue> {
    FlowValue *parent;
    size_t index;
    Type *parameter_type;

    ParameterValue() :
        parent(nullptr),
        index(-1),
        parameter_type(nullptr) {
    }

    FlowValue *getParent() const {
        return parent;
    }

    Type *inferType() const {
        if (parameter_type)
            return parameter_type;
        else
            return Type::Any;
    }

    std::string getRepr() const {
        return format("%s%zu %s %s",
            ansi(ANSI_STYLE_OPERATOR,"@").c_str(),
            index,
            ansi(ANSI_STYLE_OPERATOR,":").c_str(),
            bangra::getRepr(getType(this)).c_str());
    }

    std::string getRefRepr () const;

    static ParameterValue *create(Type *type = nullptr) {
        auto value = new ParameterValue();
        value->index = (size_t)-1;
        value->parameter_type = type;
        return value;
    }
};

//------------------------------------------------------------------------------

struct IntrinsicValue :
    ValueImpl<IntrinsicValue, Value::Intrinsic, Value> {
    static IntrinsicValue *Branch;

    std::string name;
    Type *intrinsic_type;

    Type *inferType() const {
        return intrinsic_type;
    }

    std::string getRepr () const {
        std::stringstream ss;
        ss << ansi(ANSI_STYLE_INSTRUCTION, name);
        ss << " " << ansi(ANSI_STYLE_OPERATOR, ":") << " ";
        ss << bangra::getRepr(getType(this));
        return ss.str();
    }

    std::string getRefRepr () const {
        return ansi(ANSI_STYLE_INSTRUCTION, name);
    }

    static IntrinsicValue *create(
        const std::string &name, Type *type) {
        assert(type);
        auto value = new IntrinsicValue();
        value->name = name;
        value->intrinsic_type = type;
        return value;
    }

    static void initIntrinsics() {
        Branch = create("branch",
            Type::Continuation({ Type::Bool,
                Type::Continuation({}),
                Type::Continuation({}) }));
    }
};

IntrinsicValue *IntrinsicValue::Branch;

//------------------------------------------------------------------------------

struct FlowValue :
    ValueImpl<FlowValue, Value::Flow, Value> {
private:
    static int64_t unique_id_counter;
protected:
    int64_t uid;

public:
    FlowValue() :
        uid(unique_id_counter++) {
    }

    std::string name;
    std::vector<ParameterValue *> parameters;
    std::vector<Value *> values;

    void clear() {
        parameters.clear();
        values.clear();
    }

    size_t getParameterCount() {
        return parameters.size();
    }

    ParameterValue *getParameter(size_t i) {
        return parameters[i];
    }

    size_t getValueCount() {
        return values.size();
    }

    Value *getValue(size_t i) {
        return values[i];
    }

    size_t getArgumentCount() {
        return values.size() - 1;
    }

    Value *getArgument(size_t i) {
        return values[i + 1];
    }

    std::string getRepr () const {
        std::stringstream ss;
        ss << getRefRepr();
        ss << " " << ansi(ANSI_STYLE_OPERATOR, "(");
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0) {
                ss << ansi(ANSI_STYLE_OPERATOR, ", ");
            }
            ss << bangra::getRepr(parameters[i]);
        }
        ss << ansi(ANSI_STYLE_OPERATOR, ")");
        if (values.size()) {
            for (size_t i = 0; i < values.size(); ++i) {
                ss << " ";
                ss << bangra::getRefRepr(values[i]);
            }
        } else {
            ss << ansi(ANSI_STYLE_ERROR, "<missing term>");
        }
        return ss.str();
    }

    std::string getRefRepr () const {
        return format("%s%s%" PRId64,
            ansi(ANSI_STYLE_KEYWORD, "λ").c_str(),
            name.c_str(),
            uid);
    }

    Type *inferType() const {
        std::vector<Type *> params;
        for (size_t i = 0; i < parameters.size(); ++i) {
            params.push_back(getType(parameters[i]));
        }
        return Type::Continuation(params);
    }

    ParameterValue *appendParameter(ParameterValue *param) {
        param->parent = this;
        param->index = parameters.size();
        parameters.push_back(param);
        return param;
    }

    static FlowValue *create(
        size_t paramcount = 0,
        const std::string &name = "") {
        auto value = new FlowValue();
        value->name = name;
        for (size_t i = 0; i < paramcount; ++i) {
            value->appendParameter(ParameterValue::create());
        }
        return value;
    }
};

int64_t FlowValue::unique_id_counter = 1;

std::string ParameterValue::getRefRepr () const {
    auto parent = getParent();
    if (parent) {
        return format("%s%s%zu",
            parent->getRefRepr().c_str(),
            ansi(ANSI_STYLE_OPERATOR, "@").c_str(),
            index);
    } else {
        return ansi(ANSI_STYLE_ERROR, "<unbound>");
    }
}

//------------------------------------------------------------------------------

struct StringValue :
    ValueImpl<StringValue, Value::String, ConstantValue> {
    std::string value;

    static StringValue *create(const std::string &s) {
        auto result = new StringValue();
        result->value = s;
        return result;
    }

    static StringValue *create(const char *s, size_t len) {
        return create(std::string(s, len));
    }

    Type *inferType() const {
        return Type::Array(Type::Int8, value.size() + 1);
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        return quoteReprString(value);
    }
};

//------------------------------------------------------------------------------

struct SymbolValue :
    ValueImpl<SymbolValue, Value::Symbol, ConstantValue> {
    std::string value;

    static SymbolValue *create(const std::string &s) {
        auto result = new SymbolValue();
        result->value = s;
        return result;
    }

    static SymbolValue *create(const char *s, size_t len) {
        return create(std::string(s, len));
    }

    Type *inferType() const {
        return Type::Array(Type::Int8, value.size() + 1);
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        return quoteReprString(value);
    }
};

//------------------------------------------------------------------------------

struct IntegerValue :
    ValueImpl<IntegerValue, Value::Integer, ConstantValue> {
    int64_t value;
    IntegerType *value_type;

    static IntegerValue *create(int64_t value, Type *cdest) {
        assert(cdest);
        auto result = new IntegerValue();
        result->value_type = llvm::cast<IntegerType>(cdest);
        result->value = value;
        return result;
    }

    Type *inferType() const {
        return value_type;
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        if (value_type == Type::Bool) {
            return ansi(ANSI_STYLE_KEYWORD,
                value?"true":"false");
        } else {
            return ansi(ANSI_STYLE_NUMBER,
                format("%" PRIi64, value));
        }
    }
};

//------------------------------------------------------------------------------

struct RealValue :
    ValueImpl<RealValue, Value::Real, ConstantValue> {
    double value;
    RealType *value_type;

    static RealValue *create(double value, Type *cdest) {
        assert(cdest);
        auto result = new RealValue();
        result->value_type = llvm::cast<RealType>(cdest);
        result->value = value;
        return result;
    }

    Type *inferType() const {
        return value_type;
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        return ansi(ANSI_STYLE_NUMBER,
            format("%f", value));
    }
};

//------------------------------------------------------------------------------

struct TupleValue :
    ValueImpl<TupleValue, Value::Tuple, ConstantValue> {
    std::vector<ConstantValue *> values;

    static TupleValue *create(
        const std::vector<ConstantValue *> &values_) {
        auto result = new TupleValue();
        result->values = values_;
        return result;
    }

    Type *inferType() const {
        TypeArray types;
        for (auto &value : values) {
            types.push_back(getType(value));
        }
        return Type::Tuple(types);
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "tupleof");
        for (auto &value : values) {
            ss << " " << bangra::getRefRepr(value);
        }
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct StructValue :
    ValueImpl<StructValue, Value::Struct, ConstantValue> {
    std::vector<ConstantValue *> values;
    StructType *struct_type;

    static StructValue *create(const std::vector<ConstantValue *> &values,
        StructType *struct_type) {
        auto result = new StructValue();
        result->values = values;
        result->struct_type = struct_type;
        assert(result->values.size() == struct_type->getFieldCount());
        return result;
    }

    void addField(ConstantValue *c, const StructType::Field &field) {
        struct_type->addField(field);
        values.push_back(c);
    }

    Type *inferType() const {
        return struct_type;
    }

    std::string getRepr() const {
        std::stringstream ss;
        ss << "(" << struct_type->getRepr();
        ss << " " << this;
        for (size_t i = 0; i < struct_type->getFieldCount(); ++i) {
            auto &field = struct_type->getField(i);
            ss << "\n  " << quoteReprString(field.getName())
                << " " << ansi(ANSI_STYLE_OPERATOR, "=") << " "
                << bangra::getRefRepr(values[i]);
        }
        ss << ")";
        return ss.str();
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << struct_type->getRepr();
        ss << " " << this << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct PointerValue :
    ValueImpl<PointerValue, Value::Pointer, ConstantValue> {
    void *value;
    Type *pointer_type;

    static PointerValue *create(
        void *value, Type *pointer_type) {
        auto result = new PointerValue();
        result->value = value;
        result->pointer_type = pointer_type;
        return result;
    }

    Type *inferType() const {
        return pointer_type;
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        if (pointer_type == Type::TypePointer) {
            ss << bangra::getRepr((Type *)value);
        } else {
            ss << "(" << bangra::getRepr(pointer_type) << " " << value << ")";
        }
        return ss.str();
    }
};

/*
static Type *extract_consttype(const Value *value) {
    if (auto resulttype = llvm::dyn_cast<ILConstTypePointer>(value.get())) {
        return resulttype->value;
    }
    return Type::Any;
}
*/

//------------------------------------------------------------------------------

typedef ConstantValue *(*ILBuiltinFunction)(const std::vector<ConstantValue *> &args);

struct BuiltinValue :
    ValueImpl<BuiltinValue, Value::Builtin, ConstantValue> {

    ILBuiltinFunction handler;

    static BuiltinValue *create(ILBuiltinFunction func) {
        auto result = new BuiltinValue();
        result->handler = func;
        return result;
    }

    Type *inferType() const {
        return Type::Any;
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "builtin");
        ss << " " << handler;
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct ExternalValue :
    ValueImpl<ExternalValue, Value::External, ConstantValue> {
    std::string name;
    Type *external_type;

    static ExternalValue *create(
        const std::string &name,
        Type *external_type) {
        auto result = new ExternalValue();
        result->name = name;
        result->external_type = external_type;
        return result;
    }

    Type *inferType() const {
        return external_type;
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const {
        std::stringstream ss;
        ss << "(" << ansi(ANSI_STYLE_KEYWORD, "external");
        ss << " " << name;
        ss << " " << bangra::getRepr(external_type);
        ss << ")";
        return ss.str();
    }
};

//------------------------------------------------------------------------------

struct FrameValue;

struct ClosureValue :
    ValueImpl<ClosureValue, Value::Closure, ConstantValue> {
    FlowValue *cont;
    FrameValue *frame;

    static ClosureValue *create(
        FlowValue *cont,
        FrameValue *frame) {
        auto result = new ClosureValue();
        result->cont = cont;
        result->frame = frame;
        return result;
    }

    Type *inferType() const {
        return Type::Any;
    }

    std::string getRepr () const {
        return bangra::getRefRepr(this);
    }

    std::string getRefRepr() const;
};

//------------------------------------------------------------------------------

std::string getRepr (const Value *value) {
    assert(value);
#define ILVALUE_KIND(NAME) \
    case Value::NAME: { \
        auto spec = llvm::cast<NAME ## Value>(value); \
        return spec->getRepr(); \
    } break;
#define ILVALUE_KIND_ABSTRACT(NAME)
#define ILVALUE_KIND_EOK(NAME)
    switch(value->kind) {
        ILVALUE_ENUM_KINDS()
        default: {
            assert(false && "invalid IL value kind");
            return "?";
        } break;
    }
#undef ILVALUE_KIND
#undef ILVALUE_KIND_ABSTRACT
#undef ILVALUE_KIND_EOK
}

std::string getRefRepr (const Value *value) {
    assert(value);
#define ILVALUE_KIND(NAME) \
    case Value::NAME: { \
        auto spec = llvm::cast<NAME ## Value>(value); \
        return spec->getRefRepr(); \
    } break;
#define ILVALUE_KIND_ABSTRACT(NAME)
#define ILVALUE_KIND_EOK(NAME)
    switch(value->kind) {
        ILVALUE_ENUM_KINDS()
        default: {
            assert(false && "invalid IL value kind");
            return "?";
        } break;
    }
#undef ILVALUE_KIND
#undef ILVALUE_KIND_ABSTRACT
#undef ILVALUE_KIND_EOK
}

Type *getType(const Value *value) {
    assert(value);
#define ILVALUE_KIND(NAME) \
    case Value::NAME: { \
        auto spec = llvm::cast<NAME ## Value>(value); \
        return spec->inferType(); \
    } break;
#define ILVALUE_KIND_ABSTRACT(NAME)
#define ILVALUE_KIND_EOK(NAME)
    switch(value->kind) {
        ILVALUE_ENUM_KINDS()
        default: {
            assert(false && "invalid IL value kind");
            return nullptr;
        } break;
    }
#undef ILVALUE_KIND
#undef ILVALUE_KIND_ABSTRACT
#undef ILVALUE_KIND_EOK
}

const char *getClassName(Value::Kind kind) {
#define ILVALUE_KIND(NAME) \
    case Value::NAME: { \
        return #NAME; \
    } break;
#define ILVALUE_KIND_ABSTRACT(NAME)
#define ILVALUE_KIND_EOK(NAME)
    switch(kind) {
        ILVALUE_ENUM_KINDS()
        default: {
            return "???";
        } break;
    }
#undef ILVALUE_KIND
#undef ILVALUE_KIND_ABSTRACT
#undef ILVALUE_KIND_EOK
}

//------------------------------------------------------------------------------
// IL MODEL UTILITY FUNCTIONS
//------------------------------------------------------------------------------

static void unescape(StringValue *s) {
    s->value.resize(inplace_unescape(&s->value[0]));
}

static void unescape(SymbolValue *s) {
    s->value.resize(inplace_unescape(&s->value[0]));
}

// matches ((///...))
static bool is_comment(ConstantValue *expr) {
    if (auto tuple = llvm::dyn_cast<TupleValue>(expr)) {
        if (tuple->values.size() > 0) {
            if (auto sym = llvm::dyn_cast<SymbolValue>(tuple->values.front())) {
                if (!memcmp(sym->value.c_str(),"///",3))
                    return true;
            }
        }
    }
    return false;
}

static ConstantValue *strip(ConstantValue *expr) {
    if (!expr) return nullptr;
    if (auto tuple = llvm::dyn_cast<TupleValue>(expr)) {
        auto copy = TupleValue::create({});
        auto &values = tuple->values;
        for (size_t i = 0; i < values.size(); ++i) {
            auto value = strip(values[i]);
            if (!is_comment(value)) {
                copy->values.push_back(value);
            }
        }
        return copy;
    }
    return expr;
}

static const Anchor *find_valid_anchor(const Value *expr) {
    if (!expr) return nullptr;
    if (expr->anchor.isValid()) return &expr->anchor;
    if (auto tuple = llvm::dyn_cast<TupleValue>(expr)) {
        auto &values = tuple->values;
        for (size_t i = 0; i < values.size(); ++i) {
            const Anchor *result = find_valid_anchor(values[i]);
            if (result) return result;
        }
    }
    return nullptr;
}

//------------------------------------------------------------------------------
// PRINTING
//------------------------------------------------------------------------------

#if 0
template<typename T>
static void streamTraceback(T &stream) {
    void *array[10];
    size_t size;
    char **strings;
    size_t i;

    size = backtrace (array, 10);
    strings = backtrace_symbols (array, size);

    for (i = 0; i < size; i++) {
        stream << format("%s\n", strings[i]);
    }

    free (strings);
}

static std::string formatTraceback() {
    std::stringstream ss;
    streamTraceback(ss);
    return ss.str();
}
#endif

static bool isNested(ConstantValue *e) {
    if (auto tuple = llvm::dyn_cast<TupleValue>(e)) {
        auto &values = tuple->values;
        for (size_t i = 0; i < values.size(); ++i) {
            if (llvm::isa<TupleValue>(values[i]))
                return true;
        }
    }
    return false;
}

template<typename T>
static void streamAnchor(T &stream, ConstantValue *e, size_t depth=0) {
    if (e) {
        const Anchor *anchor = find_valid_anchor(e);
        if (!anchor)
            anchor = &e->anchor;
        stream <<
            format("%s:%i:%i: ",
                anchor->path,
                anchor->lineno,
                anchor->column);
    }
    for(size_t i = 0; i < depth; i++)
        stream << "    ";
}

template<typename T>
static void streamValue(T &stream, ConstantValue *e, size_t depth=0, bool naked=true) {
    if (naked) {
        streamAnchor(stream, e, depth);
    }

	if (!e) {
        stream << "#null#";
        if (naked)
            stream << '\n';
        return;
    }

	switch(e->kind) {
	case Value::Tuple: {
        auto tuple = llvm::cast<TupleValue>(e);
        auto &values = tuple->values;
        if (values.empty()) {
            stream << "()";
            if (naked)
                stream << '\n';
            break;
        }
        if (naked) {
            size_t offset = 0;
            bool single = ((offset + 1) == values.size());
        print_terse:
            streamValue(stream, values[offset], depth, false);
            offset++;
            while (offset != values.size()) {
                if (isNested(values[offset]))
                    break;
                stream << ' ';
                streamValue(stream, values[offset], depth, false);
                offset++;
            }
            stream << (single?";\n":"\n");
        //print_sparse:
            while (offset != values.size()) {
                auto value = values[offset];
                if (!llvm::isa<TupleValue>(value) // not a list
                    && (offset >= 1) // not first element in list
                    && ((offset + 1) != values.size()) // not last element in list
                    && !isNested(values[offset + 1])) { // next element can be terse packed too
                    single = false;
                    streamAnchor(stream, values[offset], depth + 1);
                    stream << "\\ ";
                    goto print_terse;
                }
                streamValue(stream, value, depth + 1);
                offset++;
            }

        } else {
            stream << '(';
            size_t offset = 0;
            while (offset != values.size()) {
                if (offset > 0)
                    stream << ' ';
                streamValue(stream, values[offset], depth + 1, false);
                offset++;
            }
            stream << ')';
            if (naked)
                stream << '\n';
        }
    } return;
    case Value::Integer: {
        auto a = llvm::cast<IntegerValue>(e);

        if (a->value_type->isSigned())
            stream << format("%" PRIi64, a->value);
        else
            stream << format("%" PRIu64, a->value);
        if (naked)
            stream << '\n';
    } return;
    case Value::Real: {
        auto a = llvm::cast<RealValue>(e);
        stream << format("%g", a->value);
        if (naked)
            stream << '\n';
    } return;
	case Value::Symbol: {
        auto a = llvm::cast<SymbolValue>(e);
        streamString(stream, a->value, "[]{}()\"");
        if (naked)
            stream << '\n';
    } return;
	case Value::String: {
        auto a = llvm::cast<StringValue>(e);
		stream << '"';
        streamString(stream, a->value, "\"");
		stream << '"';
        if (naked)
            stream << '\n';
    } return;
    default:
        printf("invalid kind: %i\n", e->kind);
        assert (false); break;
	}
}

static std::string formatValue(ConstantValue *e, size_t depth=0, bool naked=false) {
    std::stringstream ss;
    streamValue(ss, e, depth, naked);
    return ss.str();
}

static void printValue(ConstantValue *e, size_t depth=0, bool naked=false) {
    streamValue(std::cout, e, depth, naked);
}

//------------------------------------------------------------------------------
// S-EXPR LEXER / TOKENIZER
//------------------------------------------------------------------------------

typedef enum {
    token_none = -1,
    token_eof = 0,
    token_open = '(',
    token_close = ')',
    token_square_open = '[',
    token_square_close = ']',
    token_curly_open = '{',
    token_curly_close = '}',
    token_string = '"',
    token_symbol = 'S',
    token_escape = '\\',
    token_statement = ';',
    token_integer = 'I',
    token_real = 'R',
} Token;

const char symbol_terminators[]  = "()[]{}\"';#";
const char integer_terminators[] = "()[]{}\"';#";
const char real_terminators[]    = "()[]{}\"';#";

struct Lexer {
    const char *path;
    const char *input_stream;
    const char *eof;
    const char *cursor;
    const char *next_cursor;
    // beginning of line
    const char *line;
    // next beginning of line
    const char *next_line;

    int lineno;
    int next_lineno;

    int base_offset;

    int token;
    const char *string;
    int string_len;
    int64_t integer;
    bool is_unsigned;
    double real;

    std::string error_string;

    Lexer() {}

    void init (const char *input_stream, const char *eof, const char *path, int offset = 0) {
        if (eof == NULL) {
            eof = input_stream + strlen(input_stream);
        }

        this->base_offset = offset;
        this->path = path;
        this->input_stream = input_stream;
        this->eof = eof;
        this->next_cursor = input_stream;
        this->next_lineno = 1;
        this->next_line = input_stream;
        this->error_string.clear();
    }

    void dumpLine() {
        dumpFileLine(path, offset());
    }

    int offset () {
        return base_offset + (cursor - input_stream);
    }

    int column () {
        return cursor - line + 1;
    }

    void initAnchor(Anchor &anchor) {
        anchor.path = path;
        anchor.lineno = lineno;
        anchor.column = column();
        anchor.offset = offset();
    }

    void error( const char *format, ... ) {
        va_list args;
        va_start (args, format);
        size_t size = vsnprintf(nullptr, 0, format, args);
        va_end (args);
        error_string.resize(size);
        va_start (args, format);
        vsnprintf( &error_string[0], size + 1, format, args );
        va_end (args);
        token = token_eof;
    }

    char next() {
        return *next_cursor++;
    }

    bool verifyGoodTaste(char c) {
        if (c == '\t') {
            error("please use spaces instead of tabs.");
            return false;
        }
        return true;
    }

    void readSymbol () {
        bool escape = false;
        while (true) {
            if (next_cursor == eof) {
                break;
            }
            char c = next();
            if (escape) {
                if (c == '\n') {
                    ++next_lineno;
                    next_line = next_cursor;
                }
                // ignore character
                escape = false;
            } else if (c == '\\') {
                // escape
                escape = true;
            } else if (isspace(c)
                || strchr(symbol_terminators, c)) {
                -- next_cursor;
                break;
            }
        }
        string = cursor;
        string_len = next_cursor - cursor;
    }

    void readSingleSymbol () {
        string = cursor;
        string_len = next_cursor - cursor;
    }

    void readString (char terminator) {
        bool escape = false;
        while (true) {
            if (next_cursor == eof) {
                error("unterminated sequence");
                break;
            }
            char c = next();
            if (c == '\n') {
                ++next_lineno;
                next_line = next_cursor;
            }
            if (escape) {
                // ignore character
                escape = false;
            } else if (c == '\\') {
                // escape
                escape = true;
            } else if (c == terminator) {
                break;
            }
        }
        string = cursor;
        string_len = next_cursor - cursor;
    }

    bool readInteger() {
        char *end;
        errno = 0;
        integer = std::strtoll(cursor, &end, 0);
        if ((end == cursor)
            || (errno == ERANGE)
            || (end >= eof)
            || (!isspace(*end) && !strchr(integer_terminators, *end)))
            return false;
        is_unsigned = false;
        next_cursor = end;
        return true;
    }

    bool readUInteger() {
        char *end;
        errno = 0;
        integer = std::strtoull(cursor, &end, 0);
        if ((end == cursor)
            || (errno == ERANGE)
            || (end >= eof)
            || (!isspace(*end) && !strchr(integer_terminators, *end)))
            return false;
        is_unsigned = true;
        next_cursor = end;
        return true;
    }

    bool readReal() {
        char *end;
        errno = 0;
        real = std::strtod(cursor, &end);
        if ((end == cursor)
            || (errno == ERANGE)
            || (end >= eof)
            || (!isspace(*end) && !strchr(real_terminators, *end)))
            return false;
        next_cursor = end;
        return true;
    }

    int readToken () {
        lineno = next_lineno;
        line = next_line;
        cursor = next_cursor;
        while (true) {
            if (next_cursor == eof) {
                token = token_eof;
                break;
            }
            char c = next();
            if (!verifyGoodTaste(c)) break;
            if (c == '\n') {
                ++next_lineno;
                next_line = next_cursor;
            }
            if (isspace(c)) {
                lineno = next_lineno;
                line = next_line;
                cursor = next_cursor;
            } else if (c == '#') {
                readString('\n');
                // and continue
                lineno = next_lineno;
                line = next_line;
                cursor = next_cursor;
            } else if (c == '(') {
                token = token_open;
                break;
            } else if (c == ')') {
                token = token_close;
                break;
            } else if (c == '[') {
                token = token_square_open;
                break;
            } else if (c == ']') {
                token = token_square_close;
                break;
            } else if (c == '{') {
                token = token_curly_open;
                break;
            } else if (c == '}') {
                token = token_curly_close;
                break;
            } else if (c == '\\') {
                token = token_escape;
                break;
            } else if (c == '"') {
                token = token_string;
                readString(c);
                break;
            } else if (c == '\'') {
                token = token_string;
                readString(c);
                break;
            } else if (c == ';') {
                token = token_statement;
                break;
            } else if (readInteger() || readUInteger()) {
                token = token_integer;
                break;
            } else if (readReal()) {
                token = token_real;
                break;
            } else {
                token = token_symbol;
                readSymbol();
                break;
            }
        }
        return token;
    }

    StringValue *getAsString() {
        auto result = StringValue::create(string + 1, string_len - 2);
        initAnchor(result->anchor);
        unescape(result);
        return result;
    }

    SymbolValue *getAsSymbol() {
        auto result = SymbolValue::create(string, string_len);
        initAnchor(result->anchor);
        unescape(result);
        return result;
    }

    IntegerValue *getAsInteger() {
        auto result = IntegerValue::create(integer,
            is_unsigned?Type::UInt64:Type::Int64);
        initAnchor(result->anchor);
        return result;
    }

    RealValue *getAsReal() {
        auto result = RealValue::create(real, Type::Double);
        initAnchor(result->anchor);
        return result;
    }

};

//------------------------------------------------------------------------------
// S-EXPR PARSER
//------------------------------------------------------------------------------

struct Parser {
    Lexer lexer;

    Anchor error_origin;
    Anchor parse_origin;
    std::string error_string;
    int errors;

    Parser() :
        errors(0)
        {}

    void init() {
        error_string.clear();
    }

    void error( const char *format, ... ) {
        ++errors;
        lexer.initAnchor(error_origin);
        parse_origin = error_origin;
        va_list args;
        va_start (args, format);
        size_t size = vsnprintf(nullptr, 0, format, args);
        va_end (args);
        error_string.resize(size);
        va_start (args, format);
        vsnprintf( &error_string[0], size + 1, format, args );
        va_end (args);
    }

    struct ListBuilder {
    protected:
        TupleValue *result;
        size_t start;
        Anchor anchor;
    public:

        ListBuilder(Lexer &lexer) :
            result(TupleValue::create({})),
            start(0) {
            lexer.initAnchor(anchor);
        }

        const Anchor &getAnchor() const {
            return anchor;
        }

        void resetStart() {
            start = getResultSize();
        }

        bool split() {
            // if we haven't appended anything, that's an error
            if (start == getResultSize()) {
                return false;
            }
            // move tail to new list
            auto newtuple = TupleValue::create({});
            for (size_t i = start; i < result->values.size(); ++i) {
                newtuple->values.push_back(result->values[i]);
            }
            // remove tail
            result->values.erase(
                result->values.begin() + start,
                result->values.end());
            // append new list
            append(newtuple);
            resetStart();
            return true;
        }

        void append(ConstantValue *item) {
            result->values.push_back(item);
        }

        size_t getResultSize() {
            return result->values.size();
        }

        ConstantValue *getSingleResult() {
            return result->values.front();
        }

        TupleValue *getResult() {
            result->anchor = anchor;
            return result;
        }

    };

    TupleValue *parseList(int end_token) {
        ListBuilder builder(lexer);
        lexer.readToken();
        while (true) {
            if (lexer.token == end_token) {
                break;
            } else if (lexer.token == token_escape) {
                int column = lexer.column();
                lexer.readToken();
                auto elem = parseNaked(column, 1, end_token);
                if (errors) return nullptr;
                builder.append(elem);
            } else if (lexer.token == token_eof) {
                error("missing closing bracket");
                // point to beginning of list
                error_origin = builder.getAnchor();
                return nullptr;
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return nullptr;
                }
                lexer.readToken();
            } else {
                auto elem = parseAny();
                if (errors) return nullptr;
                builder.append(elem);
                lexer.readToken();
            }
        }
        return builder.getResult();
    }

    ConstantValue *parseAny () {
        assert(lexer.token != token_eof);
        if (lexer.token == token_open) {
            return parseList(token_close);
        } else if (lexer.token == token_square_open) {
            auto list = parseList(token_square_close);
            if (errors) return nullptr;
            auto sym = SymbolValue::create("[");
            sym->anchor = list->anchor;
            list->values.insert(list->values.begin(), sym);
            return list;
        } else if (lexer.token == token_curly_open) {
            auto list = parseList(token_curly_close);
            if (errors) return nullptr;
            auto sym = SymbolValue::create("{");
            sym->anchor = list->anchor;
            list->values.insert(list->values.begin(), sym);
            return list;
        } else if ((lexer.token == token_close)
            || (lexer.token == token_square_close)
            || (lexer.token == token_curly_close)) {
            error("stray closing bracket");
        } else if (lexer.token == token_string) {
            return lexer.getAsString();
        } else if (lexer.token == token_symbol) {
            return lexer.getAsSymbol();
        } else if (lexer.token == token_integer) {
            return lexer.getAsInteger();
        } else if (lexer.token == token_real) {
            return lexer.getAsReal();
        } else {
            error("unexpected token: %c (%i)", *lexer.cursor, (int)*lexer.cursor);
        }

        return nullptr;
    }

    ConstantValue *parseNaked (int column = 0, int depth = 0, int end_token = token_none) {
        int lineno = lexer.lineno;

        bool escape = false;
        int subcolumn = 0;

        ListBuilder builder(lexer);

        while (lexer.token != token_eof) {
            if (lexer.token == end_token) {
                break;
            } if (lexer.token == token_escape) {
                escape = true;
                lexer.readToken();
                if (lexer.lineno <= lineno) {
                    error("escape character is not at end of line");
                    parse_origin = builder.getAnchor();
                    return nullptr;
                }
                lineno = lexer.lineno;
            } else if (lexer.lineno > lineno) {
                if (depth > 0) {
                    if (subcolumn == 0) {
                        subcolumn = lexer.column();
                    } else if (lexer.column() != subcolumn) {
                        error("indentation mismatch");
                        parse_origin = builder.getAnchor();
                        return nullptr;
                    }
                } else {
                    subcolumn = lexer.column();
                }
                if (column != subcolumn) {
                    if ((column + 4) != subcolumn) {
                        //printf("%i %i\n", column, subcolumn);
                        error("indentations must nest by 4 spaces.");
                        return nullptr;
                    }
                }

                escape = false;
                builder.resetStart();
                lineno = lexer.lineno;
                // keep adding elements while we're in the same line
                while ((lexer.token != token_eof)
                        && (lexer.token != end_token)
                        && (lexer.lineno == lineno)) {
                    auto elem = parseNaked(subcolumn, depth + 1, end_token);
                    if (errors) return nullptr;
                    builder.append(elem);
                }
            } else if (lexer.token == token_statement) {
                if (!builder.split()) {
                    error("empty expression");
                    return nullptr;
                }
                lexer.readToken();
                if (depth > 0) {
                    // if we are in the same line and there was no preceding ":",
                    // continue in parent
                    if (lexer.lineno == lineno)
                        break;
                }
            } else {
                auto elem = parseAny();
                if (errors) return nullptr;
                builder.append(elem);
                lineno = lexer.next_lineno;
                lexer.readToken();
            }

            if (depth > 0) {
                if ((!escape || (lexer.lineno > lineno))
                    && (lexer.column() <= column)) {
                    break;
                }
            }
        }

        if (builder.getResultSize() == 1) {
            return builder.getSingleResult();
        } else {
            return builder.getResult();
        }
    }

    ConstantValue *parseMemory (
        const char *input_stream, const char *eof, const char *path, int offset = 0) {
        init();
        lexer.init(input_stream, eof, path, offset);

        lexer.readToken();

        auto result = parseNaked(lexer.column());

        if (error_string.empty() && !lexer.error_string.empty()) {
            error_string = lexer.error_string;
            lexer.initAnchor(error_origin);
            parse_origin = error_origin;
        }

        if (!error_string.empty()) {
            printf("%s:%i:%i: error: %s\n",
                error_origin.path,
                error_origin.lineno,
                error_origin.column,
                error_string.c_str());
            dumpFileLine(path, error_origin.offset);
            if (!(parse_origin == error_origin)) {
                printf("%s:%i:%i: while parsing expression\n",
                    parse_origin.path,
                    parse_origin.lineno,
                    parse_origin.column);
                dumpFileLine(path, parse_origin.offset);
            }
            return nullptr;
        }

        assert(result);
        return strip(result);
    }

    ConstantValue *parseFile (const char *path) {
        auto file = MappedFile::open(path);
        if (file) {
            return parseMemory(
                file->strptr(), file->strptr() + file->size(),
                path);
        } else {
            fprintf(stderr, "unable to open file: %s\n", path);
            return NULL;
        }
    }


};

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

struct ILBuilder {
    FlowValue *continuation;

    void continueAt(FlowValue *cont) {
        this->continuation = cont;
        assert(!cont->values.size());
    }

    void insertAndAdvance(
        const std::vector<Value *> &values,
        FlowValue *next) {
        assert(continuation);
        assert(!continuation->values.size());
        continuation->values = values;
        continuation = next;
    }

    void br(const std::vector<Value *> &arguments) {
        insertAndAdvance(arguments, nullptr);
    }

    /*
    ILParameterRef toparam(const Value *value) {
        if (llvm::isa<ParameterValue>(value.get())) {
            return value->getSharedPtr<ParameterValue>();
        } else {
            auto next = FlowValue::create(module, 1);
            insertAndAdvance( { next, value } , next);
            return next->parameters[0];
        }
    }
    */

    ParameterValue *call(std::vector<Value *> values) {
        auto next = FlowValue::create(1, "cret");
        values.push_back(next);
        insertAndAdvance(values, next);
        return next->parameters[0];
    }

};

//------------------------------------------------------------------------------
// FOREIGN FUNCTION INTERFACE
//------------------------------------------------------------------------------

struct FFI {
    typedef std::pair<CFunctionType *, LLVMValueRef> TypeLLVMValuePair;

    LLVMModuleRef module;
    LLVMExecutionEngineRef engine;
    std::unordered_map<Type *, LLVMTypeRef> typecache;
    std::unordered_map<std::string, TypeLLVMValuePair > functions;

    LLVMTypeRef thunkfunctype;

    FFI() :
        engine(nullptr) {
        module = LLVMModuleCreateWithName("ffi");

        LLVMTypeRef paramtypes[] = {
            LLVMPointerType(LLVMInt8Type(), 0),
            LLVMPointerType(LLVMPointerType(LLVMInt8Type(), 0), 0)
        };
        thunkfunctype = LLVMFunctionType(
                    LLVMVoidType(), paramtypes, 2, false);
    }

    ~FFI() {
        destroyEngine();
        LLVMDisposeModule(module);
    }

    void destroyEngine() {
        if (!engine) return;
        // leak the EE for now so we can reuse the module
        //LLVMDisposeExecutionEngine(engine);
        engine = nullptr;
    }

    void verifyModule() {
        char *error = NULL;
        LLVMVerifyModule(module, LLVMAbortProcessAction, &error);
        LLVMDisposeMessage(error);
    }

    void createEngine() {
        if (engine) return;

        char *error = NULL;

        #if 0
        LLVMCreateExecutionEngineForModule(&engine,
                                        module,
                                        &error);
        #else
        LLVMMCJITCompilerOptions options;
        LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
        //options.OptLevel = 0;
        //options.EnableFastISel = true;
        //options.CodeModel = LLVMCodeModelLarge;

        LLVMCreateMCJITCompilerForModule(&engine, module,
            &options, sizeof(options), &error);
        #endif

        if (error) {
            LLVMDisposeMessage(error);
            engine = nullptr;
            return;
        }
    }

    LLVMTypeRef convertType(Type *il_type) {
        LLVMTypeRef result = typecache[il_type];
        if (!result) {
            switch(il_type->getKind()) {
                case T_Void: {
                    result = LLVMVoidType();
                } break;
                case T_Null: {
                    result = LLVMVoidType();
                } break;
                case T_Integer: {
                    auto integer = llvm::cast<IntegerType>(il_type);
                    result = LLVMIntType(integer->getWidth());
                } break;
                case T_Real: {
                    auto real = llvm::cast<RealType>(il_type);
                    switch(real->getWidth()) {
                        case 16: {
                            result = LLVMHalfType();
                        } break;
                        case 32: {
                            result = LLVMFloatType();
                        } break;
                        case 64: {
                            result = LLVMDoubleType();
                        } break;
                        default: {
                            ilError(nullptr, "illegal real type");
                        } break;
                    }
                } break;
                case T_Struct: {
                    auto st = llvm::cast<StructType>(il_type);
                    size_t fieldcount = st->getFieldCount();
                    LLVMTypeRef types[fieldcount];
                    for (size_t i = 0; i < fieldcount; ++i) {
                        types[i] = convertType(st->getField(i).getType());
                    }
                    result = LLVMStructType(types, fieldcount, false);
                } break;
                case T_Array: {
                    auto array = llvm::cast<ArrayType>(il_type);
                    result = LLVMArrayType(
                        convertType(
                            array->getElement()),
                        array->getSize());
                } break;
                case T_Pointer: {
                    result = LLVMPointerType(
                        convertType(
                            llvm::cast<PointerType>(il_type)->getElement()),
                        0);
                } break;
                case T_CFunction: {
                    auto ftype = llvm::cast<CFunctionType>(il_type);

                    LLVMTypeRef parameters[ftype->getParameterCount()];
                    for (size_t i = 0; i < ftype->getParameterCount(); ++i) {
                        parameters[i] = convertType(ftype->getParameter(i));
                    }
                    result = LLVMFunctionType(
                        convertType(ftype->getResult()),
                        parameters, ftype->getParameterCount(),
                        ftype->isVarArg());
                } break;
                default: {
                    ilError(nullptr, "can not translate type %s",
                        bangra::getRepr(il_type).c_str());
                } break;
            }
            assert(result);
            typecache[il_type] = result;
        }
        return result;
    }

    Type *varArgType(Type *type) {
        // todo: expand float to double
        return type;
    }

    struct Variant {
        union {
            int8_t int8val;
            int16_t int16val;
            int32_t int32val;
            int64_t int64val;
            float floatval;
            double doubleval;
            void *ptrval;
        };

        void set_integer(Type *il_type, int64_t value) {
            auto integer = llvm::cast<IntegerType>(il_type);
            switch(integer->getWidth()) {
                case 8: { int8val = value; } break;
                case 16: { int16val = value; } break;
                case 32: { int32val = value; } break;
                case 64: { int64val = value; } break;
                default: {
                    ilError(nullptr, "can't convert constant");
                } break;
            }
        }

        void set_real(Type *il_type, double value) {
            auto real = llvm::cast<RealType>(il_type);
            switch(real->getWidth()) {
                case 32: { floatval = value; } break;
                case 64: { doubleval = value; } break;
                default: {
                    ilError(nullptr, "can't convert constant");
                } break;
            }
        }

    };

    ConstantValue *makeConstant(Type *il_type, const Variant &value) {
        switch(il_type->getKind()) {
            case T_Void: {
                return TupleValue::create({});
            } break;
            case T_Integer: {
                auto integer = llvm::cast<IntegerType>(il_type);
                int64_t ivalue = 0;
                switch(integer->getWidth()) {
                    case 8: { ivalue = value.int8val; } break;
                    case 16: { ivalue = value.int16val; } break;
                    case 32: { ivalue = value.int32val; } break;
                    case 64: { ivalue = value.int64val; } break;
                    default: {
                        ilError(nullptr, "cannot make integer constant");
                    } break;
                }
                return IntegerValue::create(ivalue,
                    static_cast<IntegerType *>(il_type));
            } break;
            case T_Real: {
                auto real = llvm::cast<RealType>(il_type);
                double flvalue = 0.0;
                switch(real->getWidth()) {
                    case 32: { flvalue = value.floatval; } break;
                    case 64: { flvalue = value.doubleval; } break;
                    default: {
                        ilError(nullptr, "cannot make real constant");
                    } break;
                }
                return RealValue::create(flvalue,
                    static_cast<RealType *>(il_type));
            } break;
            case T_Pointer: {
                return PointerValue::create(value.ptrval, il_type);
            } break;
            default: {
                ilError(nullptr, "can not make constant for type");
                return nullptr;
            } break;
        }
    }

    Variant convertConstant(
        Type *il_type, const ConstantValue *c) {
        Variant result;
        switch(il_type->getKind()) {
            case T_Integer: {
                switch(c->kind) {
                    case Value::Integer: {
                        auto ci = llvm::cast<IntegerValue>(c);
                        result.set_integer(il_type, ci->value);
                    } break;
                    case Value::Real: {
                        auto cr = llvm::cast<RealValue>(c);
                        result.set_integer(il_type, (int64_t)cr->value);
                    } break;
                    default: {
                        ilError(nullptr, "can't convert %s to integer",
                            bangra::getRepr(getType(c)).c_str());
                    } break;
                }
            } break;
            case T_Real: {
                switch(c->kind) {
                    case Value::Integer: {
                        auto ci = llvm::cast<IntegerValue>(c);
                        result.set_real(il_type, (double)ci->value);
                    } break;
                    case Value::Real: {
                        auto cr = llvm::cast<RealValue>(c);
                        result.set_real(il_type, cr->value);
                    } break;
                    default: {
                        ilError(nullptr, "can't convert %s to real",
                            bangra::getRepr(getType(c)).c_str());
                    } break;
                }
            } break;
            case T_Pointer: {
                switch(c->kind) {
                    case Value::Pointer: {
                        auto cp = llvm::cast<PointerValue>(c);
                        result.ptrval = cp->value;
                    } break;
                    case Value::String: {
                        auto cs = llvm::cast<StringValue>(c);
                        result.ptrval = (void *)cs->value.c_str();
                    } break;
                    default: {
                        ilError(nullptr, "can't convert %s to pointer",
                            bangra::getRepr(getType(c)).c_str());
                    } break;
                }
            } break;
            default: {
                ilError(nullptr, "can not convert constant");
            } break;
        }
        return result;
    }

    Type *pointerifyType(Type *il_type) {
        if (il_type->getKind() == T_Pointer)
            return il_type;
        else
            return Type::Pointer(il_type);
    }

    TypeLLVMValuePair convertFunction(
        const ConstantValue *value) {
        auto ilfunc = llvm::dyn_cast<ExternalValue>(value);
        if (!ilfunc) {
            ilError(nullptr, "not an external");
        }
        auto &result = functions[ilfunc->name];
        if (!result.second) {
            if (ilfunc->external_type->getKind() != T_CFunction) {
                ilError(nullptr, "not a C function type");
            }
            auto ilfunctype = static_cast<CFunctionType *>(ilfunc->external_type);

            LLVMTypeRef ofunctype = convertType(ilfunctype);
            LLVMValueRef ofunc = LLVMAddFunction(
                module, ilfunc->name.c_str(), ofunctype);

            LLVMValueRef thunkfunc = LLVMAddFunction(
                module, ("bangra_" + ilfunc->name + "_thunk").c_str(),
                thunkfunctype);

            LLVMBuilderRef builder = LLVMCreateBuilder();
            LLVMPositionBuilderAtEnd(builder, LLVMAppendBasicBlock(thunkfunc, ""));

            size_t paramcount = ilfunctype->getParameterCount();
            LLVMValueRef args[paramcount];
            LLVMValueRef argparam = LLVMGetParam(thunkfunc, 1);
            for (size_t i = 0; i < paramcount; ++i) {
                Type *paramtype = ilfunctype->getParameter(i);

                LLVMValueRef indices[] = {
                    LLVMConstInt(LLVMInt32Type(), i, false) };

                LLVMValueRef arg = LLVMBuildGEP(
                    builder, argparam, indices, 1, "");

                arg = LLVMBuildBitCast(builder, arg,
                    convertType(Type::Pointer(Type::Pointer(paramtype))), "");

                arg = LLVMBuildLoad(builder, arg, "");

                args[i] = LLVMBuildLoad(builder, arg, "");
            }
            LLVMValueRef cr = LLVMBuildCall(builder, ofunc, args, paramcount, "");
            if (ilfunctype->getResult() != Type::Void) {
                Type *rettype = ilfunctype->getResult();
                LLVMValueRef retparam = LLVMGetParam(thunkfunc, 0);

                retparam = LLVMBuildBitCast(builder, retparam,
                      convertType(Type::Pointer(rettype)), "");

                LLVMBuildStore(builder, cr, retparam);
            }

            LLVMBuildRetVoid(builder);
            LLVMDisposeBuilder(builder);

            //LLVMDumpModule(module);
            //verifyModule();

            result.first = ilfunctype;
            result.second = thunkfunc;

            destroyEngine();
        }
        return result;
    }

    ConstantValue *runFunction(
        const ConstantValue *func, const std::vector<ConstantValue *> &args) {

        auto F = convertFunction(func);
        auto ilfunctype = F.first;

        bool isvararg = ilfunctype->isVarArg();
        unsigned paramcount = ilfunctype->getParameterCount();

        if (args.size() < paramcount) {
            ilError(nullptr, "not enough arguments");
        } else if (!isvararg && (args.size() > paramcount)) {
            ilError(nullptr, "too many arguments");
        }

        Variant variants[paramcount];
        void *ptrs[paramcount];
        for (size_t i = 0; i < paramcount; ++i) {
            variants[i] = convertConstant(
                ilfunctype->getParameter(i), args[i]);
            ptrs[i] = &variants[i];
        }

        verifyModule();
        createEngine();
        typedef void (*FuncType)(void *, void **);

        FuncType f = (FuncType)LLVMGetPointerToGlobal(engine, F.second);

        Variant result;
        f(&result, ptrs);

        return makeConstant(ilfunctype->getResult(), result);
    }

};

static FFI *ffi;

//------------------------------------------------------------------------------
// INTERPRETER
//------------------------------------------------------------------------------

typedef std::unordered_map<FlowValue *, std::vector<ConstantValue *> >
    Cont2ValuesMap;

struct FrameValue {
    size_t idx;
    FrameValue *parent;
    Cont2ValuesMap map;

    std::string getRefRepr() {
        std::stringstream ss;
        ss << "#" << idx << ":" << this;
        return ss.str();
    }

    std::string getRepr() {
        std::stringstream ss;
        ss << "#" << idx << ":" << this << ":\n";
        bool any_entries = false;
        for (auto &entry : map) {
            any_entries = true;
            ss << "  " << entry.first->getRefRepr();
            auto &value = entry.second;
            for (size_t i = 0; i < value.size(); ++i) {
                ss << " " << bangra::getRefRepr(value[i]);
            }
            ss << "\n";
        }
        return ss.str();
    }

    static FrameValue *create() {
        // create closure
        FrameValue *newframe = new FrameValue();
        newframe->parent = nullptr;
        newframe->idx = 0;
        return newframe;
    }

    static FrameValue *create(FrameValue *frame) {
        // create closure
        FrameValue *newframe = new FrameValue();
        newframe->parent = frame;
        newframe->idx = frame->idx + 1;
        return newframe;
    }
};

std::string ClosureValue::getRefRepr() const {
    std::stringstream ss;
    ss << "(" << ansi(ANSI_STYLE_KEYWORD, "closure");
    ss << " " << cont->getRefRepr();
    ss << " " << frame->getRefRepr();
    ss << ")";
    return ss.str();
}

typedef std::vector<Value *> ILValueArray;

ConstantValue *evaluate(FrameValue *frame, Value *value) {
    if (auto c = llvm::dyn_cast<ConstantValue>(value)) {
        return c;
    }
    switch(value->kind) {
        case Value::Parameter: {
            auto param = llvm::cast<ParameterValue>(value);
            FrameValue *ptr = frame;
            while (ptr) {
                auto cont = param->parent;
                assert(cont && "parameter has no parent");
                if (ptr->map.count(cont)) {
                    auto &values = ptr->map[cont];
                    assert(param->index < values.size());
                    return values[param->index];
                }
                ptr = ptr->parent;
            }
            assert(false && "parameter not bound in any frame");
            return nullptr;
        } break;
        case Value::Flow: {
            // create closure
            return ClosureValue::create(
                llvm::cast<FlowValue>(value),
                frame);
        } break;
        default:
            ilError(value, "value can not be evaluated");
            break;
    }
    return nullptr;
}

void evaluate_values(
    const ILValueArray &arguments,
    FrameValue *frame,
    std::vector<ConstantValue *> &values) {
    size_t argcount = arguments.size();
    for (size_t i = 1; i < argcount; ++i) {
        auto value = evaluate(frame, arguments[i]);
        assert(value);
        values.push_back(value);
    }
}

void map_continuation_arguments(
    const ILValueArray &arguments,
    FlowValue *nextcont,
    FrameValue *frame,
    FrameValue *nextframe) {
    std::vector<ConstantValue *> values;
    evaluate_values(arguments, frame, values);
    size_t argcount = arguments.size() - 1;
    size_t paramcount = nextcont->getParameterCount();
    if (argcount != paramcount) {
        ilError(nullptr, "argument count mismatch (%zu passed, %zu required)",
            argcount, paramcount);
    }
    nextframe->map[nextcont] = values;
}

void map_closure(ClosureValue *closure,
    ILValueArray &arguments,
    FrameValue *&frame) {
    map_continuation_arguments(
        arguments, closure->cont, frame, closure->frame);
    frame = closure->frame;
    arguments = closure->cont->values;
}

static bool extract_bool(const ConstantValue *value);
ConstantValue *execute(std::vector<Value *> arguments) {

    FrameValue *frame = FrameValue::create();
    frame->idx = 0;

    auto retcont = FlowValue::create(1);
    // add special continuation as return function
    arguments.push_back(retcont);

    while (true) {
#ifdef BANGRA_DEBUG_IL
        std::cout << frame->getRepr() << "\n";
        //std::cout << cont->getRepr() << "\n";
        fflush(stdout);
#endif
        assert(arguments.size() >= 1);
        Value *callee = arguments[0];
        if (callee->kind == Value::Parameter) {
            callee = evaluate(frame, callee);
        }
        switch(callee->kind) {
            case Value::Closure: {
                auto closure = llvm::cast<ClosureValue>(callee);
                if (closure->cont == retcont) {
                    if (arguments.size() >= 2) {
                        return evaluate(frame, arguments[1]);
                    } else {
                        return TupleValue::create({});
                    }
                }
                map_closure(
                    closure, arguments, frame);
            } break;
            case Value::Flow: {
                auto nextcont = llvm::cast<FlowValue>(callee);
                FrameValue *nextframe = FrameValue::create(frame);
                map_continuation_arguments(arguments, nextcont, frame, nextframe);
                arguments = nextcont->values;
                frame = nextframe;
            } break;
            case Value::Intrinsic: {
                if (callee == IntrinsicValue::Branch) {
                    auto condarg = arguments[1];
                    bool condvalue = extract_bool(
                        evaluate(frame, condarg));
                    auto thenarg = arguments[2];
                    auto elsearg = arguments[3];
                    assert(thenarg->kind == Value::Flow);
                    assert(elsearg->kind == Value::Flow);
                    if (condvalue) {
                        arguments = llvm::cast<FlowValue>(thenarg)->values;
                    } else {
                        arguments = llvm::cast<FlowValue>(elsearg)->values;
                    }
                } else {
                    ilError(callee, "unhandled intrinsic");
                }
            } break;
            case Value::Builtin: {
                auto cb = llvm::cast<BuiltinValue>(callee);
                std::vector<ConstantValue *> values;
                evaluate_values(arguments, frame, values);
                assert(values.size() >= 1);
                ConstantValue *closure = values.back();
                values.pop_back();
                ConstantValue *result = cb->handler(values);
                // generate fitting resume
                arguments.clear();
                arguments.push_back(closure);
                arguments.push_back(result);
            } break;
            case Value::External: {
                auto cb = llvm::cast<ExternalValue>(callee);
                std::vector<ConstantValue *> values;
                evaluate_values(arguments, frame, values);
                assert(values.size() >= 1);
                ConstantValue *closure = values.back();
                values.pop_back();
                ConstantValue *result = ffi->runFunction(cb, values);
                // generate fitting resume
                arguments.clear();
                arguments.push_back(closure);
                arguments.push_back(result);
            } break;
            default: {
                ilError(callee, "can not apply expression");
            } break;
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// C BRIDGE (CLANG)
//------------------------------------------------------------------------------

class CVisitor : public clang::RecursiveASTVisitor<CVisitor> {
public:
    StructValue *dest;
    clang::ASTContext *Context;
    std::unordered_map<clang::RecordDecl *, bool> record_defined;
    std::unordered_map<clang::EnumDecl *, bool> enum_defined;
    std::unordered_map<const char *, char *> path_cache;
    std::unordered_map<std::string, Type *> named_structs;
    std::unordered_map<std::string, Type *> named_enums;
    std::unordered_map<std::string, Type *> typedefs;

    CVisitor() : Context(NULL) {
    }

    Anchor anchorFromLocation(clang::SourceLocation loc) {
        Anchor anchor;
        auto &SM = Context->getSourceManager();

        auto PLoc = SM.getPresumedLoc(loc);

        if (PLoc.isValid()) {
            auto fname = PLoc.getFilename();
            // get resident path by pointer
            char *rpath = path_cache[fname];
            if (!rpath) {
                rpath = strdup(fname);
                path_cache[fname] = rpath;
            }

            anchor.path = rpath;
            anchor.lineno = PLoc.getLine();
            anchor.column = PLoc.getColumn();
        }

        return anchor;
    }

    /*
    static ValueRef fixAnchor(Anchor &anchor, ValueRef value) {
        if (value && !value->anchor.isValid() && anchor.isValid()) {
            value->anchor = anchor;
            if (auto ptr = llvm::dyn_cast<Pointer>(value)) {
                auto elem = ptr->getAt();
                while (elem) {
                    fixAnchor(anchor, elem);
                    elem = elem->getNext();
                }
            }
            fixAnchor(anchor, value->getNext());
        }
        return value;
    }
    */

    void SetContext(clang::ASTContext * ctx, StructValue *dest_) {
        Context = ctx;
        dest = dest_;
    }

    void GetFields(StructType *struct_type, clang::RecordDecl * rd) {
        auto &rl = Context->getASTRecordLayout(rd);

        bool opaque = false;
        for(clang::RecordDecl::field_iterator it = rd->field_begin(), end = rd->field_end(); it != end; ++it) {
            clang::DeclarationName declname = it->getDeclName();

            unsigned idx = it->getFieldIndex();

            auto offset = rl.getFieldOffset(idx);
            //unsigned width = it->getBitWidthValue(*Context);

            if(it->isBitField() || (!it->isAnonymousStructOrUnion() && !declname)) {
                opaque = true;
                break;
            }
            clang::QualType FT = it->getType();
            Type *fieldtype = TranslateType(FT);
            if(!fieldtype) {
                opaque = true;
                break;
            }
            // todo: work offset into structure
            struct_type->addField(
                StructType::Field(
                    it->isAnonymousStructOrUnion()?"":
                                        declname.getAsString(),
                    fieldtype,
                    anchorFromLocation(it->getSourceRange().getBegin())));
        }
    }

    Type *TranslateRecord(clang::RecordDecl *rd) {
        if (!rd->isStruct() && !rd->isUnion()) return NULL;

        std::string name = rd->getName();

        StructType *struct_type = nullptr;
        if (name.size() && named_structs.count(name)) {
            struct_type = llvm::cast<StructType>(named_structs[name]);
        } else {
            struct_type =
                llvm::cast<StructType>(
                    Type::Struct(name, false, rd->isUnion()));
            if (!struct_type->isUnnamed()) {
                named_structs[name] = struct_type;
            }
        }

        clang::RecordDecl * defn = rd->getDefinition();
        if (defn && !record_defined[rd]) {
            Anchor anchor = anchorFromLocation(rd->getSourceRange().getBegin());
            struct_type->setAnchor(anchor);

            GetFields(struct_type, defn);

            auto &rl = Context->getASTRecordLayout(rd);
            auto align = rl.getAlignment();
            auto size = rl.getSize();

            // todo: make sure these fit
            // align.getQuantity()
            // size.getQuantity()

            record_defined[rd] = true;
        }

        return struct_type;
    }

    Type *TranslateEnum(clang::EnumDecl *ed) {
        std::string name = ed->getName();

        EnumType *enum_type = nullptr;
        if (name.size() && named_enums.count(name)) {
            enum_type = llvm::cast<EnumType>(named_enums[name]);
        } else {
            enum_type = llvm::cast<EnumType>(Type::Enum(name));
            if (!enum_type->isUnnamed()) {
                named_enums[name] = enum_type;
            }
        }

        clang::EnumDecl * defn = ed->getDefinition();
        if (defn && !enum_defined[ed]) {
            enum_type->setAnchor(
                anchorFromLocation(ed->getIntegerTypeRange().getBegin()));

            enum_type->setType(TranslateType(ed->getIntegerType()));

            for (auto it : ed->enumerators()) {
                Anchor anchor = anchorFromLocation(it->getSourceRange().getBegin());
                auto &val = it->getInitVal();

                enum_type->addField(
                    EnumType::Field(it->getName().data(),
                        val.getExtValue(), anchor));
            }

            enum_defined[ed] = true;
        }

        return enum_type;
    }

    Type *TranslateType(clang::QualType T) {
        using namespace clang;

        const clang::Type *Ty = T.getTypePtr();

        switch (Ty->getTypeClass()) {
        case clang::Type::Elaborated: {
            const ElaboratedType *et = dyn_cast<ElaboratedType>(Ty);
            return TranslateType(et->getNamedType());
        } break;
        case clang::Type::Paren: {
            const ParenType *pt = dyn_cast<ParenType>(Ty);
            return TranslateType(pt->getInnerType());
        } break;
        case clang::Type::Typedef: {
            const TypedefType *tt = dyn_cast<TypedefType>(Ty);
            TypedefNameDecl * td = tt->getDecl();
            auto it = typedefs.find(td->getName().data());
            assert (it != typedefs.end());
            return it->second;
        } break;
        case clang::Type::Record: {
            const RecordType *RT = dyn_cast<RecordType>(Ty);
            RecordDecl * rd = RT->getDecl();
            return TranslateRecord(rd);
        }  break;
        case clang::Type::Enum: {
            const clang::EnumType *ET = dyn_cast<clang::EnumType>(Ty);
            EnumDecl * ed = ET->getDecl();
            return TranslateEnum(ed);
        } break;
        case clang::Type::Builtin:
            switch (cast<BuiltinType>(Ty)->getKind()) {
            case clang::BuiltinType::Void: {
                return Type::Void;
            } break;
            case clang::BuiltinType::Bool: {
                return Type::Bool;
            } break;
            case clang::BuiltinType::Char_S:
            case clang::BuiltinType::Char_U:
            case clang::BuiltinType::SChar:
            case clang::BuiltinType::UChar:
            case clang::BuiltinType::Short:
            case clang::BuiltinType::UShort:
            case clang::BuiltinType::Int:
            case clang::BuiltinType::UInt:
            case clang::BuiltinType::Long:
            case clang::BuiltinType::ULong:
            case clang::BuiltinType::LongLong:
            case clang::BuiltinType::ULongLong:
            case clang::BuiltinType::WChar_S:
            case clang::BuiltinType::WChar_U:
            case clang::BuiltinType::Char16:
            case clang::BuiltinType::Char32: {
                int sz = Context->getTypeSize(T);
                return Type::Integer(sz, !Ty->isUnsignedIntegerType());
            } break;
            case clang::BuiltinType::Half: {
                return Type::Half;
            } break;
            case clang::BuiltinType::Float: {
                return Type::Float;
            } break;
            case clang::BuiltinType::Double: {
                return Type::Double;
            } break;
            case clang::BuiltinType::LongDouble:
            case clang::BuiltinType::NullPtr:
            case clang::BuiltinType::UInt128:
            default:
                break;
            }
        case clang::Type::Complex:
        case clang::Type::LValueReference:
        case clang::Type::RValueReference:
            break;
        case clang::Type::Pointer: {
            const clang::PointerType *PTy = cast<clang::PointerType>(Ty);
            QualType ETy = PTy->getPointeeType();
            Type *pointee = TranslateType(ETy);
            if (pointee != NULL) {
                return Type::Pointer(pointee);
            }
        } break;
        case clang::Type::VariableArray:
        case clang::Type::IncompleteArray:
            break;
        case clang::Type::ConstantArray: {
            const ConstantArrayType *ATy = cast<ConstantArrayType>(Ty);
            Type *at = TranslateType(ATy->getElementType());
            if(at) {
                int sz = ATy->getSize().getZExtValue();
                Type::Array(at, sz);
            }
        } break;
        case clang::Type::ExtVector:
        case clang::Type::Vector: {
                const clang::VectorType *VT = cast<clang::VectorType>(T);
                Type *at = TranslateType(VT->getElementType());
                if(at) {
                    int n = VT->getNumElements();
                    return Type::Vector(at, n);
                }
        } break;
        case clang::Type::FunctionNoProto:
        case clang::Type::FunctionProto: {
            const clang::FunctionType *FT = cast<clang::FunctionType>(Ty);
            if (FT) {
                return TranslateFuncType(FT);
            }
        } break;
        case clang::Type::ObjCObject: break;
        case clang::Type::ObjCInterface: break;
        case clang::Type::ObjCObjectPointer: break;
        case clang::Type::BlockPointer:
        case clang::Type::MemberPointer:
        case clang::Type::Atomic:
        default:
            break;
        }
        fprintf(stderr, "type not understood: %s (%i)\n",
            T.getAsString().c_str(),
            Ty->getTypeClass());

        return NULL;
    }

    Type *TranslateFuncType(const clang::FunctionType * f) {

        bool valid = true; // decisions about whether this function can be exported or not are delayed until we have seen all the potential problems
        clang::QualType RT = f->getReturnType();

        Type *returntype = TranslateType(RT);

        if (!returntype)
            valid = false;

        bool vararg = false;

        const clang::FunctionProtoType * proto = f->getAs<clang::FunctionProtoType>();
        //proto is null if the function was declared without an argument list (e.g. void foo() and not void foo(void))
        //we don't support old-style C parameter lists, we just treat them as empty
        std::vector<Type *> argtypes;
        if(proto) {
            vararg = proto->isVariadic();
            for(size_t i = 0; i < proto->getNumParams(); i++) {
                clang::QualType PT = proto->getParamType(i);
                Type *paramtype = TranslateType(PT);
                if(!paramtype) {
                    valid = false; //keep going with attempting to parse type to make sure we see all the reasons why we cannot support this function
                } else if(valid) {
                    argtypes.push_back(paramtype);
                }
            }
        }

        if(valid) {
            return Type::CFunction(returntype, argtypes, vararg);
        }

        return NULL;
    }

    void exportType(const std::string &name, Type *type, const Anchor &anchor) {
        dest->addField(
            PointerValue::create(type, Type::TypePointer),
            StructType::Field(
                name,
                Type::TypePointer,
                anchor));
    }

    void exportExternal(const std::string &name, Type *type, const Anchor &anchor) {
        dest->addField(
            ExternalValue::create(name, type),
            StructType::Field(
                name,
                type,
                anchor));
    }

    bool TraverseRecordDecl(clang::RecordDecl *rd) {
        if (rd->isFreeStanding()) {
            auto type = llvm::cast<StructType>(TranslateRecord(rd));
            if (!type->isUnnamed()) {
                exportType(type->getName(), type, type->getAnchor());
            }
        }
        return true;
    }

    bool TraverseEnumDecl(clang::EnumDecl *ed) {
        if (ed->isFreeStanding()) {
            auto type = llvm::cast<EnumType>(TranslateEnum(ed));
            if (!type->isUnnamed()) {
                exportType(type->getName(), type, type->getAnchor());
            }
        }
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *vd) {
        if (vd->isExternC()) {
            Anchor anchor = anchorFromLocation(vd->getSourceRange().getBegin());

            Type *type = TranslateType(vd->getType());
            if (!type) return true;

            exportExternal(vd->getName().data(), type, anchor);
        }

        return true;

    }

    bool TraverseTypedefDecl(clang::TypedefDecl *td) {

        Anchor anchor = anchorFromLocation(td->getSourceRange().getBegin());

        Type *type = TranslateType(td->getUnderlyingType());
        if (!type) return true;

        typedefs[td->getName().data()] = type;
        exportType(td->getName().data(), type, anchor);

        return true;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *f) {
        clang::DeclarationName DeclName = f->getNameInfo().getName();
        std::string FuncName = DeclName.getAsString();
        const clang::FunctionType * fntyp = f->getType()->getAs<clang::FunctionType>();

        if(!fntyp)
            return true;

        if(f->getStorageClass() == clang::SC_Static) {
            return true;
        }

        Type *functype = TranslateFuncType(fntyp);
        if (!functype)
            return true;

        std::string InternalName = FuncName;
        clang::AsmLabelAttr * asmlabel = f->getAttr<clang::AsmLabelAttr>();
        if(asmlabel) {
            InternalName = asmlabel->getLabel();
            #ifndef __linux__
                //In OSX and Windows LLVM mangles assembler labels by adding a '\01' prefix
                InternalName.insert(InternalName.begin(), '\01');
            #endif
        }

        Anchor anchor = anchorFromLocation(f->getSourceRange().getBegin());

        exportExternal(FuncName.c_str(), functype, anchor);

        return true;
    }
};

class CodeGenProxy : public clang::ASTConsumer {
public:
    StructValue *dest;
    CVisitor visitor;

    CodeGenProxy(StructValue *dest_) : dest(dest_) {}
    virtual ~CodeGenProxy() {}

    virtual void Initialize(clang::ASTContext &Context) {
        visitor.SetContext(&Context, dest);
    }

    virtual bool HandleTopLevelDecl(clang::DeclGroupRef D) {
        for (clang::DeclGroupRef::iterator b = D.begin(), e = D.end(); b != e; ++b)
            visitor.TraverseDecl(*b);
        return true;
    }
};

// see ASTConsumers.h for more utilities
class BangEmitLLVMOnlyAction : public clang::EmitLLVMOnlyAction {
public:
    StructValue *dest;

    BangEmitLLVMOnlyAction(StructValue *dest_) :
        EmitLLVMOnlyAction((llvm::LLVMContext *)LLVMGetGlobalContext()),
        dest(dest_)
    {
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                 clang::StringRef InFile) override {

        std::vector< std::unique_ptr<clang::ASTConsumer> > consumers;
        consumers.push_back(clang::EmitLLVMOnlyAction::CreateASTConsumer(CI, InFile));
        consumers.push_back(llvm::make_unique<CodeGenProxy>(dest));
        return llvm::make_unique<clang::MultiplexConsumer>(std::move(consumers));
    }
};

static StructValue *importCModule (
    const std::string &path, const std::vector<std::string> &args,
    const char *buffer = nullptr) {
    using namespace clang;

    std::vector<const char *> aargs;
    aargs.push_back("clang");
    aargs.push_back(path.c_str());
    for (size_t i = 0; i < args.size(); ++i) {
        aargs.push_back(args[i].c_str());
    }

    CompilerInstance compiler;
    compiler.setInvocation(createInvocationFromCommandLine(aargs));

    if (buffer) {
        auto &opts = compiler.getPreprocessorOpts();

        llvm::MemoryBuffer * membuffer =
            llvm::MemoryBuffer::getMemBuffer(buffer, "<buffer>").release();

        opts.addRemappedFile(path, membuffer);
    }

    // Create the compilers actual diagnostics engine.
    compiler.createDiagnostics();

    // Infer the builtin include path if unspecified.
    //~ if (compiler.getHeaderSearchOpts().UseBuiltinIncludes &&
        //~ compiler.getHeaderSearchOpts().ResourceDir.empty())
        //~ compiler.getHeaderSearchOpts().ResourceDir =
            //~ CompilerInvocation::GetResourcesPath(bangra_argv[0], MainAddr);

    LLVMModuleRef M = NULL;


    auto result = StructValue::create({},
        llvm::cast<StructType>(Type::Struct("", false)));

    // Create and execute the frontend to generate an LLVM bitcode module.
    std::unique_ptr<CodeGenAction> Act(new BangEmitLLVMOnlyAction(result));
    if (compiler.ExecuteAction(*Act)) {
        M = (LLVMModuleRef)Act->takeModule().release();
        assert(M);
        return result;
    } else {
        ilError(nullptr, "compilation failed");
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// BUILTINS
//------------------------------------------------------------------------------

static bool builtin_checkparams (const std::vector<ConstantValue *> &args,
    int mincount, int maxcount) {
    if ((mincount <= 0) && (maxcount == -1))
        return true;

    int argcount = (int)args.size();

    if ((maxcount >= 0) && (argcount > maxcount)) {
        ilError(nullptr,
            "excess argument. At most %i arguments expected", maxcount);
        return false;
    }
    if ((mincount >= 0) && (argcount < mincount)) {
        ilError(nullptr, "at least %i arguments expected", mincount);
        return false;
    }
    return true;
}

static bool extract_bool(const ConstantValue *value) {
    if (auto resulttype = llvm::dyn_cast<IntegerValue>(value)) {
        if (getType(resulttype) == Type::Bool) {
            return (bool)resulttype->value;
        }
    }
    ilError(value, "boolean expected");
    return false;
}

static std::string extract_string(const ConstantValue *value) {
    auto resulttype = llvm::dyn_cast<StringValue>(value);
    if (!resulttype) {
        ilError(value, "string constant expected");
    }
    return resulttype->value;
}

static Type *extract_type(const ConstantValue *value) {
    auto resulttype = llvm::dyn_cast<PointerValue>(value);
    if (!resulttype) {
        ilError(value, "pointer constant expected");
    }
    if (getType(resulttype) != Type::TypePointer) {
        ilError(value, "pointer type constant expected");
    }
    return (Type *)resulttype->value;
}

static const std::vector<ConstantValue *> &extract_tuple(
    const ConstantValue *value) {
    auto resulttype = llvm::dyn_cast<TupleValue>(value);
    if (!resulttype) {
        ilError(value, "tuple expected");
    }
    return resulttype->values;
}

static PointerValue *wrap(Type *type) {
    return PointerValue::create(type, Type::TypePointer);
}

static IntegerValue *wrap(bool value) {
    return IntegerValue::create((int64_t)value,
        static_cast<IntegerType *>(Type::Bool));
}

static IntegerValue *wrap(int64_t value) {
    return IntegerValue::create(value,
        static_cast<IntegerType *>(Type::Int64));
}

static TupleValue *wrap(
    const std::vector<ConstantValue *> &args) {
    return TupleValue::create(args);
}

static RealValue *wrap(double value) {
    return RealValue::create(value,
        static_cast<RealType *>(Type::Double));
}

static BuiltinValue *wrap(ILBuiltinFunction func) {
    return BuiltinValue::create(func);
}

static StringValue *wrap(const std::string &s) {
    return StringValue::create(s);
}

static ConstantValue *builtin_print(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 0, -1);
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0)
            std::cout << " ";
        auto &arg = args[i];
        switch(arg->kind) {
            case Value::String: {
                auto cs = llvm::cast<StringValue>(arg);
                std::cout << cs->value;
            } break;
            default: {
                std::cout << getRefRepr(args[i]);
            } break;
        }
    }
    std::cout << "\n";
    return TupleValue::create({});
}

static ConstantValue *builtin_repr(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 1, 1);
    return wrap(getRepr(args[0]));
}

static ConstantValue *builtin_tupleof(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 0, -1);
    return wrap(args);
}

static ConstantValue *builtin_cdecl(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 3, 3);
    Type *rettype = extract_type(args[0]);
    const std::vector<ConstantValue *> &params = extract_tuple(args[1]);
    bool vararg = extract_bool(args[2]);

    std::vector<Type *> paramtypes;
    size_t paramcount = params.size();
    for (size_t i = 0; i < paramcount; ++i) {
        paramtypes.push_back(extract_type(params[i]));
    }
    return wrap(Type::CFunction(rettype, paramtypes, vararg));
}

static ConstantValue *builtin_external(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 2, 2);
    std::string name = extract_string(args[0]);
    Type *type = extract_type(args[1]);
    return ExternalValue::create(name, type);
}

// (import-c const-path (tupleof const-string ...))
static ConstantValue *builtin_import_c(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 2, 2);
    std::string path = extract_string(args[0]);
    auto compile_args = extract_tuple(args[1]);
    std::vector<std::string> cargs;
    for (size_t i = 0; i < compile_args.size(); ++i) {
        cargs.push_back(extract_string(compile_args[i]));
    }
    return bangra::importCModule(path, cargs);
}

static ConstantValue *builtin_at_op(const std::vector<ConstantValue *> &args) {
    builtin_checkparams(args, 2, 2);
    ConstantValue *obj = args[0];
    ConstantValue *key = args[1];
    switch(obj->kind) {
        case Value::Struct: {
            auto cs = llvm::cast<StructValue>(obj);
            auto t = llvm::cast<StructType>(getType(cs));
            switch(key->kind) {
                case Value::Integer: {
                    auto ci = llvm::cast<IntegerValue>(key);
                    if ((size_t)ci->value < t->getFieldCount()) {
                        return cs->values[ci->value];
                    } else {
                        ilError(key, "index out of bounds");
                        return nullptr;
                    }
                } break;
                case Value::String: {
                    auto cstr = llvm::cast<StringValue>(key);
                    size_t idx = t->getFieldIndex(cstr->value);
                    if (idx != (size_t)-1) {
                        return cs->values[idx];
                    } else {
                        ilError(key, "no such member");
                        return nullptr;
                    }
                } break;
                default: {
                    ilError(key, "illegal key type");
                    return nullptr;
                } break;
            }
        } break;
        default: {
            ilError(obj, "unsubscriptable type");
            return nullptr;
        } break;
    }

}


template<typename A, typename B>
class cast_join_type {};
template<typename T> class cast_join_type<T, T> {
    public: typedef T return_type; };
template<> class cast_join_type<int64_t, double> {
    public: typedef double return_type; };
template<> class cast_join_type<double, int64_t> {
    public: typedef double return_type; };

class builtin_add_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a + b; }
};
class builtin_sub_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a - b; }
};
class builtin_mul_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a * b; }
};
class builtin_div_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return a / b; }
    static double operate(const int64_t &a, const int64_t &b) {
            return (double)a / (double)b; }
};
class builtin_mod_op { public:
    template<typename Ta, typename Tb>
    static typename cast_join_type<Ta,Tb>::return_type
        operate(const Ta &a, const Tb &b) { return fmod(a,b); }
};

class builtin_bitand_op { public:
    template<typename T>
    static T operate(const T &a, const T &b) { return a & b; }
};
class builtin_bitor_op { public:
    template<typename T>
    static T operate(const T &a, const T &b) { return a | b; }
};
class builtin_bitxor_op { public:
    template<typename T>
    static T operate(const T &a, const T &b) { return a ^ b; }
};
class builtin_bitnot_op { public:
    template<typename T>
    static T operate(const T &x) { return ~x; }
};
class builtin_not_op { public:
    template<typename T>
    static T operate(const T &x) { return !x; }
};

class builtin_eq_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a == b; }
};
class builtin_ne_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a != b; }
};
class builtin_gt_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a > b; }
};
class builtin_ge_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a >= b; }
};
class builtin_lt_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a < b; }
};
class builtin_le_op { public:
    template<typename Ta, typename Tb>
    static bool operate(const Ta &a, const Tb &b) { return a <= b; }
};

template <class NextT>
class builtin_filter_op { public:
    static ConstantValue *operate(const double &a, const int64_t &b) {
        return wrap(NextT::operate(a, b));
    }
    static ConstantValue *operate(const int64_t &a, const double &b) {
        return wrap(NextT::operate(a, b));
    }
    template<typename T>
    static ConstantValue *operate(const T &a, const T &b) {
        return wrap(NextT::operate(a, b));
    }
    template<typename Ta, typename Tb>
    static ConstantValue *operate(const Ta &a, const Tb &b) {
        ilError(nullptr, "illegal operands");
        return nullptr;
    }
};


class dispatch_types_failed {
public:
    template<typename F>
    static ConstantValue *dispatch(const ConstantValue *v, const F &next) {
        ilError(v, "illegal operand");
        return nullptr;
    }
};

template<typename NextT>
class dispatch_string_type {
public:
    template<typename F>
    static ConstantValue *dispatch(const ConstantValue *v, const F &next) {
        if (v->kind == Value::String) {
            auto ca = llvm::cast<StringValue>(v);
            return next(ca->value);
        } else {
            return NextT::template dispatch<F>(v, next);
        }
    }
};

template<typename NextT>
class dispatch_integer_type {
public:
    template<typename F>
    static ConstantValue *dispatch(const ConstantValue *v, const F &next) {
        if (v->kind == Value::Integer) {
            auto ca = llvm::cast<IntegerValue>(v);
            if (ca->value_type != Type::Bool) {
                return next(ca->value);
            }
        }
        return NextT::template dispatch<F>(v, next);
    }
};

template<typename NextT>
class dispatch_bool_type {
public:
    template<typename F>
    static ConstantValue *dispatch(const ConstantValue *v, const F &next) {
        if (v->kind == Value::Integer) {
            auto ca = llvm::cast<IntegerValue>(v);
            if (ca->value_type == Type::Bool) {
                return next((bool)ca->value);
            }
        }
        return NextT::template dispatch<F>(v, next);
    }
};

template<typename NextT>
class dispatch_real_type {
public:
    template<typename F>
    static ConstantValue *dispatch(const ConstantValue *v, const F &next) {
        if (v->kind == Value::Real) {
            auto ca = llvm::cast<RealValue>(v);
            return next(ca->value);
        } else {
            return NextT::template dispatch<F>(v, next);
        }
    }
};

typedef dispatch_integer_type<
    dispatch_real_type <
        dispatch_types_failed> >
    dispatch_arithmetic_types;

typedef dispatch_integer_type<
            dispatch_real_type<
                dispatch_types_failed> >
    dispatch_arith_types;
typedef dispatch_integer_type<
            dispatch_real_type<
                dispatch_string_type<
                    dispatch_types_failed> > >
    dispatch_arith_string_types;
typedef dispatch_arithmetic_types dispatch_arith_cmp_types;
typedef dispatch_arith_string_types dispatch_cmp_types;

typedef dispatch_integer_type<dispatch_types_failed>
    dispatch_bit_types;
typedef dispatch_bool_type<dispatch_types_failed>
    dispatch_boolean_types;

template<class D, class F>
class builtin_binary_op1 {
public:
    template<typename Q>
    ConstantValue *operator ()(const Q &ca_value) const {
        return wrap(F::operate(ca_value));
    }
};

template<class D, class F, typename T>
class builtin_binary_op3 {
public:
    const T &ca_value;
    builtin_binary_op3(const T &ca_value_) : ca_value(ca_value_) {}

    template<typename Q>
    ConstantValue *operator ()(const Q &cb_value) const {
        return builtin_filter_op<F>::operate(ca_value, cb_value);
    }
};

template<class D, class F>
class builtin_binary_op2 {
public:
    const ConstantValue *b;
    builtin_binary_op2(const ConstantValue *b_) : b(b_) {}

    template<typename T>
    ConstantValue *operator ()(const T &ca_value) const {
        return D::dispatch(b, builtin_binary_op3<D, F, T>(ca_value));
    }
};

template<class D, class F>
static ConstantValue *builtin_binary_op(
    const std::vector<ConstantValue *> &args) {
    if (args.size() != 2) {
        ilError(nullptr, "invalid number of arguments");
    }
    return D::dispatch(args[0], builtin_binary_op2<D, F>(args[1]));
}


template<class D, class F>
static ConstantValue *builtin_unary_op(
    const std::vector<ConstantValue *> &args) {
    if (args.size() != 1) {
        ilError(nullptr, "invalid number of arguments");
    }
    return D::dispatch(args[0], builtin_binary_op1<D, F>());
}

//------------------------------------------------------------------------------
// TRANSLATION
//------------------------------------------------------------------------------

struct Environment;
typedef ConstantValue *(*bangra_preprocessor)(Environment *, ConstantValue *);

typedef std::map<std::string, bangra_preprocessor> NameMacroMap;
typedef std::unordered_map<std::string, Type *> NameTypeMap;
typedef std::unordered_map<std::string, Value *> NameValueMap;

//------------------------------------------------------------------------------

struct GlobalEnvironment {
    ILBuilder builder;
};

struct Environment {
protected:
    NameValueMap values;
public:
    GlobalEnvironment &global;
    Environment *parent;

    Environment(GlobalEnvironment &global_) :
        global(global_),
        parent(nullptr)
    {
        assert(!values.size());
    }

    Environment(Environment &parent_) :
        global(parent_.global),
        parent(&parent_)
    {
        assert(!values.size());
    }

    const NameValueMap &getLocals() {
        return values;
    }

    void dump() {
        printf("%i values\n",
            (int)values.size());
        for (auto entry : values) {
            ilMessage(entry.second, "%p.%s\n",
                (void *)this, entry.first.c_str());
            assert(entry.second);
        }
    }

    bool isLocal(const std::string &name) {
        return values.count(name);
    }

    Value *getLocal(const std::string &name) {
        if (values.count(name)) {
            return values.at(name);
        }
        return nullptr;
    }

    /*
    bool set(const std::string &name, Value *value) {
        if (isLocal(name)) {
            replaceLocal(name, value);
        } else {
            if (resolve(name)) {
                replaceScoped(name, value);
            } else {
                return false;
            }
        }
        return true;
    }
    */

    void setLocal(const std::string &name, Value *value) {
        assert(!isLocal(name));
        assert(value);
        values[name] = value;
    }

    void replaceLocal(const std::string &name, Value *value) {
        assert(isLocal(name));
        assert(value);
        values[name] = value;
    }

    Value *resolve(const std::string &name) {
        Environment *penv = this;
        while (penv) {
            Value *result = (*penv).getLocal(name);
            if (result) {
                return result;
            }
            penv = penv->parent;
        }
        return nullptr;
    }

};

static std::unordered_map<std::string, bangra_preprocessor> preprocessors;

//------------------------------------------------------------------------------

static bool isSymbol (const ConstantValue *expr, const char *sym) {
    if (expr) {
        if (auto symexpr = llvm::dyn_cast<SymbolValue>(expr))
            return (symexpr->value == sym);
    }
    return false;
}

//------------------------------------------------------------------------------

Value *translate(Environment &env, ConstantValue *expr);

void valueError (ConstantValue *expr, const char *format, ...) {
    const Anchor *anchor = find_valid_anchor(expr);
    if (!anchor) {
        if (expr)
            printValue(expr);
    }
    va_list args;
    va_start (args, format);
    Anchor::printErrorV(anchor, format, args);
    va_end (args);
}

void valueErrorV (ConstantValue *expr, const char *fmt, va_list args) {
    const Anchor *anchor = find_valid_anchor(expr);
    if (!anchor) {
        if (expr)
            printValue(expr);
    }
    Anchor::printErrorV(anchor, fmt, args);
}

template <typename T>
static T *astVerifyKind(ConstantValue *expr) {
    T *obj = expr?llvm::dyn_cast<T>(expr):NULL;
    if (obj) {
        return obj;
    } else {
        valueError(expr, "%s expected, not %s",
            getClassName(T::classkind()),
            getClassName(expr->kind));
    }
    return nullptr;
}

struct TupleIter {
protected:
    TupleValue *tuple;
    size_t index;
public:
    size_t get_index() const {
        return index;
    }

    TupleIter(TupleValue *value, size_t i) :
        tuple(value),
        index(i)
    {}

    TupleIter(ConstantValue *value, size_t i=0) :
        tuple(llvm::cast<TupleValue>(value)),
        index(i) {
    }

    void next() {
        index++;
    }

    operator bool() const {
        return index < tuple->values.size();
    }

    ConstantValue *operator *() const {
        assert(index < tuple->values.size());
        return tuple->values[index];
    }

};

static Value *parse_do (Environment &env, ConstantValue *expr, size_t offset) {
    TupleIter it(expr, offset);

    Environment subenv(env);

    Value *value = nullptr;
    while (it) {
        value = translate(subenv, *it);
        it.next();
    }

    if (value)
        return value;
    else
        return TupleValue::create({});

}

static Value *parse_do (Environment &env, ConstantValue *expr) {
    return parse_do(env, expr, 1);
}

static Value *parse_function (Environment &env, ConstantValue *expr) {
    TupleIter it(expr, 1);
    auto expr_parameters = *it; it.next();

    auto currentblock = env.global.builder.continuation;

    auto function = FlowValue::create(0, "func");

    env.global.builder.continueAt(function);
    Environment subenv(env);

    subenv.setLocal("this-function", function);

    auto params = astVerifyKind<TupleValue>(expr_parameters);
    TupleIter param(params);
    while (param) {
        auto symname = astVerifyKind<SymbolValue>(*param);
        auto bp = ParameterValue::create();
        function->appendParameter(bp);
        subenv.setLocal(symname->value, bp);
        param.next();
    }
    auto ret = function->appendParameter(ParameterValue::create());

    auto result = parse_do(subenv, expr, 2);

    env.global.builder.br({ret, result});

    env.global.builder.continueAt(currentblock);

    return function;
}

static Value *parse_proto_eval (Environment &env, ConstantValue *expr) {
    TupleIter it(expr, 1);
    auto expr_protoeval = *it; it.next();

    auto currentblock = env.global.builder.continuation;

    auto mainfunc = FlowValue::create();
    auto ret = mainfunc->appendParameter(ParameterValue::create());

    Environment subenv(env);
    subenv.global.builder.continueAt(mainfunc);

    auto retval = translate(subenv, expr_protoeval);
    subenv.global.builder.br({ ret, retval });

    auto result = execute({mainfunc});

    subenv.global.builder.continueAt(currentblock);

    return result;
}

static Value *parse_implicit_apply (Environment &env, ConstantValue *expr,
    size_t start = 0) {
    TupleIter it(expr, start);
    auto expr_callable = *it; it.next();

    Value *callable = translate(env, expr_callable);

    std::vector<Value *> args;
    args.push_back(callable);

    while (it) {
        args.push_back(translate(env, *it));
        it.next();
    }

    return env.global.builder.call(args);
}

static Value *parse_apply (Environment &env, ConstantValue *expr) {
    return parse_implicit_apply(env, expr, 1);
}

bool hasTypeValue(Type *type) {
    assert(type);
    if ((type == Type::Void) || (type == Type::Empty))
        return false;
    return true;
}

static Value *parse_select (Environment &env, ConstantValue *expr) {
    TupleIter it(expr, 1);
    auto expr_condition = *it; it.next();
    auto expr_true = *it; it.next();
    auto expr_false = *it; it.next();

    Value *condition = translate(env, expr_condition);
    auto bbstart = env.global.builder.continuation;

    auto bbtrue = FlowValue::create(0, "then");
    env.global.builder.continueAt(bbtrue);

    Environment subenv_true(env);
    Value *trueexpr = translate(subenv_true, expr_true);
    auto bbtrue_end = env.global.builder.continuation;

    bool returnValue = hasTypeValue(getType(trueexpr));

    FlowValue *bbfalse = nullptr;
    FlowValue *bbfalse_end = nullptr;
    Value *falseexpr = nullptr;
    if (expr_false) {
        Environment subenv_false(env);
        bbfalse = FlowValue::create(0, "else");
        env.global.builder.continueAt(bbfalse);
        falseexpr = translate(subenv_false, expr_false);
        bbfalse_end = env.global.builder.continuation;
        returnValue = returnValue && hasTypeValue(getType(falseexpr));
    } else {
        returnValue = false;
    }

    auto bbdone = FlowValue::create(0, "endif");
    Value *result;
    std::vector<Value *> trueexprs;
    trueexprs.push_back(bbdone);
    if (returnValue) {
        auto result_param = ParameterValue::create();
        bbdone->appendParameter(result_param);
        trueexprs.push_back(trueexpr);
        result = result_param;
    } else {
        result = TupleValue::create({});
    }

    env.global.builder.continueAt(bbtrue_end);
    env.global.builder.br(trueexprs);

    if (bbfalse) {
        env.global.builder.continueAt(bbfalse_end);
        std::vector<Value *> falsexprs;
        falsexprs.push_back(bbdone);
        if (returnValue)
            falsexprs.push_back(falseexpr);
        env.global.builder.br(falsexprs);
    } else {
        bbfalse = bbdone;
    }

    env.global.builder.continueAt(bbstart);
    env.global.builder.br(
        { IntrinsicValue::Branch, condition, bbtrue, bbfalse });

    env.global.builder.continueAt(bbdone);

    return result;
}

static Value *parse_let(Environment &env, ConstantValue *expr) {
    TupleIter it(expr, 1);
    auto expr_sym = *it; it.next();
    auto expr_value = *it; it.next();

    auto symname = astVerifyKind<SymbolValue>(expr_sym);
    Value *value;

    if (expr_value)
        value = translate(env, expr_value);
    else
        value = TupleValue::create({});

    if (env.isLocal(symname->value)) {
        valueError(symname, "already defined");
    }
    env.setLocal(symname->value, value);

    return value;
}

struct TranslateTable {
    typedef Value *(*TranslatorFunc)(Environment &env, ConstantValue *expr);

    struct Translator {
        int mincount;
        int maxcount;
        unsigned flags;

        TranslatorFunc translate;

        Translator() :
            mincount(-1),
            maxcount(-1),
            flags(0),
            translate(NULL)
            {}

    };

    std::unordered_map<std::string, Translator> translators;

    void set(TranslatorFunc translate, const std::string &name,
        int mincount, int maxcount, unsigned flags=0) {
        Translator translator;
        translator.mincount = mincount;
        translator.maxcount = maxcount;
        translator.translate = translate;
        translator.flags = flags;
        translators[name] = translator;
    }

    static bool verifyParameterCount (TupleValue *expr,
        int mincount, int maxcount) {
        if ((mincount <= 0) && (maxcount == -1))
            return true;
        int argcount = (int)expr->values.size() - 1;

        if ((maxcount >= 0) && (argcount > maxcount)) {
            valueError(expr->values[maxcount + 1],
                "excess argument. At most %i arguments expected", maxcount);
            return false;
        }
        if ((mincount >= 0) && (argcount < mincount)) {
            valueError(expr, "at least %i arguments expected", mincount);
            return false;
        }
        return true;
    }

    TranslatorFunc match(TupleValue *expr) {
        auto head = astVerifyKind<SymbolValue>(expr->values.front());
        auto &t = translators[head->value];
        if (!t.translate) return nullptr;
        verifyParameterCount(expr, t.mincount, t.maxcount);
        return t.translate;
    }

};

static TranslateTable translators;

//------------------------------------------------------------------------------

static void registerTranslators() {
    auto &t = translators;
    t.set(parse_let, "let", 1, 2);
    t.set(parse_apply, "apply", 1, -1);
    t.set(parse_do, "do", 0, -1);
    t.set(parse_select, "select", 2, 3);
    t.set(parse_function, "function", 1, -1);
    t.set(parse_proto_eval, "proto-eval", 1, 1);
}

static Value *translateFromList (Environment &env, TupleValue *expr) {
    assert(expr);
    if (expr->values.size() < 1) {
        valueError(expr, "symbol expected");
    }
    auto func = translators.match(expr);
    if (func) {
        return func(env, expr);
    } else {
        return parse_implicit_apply(env, expr);
    }
}

Value *translate (Environment &env, ConstantValue *expr) {
    assert(expr);
    Value *result = nullptr;
    switch(expr->kind) {
        case Value::Tuple: {
            result = translateFromList(env, llvm::cast<TupleValue>(expr));
        } break;
        case Value::Symbol: {
            auto sym = llvm::cast<SymbolValue>(expr);
            std::string value = sym->value;
            result = env.resolve(value);
            if (!result) {
                valueError(expr,
                    "unknown symbol '%s'", value.c_str());
            }
        } break;
        default: {
            result = expr;
        } break;
    }
    if (result && !result->anchor.isValid()) {
        const Anchor *anchor = find_valid_anchor(expr);
        if (anchor) {
            result->anchor = *anchor;
        }
    }
    assert(result);
    return result;
}

//------------------------------------------------------------------------------
// FILE SYSTEM
//------------------------------------------------------------------------------

struct ModuleLoader {
    std::unordered_map<std::string, StructValue *> modules;

    ModuleLoader()
    {}

    StructValue *import_module(const std::string &name) {
        auto it = modules.find(name);
        if (it != modules.end()) {
            return it->second;
        }
        return nullptr;
    }
};

ModuleLoader loader;


//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

static void init() {
    bangra::support_ansi = isatty(fileno(stdout));

    Type::initTypes();
    IntrinsicValue::initIntrinsics();
    registerTranslators();

    LLVMEnablePrettyStackTrace();
    LLVMLinkInMCJIT();
    //LLVMLinkInInterpreter();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeDisassembler();

    ffi = new FFI();

}

//------------------------------------------------------------------------------

static void setupRootEnvironment (Environment &env) {
    env.setLocal("void", wrap(Type::Void));
    env.setLocal("null", wrap(Type::Null));
    env.setLocal("half", wrap(Type::Half));
    env.setLocal("float", wrap(Type::Float));
    env.setLocal("double", wrap(Type::Double));
    env.setLocal("bool", wrap(Type::Bool));

    env.setLocal("int8", wrap(Type::Int8));
    env.setLocal("int16", wrap(Type::Int16));
    env.setLocal("int32", wrap(Type::Int32));
    env.setLocal("int64", wrap(Type::Int64));

    env.setLocal("uint8", wrap(Type::UInt8));
    env.setLocal("uint16", wrap(Type::UInt16));
    env.setLocal("uint32", wrap(Type::UInt32));
    env.setLocal("uint64", wrap(Type::UInt64));

    env.setLocal("usize_t",
        wrap(Type::Integer(sizeof(size_t)*8,false)));

    env.setLocal("rawstring", wrap(Type::Rawstring));

    env.setLocal("int", env.resolve("int32"));

    auto booltype = llvm::cast<IntegerType>(Type::Bool);
    env.setLocal("true", IntegerValue::create(1, booltype));
    env.setLocal("false", IntegerValue::create(0, booltype));

    env.setLocal("print", wrap(builtin_print));
    env.setLocal("repr", wrap(builtin_repr));
    env.setLocal("cdecl", wrap(builtin_cdecl));
    env.setLocal("tupleof", wrap(builtin_tupleof));
    env.setLocal("external", wrap(builtin_external));
    env.setLocal("import-c", wrap(builtin_import_c));

    env.setLocal("@", wrap(builtin_at_op));

    env.setLocal("+",
        wrap(builtin_binary_op<dispatch_arith_string_types, builtin_add_op>));
    env.setLocal("-",
        wrap(builtin_binary_op<dispatch_arith_types, builtin_sub_op>));
    env.setLocal("*",
        wrap(builtin_binary_op<dispatch_arith_types, builtin_mul_op>));
    env.setLocal("/",
        wrap(builtin_binary_op<dispatch_arith_types, builtin_div_op>));
    env.setLocal("%",
        wrap(builtin_binary_op<dispatch_arith_types, builtin_mod_op>));

    env.setLocal("&",
        wrap(builtin_binary_op<dispatch_bit_types, builtin_bitand_op>));
    env.setLocal("|",
        wrap(builtin_binary_op<dispatch_bit_types, builtin_bitor_op>));
    env.setLocal("^",
        wrap(builtin_binary_op<dispatch_bit_types, builtin_bitxor_op>));
    env.setLocal("~",
        wrap(builtin_unary_op<dispatch_bit_types, builtin_bitnot_op>));

    env.setLocal("not",
        wrap(builtin_unary_op<dispatch_boolean_types, builtin_not_op>));

    env.setLocal("==",
        wrap(builtin_binary_op<dispatch_cmp_types, builtin_eq_op>));
    env.setLocal("!=",
        wrap(builtin_binary_op<dispatch_cmp_types, builtin_ne_op>));
    env.setLocal(">",
        wrap(builtin_binary_op<dispatch_cmp_types, builtin_gt_op>));
    env.setLocal(">=",
        wrap(builtin_binary_op<dispatch_cmp_types, builtin_ge_op>));
    env.setLocal("<",
        wrap(builtin_binary_op<dispatch_cmp_types, builtin_lt_op>));
    env.setLocal("<=",
        wrap(builtin_binary_op<dispatch_cmp_types, builtin_le_op>));

}

static void handleException(Environment &env, ConstantValue *expr) {
    streamValue(std::cerr, expr, 0, true);
    valueError(expr, "an exception was raised");
}

static bool translateRootValueList (Environment &env, ConstantValue *expr) {

    auto mainfunc = FlowValue::create();
    auto ret = mainfunc->appendParameter(ParameterValue::create());
    env.global.builder.continueAt(mainfunc);

    parse_do(env, expr);
    env.global.builder.br({ ret });

/*
#ifdef BANGRA_DEBUG_IL
    std::cout << env.global.module->getRepr();
    fflush(stdout);
#endif
*/

    execute({mainfunc});

    return true;
}

static bool compileMain (ConstantValue *expr) {
    assert(expr);
    auto tuple = astVerifyKind<TupleValue>(expr);

    GlobalEnvironment global;
    Environment env(global);
    setupRootEnvironment(env);

    std::string lastlang = "";
    while (true) {
        auto head = astVerifyKind<SymbolValue>(tuple->values.front());
        if (!head) return false;
        if (head->value == BANGRA_HEADER)
            break;
        auto preprocessor = preprocessors[head->value];
        if (!preprocessor) {
            valueError(expr, "unrecognized header: '%s'; try '%s' instead.",
                head->value.c_str(),
                BANGRA_HEADER);
            return false;
        }
        if (lastlang == head->value) {
            valueError(expr,
                "header has not changed after preprocessing; is still '%s'.",
                head->value.c_str());
        }
        lastlang = head->value;
        auto orig_expr = expr;
        try {
            expr = preprocessor(&env, expr);
        } catch (ConstantValue *expr) {
            handleException(env, expr);
            return false;
        }
        if (!expr) {
            valueError(orig_expr,
                "preprocessor returned null.");
            return false;
        }
    }

    return translateRootValueList (env, expr);
}

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
std::string GetExecutablePath(const char *Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *MainAddr = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

static ConstantValue *parseLoader(const char *executable_path) {
    // attempt to read bootstrap expression from end of binary
    auto file = MappedFile::open(executable_path);
    if (!file) {
        fprintf(stderr, "could not open binary\n");
        return NULL;
    }
    auto ptr = file->strptr();
    auto size = file->size();
    auto cursor = ptr + size - 1;
    while ((*cursor == '\n')
        || (*cursor == '\r')
        || (*cursor == ' ')) {
        // skip the trailing text formatting garbage
        // that win32 echo produces
        cursor--;
        if (cursor < ptr) return NULL;
    }
    if (*cursor != ')') return NULL;
    cursor--;
    // seek backwards to find beginning of expression
    while ((cursor >= ptr) && (*cursor != '('))
        cursor--;

    bangra::Parser footerParser;
    auto expr = footerParser.parseMemory(
        cursor, ptr + size, executable_path, cursor - ptr);
    if (!expr) {
        fprintf(stderr, "could not parse footer expression\n");
        return NULL;
    }
    if (expr->kind != Value::Tuple)  {
        fprintf(stderr, "footer expression is not a list\n");
        return NULL;
    }
    auto tuple = llvm::cast<TupleValue>(expr);
    if (tuple->values.size() < 2) {
        fprintf(stderr, "footer needs at least two arguments\n");
        return NULL;
    }
    auto head = tuple->values[0];
    if (head->kind != Value::Symbol)  {
        fprintf(stderr, "footer expression does not begin with symbol\n");
        return NULL;
    }
    if (!isSymbol(head, "script-size"))  {
        fprintf(stderr, "footer expression does not begin with 'script-size'\n");
        return NULL;
    }
    auto arg = tuple->values[1];
    if (arg->kind != Value::Integer)  {
        fprintf(stderr, "script-size argument is not integer\n");
        return NULL;
    }
    auto offset = llvm::cast<IntegerValue>(expr)->value;
    if (offset <= 0) {
        fprintf(stderr, "script-size must be larger than zero\n");
        return NULL;
    }
    bangra::Parser parser;
    auto script_start = cursor - offset;
    return parser.parseMemory(script_start, cursor, executable_path, script_start - ptr);
}

static bool compileStartupScript() {
    char *base = strdup(bang_executable_path);
    char *ext = extension(base);
    if (ext) {
        *ext = 0;
    }
    std::string path = format("%s.b", base);
    free(base);

    ConstantValue *expr = NULL;
    {
        auto file = MappedFile::open(path.c_str());
        if (file) {
            // keep a resident copy
            char *pathcpy = strdup(path.c_str());
            bangra::Parser parser;
            expr = parser.parseMemory(
                file->strptr(), file->strptr() + file->size(),
                pathcpy);
        }
    }

    if (expr) {
        return bangra::compileMain(expr);
    }

    return true;
}

} // namespace bangra

// C API
//------------------------------------------------------------------------------

char *bang_executable_path = NULL;
int bang_argc = 0;
char **bang_argv = NULL;

void print_version() {
    std::string versionstr = bangra::format("%i.%i",
        BANGRA_VERSION_MAJOR, BANGRA_VERSION_MINOR);
    if (BANGRA_VERSION_PATCH) {
        versionstr += bangra::format(".%i", BANGRA_VERSION_PATCH);
    }
    printf(
    "Bangra version %s\n"
    "Executable path: %s\n"
    , versionstr.c_str()
    , bang_executable_path
    );
    exit(0);
}

void print_help(const char *exename) {
    printf(
    "usage: %s [option [...]] [filename]\n"
    "Options:\n"
    "   -h, --help                  print this text and exit.\n"
    "   -v, --version               print program version and exit.\n"
    "   --skip-startup              skip startup script.\n"
    "   -a, --enable-ansi           enable ANSI output.\n"
    "   --                          terminate option list.\n"
    , exename
    );
    exit(0);
}

int bangra_main(int argc, char ** argv) {
    bang_argc = argc;
    bang_argv = argv;

    bangra::init();

    bangra::ConstantValue *expr = NULL;

    if (argv) {
        if (argv[0]) {
            std::string loader = bangra::GetExecutablePath(argv[0]);
            // string must be kept resident
            bang_executable_path = strdup(loader.c_str());

            expr = bangra::parseLoader(bang_executable_path);
        }

        if (!expr) {
            // running in interpreter mode
            char *sourcepath = NULL;
            // skip startup script
            bool skip_startup = false;

            if (argv[1]) {
                bool parse_options = true;

                char ** arg = argv;
                while (*(++arg)) {
                    if (parse_options && (**arg == '-')) {
                        if (!strcmp(*arg, "--help") || !strcmp(*arg, "-h")) {
                            print_help(argv[0]);
                        } else if (!strcmp(*arg, "--version") || !strcmp(*arg, "-v")) {
                            print_version();
                        } else if (!strcmp(*arg, "--skip-startup")) {
                            skip_startup = true;
                        } else if (!strcmp(*arg, "--enable-ansi") || !strcmp(*arg, "-a")) {
                            bangra::support_ansi = true;
                        } else if (!strcmp(*arg, "--")) {
                            parse_options = false;
                        } else {
                            printf("unrecognized option: %s. Try --help for help.\n", *arg);
                            exit(1);
                        }
                    } else if (!sourcepath) {
                        sourcepath = *arg;
                    } else {
                        printf("unrecognized argument: %s. Try --help for help.\n", *arg);
                        exit(1);
                    }
                }
            }

            if (!skip_startup && bang_executable_path) {
                if (!bangra::compileStartupScript()) {
                    return 1;
                }
            }

            if (sourcepath) {
                bangra::Parser parser;
                expr = parser.parseFile(sourcepath);
            }
        }
    }

    if (expr) {
        bangra::compileMain(expr);
    } else {
        return 1;
    }

    return 0;
}

bangra::ConstantValue *bangra_parse_file(const char *path) {
    bangra::Parser parser;
    return parser.parseFile(path);
}

//------------------------------------------------------------------------------

#endif // BANGRA_CPP_IMPL
#ifdef BANGRA_MAIN_CPP_IMPL

//------------------------------------------------------------------------------
// MAIN EXECUTABLE IMPLEMENTATION
//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    return bangra_main(argc, argv);
}

#endif // BANGRA_MAIN_CPP_IMPL
