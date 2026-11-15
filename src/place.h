#pragma once

#include <QSize>
#include <QStringList>

// On Hyprland, a photo opened from the file manager would get a tile of its
// own and squeeze every other window. Diapo instead asks the compositor to
// start it floating exactly over the file manager's window, so it opens in
// the same place and nothing else on screen moves.
namespace place {

// Starts Diapo again through Hyprland, placed over the focused file manager.
// True when that was done and this process should simply exit.
bool overFileManager(const QString &appId, const QStringList &files);
// When Diapo is already open and a photo is opened from the file manager, the
// open window moves to the middle of the file manager to take it.
void moveRunningOverFileManager(const QString &appId, const QString &photo);

} // namespace place
