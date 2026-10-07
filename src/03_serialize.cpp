// Обобщённая сериализация агрегатов в JSON с настройкой через аннотации
// P3394 ([[=value]]) и обобщённое сравнение по полям.
#include "meta_util.hpp"

#include <string>
#include <type_traits>
#include <vector>

using namespace std::meta;

namespace json {

// Типы аннотаций: значения [[=...]] доступны через annotations_of.
struct rename { const char* name; };
struct skip {};

template<typename T>
void write(std::string& out, const T& value);

// Имя ключа: из аннотации json::rename, если она есть, иначе имя поля.
consteval const char* key_of(std::meta::info m) {
  auto renames = annotations_of_with_type(m, ^^rename);
  if (renames.empty()) return util::name_of(m);
  return extract<rename>(renames[0]).name;
}

// Отдельная consteval-функция: clang-p2996 не освобождает временный vector
// прямо в условии if constexpr.
consteval bool is_skipped(std::meta::info m) {
  return !annotations_of_with_type(m, ^^skip).empty();
}

inline void write_string(std::string& out, std::string_view s) {
  out += '"';
  out += s;
  out += '"';
}

template<typename T>
void write_object(std::string& out, const T& obj) {
  out += '{';
  bool first = true;
  constexpr auto fields = std::define_static_array(util::fields_of(^^T));
  template for (constexpr auto m : fields) {
    if constexpr (!is_skipped(m)) {
      if (!first) out += ',';
      first = false;
      constexpr const char* key = key_of(m);
      write_string(out, key);
      out += ':';
      write(out, obj.[:m:]);
    }
  }
  out += '}';
}

template<typename T>
void write(std::string& out, const T& value) {
  if constexpr (std::is_same_v<T, bool>) {
    out += value ? "true" : "false";
  } else if constexpr (std::is_arithmetic_v<T>) {
    out += std::to_string(value);
  } else if constexpr (std::is_same_v<T, std::string>) {
    write_string(out, value);
  } else if constexpr (has_template_arguments(^^T) &&
                       template_of(^^T) == ^^std::vector) {
    out += '[';
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (i) out += ',';
      write(out, value[i]);
    }
    out += ']';
  } else {
    static_assert(is_aggregate_type(^^T), "unsupported type");
    write_object(out, value);
  }
}

template<typename T>
std::string to_json(const T& value) {
  std::string out;
  write(out, value);
  return out;
}

}  // namespace json

// Сравнение «по всем полям» без ручного перечисления членов.
template<typename T>
constexpr bool fields_equal(const T& a, const T& b) {
  bool eq = true;
  constexpr auto fields = std::define_static_array(util::fields_of(^^T));
  template for (constexpr auto m : fields) {
    eq = eq && a.[:m:] == b.[:m:];
  }
  return eq;
}

struct Address {
  std::string city;
  int zip;
};

struct User {
  // Значение аннотации должно годиться как аргумент шаблона, а указатель
  // на строковый литерал — нет (clang это проверяет).
  [[=json::rename{std::define_static_string("user_id")}]] int id;
  std::string name;
  bool admin;
  std::vector<int> scores;
  Address address;
  [[=json::skip{}]] std::string password_hash;
};

struct Vec3 { int x, y, z; };
static_assert(fields_equal(Vec3{1, 2, 3}, Vec3{1, 2, 3}));
static_assert(!fields_equal(Vec3{1, 2, 3}, Vec3{1, 2, 4}));

int main() {
  User u{7, "Ada", true, {90, 85, 100}, {"London", 12345}, "secret"};
  std::printf("%s\n", json::to_json(u).c_str());

  util::section("аннотации полей User");
  constexpr auto fields = std::define_static_array(util::fields_of(^^User));
  template for (constexpr auto m : fields) {
    constexpr std::size_t count = annotations_of(m).size();
    std::printf("  %-14s аннотаций: %zu\n", util::name_of(m), count);
  }
}
