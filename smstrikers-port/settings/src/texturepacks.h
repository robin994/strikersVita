// What the game will find in a texture pack folder.

#pragma once

#include <QString>
#include <QVector>

struct TexturePack
{
    QString name;
    QString path;
    int textures = 0;
};

namespace TexturePacks {

// Each folder in `root` is a pack; loose texture files in `root` count as one named after it.
QVector<TexturePack> scan(const QString& root);

} // namespace TexturePacks
