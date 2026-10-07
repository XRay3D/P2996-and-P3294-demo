// Основы P2996: оператор ^^, сплайсеры [: :], запросы к std::meta::info,
// substitute / reflect_constant / extract.
#include "meta_util.hpp"

#include <array>
#include <utility>

using namespace std::meta;

namespace geo {

struct Shape {
  virtual ~Shape() = default;
};

struct Circle : Shape {
  double radius = 1.0;
  static constexpr int kind = 1;
  double area() const { return 3.14159 * radius * radius; }
};

}  // namespace geo

int scale(int value, int factor) { return value * factor; }

// Сигнатура функции, собранная по рефлексиям её параметров
// целиком в consteval-функции.
consteval const char* signature_of(info f) {
  std::string s = std::string(identifier_of(f)) + "(";
  bool first = true;
  for (info p : parameters_of(f)) {
    if (!first) s += ", ";
    first = false;
    s += display_string_of(type_of(p));
    s += " ";
    s += identifier_of(p);
  }
  s += ") -> ";
  s += display_string_of(return_type_of(f));
  return std::define_static_string(s);
}

// Рефлексии — обычные значения времени компиляции: их можно сравнивать,
// хранить в переменных и передавать в consteval-функции.
constexpr info int_refl = ^^int;
static_assert(int_refl == ^^int);
static_assert(int_refl != ^^long);
static_assert(is_type(^^int) && is_namespace(^^geo) && is_function(^^scale));
static_assert(is_class_type(^^geo::Circle) && is_polymorphic_type(^^geo::Circle));
static_assert(type_of(^^geo::Circle::radius) == ^^double);
static_assert(return_type_of(^^scale) == ^^int);
static_assert(parent_of(^^geo::Circle) == ^^geo);

// Сплайсер типа: [: r :] превращает рефлексию обратно в конструкцию языка.
typename [: ^^unsigned long :] big = 42;

int main() {
  util::section("имена и описание сущностей");
  std::printf("identifier_of(^^geo::Circle)     = %s\n",
              util::name_of(^^geo::Circle));
  std::printf("display_string_of(^^geo::Circle) = %s\n",
              util::display_of(^^geo::Circle));
  std::printf("display_string_of(type_of(^^scale)) = %s\n",
              util::display_of(type_of(^^scale)));

  util::section("сплайсы выражений и членов");
  geo::Circle c;
  c.[: ^^geo::Circle::radius :] = 2.0;                 // доступ к члену
  std::printf("radius = %g, area = %g\n",
              c.radius, (c.[: ^^geo::Circle::area :])());
  std::printf("[:^^scale:](6, 7) = %d\n", [: ^^scale :](6, 7));
  std::printf("kind (static member через сплайс) = %d\n",
              [: ^^geo::Circle::kind :]);
  std::printf("big = %lu\n", big);

  util::section("параметры функции");
  std::printf("  %s\n", signature_of(^^scale));

  util::section("члены и базы класса");
  constexpr auto bases = std::define_static_array(
      bases_of(^^geo::Circle, access_context::unchecked()));
  template for (constexpr auto b : bases) {
    std::printf("  база: %s\n", util::display_of(type_of(b)));
  }
  constexpr auto fields = std::define_static_array(util::fields_of(^^geo::Circle));
  template for (constexpr auto m : fields) {
    std::printf("  поле: %s %s, offset %zu\n",
                util::display_of(type_of(m)), util::name_of(m),
                offset_of(m).bytes);
  }

  util::section("substitute / reflect_constant / extract");
  // std::array<int, 3> собирается из рефлексий аргументов шаблона.
  constexpr info arr = substitute(^^std::array, {^^int, reflect_constant(3)});
  typename [: arr :] values{1, 2, 3};
  std::printf("%s, size = %zu\n", util::display_of(arr), values.size());

  using Pair = std::pair<int, char>;
  constexpr auto pair_args =
      std::define_static_array(template_arguments_of(dealias(^^Pair)));
  template for (constexpr auto a : pair_args) {
    std::printf("  аргумент шаблона pair: %s\n", util::display_of(a));
  }

  constexpr int answer = extract<int>(reflect_constant(40 + 2));
  std::printf("extract<int>(reflect_constant(42)) = %d\n", answer);
}
