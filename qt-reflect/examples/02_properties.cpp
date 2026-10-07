// Свойства: qr::auto_property, READ/WRITE через геттер и сеттер,
// наследование метаобъектов.
#include <qr/qobject.hpp>

#include <QtCore/qstring.h>

#include "dump.hpp"

namespace demo {

class Person : public qr::Object<Person> {
public:
  // Свойство с хранилищем и сигналом nameChanged(const QString&), который
  // есть только в метаобъекте.
  qr::auto_property<QString> name{this};
  qr::auto_property<int> age{this};

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
  [[=qr::property]] bool adult() const { return age >= 18; }

private:
  QString m_email;
};

// Наследник: свой метаобъект поверх метаобъекта Person.
class Employee : public qr::Object<Employee, Person> {
public:
  qr::auto_property<QString> position{this};

  [[=qr::slot]] void promote() { position = "senior " + position.get(); }
};

}  // namespace demo

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

  dump::section("auto_property: запись и сигналы");
  demo::Employee e;
  // Сигнал auto_property - не функция-член, указатель на него даёт qr::changed.
  QObject::connect(&e, qr::changed<&demo::Person::name>, [](const QString& n) {
    std::printf("nameChanged(%s)\n", qPrintable(n));
  });
  QObject::connect(&e, qr::changed<&demo::Employee::position>, [](const QString& p) {
    std::printf("positionChanged(%s)\n", qPrintable(p));
  });
  // Строковый connect тоже работает.
  QObject::connect(&e, SIGNAL(ageChanged(int)), &e, SLOT(promote()));
  QObject::connect(&e, &demo::Person::emailChanged,
                   [] { std::printf("emailChanged()\n"); });
  e.name = "Ada";
  e.name = "Ada";  // значение не изменилось - сигнала нет
  e.position = "engineer";
  e.age = 36;      // ageChanged -> promote()
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
