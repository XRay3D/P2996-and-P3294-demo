# Qt 6 без moc: сигналы, слоты и свойства из аннотаций

Сигналы, слоты, `Q_INVOKABLE` и свойства размечаются аннотациями C++26
(P3394). Класс наследует CRTP-базу `qr::Object<D>`, и она сама строит его
метаобъект рефлексией (P2996): `staticMetaObject`, `qt_static_metacall`,
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

class Counter : public qr::Object<Counter> {      // вместо QObject + Q_OBJECT
public:
  [[=qr::signal]]    void valueChanged(int value);   // тело генерируется
  [[=qr::slot]]      void reset() { value = 0; valueChanged(0); }
  [[=qr::invokable]] QString describe() const;

  [[=qr::property]]  int value = 0;                  // MEMBER, NOTIFY valueChanged

  // Свойство title и сигнал titleChanged(const QString&).
  qr::auto_property<QString> title{this};
};

class Child : public qr::Object<Child, Counter> {   // наследник со своим метаобъектом
  ...
};

struct Point : qr::Gadget<Point> {                  // вместо Q_GADGET
  [[=qr::property]] double x = 0;
};
```

Больше ничего не нужно: ни макросов, ни шага после класса. Работают
`connect` по указателям на члены и по строкам `SIGNAL()`/`SLOT()`,
`setProperty`/`property`, `invokeMethod` с возвращаемым значением,
queued-соединения, `qobject_cast` и `inherits` по цепочке наследования.

## API

| | |
|---|---|
| `class D : public qr::Object<D>` | Вместо `QObject` и `Q_OBJECT`. Вторым параметром задаётся другая база: свой `qr::Object` или любой класс с `Q_OBJECT`, например `qr::Object<D, QTimer>`. Конструкторы базы наследуются. |
| `struct D : qr::Gadget<D>` | Вместо `Q_GADGET`. Вторым параметром задаётся базовый гаджет. `QMetaType` признаёт класс гаджетом. Доступны свойства и invokable-методы, сигнал вызывает ошибку компиляции. |
| `[[=qr::signal]]` | Сигнал. Тело генерируется (`QMetaObject::activate`), писать его не нужно. Возвращает только `void`. |
| `[[=qr::slot]]` | Слот. Уровень доступа (public/protected/private) попадает в метаобъект. |
| `[[=qr::invokable]]` | `Q_INVOKABLE`. |
| `[[=qr::property]]` на поле | Свойство `MEMBER`. NOTIFY — сигнал `<name>Changed`, если он есть. Запись через метаобъект испускает его только при изменении значения. |
| `[[=qr::property]]` на const-геттере | Свойство `READ`. WRITE — метод `set<Name>` с одним параметром, если он есть. NOTIFY — как у поля. |
| `qr::auto_property<T> name{this}` | Член класса: свойство `name` и сигнал `nameChanged(const T&)`. Чтение: `name.get()`, `name->...` или неявное приведение к `const T&`. Запись: `name = v` или `name.set(v)`, сигнал испускается только при изменении. Начальное значение: `name{this, v}`. |
| `qr::changed<&D::name>` | Сигнал `nameChanged` для `QObject::connect`/`disconnect`: `connect(&obj, qr::changed<&D::name>, ctx, f)`. |

Порядок методов такой же, как у moc: сигналы, затем слоты, затем invokable.
Внутри каждой группы и у свойств порядок совпадает с порядком объявления.
Он не зависит от того, в каком порядке компилятор отдаёт `members_of`.

## Как устроено

Члены `qr::Object<D>` — обычный код шаблона. Шаблон инстанцируется, когда
`D` уже определён, поэтому его члены видны рефлексии:

- **таблицы метаданных** собираются `template for` и `index_sequence` по
  спискам методов и свойств, строки передаются в `StringRefStorage` как
  массивы `const char[N]`;
- **`qt_static_metacall`** обращается к членам `D` через указатели на члены,
  полученные из рефлексии;
- **тела сигналов** — единственное место, где нужен token injection (P3294).
  Объявить функцию в `D` из базы нельзя, а определить можно: определения
  `inline void D::signal(...)` внедряются в пространство имён `D` через
  `namespace_inject` при инстанцировании `qt_static_metacall`.

Qt распознаёт `Q_OBJECT` и `Q_GADGET` по типу указателя на член
(`&T::qt_metacall`). В CRTP эти члены объявлены в базе, поэтому заголовок
добавляет частичные специализации `QtPrivate::HasQ_OBJECT_Macro`,
`IsGadgetHelper` и `IsPointerToGadgetHelper` для классов `qr`.

Сигнал `auto_property` есть только в метаобъекте. Функцию-член для него
в `D` не объявить, поэтому `qr::Object` содержит шаблон
`qr_changed<&D::name>`. Его адрес, `qr::changed<&D::name>`, служит
указателем на сигнал для `connect`, и `qt_static_metacall` узнаёт его в
запросе `IndexOfMethod`. Сам `auto_property` находит номер своего сигнала по
смещению поля, поэтому ему нужен указатель на владельца: `{this}`.

## Сборка

Подпроект подключается из корневого `CMakeLists.txt`, если найден Qt6 и
компилятор работает с libstdc++. Это EDG или clang-p2996 с поддержкой
libstdc++:

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
  ABI eccp, и без этой переменной `find_package` не заглядывает в `lib64`;
- `-lm` добавляется при линковке: eccp линкует через gcc, а не g++.

В примерах подавлены предупреждения 1675 («unrecognized GCC pragma» от
`QT_WARNING_PUSH`) и 111 в заголовках Qt.

## Примеры

| Файл | Что показывает |
|---|---|
| `01_counter.cpp` | сигналы, слоты, MEMBER-свойства, `connect` по PMF и по строкам, `invokeMethod`, `qobject_cast` |
| `02_properties.cpp` | `qr::auto_property` и `qr::changed`, READ/WRITE-свойства через геттер и `set<Name>`, свойство только для чтения, наследник со своим метаобъектом |
| `03_invokable.cpp` | возвращаемые значения, перегруженные слоты, const-методы, protected-слот, пользовательский тип в сигнале, queued-соединение |
| `04_vs_moc.cpp` | один и тот же класс через `Q_OBJECT` + moc ([moc_reference.h](examples/moc_reference.h)) и через `qr`, сравнение метаобъектов. Код возврата 1 при расхождении |
| `05_gadget.cpp` | `qr::Gadget`: `readOnGadget`/`writeOnGadget`, `invokeOnGadget`, наследник гаджета, гаджет в `QVariant` и как свойство QObject |

## Ограничения EDG, найденные по ходу

Все обходятся внутри `qobject.hpp`, обходы только для EDG стоят под
`#ifdef __EDG__`:

- **Сплайс члена проверяет доступ.** Из `qr::detail` недоступны
  protected- и private-члены `D`, а clang-p2996 доступ при сплайсе не
  проверяет. Под EDG указатель на член берётся через `std::meta::extract`
  с точным типом указателя, под clang — `&[:m:]`.
- **`parent_of` не применяется к пространству имён.** Полное имя класса
  `demo::Counter` под EDG берётся из `display_string_of`, который уже
  квалифицирует имя. Под clang имя собирается по цепочке `parent_of`.
- **Имена параметров берутся из последнего объявления.** После внедрения
  тела сигнала EDG видит имена его параметров. Поэтому внедрённые
  определения повторяют имена из объявления.
- **`namespace_inject` принимает одно объявление за вызов.** На втором
  объявлении он выдаёт «extraneous tokens injected after declaration»,
  поэтому тело каждого сигнала внедряется отдельным вызовом.
- **В `\[...]` годятся только `string_view` на литералы и результат
  `identifier_of`.** `std::string` не принимается.
- **Временные `std::vector`/`std::string` разрушаются неверно** в операнде
  `?:` и в выражениях вроде `"set" + s` при константном вычислении. Выход —
  именованные локальные переменные.
- **Сплайс `type_of(метод)` не сопоставляется с `R(Args...)`.**
  `QtMocHelpers::FunctionData<R(Args...)>` от него не выбирается, поэтому тип
  функции собирается заново: `qr::detail::fn<R, Args...>` (или `const_fn`).
- **Кириллица в `static_assert` выводится как `?`.** Поэтому тексты
  проверок в заголовке на английском.

## Ограничения

Чего нет в сравнении с moc:

- `Q_ENUM`/`Q_FLAG`, `Q_CLASSINFO`, `QPrivateSignal`;
- `RESET`, `BINDABLE`, `REVISION`, а также флаги `CONSTANT`, `FINAL` и `USER`
  у свойств;
- клоны методов с аргументами по умолчанию;
- сигналы, возвращающие значение;
- сигналы в классах, вложенных в другие классы: тела сигналов внедряются в
  пространство имён.

Особенности CRTP:

- **Тела сигналов `inline` и появляются в той единице трансляции, где
  инстанцирован метаобъект.** Это любая единица, где создаётся объект класса
  или используется `staticMetaObject`. Если единица трансляции только
  вызывает сигнал, ссылка на него не разрешится. В примерах всё в одном
  файле.
- **Сигнал `auto_property` не функция-член.** Для `connect` нужен
  `qr::changed<&D::name>` или строковый `SIGNAL(nameChanged(T))`.
  Испустить его вручную можно через `name.notify()`.
- **`auto_property` требует `{this}`** и запрещает копирование. Для
  наследников `QObject` это не ограничение.
- **Гаджет наследует пустую базу.** Агрегатная инициализация требует для
  неё `{}`: `Point{{}, 1, 2}`.

Пользовательские типы параметров записываются по `display_string_of`, как
это делает moc для незарегистрированных типов. Для queued-соединений тип
нужно объявить через `Q_DECLARE_METATYPE`.
