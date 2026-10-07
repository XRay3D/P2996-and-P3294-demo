// Гаджеты: qr::gadget() вместо Q_GADGET. Свойства и invokable-методы у
// класса без QObject, доступ через readOnGadget/writeOnGadget/invokeOnGadget,
// наследование, гаджет в QVariant и как тип свойства QObject.
#include <qr/qobject.hpp>

#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#include <cmath>

#include "dump.hpp"

namespace demo {

struct Point {
  consteval { qr::gadget(); }

public:
  [[=qr::property]] double x = 0;
  [[=qr::property]] double y = 0;

  // READ-свойство, WRITE - setLabel.
  [[=qr::property]] QString label() const { return m_label; }
  void setLabel(const QString& l) { m_label = l.trimmed(); }

  // Только чтение.
  [[=qr::property]] double length() const { return std::hypot(x, y); }

  [[=qr::invokable]] void scale(double k) {
    x *= k;
    y *= k;
  }
  [[=qr::invokable]] QString toString() const {
    return QString("%1(%2, %3)").arg(m_label).arg(x).arg(y);
  }

  // Нужен записи Point в MEMBER-свойство QObject: значение пишется и
  // сигнал эмитится только при изменении.
  bool operator==(const Point&) const = default;

private:
  QString m_label;
};

// Наследник: свой метаобъект, база - метаобъект Point.
struct Point3D : Point {
  consteval { qr::gadget(); }

public:
  [[=qr::property]] double z = 0;
};

// Обычный Q_GADGET тоже подходит: qr::meta_object заменяет только moc.
struct Size {
  Q_GADGET

public:
  [[=qr::property]] int width = 0;
  [[=qr::property]] int height = 0;
};

// Гаджет как тип свойства QObject.
class Marker : public QObject {
  consteval { qr::object(); }

public:
  [[=qr::property]] Point pos;
  [[=qr::signal]] void posChanged(const Point& pos);
};

}  // namespace demo

consteval {
  qr::meta_object(^^demo::Point);
  qr::meta_object(^^demo::Point3D);
  qr::meta_object(^^demo::Size);
  qr::meta_object(^^demo::Marker);
}

// Все свойства гаджета, включая унаследованные: у гаджета нет базы QObject,
// и свойства базы начинаются с индекса 0.
static void print(const QMetaObject& mo, const void* gadget) {
  std::printf("%s {", mo.className());
  for (int i = 0; i < mo.propertyCount(); ++i) {
    QMetaProperty p = mo.property(i);
    std::printf(" %s=%s", p.name(), qPrintable(p.readOnGadget(gadget).toString()));
  }
  std::printf(" }\n");
}

static bool write(void* gadget, const QMetaObject& mo, const char* name, const QVariant& v) {
  return mo.property(mo.indexOfProperty(name)).writeOnGadget(gadget, v);
}

int main() {
  const QMetaObject& pmo = demo::Point::staticMetaObject;

  dump::section("метаобъекты");
  dump::meta_object(pmo);
  dump::meta_object(demo::Point3D::staticMetaObject);
  dump::meta_object(demo::Size::staticMetaObject);

  dump::section("QMetaType узнаёт гаджет");
  QMetaType t = QMetaType::fromType<demo::Point>();
  std::printf("%s: IsGadget=%s, metaObject=%s\n", t.name(),
              t.flags() & QMetaType::IsGadget ? "true" : "false",
              t.metaObject() == &pmo ? "&Point::staticMetaObject" : "?");

  dump::section("свойства по имени: readOnGadget / writeOnGadget");
  demo::Point p;
  write(&p, pmo, "x", 3.0);
  write(&p, pmo, "y", 4.0);
  write(&p, pmo, "label", QString("  A  "));  // через setLabel
  bool ok = write(&p, pmo, "length", 10.0);    // только для чтения
  std::printf("writeOnGadget(length) = %s\n", ok ? "true" : "false");
  print(pmo, &p);

  dump::section("методы: invokeOnGadget");
  pmo.method(pmo.indexOfMethod("scale(double)")).invokeOnGadget(&p, 2.0);
  QString s;
  pmo.method(pmo.indexOfMethod("toString()")).invokeOnGadget(&p, qReturnArg(s));
  std::printf("toString() = %s\n", qPrintable(s));

  dump::section("наследник");
  demo::Point3D q;
  const QMetaObject& qmo = demo::Point3D::staticMetaObject;
  write(&q, qmo, "x", 1.0);  // свойство базы через метаобъект наследника
  write(&q, qmo, "z", 2.0);
  print(qmo, &q);
  demo::Size sz;
  write(&sz, demo::Size::staticMetaObject, "width", 640);
  write(&sz, demo::Size::staticMetaObject, "height", 480);
  print(demo::Size::staticMetaObject, &sz);

  dump::section("гаджет в QVariant");
  QVariant v = QVariant::fromValue(p);
  print(*v.metaType().metaObject(), v.constData());

  dump::section("гаджет как свойство QObject");
  demo::Marker m;
  QObject::connect(&m, &demo::Marker::posChanged, [](const demo::Point& pos) {
    std::printf("posChanged(%s)\n", qPrintable(pos.toString()));
  });
  m.setProperty("pos", v);
  m.setProperty("pos", v);  // значение не изменилось - сигнала нет
  auto pos = m.property("pos").value<demo::Point>();
  std::printf("property(pos).length = %g\n", pos.length());
}
