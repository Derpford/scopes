/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#define SCOPESRT_IMPL

#include "boot.hpp"
#include "timer.hpp"
#include "gc.hpp"
#include "source_file.hpp"
#include "utils.hpp"
#include "lexerparser.hpp"
#include "type.hpp"
#include "list.hpp"
#include "execution.hpp"
#include "globals.hpp"
#include "error.hpp"
#include "scope.hpp"
#include "expander.hpp"
#include "prover.hpp"
#include "gen_llvm.hpp"
#include "stream_ast.hpp"
#include "value.hpp"
#include "compiler_flags.hpp"
#include "dyn_cast.inc"
#include "scopes/scopes.h"

#include "types.hpp"

#ifdef SCOPES_WIN32
#include "stdlib_ex.h"
#include "dlfcn.h"
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>

#if SCOPES_USE_WCHAR
#include <codecvt>
#endif

#include "llvm/ExecutionEngine/SectionMemoryManager.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <ostream>
#include <iterator>

namespace scopes {

//------------------------------------------------------------------------------
// SCOPES CORE
//------------------------------------------------------------------------------

/* this function looks for a header at the end of the compiler executable
   that indicates a scopes core.

   the header has the format (core-size <size>), where size is a i32 value
   holding the size of the core source file in bytes.

   the compiler uses this function to override the default scopes core 'core.sc'
   located in the compiler's directory.

   to later override the default core file and load your own, cat the new core
   file behind the executable and append the header, like this:

   $ cp scopes myscopes
   $ cat mycore.sc >> myscopes
   $ echo "(core-size " >> myscopes
   $ wc -c < mycore.sc >> myscopes
   $ echo ")" >> myscopes

   */

static SCOPES_RESULT(Value *) load_custom_core(const char *executable_path) {
    SCOPES_RESULT_TYPE(Value *);
    // attempt to read bootstrap expression from end of binary
    auto file = SourceFile::from_file(
        Symbol(String::from_cstr(executable_path)));
    if (!file) {
        SCOPES_LOCATION_ERROR(String::from("could not open binary"));
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
        if (cursor < ptr) return nullptr;
    }
    if (*cursor != ')') return nullptr;
    cursor--;
    // seek backwards to find beginning of expression
    while ((cursor >= ptr) && (*cursor != '('))
        cursor--;
    LexerParser footerParser(file, cursor - ptr);
    auto expr = SCOPES_GET_RESULT(extract_list_constant(SCOPES_GET_RESULT(footerParser.parse())));
    if (expr == EOL) {
        SCOPES_LOCATION_ERROR(String::from("footer parser returned illegal structure"));
    }
    auto it = SCOPES_GET_RESULT(extract_list_constant(expr->at));
    if (it == EOL) {
        SCOPES_LOCATION_ERROR(String::from("footer expression is empty"));
    }
    auto head = it->at;
    auto sym = SCOPES_GET_RESULT(extract_symbol_constant(head));
    if (sym != Symbol("core-size"))  {
        SCOPES_LOCATION_ERROR(String::from("footer expression does not begin with 'core-size'"));
    }
    it = it->next;
    if (it == EOL) {
        SCOPES_LOCATION_ERROR(String::from("footer expression needs two arguments"));
    }
    auto script_size = SCOPES_GET_RESULT(extract_integer_constant(it->at));
    if (script_size <= 0) {
        SCOPES_LOCATION_ERROR(String::from("script-size must be larger than zero"));
    }
    LexerParser parser(file, cursor - script_size - ptr, script_size);
    return parser.parse();
}

//------------------------------------------------------------------------------
// MAIN
//------------------------------------------------------------------------------

static bool terminal_supports_ansi() {
#ifdef SCOPES_WIN32
    if (isatty(STDOUT_FILENO))
        return true;
    return getenv("TERM") != nullptr;
#else
    //return isatty(fileno(stdout));
    return isatty(STDOUT_FILENO);
#endif
}

static void setup_stdio() {
    if (terminal_supports_ansi()) {
        stream_default_style = stream_ansi_style;
        #ifdef SCOPES_WIN32
        #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
        #endif

        // turn on ANSI code processing
        auto hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        auto hStdErr = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode;
        GetConsoleMode(hStdOut, &mode);
        SetConsoleMode(hStdOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        GetConsoleMode(hStdErr, &mode);
        SetConsoleMode(hStdErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        setbuf(stdout, 0);
        setbuf(stderr, 0);
#if SCOPES_USE_WCHAR
        _setmode(_fileno(stdout), _O_U16TEXT);
        _setmode(_fileno(stderr), _O_U16TEXT);
        //std::wcout.imbue(std::locale(std::locale("C"), new std::codecvt_utf8<wchar_t>));
        //std::wcerr.imbue(std::locale(std::locale("C"), new std::codecvt_utf8<wchar_t>));
#else
        SetConsoleOutputCP(CP_UTF8);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
        //fcntl(_fileno(stdout), F_SETFL, fcntl(_fileno(stdout), F_GETFL) | O_NONBLOCK);
#endif
        #endif
    }
}

} // namespace scopes

#if 0
#ifndef SCOPES_WIN32
static void crash_handler(int sig) {
  void *array[20];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 20);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}
#endif
#endif

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

using scopes::Result;

SCOPES_RESULT(int) try_main(int argc, char *argv[]) {
    SCOPES_RESULT_TYPE(int);
    using namespace scopes;
    uint64_t c = 0;
    g_stack_start = (char *)&c;

    on_startup();

    Symbol::_init_symbols();
    init_llvm();

    setup_stdio();
    scopes_argc = argc;
    scopes_argv = argv;

    scopes_compiler_path = nullptr;
    scopes_compiler_dir = nullptr;
    scopes_clang_include_dir = nullptr;
    scopes_include_dir = nullptr;
    if (argv) {
        if (argv[0]) {
            std::string loader = GetExecutablePath(argv[0]);
            // string must be kept resident
            scopes_compiler_path = strdup(loader.c_str());
        } else {
            scopes_compiler_path = strdup("");
        }

        char *path_copy = strdup(scopes_compiler_path);
        scopes_compiler_dir = format("%s/..", dirname(path_copy))->data;
        free(path_copy);
        scopes_clang_include_dir = format("%s/lib/clang/include", scopes_compiler_dir)->data;
        scopes_include_dir = format("%s/include", scopes_compiler_dir)->data;
    }

    init_types();
    init_globals(argc, argv);

    Value *expr = SCOPES_GET_RESULT(load_custom_core(scopes_compiler_path));
    if (expr) {
        goto skip_regular_load;
    }

    {
        SourceFile *sf = nullptr;
#if 0
        Symbol name = format("%s/lib/scopes/%i.%i.%i/core.sc",
            scopes_compiler_dir,
            SCOPES_VERSION_MAJOR,
            SCOPES_VERSION_MINOR,
            SCOPES_VERSION_PATCH);
#else
        Symbol name = format("%s/lib/scopes/core.sc",
            scopes_compiler_dir);
#endif
        sf = SourceFile::from_file(name);
        if (!sf) {
            SCOPES_LOCATION_ERROR(String::from("core missing\n"));
        }
        LexerParser parser(sf);
        expr = SCOPES_GET_RESULT(parser.parse());
    }

skip_regular_load:
    const Anchor *anchor = expr->anchor();
    auto list = SCOPES_GET_RESULT(extract_list_constant(expr));
    Template *tmpfn = SCOPES_GET_RESULT(expand_module(anchor, list, Scope::from(sc_get_globals())));

#if 0 //SCOPES_DEBUG_CODEGEN
    StyledStream ss(std::cout);
    std::cout << "non-normalized:" << std::endl;
    stream_ast(ss, tmpfn, StreamASTFormat());
    std::cout << std::endl;
#endif

    Function *fn = SCOPES_GET_RESULT(prove(nullptr, tmpfn, {}));

    auto main_func_type = pointer_type(raising_function_type(
        arguments_type({}), {}), PTF_NonWritable, SYM_Unnamed);

    auto stage_func_type = pointer_type(raising_function_type(
        arguments_type({TYPE_CompileStage}), {}), PTF_NonWritable, SYM_Unnamed);

compile_stage:
    if (fn->get_type() == stage_func_type) {
        typedef sc_value_raises_t (*StageFuncType)();
        StageFuncType fptr = (StageFuncType)SCOPES_GET_RESULT(compile(fn, 0))->value;
        SCOPES_ANCHOR(fn->anchor());
        auto result = fptr();
        if (!result.ok) {
            SCOPES_RETURN_ERROR(result.except);
        }
        auto value = result._0;
        if (isa<Function>(value)) {
            fn = cast<Function>(value);
            goto compile_stage;
        } else {
            return 0;
        }
    }

    if (fn->get_type() != main_func_type) {
        SCOPES_ANCHOR(fn->anchor());
        StyledString ss;
        ss.out << "core module function has wrong type "
            << fn->get_type() << ", must be " << main_func_type;
        SCOPES_LOCATION_ERROR(ss.str());
    }

#if 0 //SCOPES_DEBUG_CODEGEN
    std::cout << "normalized:" << std::endl;
    stream_ast(ss, fn, StreamASTFormat());
    std::cout << std::endl;

    auto flags = CF_DumpModule;
#else
    auto flags = 0;
#endif

    typedef sc_void_raises_t (*MainFuncType)();
    MainFuncType fptr = (MainFuncType)SCOPES_GET_RESULT(compile(fn, flags))->value;
    {
        SCOPES_ANCHOR(fn->anchor());
        auto result = fptr();
        if (!result.ok) {
            SCOPES_RETURN_ERROR(result.except);
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    using namespace scopes;
    auto result = try_main(argc, argv);
    if (!result.ok()) {
        print_error(result.assert_error());
        f_exit(1);
    }
    f_exit(result.assert_ok());
    return 0;
}