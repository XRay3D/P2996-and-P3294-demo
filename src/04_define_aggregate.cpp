// Генерация типов через define_aggregate и data_member_spec:
// struct-of-arrays и именованный кортеж.
#include "meta_util.hpp"

#include <array>

using namespace std::meta;

// struct-of-arrays: для каждого поля T — поле std::array<тип_поля, N>
// с тем же именем.
template<typename T, std::size_t N>
struct struct_of_arrays_impl {
  struct type;

  consteval {
    std::vector<info> specs;
    for (info m : util::fields_of(^^T)) {
      info array_type = substitute(^^std::array,
                                   {type_of(m), reflect_constant(N)});
      specs.push_back(data_member_spec(array_type,
                                       {.name = identifier_of(m)}));
    }
    define_aggregate(^^type, specs);
  }
};

template<typename T, std::size_t N>
using struct_of_arrays = struct_of_arrays_impl<T, N>::type;

struct Particle {
  float x, y;
  int id;
};

// Именованный кортеж: поля задаются парами (тип, имя).
struct Field {
  info type;
  const char* name;
};

struct Rgb;
consteval {
  constexpr Field fields[] = {
    {^^unsigned char, "r"}, {^^unsigned char, "g"}, {^^unsigned char, "b"},
  };
  std::vector<info> specs;
  for (const Field& f : fields) {
    specs.push_back(data_member_spec(f.type, {.name = f.name}));
  }
  define_aggregate(^^Rgb, specs);
}

// Битовые поля и выравнивание тоже задаются через data_member_options.
// В clang-p2996 поле ширины называется width, а не bit_width.
#ifdef __EDG__
#define BIT_WIDTH bit_width
#else
#define BIT_WIDTH width
#endif
struct Flags;
consteval {
  define_aggregate(^^Flags, {
    data_member_spec(^^unsigned, {.name = "ready", .BIT_WIDTH = 1}),
    data_member_spec(^^unsigned, {.name = "mode", .BIT_WIDTH = 3}),
    data_member_spec(^^int, {.name = "aligned", .alignment = 16}),
  });
}

template<typename T>
void describe() {
  std::printf("%s: sizeof = %zu, alignof = %zu\n", util::display_of(^^T),
              sizeof(T), alignof(T));
  constexpr auto fields = std::define_static_array(util::fields_of(^^T));
  template for (constexpr auto m : fields) {
    std::printf("  %-28s %-8s offset %zu",
                util::display_of(type_of(m)), util::name_of(m),
                offset_of(m).bytes);
    if constexpr (is_bit_field(m)) {
      std::printf(", bits %zu", bit_size_of(m));
    }
    std::printf("\n");
  }
}

int main() {
  using Particles = struct_of_arrays<Particle, 4>;
  describe<Particles>();

  Particles ps{};
  for (int i = 0; i < 4; ++i) {
    ps.x[i] = i * 0.5f;
    ps.y[i] = i * 2.0f;
    ps.id[i] = 100 + i;
  }
  std::printf("ps.id[3] = %d, ps.y[2] = %g\n\n", ps.id[3], ps.y[2]);

  describe<Rgb>();
  Rgb orange{255, 165, 0};
  std::printf("orange = (%d, %d, %d)\n\n", orange.r, orange.g, orange.b);

  describe<Flags>();
  Flags f{1, 5, 42};
  std::printf("ready = %u, mode = %u, aligned = %d\n", f.ready, f.mode,
              f.aligned);
}
