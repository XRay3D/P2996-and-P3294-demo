// Метаобъекты Qt 6 без moc: CRTP-база qr::Object<D> сама строит метаобъект
// класса D по аннотациям его членов.
//
//   class Counter : public qr::Object<Counter> {   // вместо Q_OBJECT
//   public:
//     [[=qr::signal]]    void valueChanged(int value);   // тело генерируется
//     [[=qr::slot]]      void reset();
//     [[=qr::invokable]] int twice(int x) const;
//     [[=qr::property]]  int value = 0;                  // свойство MEMBER
//     qr::auto_property<QString> title{this};            // свойство + сигнал titleChanged
//   };
//   class Child : public qr::Object<Child, Counter> { ... };   // своя база вторым параметром
//   struct Point : qr::Gadget<Point> { ... };                 // вместо Q_GADGET
//
// Как устроено. Члены qr::Object<D> (staticMetaObject, qt_static_metacall,
// qt_metacall, qt_metacast, metaObject) - обычный код шаблона. Он
// инстанцируется, когда D уже полный, и обходит члены D рефлексией. Таблицы
// метаданных - те же, что у moc 6.11: QtMocHelpers::metaObjectData из
// <QtCore/qtmochelpers.h>. Token injection нужен только для тел сигналов:
// их определения внедряются в пространство имён D при инстанцировании
// qt_static_metacall.
//
// Соглашения (как у Q_PROPERTY):
//   [[=qr::property]] на поле           - MEMBER-свойство;
//   [[=qr::property]] на const-геттере  - READ-свойство, WRITE - метод
//                                         set<Name>, если он есть;
//   NOTIFY - сигнал <name>Changed, если он есть;
//   qr::auto_property<T> name{this}     - свойство name и сигнал
//                                         nameChanged(const T&), который
//                                         есть только в метаобъекте. Для
//                                         connect: qr::changed<&D::name>.
#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qtmochelpers.h>
#include <QtCore/qvariant.h>

#include <experimental/meta>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace qr {

// Аннотации.
struct signal_t {};
struct slot_t {};
struct invokable_t {};
struct property_t {};

inline constexpr signal_t signal{};
inline constexpr slot_t slot{};
inline constexpr invokable_t invokable{};
inline constexpr property_t property{};

template <typename T>
class auto_property;

namespace detail {

using std::meta::info;
using namespace std::string_view_literals;

consteval std::meta::access_context uc() { return std::meta::access_context::unchecked(); }

// EDG: annotations_of_with_type(r, type); clang-p2996: перегрузка
// annotations_of(r, type).
consteval bool has(info r, info annotation_type) {
#ifdef __EDG__
  return !std::meta::annotations_of_with_type(r, annotation_type).empty();
#else
  return !std::meta::annotations_of(r, annotation_type).empty();
#endif
}

consteval bool is_auto_property(info type) {
  type = std::meta::remove_cvref(type);
  return std::meta::has_template_arguments(type) &&
         std::meta::template_of(type) == ^^auto_property;
}

consteval bool is_auto_member(info m) {
  return std::meta::is_nonstatic_data_member(m) && is_auto_property(std::meta::type_of(m));
}

// T из auto_property<T>.
consteval info auto_value_type(info m) {
  return std::meta::template_arguments_of(std::meta::remove_cvref(std::meta::type_of(m)))[0];
}

template <typename M>
struct member_traits;
template <typename C, typename T>
struct member_traits<auto_property<T> C::*> {
  using owner = C;
  using value = T;
};

// Id типа для таблицы методов/свойств: встроенные типы - QMetaType::Type,
// остальные - индекс имени в таблице строк с флагом IsUnresolvedType.
template <typename T>
constexpr uint type_id(uint name_index) {
  using U = std::remove_cvref_t<T>;
  if constexpr (QMetaTypeId2<U>::IsBuiltIn)
    return uint(QMetaTypeId2<U>::MetaType);
  else
    return 0x80000000u | name_index;
}

// Тип функции для SignalData<R(Args...)>: собирается заново, сплайс
// type_of(метод) EDG не сопоставляет с частичной специализацией R(Args...).
template <typename R, typename... Args>
using fn = R(Args...);
template <typename R, typename... Args>
using const_fn = R(Args...) const;

// Указатель на член D для обращения из qt_static_metacall.
#ifndef __EDG__
// clang-p2996: сплайс не проверяет доступ.
template <typename D, info M>
inline constexpr auto member_pointer = &[:M:];
#else
// EDG: сплайс проверяет доступ, и protected/private-слоты и свойства из
// qr::detail недоступны. extract берёт указатель на член без проверки
// доступа, но ему нужен точный тип указателя.
template <typename C, typename T>
using data_member = T C::*;
template <typename C, typename R, typename... Args>
using member_fn = R (C::*)(Args...);
template <typename C, typename R, typename... Args>
using const_member_fn = R (C::*)(Args...) const;
template <typename C, typename R, typename... Args>
using noexcept_member_fn = R (C::*)(Args...) noexcept;
template <typename C, typename R, typename... Args>
using const_noexcept_member_fn = R (C::*)(Args...) const noexcept;

consteval info member_pointer_type(info cls, info m) {
  if (!std::meta::is_function(m)) return dealias(substitute(^^data_member, {cls, type_of(m)}));
  std::vector<info> args{cls, return_type_of(m)};
  for (info p : parameters_of(m)) args.push_back(type_of(p));
  bool c = std::meta::is_const(m), n = std::meta::is_noexcept(m);
  info alias = c && n ? ^^const_noexcept_member_fn
               : c    ? ^^const_member_fn
               : n    ? ^^noexcept_member_fn
                      : ^^member_fn;
  return dealias(substitute(alias, args));
}

template <typename D, info M>
inline constexpr auto member_pointer =
    std::meta::extract<typename[:member_pointer_type(^^D, M):]>(M);
#endif

// Строка как массив const char[N]: StringRefStorage берёт размер из типа.
template <std::size_t N>
struct fixed_string {
  char data[N]{};
};
template <fixed_string S>
inline constexpr const auto& literal = S.data;

consteval std::size_t length(const char* s) {
  std::size_t n = 0;
  while (s[n]) ++n;
  return n;
}

template <std::size_t N>
consteval fixed_string<N> to_fixed(const char* s) {
  fixed_string<N> r;
  for (std::size_t i = 0; i < N; ++i) r.data[i] = s[i];
  return r;
}

consteval std::vector<std::size_t> iota(std::size_t n) {
  std::vector<std::size_t> v;
  for (std::size_t i = 0; i < n; ++i) v.push_back(i);
  return v;
}

// 0..N-1 для template for.
template <std::size_t N>
inline constexpr auto indices = std::define_static_array(iota(N));

// Первая буква имени в верхнем регистре, подстрокой литерала (EDG не любит
// временные std::string в константных вычислениях).
consteval std::string_view upper_first(std::string_view s) {
  if (s.empty()) return s;
  if (s[0] >= 'a' && s[0] <= 'z')
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZ"sv.substr(std::size_t(s[0] - 'a'), 1);
  return s.substr(0, 1);
}

// id == "set" + Property.
consteval bool is_setter_of(std::string_view id, std::string_view property) {
  return !property.empty() && id.size() == property.size() + 3 &&
         id.substr(0, 3) == "set"sv && id.substr(3, 1) == upper_first(property) &&
         id.substr(4) == property.substr(1);
}

// Порядок объявления в исходнике: EDG перечисляет в members_of сначала поля,
// потом функции, clang - как объявлено. Сигналы auto_property - поля, а
// обычные сигналы - функции, поэтому списки упорядочиваются по позиции.
consteval bool declared_before(info a, info b) {
  auto la = std::meta::source_location_of(a), lb = std::meta::source_location_of(b);
  return la.line() != lb.line() ? la.line() < lb.line() : la.column() < lb.column();
}

consteval void sort_by_declaration(std::vector<info>& v) {
  for (std::size_t i = 1; i < v.size(); ++i)
    for (std::size_t j = i; j > 0 && declared_before(v[j], v[j - 1]); --j) {
      info t = v[j];
      v[j] = v[j - 1];
      v[j - 1] = t;
    }
}

// --- состав метаобъекта

enum class method_kind { none, signal, slot, invokable };

consteval method_kind kind_of(info m) {
  if (is_auto_member(m)) return method_kind::signal;
  if (!std::meta::is_function(m) || std::meta::is_special_member_function(m))
    return method_kind::none;
  if (has(m, ^^signal_t)) return method_kind::signal;
  if (has(m, ^^slot_t)) return method_kind::slot;
  if (has(m, ^^invokable_t)) return method_kind::invokable;
  return method_kind::none;
}

template <typename D>
consteval std::vector<info> collect(method_kind k) {
  std::vector<info> v;
  for (info m : members_of(^^D, uc()))
    if (kind_of(m) == k) v.push_back(m);
  sort_by_declaration(v);
  return v;
}

// Методы в порядке moc: сигналы, слоты, остальные.
template <typename D>
consteval std::vector<info> methods_of() {
  std::vector<info> v = collect<D>(method_kind::signal);
  for (info m : collect<D>(method_kind::slot)) v.push_back(m);
  for (info m : collect<D>(method_kind::invokable)) v.push_back(m);
  return v;
}

// Свойства в порядке объявления, как строки Q_PROPERTY.
template <typename D>
consteval std::vector<info> properties_of() {
  std::vector<info> v;
  for (info m : members_of(^^D, uc())) {
    bool annotatable = std::meta::is_nonstatic_data_member(m) ||
                       (std::meta::is_function(m) && !std::meta::is_special_member_function(m));
    if (is_auto_member(m) || (annotatable && has(m, ^^property_t))) v.push_back(m);
  }
  sort_by_declaration(v);
  return v;
}

template <typename D>
inline constexpr auto methods = std::define_static_array(methods_of<D>());
template <typename D>
inline constexpr auto properties = std::define_static_array(properties_of<D>());
template <typename D>
inline constexpr std::size_t signal_count = collect<D>(method_kind::signal).size();
template <typename D>
inline constexpr std::size_t slot_count = collect<D>(method_kind::slot).size();

// Имя сигнала: у auto_property - <name>Changed.
consteval std::string method_name(info m) {
  std::string s(identifier_of(m));
  if (!std::meta::is_function(m)) s += "Changed";
  return s;
}

consteval info property_type(info p) {
  if (std::meta::is_function(p)) return std::meta::remove_cvref(return_type_of(p));
  if (is_auto_member(p)) return std::meta::remove_cvref(auto_value_type(p));
  return std::meta::remove_cvref(type_of(p));
}

// Имя с пространствами имён и объемлющими классами, как у moc. EDG уже
// квалифицирует display_string_of, а parent_of для пространства имён у него
// не работает.
consteval std::string qualified_name(info r) {
#ifdef __EDG__
  return std::string(display_string_of(r));
#else
  std::string s(identifier_of(r));
  for (info p = parent_of(r); has_identifier(p); p = parent_of(p)) {
    std::string q(identifier_of(p));
    q += "::";
    q += s;
    s = q;
  }
  return s;
#endif
}

consteval std::string type_name(info t) {
  t = std::meta::remove_cvref(t);
  if ((std::meta::is_class_type(t) || std::meta::is_enum_type(t)) && has_identifier(t) &&
      !std::meta::has_template_arguments(t))
    return qualified_name(t);
  return std::string(display_string_of(t));
}

// Индекс NOTIFY-сигнала <name>Changed в methods<D> или -1.
template <typename D>
consteval int notify_of(info p) {
  std::string n(identifier_of(p));
  n += "Changed";
  for (std::size_t i = 0; i < signal_count<D>; ++i)
    if (method_name(methods<D>[i]) == n) return int(i);
  return -1;
}

// Индекс сеттера set<Name> в members_of(D) или -1.
template <typename D>
consteval int setter_of(info p) {
  if (!std::meta::is_function(p)) return -1;
  std::vector<info> ms = members_of(^^D, uc());
  for (std::size_t i = 0; i < ms.size(); ++i)
    if (std::meta::is_function(ms[i]) && has_identifier(ms[i]) &&
        is_setter_of(identifier_of(ms[i]), identifier_of(p)) && parameters_of(ms[i]).size() == 1)
      return int(i);
  return -1;
}

template <typename D>
consteval info setter(info p) {
  return members_of(^^D, uc())[std::size_t(setter_of<D>(p))];
}

// --- таблица строк: 0 - имя класса, 1 - пустая строка, повторов нет

template <typename D>
consteval std::vector<std::string> strings_of() {
  std::vector<std::string> v;
  auto add = [&](const std::string& s) {
    for (const std::string& x : v)
      if (x == s) return;
    v.push_back(s);
  };
  add(qualified_name(^^D));
  add("");
  for (info m : methods_of<D>()) {
    add(method_name(m));
    if (std::meta::is_function(m)) {
      add(type_name(return_type_of(m)));
      for (info p : parameters_of(m)) {
        add(type_name(type_of(p)));
        if (has_identifier(p)) add(std::string(identifier_of(p)));
      }
    } else {
      add(type_name(auto_value_type(m)));
      add("value");
    }
  }
  for (info p : properties_of<D>()) {
    add(std::string(identifier_of(p)));
    add(type_name(property_type(p)));
  }
  return v;
}

template <typename D>
consteval std::vector<const char*> string_pointers_of() {
  std::vector<const char*> v;
  for (const std::string& s : strings_of<D>()) v.push_back(std::define_static_string(s));
  return v;
}

template <typename D>
inline constexpr auto strings = std::define_static_array(string_pointers_of<D>());

void string_not_in_table();

template <typename D>
consteval uint string_index(std::string_view s) {
  for (std::size_t i = 0; i < strings<D>.size(); ++i)
    if (std::string_view(strings<D>[i]) == s) return uint(i);
  string_not_in_table();
  return 0;
}

// Как string_index, но для отсутствующей строки - пустая (индекс 1).
template <typename D>
consteval uint string_index_or_empty(std::string_view s) {
  for (std::size_t i = 0; i < strings<D>.size(); ++i)
    if (std::string_view(strings<D>[i]) == s) return uint(i);
  return 1;
}

template <typename D>
consteval uint type_name_index(info t) {
  return string_index<D>(type_name(t));
}

template <typename D, std::size_t... I>
constexpr auto make_strings(std::index_sequence<I...>) {
  return QtMocHelpers::StringRefStorage{
      literal<to_fixed<length(strings<D>[I]) + 1>(strings<D>[I])>...};
}

// --- таблица методов

consteval info function_type(info m) {
  std::vector<info> args{return_type_of(m)};
  for (info p : parameters_of(m)) args.push_back(type_of(p));
  return dealias(substitute(std::meta::is_const(m) ? ^^const_fn : ^^fn, args));
}

template <template <typename> class Kind, typename D, info M, typename F>
constexpr auto function_data(uint access) {
  constexpr auto params = std::define_static_array(parameters_of(M));
  typename Kind<F>::ParametersArray array{};
  std::size_t k = 0;
  template for (constexpr auto p : params) {
    array[k++] = {type_id<typename[:type_of(p):]>(type_name_index<D>(type_of(p))),
                  has_identifier(p) ? string_index_or_empty<D>(identifier_of(p)) : 1u};
  }
  (void)k;
  return Kind<F>(string_index<D>(identifier_of(M)), 1, access,
                 type_id<typename[:return_type_of(M):]>(type_name_index<D>(return_type_of(M))),
                 array);
}

template <typename D, std::size_t I>
constexpr auto method_data() {
  namespace QMC = QtMocConstants;
  constexpr auto m = methods<D>[I];
  if constexpr (is_auto_member(m)) {
    using T = typename[:auto_value_type(m):];
    return QtMocHelpers::SignalData<void(const T&)>(
        string_index<D>(method_name(m)), 1, QMC::AccessPublic, QMetaType::Void,
        {{{type_id<T>(type_name_index<D>(^^T)), string_index<D>("value")}}});
  } else {
    using F = typename[:function_type(m):];
    constexpr uint access = std::meta::is_public(m)      ? QMC::AccessPublic
                            : std::meta::is_protected(m) ? QMC::AccessProtected
                                                         : QMC::AccessPrivate;
    if constexpr (I < signal_count<D>) {
      static_assert(return_type_of(m) == ^^void, "qr: a signal must return void");
      return function_data<QtMocHelpers::SignalData, D, m, F>(access);
    } else if constexpr (I < signal_count<D> + slot_count<D>) {
      return function_data<QtMocHelpers::SlotData, D, m, F>(access);
    } else {
      return function_data<QtMocHelpers::MethodData, D, m, F>(access);
    }
  }
}

template <typename D, std::size_t... I>
constexpr auto make_methods(std::index_sequence<I...>) {
  return QtMocHelpers::UintData{method_data<D, I>()...};
}

// --- таблица свойств

template <typename D, std::size_t I>
constexpr auto property_data() {
  namespace QMC = QtMocConstants;
  constexpr auto p = properties<D>[I];
  using T = typename[:property_type(p):];
  constexpr uint flags =
      !std::meta::is_function(p) ? uint(QMC::DefaultPropertyFlags | QMC::Writable)
      : setter_of<D>(p) >= 0     ? uint(QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet)
                                 : uint(QMC::DefaultPropertyFlags);
  constexpr int notify = notify_of<D>(p);
  constexpr uint name = string_index<D>(identifier_of(p));
  constexpr uint type = type_id<T>(type_name_index<D>(^^T));
  if constexpr (notify >= 0)
    return QtMocHelpers::PropertyData<T>(name, type, flags, uint(notify));
  else
    return QtMocHelpers::PropertyData<T>(name, type, flags);
}

template <typename D, std::size_t... I>
constexpr auto make_properties(std::index_sequence<I...>) {
  return QtMocHelpers::UintData{property_data<D, I>()...};
}

// --- метаобъект целиком

template <typename D>
concept gadget = requires { typename D::qr_gadget_derived; };

template <typename D>
struct meta_tag {};

template <typename D>
constexpr auto create_meta_object_data() {
  namespace QMC = QtMocConstants;
  QtMocHelpers::UintData enums{};
  constexpr uint flags = gadget<D> ? uint(QMC::PropertyAccessInStaticMetaCall) : 0u;
  return QtMocHelpers::metaObjectData<D, meta_tag<D>>(
      flags, make_strings<D>(std::make_index_sequence<strings<D>.size()>{}),
      make_methods<D>(std::make_index_sequence<methods<D>.size()>{}),
      make_properties<D>(std::make_index_sequence<properties<D>.size()>{}), enums);
}

template <typename D>
inline constexpr auto meta_object_data = create_meta_object_data<D>();

// --- тела сигналов: определения внедряются в пространство имён класса

void signals_need_class_in_namespace();

template <typename D>
consteval void inject_signal_bodies() {
  info ns = parent_of(^^D);
  std::string_view cn = identifier_of(^^D);
  for (std::size_t i = 0; i < signal_count<D>; ++i) {
    info m = methods<D>[i];
    if (!std::meta::is_function(m)) continue;
    if (!std::meta::is_namespace(ns)) signals_need_class_in_namespace();
    std::meta::list_builder params, args;
    unsigned j = 0;
    // Имена параметров - как в объявлении: иначе EDG после внедрения видит у
    // параметра другое имя, и таблица строк расходится с таблицей методов.
    for (info p : parameters_of(m)) {
      if (has_identifier(p)) {
        params += ^^{ typename \[:type_of(p):] \[identifier_of(p)] };
        args += ^^{ \[identifier_of(p)] };
      } else {
        params += ^^{ typename \[:type_of(p):] \["_t"sv, j] };
        args += ^^{ \["_t"sv, j] };
      }
      ++j;
    }
    info cq = std::meta::is_const(m) ? ^^{ const } : ^^{};
    info comma_args = j ? ^^{ , \{args} } : ^^{};
    std::string_view name = identifier_of(m);
    std::meta::namespace_inject(ns, ^^{
      inline void \[cn]::\[name](\{params}) \{cq} {
        QMetaObject::activate<void>(const_cast<\[cn]*>(this), &staticMetaObject,
                                    \val(unsigned(i)), nullptr \{comma_args});
      }
    });
  }
}

// --- qt_static_metacall, общий для QObject и гаджета

struct auto_offset {
  std::ptrdiff_t offset;
  int signal;
};

template <typename D>
consteval std::vector<auto_offset> auto_offsets_of() {
  std::vector<auto_offset> v;
  for (std::size_t i = 0; i < signal_count<D>; ++i)
    if (is_auto_member(methods<D>[i]))
      v.push_back({std::ptrdiff_t(std::meta::offset_of(methods<D>[i]).bytes), int(i)});
  return v;
}

template <typename D>
inline constexpr auto auto_offsets = std::define_static_array(auto_offsets_of<D>());

template <typename D>
void static_metacall(QObject* o, QMetaObject::Call c, int id, void** a) {
  D* t;
  if constexpr (gadget<D>)
    t = reinterpret_cast<D*>(o);
  else
    t = static_cast<D*>(o);
  (void)t;
  (void)id;
  (void)a;
  constexpr auto method_indices = indices<methods<D>.size()>;
  constexpr auto property_indices = indices<properties<D>.size()>;

  if (c == QMetaObject::InvokeMetaMethod) {
    template for (constexpr auto I : method_indices) {
      constexpr auto m = methods<D>[I];
      if (id == int(I)) {
        if constexpr (is_auto_member(m)) {
          // Вызов сигнала auto_property по имени - его испускание.
          if constexpr (!gadget<D>) QMetaObject::activate(t, &D::staticMetaObject, int(I), a);
        } else {
          constexpr auto ps = std::define_static_array(parameters_of(m));
          [&]<std::size_t... J>(std::index_sequence<J...>) {
            constexpr auto f = member_pointer<D, m>;
            if constexpr (return_type_of(m) == ^^void) {
              (t->*f)(*reinterpret_cast<std::remove_cvref_t<typename[:type_of(ps[J]):]>*>(
                  a[J + 1])...);
            } else {
              using R = std::remove_cvref_t<typename[:return_type_of(m):]>;
              R r = (t->*f)(*reinterpret_cast<std::remove_cvref_t<typename[:type_of(ps[J]):]>*>(
                  a[J + 1])...);
              if (a[0]) *reinterpret_cast<R*>(a[0]) = std::move(r);
            }
          }(std::make_index_sequence<ps.size()>{});
        }
      }
    }
  }

  if constexpr (!gadget<D>) {
    if (c == QMetaObject::IndexOfMethod) {
      using O = typename D::qr_object;
      template for (constexpr auto I : method_indices) {
        constexpr auto m = methods<D>[I];
        if constexpr (I < signal_count<D>) {
          if constexpr (is_auto_member(m)) {
            using T = typename[:auto_value_type(m):];
            if (QtMocHelpers::indexOfMethod<void (O::*)(const T&)>(
                    a, &O::template qr_changed<member_pointer<D, m>>, int(I)))
              return;
          } else {
            using F = std::remove_const_t<decltype(member_pointer<D, m>)>;
            if (QtMocHelpers::indexOfMethod<F>(a, member_pointer<D, m>, int(I))) return;
          }
        }
      }
    }
  }

  if (c == QMetaObject::ReadProperty) {
    void* v = a[0];
    template for (constexpr auto I : property_indices) {
      constexpr auto p = properties<D>[I];
      using T = typename[:property_type(p):];
      constexpr auto mp = member_pointer<D, p>;
      if (id == int(I)) {
        if constexpr (std::meta::is_function(p))
          *reinterpret_cast<T*>(v) = (t->*mp)();
        else if constexpr (is_auto_member(p))
          *reinterpret_cast<T*>(v) = (t->*mp).get();
        else
          *reinterpret_cast<T*>(v) = t->*mp;
      }
    }
  }

  if (c == QMetaObject::WriteProperty) {
    void* v = a[0];
    template for (constexpr auto I : property_indices) {
      constexpr auto p = properties<D>[I];
      using T = typename[:property_type(p):];
      constexpr auto mp = member_pointer<D, p>;
      if (id == int(I)) {
        if constexpr (is_auto_member(p)) {
          (t->*mp).set(*reinterpret_cast<T*>(v));
        } else if constexpr (std::meta::is_function(p)) {
          if constexpr (setter_of<D>(p) >= 0) {
            constexpr auto set = setter<D>(p);
            (t->*member_pointer<D, set>)(*reinterpret_cast<T*>(v));
          }
        } else if (QtMocHelpers::setProperty(t->*mp, *reinterpret_cast<T*>(v))) {
          // Как у moc для MEMBER: NOTIFY только при изменении значения.
          constexpr int notify = notify_of<D>(p);
          if constexpr (notify >= 0) {
            constexpr auto s = methods<D>[std::size_t(notify)];
            if constexpr (std::meta::is_function(s)) {
              constexpr auto sig = member_pointer<D, s>;
              constexpr std::size_t arity = parameters_of(s).size();
              if constexpr (arity == 0)
                (t->*sig)();
              else
                (t->*sig)(t->*mp);
            }
          }
        }
      }
    }
  }
}

struct no_base {
  bool operator==(const no_base&) const = default;
};

}  // namespace detail

// Тексты static_assert - ASCII: EDG выводит кириллицу как '?'.

// Вместо Q_OBJECT: class D : public qr::Object<D> или qr::Object<D, Base>,
// где Base - наследник QObject (свой qr::Object или класс с Q_OBJECT).
template <typename D, typename Base = QObject>
class Object : public Base {
  static_assert(std::is_base_of_v<QObject, Base>, "qr::Object<D, Base>: Base must derive from QObject");

public:
  using Base::Base;
  using qr_object = Object;
  using qr_derived = D;

  static const QMetaObject staticMetaObject;

  const QMetaObject* metaObject() const override {
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
  }

  void* qt_metacast(const char* name) override {
    if (!name) return nullptr;
    if (!std::strcmp(name, detail::strings<D>[0]))
      return static_cast<void*>(static_cast<D*>(this));
    return Base::qt_metacast(name);
  }

  int qt_metacall(QMetaObject::Call c, int id, void** a) override {
    id = Base::qt_metacall(c, id, a);
    if (id < 0) return id;
    constexpr int method_count = int(detail::methods<D>.size());
    constexpr int property_count = int(detail::properties<D>.size());
    if (c == QMetaObject::InvokeMetaMethod) {
      if (id < method_count) qt_static_metacall(this, c, id, a);
      id -= method_count;
    }
    if (c == QMetaObject::RegisterMethodArgumentMetaType) {
      if (id < method_count) *reinterpret_cast<QMetaType*>(a[0]) = QMetaType();
      id -= method_count;
    }
    if (c == QMetaObject::ReadProperty || c == QMetaObject::WriteProperty ||
        c == QMetaObject::ResetProperty || c == QMetaObject::BindableProperty ||
        c == QMetaObject::RegisterPropertyMetaType) {
      qt_static_metacall(this, c, id, a);
      id -= property_count;
    }
    return id;
  }

  static void qt_static_metacall(QObject* o, QMetaObject::Call c, int id, void** a);

  static inline QString tr(const char* s, const char* c = nullptr, int n = -1) {
    return staticMetaObject.tr(s, c, n);
  }

  // Сигнал <name>Changed свойства auto_property P = &D::name. Нужен как
  // указатель на функцию-член для connect (см. qr::changed); вызов испускает
  // сигнал с данным значением.
  template <auto P>
    requires std::is_same_v<typename detail::member_traits<decltype(P)>::owner, D>
  void qr_changed(const typename detail::member_traits<decltype(P)>::value& value) {
    D* self = static_cast<D*>(this);
    std::ptrdiff_t offset = reinterpret_cast<const char*>(std::addressof(self->*P)) -
                            reinterpret_cast<const char*>(self);
    void* args[] = {nullptr, const_cast<void*>(static_cast<const void*>(std::addressof(value)))};
    QMetaObject::activate(this, &staticMetaObject, qr_notify_index(offset), args);
  }

  // Номер сигнала auto_property по смещению поля в D.
  static int qr_notify_index(std::ptrdiff_t offset) {
    for (const detail::auto_offset& e : detail::auto_offsets<D>)
      if (e.offset == offset) return e.signal;
    return -1;
  }
};

template <typename D, typename Base>
const QMetaObject Object<D, Base>::staticMetaObject = {{
    QMetaObject::SuperData::link<Base::staticMetaObject>(),
    detail::meta_object_data<D>.staticData.stringdata,
    detail::meta_object_data<D>.staticData.data,
    qt_static_metacall,
    nullptr,
    detail::meta_object_data<D>.relocatingData.metaTypes,
    nullptr,
}};

template <typename D, typename Base>
void Object<D, Base>::qt_static_metacall(QObject* o, QMetaObject::Call c, int id, void** a) {
  static_assert(std::is_base_of_v<Object, D>, "qr::Object<D>: D must derive from qr::Object<D>");
  consteval { detail::inject_signal_bodies<D>(); }
  detail::static_metacall<D>(o, c, id, a);
}

// Вместо Q_GADGET: struct D : qr::Gadget<D> или qr::Gadget<D, Base>, где
// Base - другой гаджет. Свойства и invokable-методы, сигналов нет.
template <typename D, typename Base = void>
struct Gadget : std::conditional_t<std::is_void_v<Base>, detail::no_base, Base> {
  using qr_gadget_derived = D;

  static const QMetaObject staticMetaObject;
  static void qt_static_metacall(QObject* o, QMetaObject::Call c, int id, void** a);

  bool operator==(const Gadget&) const = default;
};

template <typename D, typename Base>
const QMetaObject Gadget<D, Base>::staticMetaObject = {{
    [] {
      if constexpr (std::is_void_v<Base>)
        return QMetaObject::SuperData(nullptr);
      else
        return QMetaObject::SuperData(QtPrivate::MetaObjectForType<Base>::value);
    }(),
    detail::meta_object_data<D>.staticData.stringdata,
    detail::meta_object_data<D>.staticData.data,
    qt_static_metacall,
    nullptr,
    detail::meta_object_data<D>.relocatingData.metaTypes,
    nullptr,
}};

template <typename D, typename Base>
void Gadget<D, Base>::qt_static_metacall(QObject* o, QMetaObject::Call c, int id, void** a) {
  static_assert(std::is_base_of_v<Gadget, D>, "qr::Gadget<D>: D must derive from qr::Gadget<D>");
  static_assert(detail::signal_count<D> == 0,
                "qr::Gadget: a gadget has no signals, [[=qr::signal]] and qr::auto_property are not allowed");
  detail::static_metacall<D>(o, c, id, a);
}

// Свойство с хранилищем и сигналом <name>Changed(const T&) - член класса
// qr::Object, инициализируется указателем на владельца:
//   qr::auto_property<QString> name{this};
//   qr::auto_property<int> age{this, 18};
// Запись (operator=, set, setProperty) испускает сигнал только при изменении
// значения. connect: QObject::connect(&obj, qr::changed<&D::name>, ...).
template <typename T>
class auto_property {
public:
  template <typename Owner>
  explicit auto_property(Owner* owner, T init = T{})
      : m_value(std::move(init)),
        m_owner(owner),
        m_meta_object(&Owner::staticMetaObject),
        m_signal(Owner::qr_notify_index(reinterpret_cast<const char*>(this) -
                                        reinterpret_cast<const char*>(owner))) {}

  // Без владельца сигнал не испустить.
#if __cpp_deleted_function >= 202403L
  auto_property() = delete("qr::auto_property needs its owner: declare it as name{this}");
#else
  auto_property() = delete;
#endif
  auto_property(const auto_property&) = delete;
  auto_property& operator=(const auto_property&) = delete;

  const T& get() const { return m_value; }
  operator const T&() const { return m_value; }
  const T* operator->() const { return std::addressof(m_value); }

  void set(const T& value) {
    if (m_value == value) return;
    m_value = value;
    notify();
  }
  auto_property& operator=(const T& value) {
    set(value);
    return *this;
  }

  // Испустить <name>Changed с текущим значением.
  void notify() {
    void* args[] = {nullptr, const_cast<void*>(static_cast<const void*>(std::addressof(m_value)))};
    QMetaObject::activate(m_owner, m_meta_object, m_signal, args);
  }

private:
  T m_value;
  QObject* m_owner;
  const QMetaObject* m_meta_object;
  int m_signal;
};

namespace detail {
template <auto P>
struct changed_signal {
  using object = typename member_traits<decltype(P)>::owner::qr_object;
  static constexpr auto value = &object::template qr_changed<P>;
};
}  // namespace detail

// Сигнал <name>Changed свойства auto_property для connect/disconnect:
//   QObject::connect(&person, qr::changed<&Person::name>, ctx, functor);
template <auto P>
inline constexpr auto changed = detail::changed_signal<P>::value;

}  // namespace qr

// Qt распознаёт Q_OBJECT и Q_GADGET по типу указателя на член
// (&T::qt_metacall, &T::qt_check_for_QGADGET_macro). В CRTP эти члены
// объявлены в базе, поэтому классы qr признаются явно.
namespace QtPrivate {

template <typename T>
  requires std::is_same_v<typename T::qr_derived, T>
struct HasQ_OBJECT_Macro<T> {
  enum { Value = true };
};

template <typename T>
  requires requires { typename T::qr_gadget_derived; }
struct IsGadgetHelper<T, void> {
  enum {
    IsRealGadget = std::is_same_v<typename T::qr_gadget_derived, T>,
    IsGadgetOrDerivedFrom = true
  };
};

template <typename T>
  requires requires { typename T::qr_gadget_derived; }
struct IsPointerToGadgetHelper<T*, void> {
  using BaseType = T;
  enum {
    IsRealGadget = std::is_same_v<typename T::qr_gadget_derived, T>,
    IsGadgetOrDerivedFrom = true
  };
};

}  // namespace QtPrivate
