// Сверка с moc: один и тот же класс описан через Q_OBJECT/Q_PROPERTY
// (moc_reference.h, обрабатывается настоящим moc) и через аннотации qr.
// Метаобъекты сравниваются поле за полем.
#include <qr/qobject.hpp>

#include <QtCore/qstring.h>

#include "dump.hpp"
#include "moc_reference.h"

namespace demo {

class Thermostat : public qr::Object<Thermostat> {
public:
  // Свойства идут в метаобъект в порядке объявления, как Q_PROPERTY у moc.
  [[=qr::property]] double target = 21;
  double current = 18;

  [[=qr::property]] QString mode() const { return m_mode; }
  void setMode(const QString& m) { m_mode = m; }
  [[=qr::property]] bool heating() const { return current < target; }

  [[=qr::signal]] void targetChanged(double target);
  [[=qr::signal]] void modeChanged(const QString& mode);
  [[=qr::signal]] void alarm();

  [[=qr::slot]] void warmer(double delta) { target += delta; }
  [[=qr::slot]] void off() {}

protected:
  [[=qr::slot]] void tick() {}

public:
  [[=qr::invokable]] QString status(int precision) const {
    return QString::number(current, 'f', precision);
  }

private:
  QString m_mode;
};

}  // namespace demo

namespace {

int failures = 0;

void check(const char* what, const QByteArray& moc, const QByteArray& qr) {
  bool same = moc == qr;
  if (!same) ++failures;
  std::printf("  %-28s %-34s %s\n", what, qr.constData(),
              same ? "ok" : ("!= moc: " + moc).constData());
}

QByteArray num(long v) { return QByteArray::number(qlonglong(v)); }

QByteArray names(const QList<QByteArray>& list) { return list.join(','); }

QByteArray property_flags(const QMetaProperty& p) {
  QByteArray f;
  if (p.isReadable()) f += "R";
  if (p.isWritable()) f += "W";
  if (p.isResettable()) f += "Z";
  if (p.hasNotifySignal()) f += "N";
  if (p.isConstant()) f += "C";
  if (p.isFinal()) f += "F";
  if (p.isDesignable()) f += "D";
  if (p.isScriptable()) f += "S";
  if (p.isStored()) f += "T";
  if (p.isUser()) f += "U";
  return f;
}

}  // namespace

int main() {
  const QMetaObject& m = ref::Thermostat::staticMetaObject;
  const QMetaObject& q = demo::Thermostat::staticMetaObject;

  dump::section("moc");
  dump::meta_object(m);
  dump::section("qr");
  dump::meta_object(q);

  dump::section("сравнение");
  check("methodCount", num(m.methodCount()), num(q.methodCount()));
  check("propertyCount", num(m.propertyCount()), num(q.propertyCount()));
  for (int i = m.methodOffset(); i < m.methodCount() && i < q.methodCount(); ++i) {
    QMetaMethod a = m.method(i), b = q.method(i);
    std::printf(" метод %d\n", i);
    check("signature", a.methodSignature(), b.methodSignature());
    check("type/access", num(a.methodType()) + "/" + num(a.access()),
          num(b.methodType()) + "/" + num(b.access()));
    check("returnType", a.typeName(), b.typeName());
    check("parameterNames", names(a.parameterNames()), names(b.parameterNames()));
    check("isConst", num(a.isConst()), num(b.isConst()));
  }
  for (int i = m.propertyOffset(); i < m.propertyCount() && i < q.propertyCount(); ++i) {
    QMetaProperty a = m.property(i), b = q.property(i);
    std::printf(" свойство %d\n", i);
    check("name/type", QByteArray(a.name()) + ":" + a.typeName(),
          QByteArray(b.name()) + ":" + b.typeName());
    check("flags", property_flags(a), property_flags(b));
    check("notify", a.notifySignal().methodSignature(),
          b.notifySignal().methodSignature());
  }

  dump::section("поведение");
  ref::Thermostat r;
  demo::Thermostat d;
  for (QObject* o : {static_cast<QObject*>(&r), static_cast<QObject*>(&d)}) {
    bool ok = QObject::connect(o, SIGNAL(targetChanged(double)), o, SLOT(off()));
    QMetaObject::invokeMethod(o, "warmer", Q_ARG(double, 1.5));
    o->setProperty("target", 25.0);
    o->setProperty("mode", QString("eco"));
    QString status;
    QMetaObject::invokeMethod(o, "status", Q_RETURN_ARG(QString, status), Q_ARG(int, 2));
    std::printf("  %-18s connect=%d target=%g mode=%s heating=%d status=%s\n",
                o->metaObject()->className(), ok, o->property("target").toDouble(),
                qPrintable(o->property("mode").toString()),
                o->property("heating").toBool(), qPrintable(status));
  }

  std::printf("\n%s\n", failures ? "РАСХОЖДЕНИЯ С MOC" : "метаобъекты совпадают с moc");
  return failures ? 1 : 0;
}
