#pragma once

#include <QStringList>

// The commands that need no window, for scripts and agents:
//   diapo compress FILE... [--format F] [--quality N] [--max N] [--keep-metadata] [--out DIR] [--json]
//   diapo look FILE... --name NAME [--strength N] [--json]
//   diapo looks [--json]
namespace cli {

bool handles(const QStringList &arguments);
int run(const QStringList &arguments);

} // namespace cli
