/*
    The Scopes Compiler Infrastructure
    This file is distributed under the MIT License.
    See LICENSE.md for details.
*/

#ifndef SCOPES_CACHE_HPP
#define SCOPES_CACHE_HPP

#include <stddef.h>
#include <string>

namespace scopes {

std::string get_cache_key(const char *content, size_t size);
int get_cache_misses();
const char *get_cache_dir();
const char *get_cache_file(const std::string &key);
const char *get_cache_key_file(const std::string &key);
void set_cache(const std::string &key,
    const char *key_content, size_t key_size,
    const char *content, size_t size);

} // namespace scopes

#endif // SCOPES_CACHE_HPP

