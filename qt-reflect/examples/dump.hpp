// Печать метаобъекта: то, что видит Qt во время выполнения.
#pragma once

#include <QtCore/qmetaobject.h>

#include <cstdio>

namespace dump {

inline void section(const char* title) { std::printf("\n--- %s ---\n", title); }

inline const char* method_kind(QMetaMethod::MethodType t) {
  switch (t) {
    case QMetaMethod::Signal: return "signal";
    case QMetaMethod::Slot: return "slot";
    case QMetaMethod::Method: return "invokable";
    default: return "constructor";
  }
}

// Только члены самого класса, без унаследованных от базы.
inline void meta_object(const QMetaObject& mo) {
  std::printf("class %s : %s\n", mo.className(),
              mo.superClass() ? mo.superClass()->className() : "-");
  for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
    QMetaMethod m = mo.method(i);
    std::printf("  %-9s %s %s\n", method_kind(m.methodType()), m.typeName(),
                m.methodSignature().constData());
  }
  for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
    QMetaProperty p = mo.property(i);
    std::printf("  property  %s %s%s%s%s\n", p.typeName(), p.name(),
                p.isWritable() ? " WRITE" : "",
                p.hasNotifySignal() ? " NOTIFY " : "",
                p.hasNotifySignal() ? p.notifySignal().name().constData() : "");
  }
}

}  // namespace dump
