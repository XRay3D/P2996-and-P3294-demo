# Рефлексия и token injection на EDG C++

Примеры статической рефлексии C++26 (P2996), аннотаций (P3394) и инъекции
последовательностей токенов (P3294). Собираются компилятором EDG `eccp`,
установленным в `/opt/edg`.

## Сборка

```sh
cmake -B build -G Ninja           # EDG_PREFIX=/opt/edg по умолчанию
cmake --build build
cmake --build build --target run  # собрать и запустить все примеры
```

[edg.cmake](edg.cmake) — toolchain-файл. Без явного `--toolchain`
[CMakeLists.txt](CMakeLists.txt) подключает его сам. Что он делает:

- Снимает пути заголовков и версию хостового `g++` (`EDG_HOST_CXX`).
- Генерирует обёртку `build/edg/eccp` с готовым окружением EDG
  (`environment.sh`, `EDG_USE_SYSTEM_HEADERS=1`, `EDG_GCC_*_SCRAPE`) и
  назначает её `CMAKE_CXX_COMPILER`. Ничего source'ить не нужно, IDE тоже
  работает.
- Обёртка переводит `-MD -MT -MF` (CMake считает EDG компилятором GNU) в
  отдельный вызов `eccp -M`, поэтому изменения заголовков отслеживаются.
- `EDG_INSTANTIATION=local` (по умолчанию) — шаблоны инстанцируются локально
  в каждом объекте (`-tlocal --no_auto_instantiation`), линковка без
  prelinker'а и перекомпиляций. `auto` — штатная модель EDG.
- Даёт функцию `edg_enable_reflection(<target> [INJECTION])`: она добавляет
  `--c++26 --set_flag=reflection [--set_flag=injection]` и подключает
  `<experimental/meta>`.

Подключение к своему проекту:

```cmake
cmake_minimum_required(VERSION 3.21)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/edg.cmake)  # или --toolchain
project(my LANGUAGES CXX)
add_executable(app main.cpp)
edg_enable_reflection(app INJECTION)
```

Замечания:

- `eccp` переводит C++ в C и компилирует его через `gcc`. Стандартная
  библиотека — libstdc++ хоста.
- Из `/opt/edg/base/include` подключается только `experimental/`:
  остальные заголовки EDG (`<compare>` и др.) конфликтуют с libstdc++.
- `CMAKE_CXX_STANDARD` / `cxx_std_NN` не использовать: CMake подставит
  `-std=...`, а EDG понимает только `--c++NN`.

### clang-p2996

Все примеры, включая qt-reflect, собираются и
[clang-p2996](https://github.com/bloomberg/clang-p2996).
[clang-p2996.cmake](clang-p2996.cmake) даёт ту же функцию
`edg_enable_reflection(... INJECTION)` с флагами `-freflection-latest
-ftoken-injection`:

```sh
cmake -B build-clang -G Ninja -DCMAKE_CXX_COMPILER=/opt/clang-p2996/bin/clang++
cmake --build build-clang --target run
cmake --build build-clang --target run_qt
```

Стандартная библиотека выбирается при настройке. Сборка clang-p2996 с ветки
`libstdcxx-reflection` (форк XRay3D) работает с libstdc++ GCC 16 из
`/opt/gcc-16`: `<experimental/meta>` лежит в ресурсном каталоге clang, а GCC
выбран в `x86_64-alt-linux.cfg` рядом с `clang++`. С libstdc++ линкуется
системный Qt, и qt-reflect собирается. Если `<experimental/meta>` с libstdc++
не находится (clang-p2996 без этой поддержки), берётся `-stdlib=libc++`, а
qt-reflect пропускается: в ABI Qt6::Core есть `std::chrono::nanoseconds`
и `std::function`, с libc++ они не линкуются.

Отличия API сглажены в исходниках: `annotations_of_with_type` — обёртка над
`annotations_of(r, type)` в `meta_util.hpp`, ширина битового поля в
`data_member_options` у clang называется `width`, а не `bit_width`.
`members_of` у EDG перечисляет сначала поля, потом функции, у clang — в
порядке объявления; qt-reflect сортирует свойства по `source_location_of`.

## Примеры

| Файл | Что показывает |
|---|---|
| `01_basics.cpp` | `^^`, сплайсы `[: :]` (типы, выражения, члены), запросы `is_*`, `type_of`, `parent_of`, `bases_of`, `parameters_of`, `substitute`, `reflect_constant`, `extract` |
| `02_enums.cpp` | enum → строка, строка → enum, перечисление значений, всё работает и в `static_assert` |
| `03_serialize.cpp` | обобщённый `to_json` для вложенных агрегатов, аннотации `[[=json::rename{...}]]` / `[[=json::skip{}]]`, сравнение по полям |
| `04_define_aggregate.cpp` | `define_aggregate` + `data_member_spec`: struct-of-arrays, генерация типов из описаний, битовые поля и выравнивание |
| `05_token_injection.cpp` | `consteval { }`, `queue_injection`, `namespace_inject`, `list_builder`, генерация констант, функций, enum, свойств класса, класса по чужим полям, диспетчера по имени |

### Подпроект qt-reflect

[qt-reflect/](qt-reflect/README.md) показывает Qt 6 без moc. Сигналы, слоты и
свойства размечаются аннотациями (`[[=qr::signal]]`, `[[=qr::slot]]`,
`[[=qr::property]]`), а метаобъект генерируется token injection в том же
формате, что у moc 6.11. Подпроект собирается EDG и clang-p2996 с libstdc++
при найденном Qt6: `cmake --build build --target run_qt`.

### Синтаксис token sequences

```cpp
^^{ ... }          // последовательность токенов, значение типа std::meta::info
\[ "get_"sv, n ]   // идентификатор из string_view и чисел (только string_view, не const char*)
\[: r :]           // сплайс рефлексии
\{ tokens }        // вставка другой последовательности
\val( expr )       // значение как литерал
\str( sv )         // строковый литерал
```

`queue_injection(ts)` вставляет токены после завершения текущего
`consteval { }` блока в ближайший класс или пространство имён.
`namespace_inject(ts)` вставляет их в ближайшее пространство имён.

## Ограничения текущего EDG (обнаружены при написании)

- **`template for` (P1306) не принимает `std::vector` напрямую**
  (`invalid use of address of interpreter storage`). Диапазон выносится в
  `constexpr`-переменную через `define_static_array`:
  ```cpp
  constexpr auto fields = std::define_static_array(util::fields_of(^^T));
  template for (constexpr auto m : fields) { ... }
  ```
  Тот же вызов `define_static_array` прямо в заголовке `template for`
  внутри шаблона не компилируется (`dynamic allocations ... were not
  deallocated`). Тело цикла должно быть составным оператором `{ ... }`.
- **В шаблонах переменная цикла объявляется как `constexpr auto`**, не
  `constexpr info`: с явным типом EDG не считает её константой, и сплайсы
  `[:e:]`, `obj.[:m:]` и вызовы вроде `identifier_of(e)` не компилируются.
- **`constexpr`-функция с результатом `string_view`, вызванная из тела
  обхода членов, иногда даёт неверный C-код** (замечено с прежним
  `expand`). Помогает `consteval` с результатом
  `const char*` через `define_static_string` (см. `key_of` в
  `03_serialize.cpp`).
- **Внутри собственного `consteval { }` класс ещё неполный**, поэтому
  `nonstatic_data_members_of` по нему не работает. Генерировать члены по
  полям класса можно только снаружи (см. `make_tracked`).
- **Перед сплайсом в начале объявления параметра нужен `typename`:**
  `typename \[:cls:]& obj`.
- **POSIX-заголовки уже видны в глобальном пространстве имён** (`read`,
  `write`), так что эти имена не годятся для енумераторов обычного `enum`.
