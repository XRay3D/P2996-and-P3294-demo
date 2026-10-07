// Эталон для 04_vs_moc: тот же класс, что и в 04_vs_moc.cpp, но через
// Q_OBJECT, Q_PROPERTY и moc (qt_wrap_cpp в CMakeLists.txt).
#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>

namespace ref {

class Thermostat : public QObject {
  Q_OBJECT
  Q_PROPERTY(double target MEMBER target NOTIFY targetChanged)
  Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
  Q_PROPERTY(bool heating READ heating)

public:
  QString mode() const { return m_mode; }
  void setMode(const QString& m) { m_mode = m; }
  bool heating() const { return current < target; }

  double target = 21;
  double current = 18;

signals:
  void targetChanged(double target);
  void modeChanged(const QString& mode);
  void alarm();

public slots:
  void warmer(double delta) { target += delta; }
  void off() {}

protected slots:
  void tick() {}

public:
  Q_INVOKABLE QString status(int precision) const { return QString::number(current, 'f', precision); }

private:
  QString m_mode;
};

}  // namespace ref
