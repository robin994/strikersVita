#include "apppaths.h"

#include "port/disc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

const char* const kExampleName = "strikers.ini.example";
const char* const kIniName = "strikers.ini";

// The two sentences below end up in the Play button's tooltip, so they are player-facing and want a
// context of their own rather than QObject's.
class Text
{
    Q_DECLARE_TR_FUNCTIONS(AppPaths)
};

} // namespace

QString AppPaths::archiveRootFor(const QString& applicationDirPath)
{
    QString dir = QDir::fromNativeSeparators(applicationDirPath);
    while (dir.size() > 1 && dir.endsWith(QLatin1Char('/')))
        dir.chop(1);

#if defined(Q_OS_MACOS)
    // Only the bundle shape, and only exactly it: a folder that happens to be called MacOS is not a
    // bundle, and stripping three levels off one would send every path here somewhere arbitrary.
    const QString suffix = QStringLiteral("/Contents/MacOS");
    if (dir.endsWith(suffix))
    {
        dir.chop(suffix.size());       // ... /strikers-settings.app
        const int slash = dir.lastIndexOf(QLatin1Char('/'));
        if (slash > 0)
            dir = dir.left(slash);     // ... /   <- the archive root
    }
#endif

    return dir;
}

QString AppPaths::archiveRoot()
{
    return archiveRootFor(QCoreApplication::applicationDirPath());
}

QString AppPaths::defaultIniPath()
{
    return archiveRoot() + QLatin1Char('/') + QLatin1String(kIniName);
}

QString AppPaths::findExample(const QString& iniPath, const QString& archiveRoot)
{
    QStringList dirs;
    const QString beside = QFileInfo(iniPath).absolutePath();
    if (!beside.isEmpty())
        dirs << beside;
    if (!archiveRoot.isEmpty() && !dirs.contains(archiveRoot))
        dirs << archiveRoot;

    for (const QString& dir : dirs)
    {
        const QString candidate = dir + QLatin1Char('/') + QLatin1String(kExampleName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

QString AppPaths::findGame(const QString& archiveRoot, QString* reason)
{
#if defined(Q_OS_WIN)
    const QString name = QStringLiteral("strikers.exe");
#else
    const QString name = QStringLiteral("strikers");
#endif
    const QString path = archiveRoot + QLatin1Char('/') + name;
    const QFileInfo info(path);
    if (!info.exists())
    {
        if (reason != nullptr)
            *reason = Text::tr("%1 is not in %2.")
                          .arg(name, QDir::toNativeSeparators(archiveRoot));
        return QString();
    }
    if (!info.isExecutable())
    {
        if (reason != nullptr)
            *reason = Text::tr("%1 is not executable.")
                          .arg(QDir::toNativeSeparators(path));
        return QString();
    }
    return path;
}

QString AppPaths::userFolder(const QString& configured)
{
    QString dir = QDir::fromNativeSeparators(configured.trimmed());
    if (dir.isEmpty())
    {
        const QString app = QStringLiteral("Super Mario Strikers");
#if defined(Q_OS_WIN)
        dir = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA")) + QLatin1Char('/') + app;
#elif defined(Q_OS_MACOS)
        dir = QDir::homePath() + QStringLiteral("/Library/Application Support/") + app;
#else
        QString base = qEnvironmentVariable("XDG_DATA_HOME");
        if (base.isEmpty())
            base = QDir::homePath() + QStringLiteral("/.local/share");
        dir = base + QLatin1Char('/') + app;
#endif
    }
    while (dir.size() > 1 && dir.endsWith(QLatin1Char('/')))
        dir.chop(1);
    return dir;
}

QString AppPaths::modsFolder(const QString& root, const QString& kind)
{
    return root + QStringLiteral("/mods/") + kind;
}

QString AppPaths::findDataBesideGame(const QString& archiveRoot)
{
    const QDir root(archiveRoot);
    if (QFileInfo::exists(root.filePath(QStringLiteral("files/common.ini"))))
        return root.filePath(QStringLiteral("files"));

    const QStringList names = root.entryList(QDir::Files | QDir::NoDotAndDotDot,
                                             QDir::Name | QDir::IgnoreCase);
    for (const QString& name : names)
    {
        if (!name.startsWith(QLatin1Char('.'))
            && port_disc_looks_like_image(QFile::encodeName(name).constData()))
            return root.filePath(name);
    }
    return QString();
}
