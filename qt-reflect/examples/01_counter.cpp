// Сигналы, слоты и MEMBER-свойство, размеченные аннотациями, без moc.
#include <qr/qobject.hpp>

#include "dump.hpp"

namespace demo {

class Counter : public QObject {
  consteval { qr::object(); }  // вместо Q_OBJECT

public:
  [[=qr::signal]] void valueChanged(int value);
  [[=qr::signal]] void overflow();

  [[=qr::slot]] void setValue(int v) {
    if (value == v) return;
    value = v;
    valueChanged(v);
    if (value > limit) overflow();
  }
  [[=qr::slot]] void increment() { setValue(value + 1); }
  [[=qr::slot]] void reset() { setValue(0); }

  // MEMBER-свойство: чтение и запись поля, NOTIFY - valueChanged.
  [[=qr::property]] int value = 0;
  [[=qr::property]] int limit = 3;

  // Не размечено - в метаобъект не попадает.
  void helper() {}
};

}  // namespace demo

// Всё, что moc написал бы в moc_counter.cpp.
consteval { qr::meta_object(^^demo::Counter); }

int main() {
  demo::Counter a, b;

  dump::section("метаобъект");
  dump::meta_object(demo::Counter::staticMetaObject);

  dump::section("connect по указателям на члены");
  QObject::connect(&a, &demo::Counter::valueChanged,
                   [](int v) { std::printf("a.valueChanged(%d)\n", v); });
  QObject::connect(&a, &demo::Counter::overflow,
                   [] { std::printf("a.overflow()\n"); });
  QObject::connect(&a, &demo::Counter::valueChanged, &b, &demo::Counter::setValue);
  a.increment();
  a.increment();
  std::printf("b.value = %d\n", b.value);

  dump::section("connect по строкам SIGNAL()/SLOT()");
  QObject::connect(&a, SIGNAL(overflow()), &b, SLOT(reset()));
  a.setValue(10);
  std::printf("b.value = %d\n", b.value);

  dump::section("свойства через QVariant");
  a.setProperty("value", 2);  // WriteProperty + NOTIFY
  std::printf("a.property(\"value\") = %d, limit = %d\n",
              a.property("value").toInt(), a.property("limit").toInt());

  dump::section("invokeMethod по имени");
  QMetaObject::invokeMethod(&a, "reset");
  std::printf("a.value = %d\n", a.value);
  std::printf("qobject_cast: %s\n",
              qobject_cast<demo::Counter*>(static_cast<QObject*>(&a)) ? "ok" : "null");
}
