// Общие включения и вспомогательные функции для примеров.
#pragma once

#include <experimental/meta>
#include <cstdio>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

// clang-p2996 предоставляет ту же функцию как перегрузку annotations_of(r, type).
#ifndef __EDG__
namespace std::meta {
consteval std::vector<info> annotations_of_with_type(info r, info type) {
  return annotations_of(r, type);
}
}  // namespace std::meta
#endif

namespace util {

// Все нестатические члены без проверки доступа: для примеров этого достаточно.
consteval std::vector<std::meta::info> fields_of(std::meta::info type) {
  return std::meta::nonstatic_data_members_of(
                          type, std::meta::access_context::unchecked());
}

// std::string_view из рефлексии не гарантированно завершается нулём;
// define_static_string делает строку с '\0' для printf.
consteval const char* name_of(std::meta::info r) {
  return std::define_static_string(std::meta::identifier_of(r));
}

consteval const char* display_of(std::meta::info r) {
  return std::define_static_string(std::meta::display_string_of(r));
}

inline void section(const char* title) {
  std::printf("\n--- %s ---\n", title);
}

}  // namespace util
