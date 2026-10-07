// Вызов методов по имени: возвращаемые значения, const и перегрузки,
// пользовательские типы параметров, отложенные (queued) соединения.
#include <qr/qobject.hpp>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qstring.h>
#include <QtCore/qtimer.h>

#include "dump.hpp"

namespace demo {

struct Point {
  int x = 0, y = 0;
};

class Calculator : public qr::Object<Calculator> {
public:
  [[=qr::signal]] void computed(const QString& expr, double result);
  [[=qr::signal]] void moved(demo::Point p);

  // Перегрузки: в метаобъект попадают обе, сигнатуры различаются.
  [[=qr::slot]] double add(double a, double b) {
    report(QString("%1 + %2").arg(a).arg(b), a + b);
    return a + b;
  }
  [[=qr::slot]] int add(int a, int b, int c) {
    report(QString("%1 + %2 + %3").arg(a).arg(b).arg(c), a + b + c);
    return a + b + c;
  }

  // Q_INVOKABLE: метод, но не слот.
  [[=qr::invokable]] QString describe() const {
    return QString("Calculator, %1 calls").arg(calls);
  }
  [[=qr::invokable]] void move(demo::Point p) { moved(p); }

protected:
  [[=qr::slot]] void clear() { calls = 0; }

private:
  void report(const QString& expr, double r) {
    ++calls;
    computed(expr, r);
  }
  int calls = 0;
};

}  // namespace demo

Q_DECLARE_METATYPE(demo::Point)

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  demo::Calculator calc;

  dump::section("метаобъект");
  dump::meta_object(demo::Calculator::staticMetaObject);

  QObject::connect(&calc, &demo::Calculator::computed,
                   [](const QString& e, double r) {
                     std::printf("computed(%s = %g)\n", qPrintable(e), r);
                   });

  dump::section("invokeMethod с возвращаемым значением");
  double d = 0;
  QMetaObject::invokeMethod(&calc, "add", Q_RETURN_ARG(double, d),
                            Q_ARG(double, 1.5), Q_ARG(double, 2.25));
  int i = 0;
  QMetaObject::invokeMethod(&calc, "add", Q_RETURN_ARG(int, i), Q_ARG(int, 1),
                            Q_ARG(int, 2), Q_ARG(int, 3));
  QString text;
  QMetaObject::invokeMethod(&calc, "describe", Q_RETURN_ARG(QString, text));
  std::printf("add(double) = %g, add(int) = %d, describe() = %s\n", d, i,
              qPrintable(text));

  dump::section("QMetaMethod::invoke для protected-слота");
  const QMetaObject* mo = calc.metaObject();
  QMetaMethod clear = mo->method(mo->indexOfSlot("clear()"));
  std::printf("clear(): access=%s\n",
              clear.access() == QMetaMethod::Protected ? "protected" : "?");
  clear.invoke(&calc);
  QMetaObject::invokeMethod(&calc, "describe", Q_RETURN_ARG(QString, text));
  std::printf("describe() = %s\n", qPrintable(text));

  dump::section("пользовательский тип и queued-соединение");
  QObject::connect(
      &calc, &demo::Calculator::moved, &app,
      [](demo::Point p) { std::printf("moved(%d, %d) в цикле событий\n", p.x, p.y); },
      Qt::QueuedConnection);
  QMetaObject::invokeMethod(&calc, "move", Q_ARG(demo::Point, (demo::Point{3, 4})));
  std::printf("move() вызван, сигнал ещё в очереди\n");
  QTimer::singleShot(0, &app, &QCoreApplication::quit);
  return app.exec();
}
