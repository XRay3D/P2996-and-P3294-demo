// Свойства: сгенерированные qr::auto_property, READ/WRITE через геттер и
// сеттер, наследование метаобъектов.
#include <qr/qobject.hpp>

#include <QtCore/qstring.h>

#include "dump.hpp"

namespace demo {

class Person : public QObject {
  consteval {
    qr::object();
    // Поле m_name, name(), setName(), сигнал nameChanged(const QString&).
    qr::auto_property(^^QString, "name");
    qr::auto_property(^^int, "age");
  }

public:
  // READ-свойство: const-геттер с аннотацией. WRITE - найденный по имени
  // setEmail, NOTIFY - сигнал emailChanged.
  [[=qr::property]] QString email() const { return m_email; }
  void setEmail(const QString& e) {
    if (!e.contains(u'@')) return;
    m_email = e;
    emailChanged();
  }
  [[=qr::signal]] void emailChanged();

  // Только чтение: ни сеттера, ни сигнала.
  [[=qr::property]] bool adult() const { return age() >= 18; }

private:
  QString m_email;
};

// Наследник: свой метаобъект поверх метаобъекта Person.
class Employee : public Person {
  consteval {
    qr::object();
    qr::auto_property(^^QString, "position");
  }

public:
  [[=qr::slot]] void promote() { setPosition("senior " + position()); }
};

}  // namespace demo

consteval {
  qr::meta_object(^^demo::Person);
  qr::meta_object(^^demo::Employee);
}

static void print(const QObject& o) {
  const QMetaObject* mo = o.metaObject();
  std::printf("%s {", mo->className());
  for (int i = QObject::staticMetaObject.propertyCount(); i < mo->propertyCount(); ++i) {
    QMetaProperty p = mo->property(i);
    std::printf(" %s=%s", p.name(), qPrintable(p.read(&o).toString()));
  }
  std::printf(" }\n");
}

int main() {
  dump::section("метаобъекты");
  dump::meta_object(demo::Person::staticMetaObject);
  dump::meta_object(demo::Employee::staticMetaObject);

  dump::section("сгенерированные геттеры, сеттеры и сигналы");
  demo::Employee e;
  QObject::connect(&e, &demo::Person::nameChanged, [](const QString& n) {
    std::printf("nameChanged(%s)\n", qPrintable(n));
  });
  QObject::connect(&e, &demo::Employee::positionChanged, [](const QString& p) {
    std::printf("positionChanged(%s)\n", qPrintable(p));
  });
  QObject::connect(&e, &demo::Person::emailChanged,
                   [] { std::printf("emailChanged()\n"); });
  e.setName("Ada");
  e.setName("Ada");  // значение не изменилось - сигнала нет
  e.setAge(36);
  e.setPosition("engineer");
  print(e);

  dump::section("запись по имени через метаобъект");
  e.setProperty("name", QString("Grace"));
  e.setProperty("email", QString("grace@navy.mil"));
  e.setProperty("email", QString("invalid"));  // сеттер отказывает
  bool ok = e.setProperty("adult", false);     // свойство только для чтения
  std::printf("setProperty(adult) = %s\n", ok ? "true" : "false");
  QMetaObject::invokeMethod(&e, "promote");
  print(e);

  dump::section("qobject_cast по цепочке наследования");
  QObject* o = &e;
  std::printf("Person*: %s, Employee*: %s, inherits(\"demo::Person\"): %s\n",
              qobject_cast<demo::Person*>(o) ? "ok" : "null",
              qobject_cast<demo::Employee*>(o) ? "ok" : "null",
              o->inherits("demo::Person") ? "true" : "false");
}
