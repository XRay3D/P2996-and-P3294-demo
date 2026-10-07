// Перечисления: enum -> строка, строка -> enum, обход всех значений.
#include "meta_util.hpp"

#include <optional>
#include <type_traits>

using namespace std::meta;

enum class Color { red, green, blue = 10, alpha };

enum Permission : unsigned { can_read = 1, can_write = 2, can_exec = 4 };

template<typename E>
  requires std::is_enum_v<E>
constexpr std::string_view enum_to_string(E value) {
  std::string_view result = "<unnamed>";
  constexpr auto enumerators = std::define_static_array(enumerators_of(^^E));
  template for (constexpr auto e : enumerators) {
    if (value == [:e:]) result = identifier_of(e);
  }
  return result;
}

template<typename E>
  requires std::is_enum_v<E>
constexpr std::optional<E> enum_from_string(std::string_view name) {
  std::optional<E> result;
  constexpr auto enumerators = std::define_static_array(enumerators_of(^^E));
  template for (constexpr auto e : enumerators) {
    if (name == identifier_of(e)) result = [:e:];
  }
  return result;
}

template<typename E>
consteval std::size_t enum_size() {
  return enumerators_of(^^E).size();
}

// Всё работает и в константных выражениях.
static_assert(enum_to_string(Color::blue) == "blue");
static_assert(enum_from_string<Color>("alpha") == Color::alpha);
static_assert(!enum_from_string<Color>("purple"));
static_assert(enum_size<Color>() == 4);

template<typename E>
void print_enum() {
  std::printf("%s (значений: %zu, базовый тип %s):\n",
              util::display_of(^^E), enum_size<E>(),
              util::display_of(underlying_type(^^E)));
  constexpr auto enumerators = std::define_static_array(enumerators_of(^^E));
  template for (constexpr auto e : enumerators) {
    std::printf("  %-9s = %lld\n", util::name_of(e),
                static_cast<long long>([:e:]));
  }
}

int main() {
  print_enum<Color>();
  print_enum<Permission>();

  util::section("преобразования во время выполнения");
  Color c = Color::green;
  std::printf("enum_to_string(c) = %s\n",
              std::string(enum_to_string(c)).c_str());
  for (std::string_view name : {"red"sv, "alpha"sv, "purple"sv}) {
    auto v = enum_from_string<Color>(name);
    std::printf("enum_from_string(\"%s\") = %s\n", std::string(name).c_str(),
                v ? std::to_string(static_cast<int>(*v)).c_str() : "nullopt");
  }
}
