/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_BOOT_HPP
#define SCOPES_BOOT_HPP

#include "valueref.inc"
#include "result.hpp"

#include <string>

namespace scopes {

extern std::string compiler_path;
extern std::string compiler_dir;
extern std::string clang_include_dir;
extern std::string include_dir;
extern size_t compiler_argc;
extern char **compiler_argv;

void on_startup();
void on_shutdown();

extern bool signal_abort;
void f_abort();
void f_exit(int c);

SCOPES_RESULT(ValueRef) load_custom_core(const char *executable_path);

void init(void *c_main, int argc, char *argv[]);
int run_main();

} // namespace scopes

#endif // SCOPES_BOOT_HPP
