# Qt 6 без moc: сигналы, слоты и свойства из аннотаций

Сигналы, слоты, `Q_INVOKABLE` и свойства размечаются аннотациями C++26
(P3394). Всё, что обычно генерирует moc, внедряет при компиляции
token injection (P3294): `staticMetaObject`, `qt_static_metacall`,
`qt_metacall`, `qt_metacast` и тела сигналов. moc и AUTOMOC не нужны.

Генератор — один заголовок [include/qr/qobject.hpp](include/qr/qobject.hpp).
Он выдаёт те же данные, что moc из Qt 6.11:
`QtMocHelpers::metaObjectData`, `SignalData`/`SlotData`/`MethodData` и
`PropertyData` из `<QtCore/qtmochelpers.h>`. Пример
[04_vs_moc](examples/04_vs_moc.cpp) сравнивает результат с настоящим moc
поле за полем.

## Пример

```cpp
#include <qr/qobject.hpp>

class Counter : public QObject {
  consteval { qr::object(); }                  // вместо Q_OBJECT

public:
  [[=qr::signal]]    void valueChanged(int value);
  [[=qr::slot]]      void reset() { value = 0; valueChanged(0); }
  [[=qr::invokable]] QString describe() const;

  [[=qr::property]]  int value = 0;            // MEMBER, NOTIFY valueChanged

  // m_title, title(), setTitle(), сигнал titleChanged(const QString&)
  consteval { qr::auto_property(^^QString, "title"); }
};

consteval { qr::meta_object(^^Counter); }      // то, что было бы в moc_counter.cpp
```

После этого работают `connect` по указателям на члены и по строкам
`SIGNAL()`/`SLOT()`, `setProperty`/`property`, `invokeMethod` с
возвращаемым значением, queued-соединения, `qobject_cast` и
`inherits` по цепочке наследования.

## API

| | |
|---|---|
| `consteval { qr::object(); }` | В теле класса вместо `Q_OBJECT`: объявления `staticMetaObject`, `metaObject`, `qt_metacast`, `qt_metacall`, `qt_static_metacall`, `tr`, `QPrivateSignal`. После вызова доступ `private`, как после `Q_OBJECT`. Обычный `Q_OBJECT` тоже подходит. |
| `consteval { qr::gadget(); }` | В теле класса вместо `Q_GADGET`: `staticMetaObject`, `qt_static_metacall`, маркеры `qt_check_for_QGADGET_macro` и `QtGadgetHelper`, по которым `QMetaType` узнаёт гаджет. QObject не нужен. Доступны свойства и invokable-методы, сигналы — ошибка компиляции `gadget_cannot_have_signals`. После вызова доступ `private`. Обычный `Q_GADGET` тоже подходит. |
| `[[=qr::signal]]` | Сигнал. Тело генерируется (`QMetaObject::activate`), писать его не нужно. Возвращает только `void`. |
| `[[=qr::slot]]` | Слот. Уровень доступа (public/protected/private) попадает в метаобъект. |
| `[[=qr::invokable]]` | `Q_INVOKABLE`. |
| `[[=qr::property]]` на поле | Свойство `MEMBER`. NOTIFY — сигнал `<name>Changed`, если он есть. Запись через метаобъект эмитит его только при изменении значения. |
| `[[=qr::property]]` на const-геттере | Свойство `READ`. WRITE — метод `set<Name>` с одним параметром, если он есть. NOTIFY — как у поля. |
| `qr::auto_property(^^T, "name")` | В consteval-блоке класса: поле `m_name`, геттер `name()` с `[[=qr::property]]`, сеттер `setName()` и сигнал `nameChanged(const T&)`. После вызова доступ `public`. |
| `consteval { qr::meta_object(^^C); }` | После определения класса, в любом месте. Код внедряется в пространство имён класса через `namespace_inject`. Гаджет распознаётся по `qt_check_for_QGADGET_macro`. Для него, как у moc, генерируются только данные метаобъекта и `qt_static_metacall`, с флагом `PropertyAccessInStaticMetaCall`. Базовый метаобъект берётся у первой базы, без базы он `nullptr`. |

Порядок методов такой же, как у moc: сигналы, затем слоты, затем invokable.
Свойства идут в порядке объявления (как строки `Q_PROPERTY`), независимо от
того, в каком порядке компилятор отдаёт `members_of`.

## Почему два шага

Внутри своего `consteval { }` класс ещё неполный, и обойти его члены там
нельзя. Поэтому `qr::object()` и `qr::auto_property()` только объявляют
члены. Всё, что зависит от полного списка членов, `qr::meta_object()`
генерирует после закрывающей скобки класса: таблицы, `qt_static_metacall`
и определения сигналов.

## Сборка

Подпроект подключается из корневого `CMakeLists.txt`, когда компилятор EDG и
найден Qt6:

```sh
cmake -B build -G Ninja
cmake --build build --target run_qt
```

Его можно собрать и отдельно:

```sh
cmake -S qt-reflect -B build-qt -G Ninja                                   # EDG
cmake -S qt-reflect -B build-qt -G Ninja -DCMAKE_CXX_COMPILER=/opt/clang-p2996/bin/clang++
cmake --build build-qt --target run_qt
```

Системный Qt (`/usr/lib64/cmake/Qt6`) собран с libstdc++, поэтому нужен
компилятор с ней: EDG или clang-p2996 с поддержкой libstdc++
(ветка `libstdcxx-reflection`, см. [../clang-p2996.cmake](../clang-p2996.cmake)).
Если clang-p2996 умеет только libc++, подпроект пропускается.

Чтобы импортированные цели Qt собирались через eccp, в обёртку из
[../edg.cmake](../edg.cmake) добавлено следующее:

- `-isystem <dir>` превращается в `--sys_include <dir>`;
- `-fPIC`/`-fPIE` уходит в gcc через `--c_to_obj_option`;
- `-std=...` отбрасывается: его добавляет compile feature `cxx_std_17` у
  `Qt6::Core`, а стандарт уже задан `--c++26`;
- `CMAKE_SIZEOF_VOID_P` берётся у хостового g++. CMake не может определить
  ABI eccp, и без этой переменной `find_package` не заглядывает в `lib64`.

В примерах подавлены предупреждения 1675 («unrecognized GCC pragma» от
`QT_WARNING_PUSH`) и 111 в заголовках Qt.

## Примеры

| Файл | Что показывает |
|---|---|
| `01_counter.cpp` | сигналы, слоты, MEMBER-свойства, `connect` по PMF и по строкам, `invokeMethod`, `qobject_cast` |
| `02_properties.cpp` | `qr::auto_property`, READ/WRITE-свойства через геттер и `set<Name>`, свойство только для чтения, наследник со своим метаобъектом |
| `03_invokable.cpp` | возвращаемые значения, перегруженные слоты, const-методы, protected-слот, пользовательский тип в сигнале, queued-соединение |
| `04_vs_moc.cpp` | один и тот же класс через `Q_OBJECT` + moc ([moc_reference.h](examples/moc_reference.h)) и через `qr`, сравнение метаобъектов. Код возврата 1 при расхождении |
| `05_gadget.cpp` | `qr::gadget()` и `Q_GADGET`: `readOnGadget`/`writeOnGadget`, `invokeOnGadget`, наследник гаджета, гаджет в `QVariant` и как свойство QObject |

## Ограничения EDG, найденные по ходу

Все обходятся внутри `qobject.hpp`:

- **`namespace_inject` принимает одно объявление за вызов.** На втором
  объявлении он выдаёт «extraneous tokens injected after declaration», поэтому
  каждое определение внедряется отдельным вызовом.
- **В `\[...]` годятся только `string_view` на строковые литералы.**
  `std::string`, `const char*` и `string_view` на результат
  `define_static_string` или на `constexpr`-массив не принимаются. Поэтому
  заглавная буква для `setName` берётся подстрокой литерала
  `"ABC…Z"sv`, а имя собирается из частей: `\["set"sv, first, rest]`.
- **Временные `std::vector`/`std::string` разрушаются неверно** в операнде
  `?:` и в выражениях вроде `"set" + s` при константном вычислении. Выход —
  именованные локальные переменные.
- **Сплайс `type_of(метод)` не сопоставляется с `R(Args...)`.**
  `QtMocHelpers::FunctionData<R(Args...)>` от него не выбирается, поэтому тип
  функции собирается заново: `qr::detail::fn<R, Args...>` (или `const_fn`).

Чего нет в сравнении с moc:

- `Q_ENUM`/`Q_FLAG`, `Q_CLASSINFO`;
- `qr::auto_property` в гаджете: он объявляет сигнал `<name>Changed`;
- `RESET`, `BINDABLE`, `REVISION`, а также флаги `CONSTANT`, `FINAL` и `USER`
  у свойств;
- клоны методов с аргументами по умолчанию;
- сигналы, возвращающие значение;
- классы, вложенные в другие классы (`qr::meta_object` требует, чтобы класс
  лежал в пространстве имён).

Пользовательские типы параметров записываются по `display_string_of`, как
это делает moc для незарегистрированных типов. Для queued-соединений тип
нужно объявить через `Q_DECLARE_METATYPE`.
