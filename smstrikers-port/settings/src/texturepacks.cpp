#include "texturepacks.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

// A replacement's name as the game parses it; the _mipN files are levels of one.
bool isTexture(const QString& fileName)
{
    static const QRegularExpression name(
        QStringLiteral("^tex1_.*\\.(png|dds)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mip(
        QStringLiteral("_mip\\d+\\.(png|dds)$"), QRegularExpression::CaseInsensitiveOption);
    return name.match(fileName).hasMatch() && !mip.match(fileName).hasMatch();
}

int count(const QString& dir, QDirIterator::IteratorFlags flags)
{
    int n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Readable, flags);
    while (it.hasNext())
    {
        it.next();
        if (isTexture(it.fileName()))
            ++n;
    }
    return n;
}

} // namespace

QVector<TexturePack> TexturePacks::scan(const QString& root)
{
    QVector<TexturePack> packs;
    const QDir dir(root);
    if (root.isEmpty() || !dir.exists())
        return packs;

    const QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                              QDir::Name | QDir::IgnoreCase);
    for (const QString& folder : folders)
    {
        TexturePack p;
        p.name = folder;
        p.path = dir.filePath(folder);
        p.textures = count(p.path, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        if (p.textures > 0)
            packs.push_back(p);
    }

    TexturePack loose;
    loose.name = QFileInfo(root).fileName();
    loose.path = root;
    loose.textures = count(root, QDirIterator::NoIteratorFlags);
    if (loose.textures > 0)
        packs.push_back(loose);
    return packs;
}
