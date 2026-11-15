#pragma once

#include <QElapsedTimer>
#include <QString>
#include <cstdio>

// With DIAPO_DEBUG set, prints how long after start something happened.
inline void mark(const QString &what) {
    static const bool debug = qEnvironmentVariableIsSet("DIAPO_DEBUG");
    static QElapsedTimer clock = [] { QElapsedTimer t; t.start(); return t; }();
    if (debug)
        fprintf(stderr, "diapo: %6lld ms  %s\n", clock.elapsed(), qPrintable(what));
}
