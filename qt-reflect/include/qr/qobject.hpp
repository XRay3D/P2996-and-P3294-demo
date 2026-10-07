// Метаобъекты Qt 6 без moc: сигналы, слоты и свойства размечаются
// аннотациями, а код, который обычно пишет moc, внедряется token injection.
//
//   class Counter : public QObject {
//     consteval { qr::object(); }              // вместо Q_OBJECT
//   public:
//     [[=qr::signal]]    void valueChanged(int value);
//     [[=qr::slot]]      void reset();
//     [[=qr::invokable]] int twice(int x) const;
//     [[=qr::property]]  int value = 0;        // свойство MEMBER
//     consteval { qr::auto_property(^^QString, "title"); }
//   };
//   consteval { qr::meta_object(^^Counter); }  // после класса
//
// Гаджет (Q_GADGET) - то же без QObject: свойства и invokable-методы,
// сигналов нет.
//
//   struct Point {
//     consteval { qr::gadget(); }               // вместо Q_GADGET
//   public:
//     [[=qr::property]] double x = 0;
//   };
//   consteval { qr::meta_object(^^Point); }
//
// Внутри своего consteval-блока класс ещё неполный, поэтому там внедряются
// только объявления (qr::object, qr::gadget, qr::auto_property). Таблицы метаданных,
// qt_static_metacall, qt_metacall и тела сигналов генерирует
// qr::meta_object, когда класс уже определён. Формат данных тот же, что у moc
// 6.11: QtMocHelpers::metaObjectData из <QtCore/qtmochelpers.h>.
//
// Соглашения (как у Q_PROPERTY):
//   [[=qr::property]] на поле           - MEMBER-свойство;
//   [[=qr::property]] на const-геттере  - READ-свойство, WRITE - метод
//                                         set<Name>, если он есть;
//   NOTIFY - сигнал <name>Changed, если он есть.
#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qtmochelpers.h>
#include <QtCore/qvariant.h>

#include <experimental/meta>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
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

namespace detail {

using std::meta::info;
using namespace std::string_view_literals;

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

template <typename R, typename... Args>
using fn = R(Args...);

template <typename R, typename... Args>
struct const_fn {
  using type = R(Args...) const;
};

// Вызов не-constexpr функции из consteval - ошибка компиляции с её именем.
void signal_must_return_void();
void class_must_be_in_namespace();
void class_must_derive_from_qobject();
void gadget_cannot_have_signals();

// EDG: annotations_of_with_type(r, type); clang-p2996: перегрузка
// annotations_of(r, type).
consteval bool has(info r, info annotation_type) {
#ifdef __EDG__
  return !std::meta::annotations_of_with_type(r, annotation_type).empty();
#else
  return !std::meta::annotations_of(r, annotation_type).empty();
#endif
}

// Первая буква имени в верхнем регистре. EDG принимает в \[...] только
// string_view, указывающие в строковые литералы (не в define_static_string и
// не в constexpr-массивы), поэтому буква берётся подстрокой литерала.
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

// Таблица строк метаобъекта: индекс 0 - имя класса, повторы не добавляются.
struct string_table {
  std::vector<std::string_view> items;

  consteval unsigned operator()(std::string_view s) {
    for (unsigned i = 0; i < items.size(); ++i)
      if (items[i] == s) return i;
    items.push_back(s);
    return unsigned(items.size() - 1);
  }
};

consteval std::string_view type_name(info type) {
  return std::meta::display_string_of(std::meta::remove_cvref(type));
}

// Порядок объявления в исходнике: EDG перечисляет в members_of сначала поля,
// потом функции, clang - как объявлено. Свойства-поля и свойства-геттеры
// лежат в одном списке, поэтому он упорядочивается по позиции.
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

// Индекс первого метода с данным именем или -1.
consteval int find_by_name(const std::vector<info>& fns, std::string_view name) {
  for (unsigned i = 0; i < fns.size(); ++i)
    if (std::meta::identifier_of(fns[i]) == name) return int(i);
  return -1;
}

}  // namespace detail

// Объявления, которые разворачивает Q_OBJECT. Вызывается в consteval-блоке
// класса; после него доступ - private, как после Q_OBJECT.
consteval void object() {
  std::meta::queue_injection(^^{
    public:
      static const QMetaObject staticMetaObject;
      virtual const QMetaObject* metaObject() const;
      virtual void* qt_metacast(const char*);
      virtual int qt_metacall(QMetaObject::Call, int, void**);
      static inline QString tr(const char* s, const char* c = nullptr, int n = -1) {
        return staticMetaObject.tr(s, c, n);
      }
    private:
      template <typename> static constexpr auto qt_create_metaobjectdata();
      template <typename Tag> static constexpr inline auto
          qt_staticMetaObjectContent = qt_create_metaobjectdata<Tag>();
      template <typename Tag> static constexpr inline auto
          qt_staticMetaObjectStaticContent = qt_staticMetaObjectContent<Tag>.staticData;
      template <typename Tag> static constexpr inline auto
          qt_staticMetaObjectRelocatingContent = qt_staticMetaObjectContent<Tag>.relocatingData;
      static void qt_static_metacall(QObject*, QMetaObject::Call, int, void**);
      struct QPrivateSignal { explicit QPrivateSignal() = default; };
  });
}

// Объявления, которые разворачивает Q_GADGET. Вызывается в consteval-блоке
// класса; после него доступ - private, как после Q_GADGET. По
// qt_check_for_QGADGET_macro и QtGadgetHelper QMetaType узнаёт гаджет, а
// qr::meta_object отличает его от QObject.
consteval void gadget() {
  std::meta::queue_injection(^^{
    public:
      static const QMetaObject staticMetaObject;
      void qt_check_for_QGADGET_macro();
      typedef void QtGadgetHelper;
    private:
      template <typename> static constexpr auto qt_create_metaobjectdata();
      template <typename Tag> static constexpr inline auto
          qt_staticMetaObjectContent = qt_create_metaobjectdata<Tag>();
      template <typename Tag> static constexpr inline auto
          qt_staticMetaObjectStaticContent = qt_staticMetaObjectContent<Tag>.staticData;
      template <typename Tag> static constexpr inline auto
          qt_staticMetaObjectRelocatingContent = qt_staticMetaObjectContent<Tag>.relocatingData;
      static void qt_static_metacall(QObject*, QMetaObject::Call, int, void**);
  });
}

// Свойство с хранилищем m_<name>, геттером <name>(), сеттером set<Name>() и
// сигналом <name>Changed. Вызывается в consteval-блоке класса; после него
// доступ - public.
consteval void auto_property(std::meta::info type, std::string_view name) {
  using namespace std::string_view_literals;
  std::string_view first = detail::upper_first(name), rest = name.substr(1);
  std::meta::queue_injection(^^{
    private:
      typename \[:type:] \["m_"sv, name]{};
    public:
      [[=::qr::property]] typename \[:type:] \[name]() const {
        return \["m_"sv, name];
      }
      void \["set"sv, first, rest](typename \[:type:] const& value) {
        if (\["m_"sv, name] == value) return;
        \["m_"sv, name] = value;
        \[name, "Changed"sv](\["m_"sv, name]);
      }
      [[=::qr::signal]] void \[name, "Changed"sv](typename \[:type:] const& value);
  });
}

// Всё, что moc пишет в moc_*.cpp. Вызывается после определения класса.
// Подходит и для QObject (qr::object, Q_OBJECT), и для гаджета (qr::gadget,
// Q_GADGET).
consteval void meta_object(std::meta::info cls) {
  using namespace std::meta;
  using namespace std::string_view_literals;
  using detail::has;

  info ns = parent_of(cls);
  if (!is_namespace(ns)) detail::class_must_be_in_namespace();
  // Гаджет объявляет qt_check_for_QGADGET_macro у себя, а не наследует:
  // members_of возвращает только собственные члены.
  bool gadget = false;
  for (info m : members_of(cls, access_context::unchecked()))
    if (has_identifier(m) && identifier_of(m) == "qt_check_for_QGADGET_macro"sv)
      gadget = true;
  // Базовый метаобъект - у первой базы. Гаджету база не обязательна.
  std::vector<info> bases = bases_of(cls, access_context::unchecked());
  if (bases.empty() && !gadget) detail::class_must_derive_from_qobject();
  const bool has_base = !bases.empty();
  info base = has_base ? type_of(bases[0]) : ^^void;
  std::string_view cn = identifier_of(cls);

  // Методы в порядке moc: сигналы, слоты, остальные.
  std::vector<info> signals_, slots_, invokables, props;
  for (info m : members_of(cls, access_context::unchecked())) {
    if (is_function(m) && !is_special_member_function(m)) {
      if (has(m, ^^signal_t)) signals_.push_back(m);
      else if (has(m, ^^slot_t)) slots_.push_back(m);
      else if (has(m, ^^invokable_t)) invokables.push_back(m);
      if (has(m, ^^property_t)) props.push_back(m);
    } else if (is_nonstatic_data_member(m) && has(m, ^^property_t)) {
      props.push_back(m);
    }
  }
  detail::sort_by_declaration(props);
  if (gadget && !signals_.empty()) detail::gadget_cannot_have_signals();
  std::vector<info> methods = signals_;
  methods.insert(methods.end(), slots_.begin(), slots_.end());
  methods.insert(methods.end(), invokables.begin(), invokables.end());
  // Кандидаты в сеттеры. Конструкторы и деструкторы без имени: у clang
  // identifier_of для них не константное выражение.
  std::vector<info> all_functions;
  for (info m : members_of(cls, access_context::unchecked()))
    if (is_function(m) && has_identifier(m)) all_functions.push_back(m);

  detail::string_table str;
  str(display_string_of(cls));
  const unsigned empty = str(""sv);

  // --- таблица методов и qt_static_metacall(InvokeMetaMethod / IndexOfMethod)
  // Ветки switch и операторы идут без разделителя, элементы таблиц - через ','.
  list_builder method_data, invoke_cases(^^{}), index_of(^^{});
  for (unsigned i = 0; i < methods.size(); ++i) {
    info m = methods[i];
    info kind = i < signals_.size()                     ? ^^{ QtMocHelpers::SignalData }
                : i < signals_.size() + slots_.size()   ? ^^{ QtMocHelpers::SlotData }
                                                        : ^^{ QtMocHelpers::MethodData };
    info access = is_public(m)    ? ^^{ QMC::AccessPublic }
                : is_protected(m) ? ^^{ QMC::AccessProtected }
                                  : ^^{ QMC::AccessPrivate };
    info ret = return_type_of(m);
    unsigned name = str(identifier_of(m));
    unsigned ret_name = str(detail::type_name(ret));

    list_builder params, args, param_types;
    unsigned j = 0;
    for (info p : parameters_of(m)) {
      info t = type_of(p);
      param_types += ^^{ typename \[:t:] };
      unsigned tn = str(detail::type_name(t));
      unsigned pn = has_identifier(p) ? str(identifier_of(p)) : empty;
      params += ^^{ { ::qr::detail::type_id<typename \[:t:]>(\val(tn)), \val(pn) } };
      args += ^^{
        *reinterpret_cast<std::add_pointer_t<std::remove_cvref_t<typename \[:t:]>>>(_a[\val(j + 1)])
      };
      ++j;
    }
    info param_array = j ? ^^{ , {{ \{params} }} } : ^^{};
    // Тип функции для SignalData<R(Args...)> собирается заново: сплайс
    // type_of(m) EDG не сопоставляет с частичной специализацией R(Args...).
    info comma_types = j ? ^^{ , \{param_types} } : ^^{};
    info fn_type = is_const(m)
        ? ^^{ typename ::qr::detail::const_fn<typename \[:ret:] \{comma_types}>::type }
        : ^^{ ::qr::detail::fn<typename \[:ret:] \{comma_types}> };

    method_data += ^^{
      \{kind}<\{fn_type}>(\val(name), \val(empty), \{access},
          ::qr::detail::type_id<typename \[:ret:]>(\val(ret_name)) \{param_array})
    };

    if (ret == ^^void) {
      invoke_cases += ^^{ case \val(i): _t->\[:m:](\{args}); break; };
    } else {
      invoke_cases += ^^{
        case \val(i): {
          using _R = std::remove_cvref_t<typename \[:ret:]>;
          _R _r = _t->\[:m:](\{args});
          if (_a[0]) *reinterpret_cast<_R*>(_a[0]) = std::move(_r);
        } break;
      };
    }

    if (i < signals_.size()) {
      if (ret != ^^void) detail::signal_must_return_void();
      index_of += ^^{
        if (QtMocHelpers::indexOfMethod<decltype(&\[:m:])>(_a, &\[:m:], \val(i)))
          return;
      };
    }
  }

  // --- свойства и qt_static_metacall(Read/WriteProperty)
  list_builder property_data, read_cases(^^{}), write_cases(^^{});
  bool has_writable = false;
  for (unsigned i = 0; i < props.size(); ++i) {
    info p = props[i];
    std::string_view pname = identifier_of(p);
    bool member = !is_function(p);
    info type = remove_cvref(member ? type_of(p) : return_type_of(p));
    unsigned name = str(pname);
    unsigned tn = str(detail::type_name(type));

    std::string notify_name(pname);
    notify_name += "Changed";
    int notify = detail::find_by_name(signals_, notify_name);
    int setter = -1;
    if (!member)
      for (unsigned k = 0; k < all_functions.size(); ++k)
        if (detail::is_setter_of(identifier_of(all_functions[k]), pname) &&
            parameters_of(all_functions[k]).size() == 1) {
          setter = int(k);
          break;
        }

    info flags = member      ? ^^{ QMC::DefaultPropertyFlags | QMC::Writable }
                 : setter >= 0 ? ^^{ QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet }
                               : ^^{ QMC::DefaultPropertyFlags };
    info notify_arg = notify >= 0 ? ^^{ , \val(unsigned(notify)) } : ^^{};
    property_data += ^^{
      QtMocHelpers::PropertyData<typename \[:type:]>(\val(name),
          ::qr::detail::type_id<typename \[:type:]>(\val(tn)), \{flags} \{notify_arg})
    };

    if (member) {
      read_cases += ^^{
        case \val(i): *reinterpret_cast<typename \[:type:]*>(_v) = _t->\[:p:]; break;
      };
      info notify_call = ^^{};
      if (notify >= 0) {
        info sig = signals_[unsigned(notify)];
        // EDG: временный vector в операнде ?: разрушается неверно,
        // поэтому размер считается отдельно.
        std::size_t arity = parameters_of(sig).size();
        if (arity == 0)
          notify_call = ^^{ _t->\[:sig:](); };
        else
          notify_call = ^^{ _t->\[:sig:](_t->\[:p:]); };
      }
      has_writable = true;
      write_cases += ^^{
        case \val(i):
          if (QtMocHelpers::setProperty(_t->\[:p:], *reinterpret_cast<typename \[:type:]*>(_v)))
            \{notify_call}
          break;
      };
    } else {
      read_cases += ^^{
        case \val(i): *reinterpret_cast<typename \[:type:]*>(_v) = _t->\[:p:](); break;
      };
      if (setter >= 0) {
        info set = all_functions[unsigned(setter)];
        has_writable = true;
        write_cases += ^^{
          case \val(i): _t->\[:set:](*reinterpret_cast<typename \[:type:]*>(_v)); break;
        };
      }
    }
  }

  // --- тела сигналов
  std::vector<info> signal_bodies;
  for (unsigned i = 0; i < signals_.size(); ++i) {
    info m = signals_[i];
    list_builder params, args;
    unsigned j = 0;
    for (info p : parameters_of(m)) {
      params += ^^{ typename \[:type_of(p):] \["_t"sv, j] };
      args += ^^{ \["_t"sv, j] };
      ++j;
    }
    info cq = is_const(m) ? ^^{ const } : ^^{};
    info comma_args = j ? ^^{ , \{args} } : ^^{};
    signal_bodies.push_back(^^{
      void \[cn]::\[identifier_of(m)](\{params}) \{cq} {
        QMetaObject::activate<void>(const_cast<\[cn]*>(this), &staticMetaObject,
                                    \val(i), nullptr \{comma_args});
      }
    });
  }

  list_builder strings;
  for (std::string_view s : str.items) strings += ^^{ \str(s) };

  const unsigned method_count = unsigned(methods.size());
  const unsigned property_count = unsigned(props.size());
  info metacall_methods = method_count ? ^^{
    if (_c == QMetaObject::InvokeMetaMethod) {
      if (_id < \val(method_count)) qt_static_metacall(this, _c, _id, _a);
      _id -= \val(method_count);
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
      if (_id < \val(method_count)) *reinterpret_cast<QMetaType*>(_a[0]) = QMetaType();
      _id -= \val(method_count);
    }
  } : ^^{};
  info metacall_properties = property_count ? ^^{
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty ||
        _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty ||
        _c == QMetaObject::RegisterPropertyMetaType) {
      qt_static_metacall(this, _c, _id, _a);
      _id -= \val(property_count);
    }
  } : ^^{};

  // Пустые ветки не генерируются (moc поступает так же).
  info invoke_block = ^^{}, read_block = ^^{}, write_block = ^^{};
  if (method_count) {
    invoke_block = ^^{
      if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
          \{invoke_cases}
          default: ;
        }
      }
      if (_c == QMetaObject::IndexOfMethod) {
        \{index_of}
      }
    };
  }
  if (property_count) {
    read_block = ^^{
      if (_c == QMetaObject::ReadProperty) {
        void* _v = _a[0];
        switch (_id) {
          \{read_cases}
          default: break;
        }
      }
    };
  }
  if (has_writable) {
    write_block = ^^{
      if (_c == QMetaObject::WriteProperty) {
        void* _v = _a[0];
        switch (_id) {
          \{write_cases}
          default: break;
        }
      }
    };
  }

  // Отличия гаджета, как у moc: флаг PropertyAccessInStaticMetaCall, база -
  // метаобъект первой базы или nullptr (MetaObjectForType даёт nullptr и для
  // базы без метаобъекта), _o на деле указывает на гаджет, а не на QObject.
  info object_flags = ^^{ QMC::MetaObjectFlag{} };
  info super_data = ^^{ nullptr };
  info this_cast = ^^{ auto* _t = static_cast<\[cn]*>(_o); };
  if (gadget) {
    object_flags = ^^{ QMC::PropertyAccessInStaticMetaCall };
    this_cast = ^^{ auto* _t = reinterpret_cast<\[cn]*>(_o); };
    if (has_base)
      super_data = ^^{ QtPrivate::MetaObjectForType<\["qr_meta_base_"sv, cn]>::value };
  } else {
    super_data = ^^{
      QMetaObject::SuperData::link<\["qr_meta_base_"sv, cn]::staticMetaObject>()
    };
  }

  namespace_inject(ns, ^^{
    struct \["qr_meta_tag_"sv, cn, "_t"sv] {};
  });
  if (has_base) {
    namespace_inject(ns, ^^{
      using \["qr_meta_base_"sv, cn] = typename \[:base:];
    });
  }
  namespace_inject(ns, ^^{
    template <> constexpr inline auto
    \[cn]::qt_create_metaobjectdata<\["qr_meta_tag_"sv, cn, "_t"sv]>() {
      namespace QMC = QtMocConstants;
      QtMocHelpers::StringRefStorage qt_stringData{ \{strings} };
      QtMocHelpers::UintData qt_methods{ \{method_data} };
      QtMocHelpers::UintData qt_properties{ \{property_data} };
      QtMocHelpers::UintData qt_enums{};
      return QtMocHelpers::metaObjectData<\[cn], \["qr_meta_tag_"sv, cn, "_t"sv]>(
          \{object_flags}, qt_stringData, qt_methods, qt_properties, qt_enums);
    }
  });
  namespace_inject(ns, ^^{
    const QMetaObject \[cn]::staticMetaObject = { {
      \{super_data},
      qt_staticMetaObjectStaticContent<\["qr_meta_tag_"sv, cn, "_t"sv]>.stringdata,
      qt_staticMetaObjectStaticContent<\["qr_meta_tag_"sv, cn, "_t"sv]>.data,
      qt_static_metacall,
      nullptr,
      qt_staticMetaObjectRelocatingContent<\["qr_meta_tag_"sv, cn, "_t"sv]>.metaTypes,
      nullptr
    } };
  });
  namespace_inject(ns, ^^{
    void \[cn]::qt_static_metacall(QObject* _o, QMetaObject::Call _c, int _id, void** _a) {
      \{this_cast}
      (void)_t; (void)_id; (void)_a;
      \{invoke_block}
      \{read_block}
      \{write_block}
    }
  });
  // Остальное есть только у QObject. Сигналов у гаджета нет (см. выше).
  if (gadget) return;
  namespace_inject(ns, ^^{
    const QMetaObject* \[cn]::metaObject() const {
      return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject()
                                        : &staticMetaObject;
    }
  });
  namespace_inject(ns, ^^{
    void* \[cn]::qt_metacast(const char* _clname) {
      if (!_clname) return nullptr;
      if (!std::strcmp(_clname,
              qt_staticMetaObjectStaticContent<\["qr_meta_tag_"sv, cn, "_t"sv]>.strings))
        return static_cast<void*>(this);
      return \["qr_meta_base_"sv, cn]::qt_metacast(_clname);
    }
  });
  namespace_inject(ns, ^^{
    int \[cn]::qt_metacall(QMetaObject::Call _c, int _id, void** _a) {
      _id = \["qr_meta_base_"sv, cn]::qt_metacall(_c, _id, _a);
      if (_id < 0) return _id;
      \{metacall_methods}
      \{metacall_properties}
      return _id;
    }
  });
  for (info body : signal_bodies) namespace_inject(ns, body);
}

}  // namespace qr
