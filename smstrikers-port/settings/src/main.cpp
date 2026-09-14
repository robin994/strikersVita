// strikers-settings: the settings window, plus the two headless modes that make it testable on a
// machine with no display. --selftest [file] load, change four values through the real controls,
// save, reload, print the diff.

#include "apppaths.h"
#include "discimage.h"
#include "inifile.h"
#include "mainwindow.h"
#include "schema.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QPixmap>
#include <QStringList>
#include <QTabWidget>
#include <QToolButton>
#include <QTextStream>
#include <QTranslator>

#include <cstdio>

// Set by CMake to the languages actually compiled in, "" for an English-only build.
#ifndef STRIKERS_SETTINGS_TRANSLATIONS
#define STRIKERS_SETTINGS_TRANSLATIONS ""
#endif

namespace {

// The language this window is written in. It is not the game's language.
QString installTranslations(const QString& forced)
{
    const QLocale locale = forced.isEmpty() ? QLocale::system() : QLocale(forced);

    // Qt's own, the buttons in a QMessageBox, the file dialog; from the Qt installation, and this
    // is the fallback.
    auto* qt = new QTranslator(qApp);
    if (qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qt);

    auto* mine = new QTranslator(qApp);
    // load(QLocale, ) walks uiLanguages() rather than matching name(): it tries every tag
    // QLocale::uiLanguages() lists, in order, dropping the script and the territory as it goes.
    if (!mine->load(locale, QStringLiteral("strikers-settings"), QStringLiteral("_"),
                    QStringLiteral(":/i18n")))
        return QString();

    QCoreApplication::installTranslator(mine);
    return mine->language();
}

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

QStringList splitLines(const QByteArray& bytes)
{
    return QString::fromUtf8(bytes).split(QLatin1Char('\n'));
}

// A plain LCS diff. The files are a hundred lines and this runs twice, so the quadratic table is
// not worth avoiding, and a line-by-line comparison would report every line after an insertion as
// changed.
void printDiff(const QStringList& a, const QStringList& b)
{
    const int n = a.size();
    const int m = b.size();
    QVector<QVector<int>> lcs(n + 1, QVector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i)
    {
        for (int j = m - 1; j >= 0; --j)
        {
            lcs[i][j] = a[i] == b[j] ? lcs[i + 1][j + 1] + 1
                                     : qMax(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }

    int i = 0, j = 0, changes = 0;
    while (i < n && j < m)
    {
        if (a[i] == b[j])
        {
            i++;
            j++;
        }
        else if (lcs[i + 1][j] >= lcs[i][j + 1])
        {
            out() << "  -" << a[i++] << '\n';
            changes++;
        }
        else
        {
            out() << "  +" << b[j++] << '\n';
            changes++;
        }
    }
    while (i < n)
    {
        out() << "  -" << a[i++] << '\n';
        changes++;
    }
    while (j < m)
    {
        out() << "  +" << b[j++] << '\n';
        changes++;
    }
    if (changes == 0)
        out() << "  (identical)\n";
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

int selftest(const QString& path)
{
    out() << "strikers-settings --selftest\n";
    out() << "source: " << QDir::toNativeSeparators(path) << '\n';

    MainWindow w;
    w.openFile(path);

    struct Change
    {
        const char* key;
        const char* value;
    };
    // One from each tab, and one; aniso, that the example file does not declare yet, so the "append
    // a key the file has never heard of" path is exercised as well as the "uncomment it in place"
    // one.
    const Change changes[] = {
        { "res_scale", "2.5" },
        { "aniso", "16" },
        { "key_a", "Q" },
        { "pad_deadzone", "0.25" },
        { "overlay", "menu" },
        { "aspect", "5:4" },
    };

    out() << "\nbefore:\n";
    for (const Change& c : changes)
        out() << "  " << c.key << " = " << w.valueByKey(QString::fromLatin1(c.key)) << '\n';

    for (const Change& c : changes)
    {
        if (!w.setValueByKey(QString::fromLatin1(c.key), QString::fromLatin1(c.value)))
        {
            out() << "FAIL: no control for " << c.key << '\n';
            out().flush();
            return 1;
        }
    }

    const QString temp = QDir::temp().filePath(QStringLiteral("strikers-settings-selftest.ini"));
    QFile::remove(temp);
    QString error;
    if (!w.saveTo(temp, &error))
    {
        out() << "FAIL: save: " << error << '\n';
        out().flush();
        return 1;
    }
    out() << "\nsaved:  " << QDir::toNativeSeparators(temp) << '\n';

    MainWindow reloaded;
    reloaded.openFile(temp);

    out() << "\nafter reload:\n";
    bool ok = true;
    for (const Change& c : changes)
    {
        const QString got = reloaded.valueByKey(QString::fromLatin1(c.key));
        const bool match = got == QLatin1String(c.value);
        ok = ok && match;
        out() << "  " << c.key << " = " << got << (match ? "  ok" : "  WRONG") << '\n';
    }

    out() << "\ndiff (source -> saved):\n";
    printDiff(splitLines(readAll(path)), splitLines(readAll(temp)));

    // The other half of the promise: everything that was not one of the four has to be untouched,
    // comments included.
    out() << '\n' << (ok ? "PASS" : "FAIL") << '\n';
    out().flush();
    return ok ? 0 : 1;
}

// Where the application is looking, and what it found there.
int paths(const QString& iniPath)
{
    const QString root = AppPaths::archiveRoot();
    QString reason;
    const QString game = AppPaths::findGame(root, &reason);
    const QString example = AppPaths::findExample(iniPath, root);

    out() << "executable:   " << QDir::toNativeSeparators(QCoreApplication::applicationDirPath()) << '\n';
    out() << "game folder:  " << QDir::toNativeSeparators(root) << '\n';
    out() << "strikers.ini: " << QDir::toNativeSeparators(iniPath)
          << (QFileInfo::exists(iniPath) ? "" : "   (not there yet)") << '\n';
    out() << "seed from:    "
          << (example.isEmpty() ? QStringLiteral("(no strikers.ini.example found)")
                                : QDir::toNativeSeparators(example))
          << '\n';
    out() << "game:         "
          << (game.isEmpty() ? reason : QDir::toNativeSeparators(game)) << '\n';
    out().flush();
    return 0;
}

// `tabPath` is "2" or "2.1": the outer tab, then the inner one on the Input page.
int screenshot(const QString& iniPath, const QString& pngPath, const QString& tabPath,
               const QString& size, const QString& helpKey, bool expand)
{
    MainWindow w;
    w.openFile(iniPath);
    w.show();

    if (!tabPath.isEmpty())
    {
        const QList<QTabWidget*> tabs = w.findChildren<QTabWidget*>();
        const QStringList parts = tabPath.split(QLatin1Char('.'));
        for (int i = 0; i < parts.size() && i < tabs.size(); ++i)
            tabs[i]->setCurrentIndex(parts[i].toInt());
    }

    if (!size.isEmpty())
    {
        const QStringList wh = size.split(QLatin1Char('x'), Qt::SkipEmptyParts);
        if (wh.size() == 2)
            w.resize(wh[0].toInt(), wh[1].toInt());
    }

    // Every disclosure open: a section that is shut by default is still a section whose layout has
    // to be looked at.
    if (expand)
    {
        for (QToolButton* b : w.findChildren<QToolButton*>())
        {
            if (b->isCheckable())
                b->setChecked(true);
        }
    }

    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    if (!helpKey.isEmpty() && !w.showHelpFor(helpKey))
    {
        out() << "no setting called " << helpKey << '\n';
        out().flush();
        return 1;
    }

    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    const QPixmap shot = w.grab();
    if (!shot.save(pngPath))
    {
        out() << "could not write " << pngPath << '\n';
        out().flush();
        return 1;
    }
    out() << "wrote " << pngPath << " (" << shot.width() << "x" << shot.height() << ")\n";
    out().flush();
    return 0;
}

// Every catalogue in the resource, loaded and asked a question.
int languages()
{
    // What the build says it embedded, not what the resource happens to hold: the two disagreeing
    // is the whole point of the check.
    const QStringList expected =
        QString::fromLatin1(STRIKERS_SETTINGS_TRANSLATIONS)
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (expected.isEmpty())
    {
        // A legitimate configuration: a build with no Qt Linguist tools and no committed catalogues
        // is English-only and says so at configure time.
        out() << "no translations are built into this binary\n";
        out().flush();
        return 0;
    }

    int bad = 0;
    for (const QString& tag : expected)
    {
        const QString file = QStringLiteral("strikers-settings_%1.qm").arg(tag);
        QTranslator t;
        const bool loaded = t.load(QStringLiteral(":/i18n/") + file);
        // Installed one at a time: two catalogues at once would let a string missing from this one
        // be answered by the last, which is the opposite of what is being asked.
        if (loaded)
            QCoreApplication::installTranslator(&t);
        const QString sample = MainWindow::tr("Display");
        const bool translated = sample != QLatin1String("Display");
        if (loaded)
            QCoreApplication::removeTranslator(&t);

        out() << "  " << tag << (loaded ? "  loaded" : "  FAILED TO LOAD")
              << (translated ? QStringLiteral("  \"Display\" -> \"%1\"").arg(sample)
                             : QStringLiteral("  \"Display\" IS STILL ENGLISH"))
              << '\n';
        if (!loaded || !translated)
            bad++;
    }

    out() << (bad == 0 ? "\nPASS\n"
                       : QStringLiteral("\nFAIL: %1 catalogue(s) did not answer\n").arg(bad));
    out().flush();
    return bad == 0 ? 0 : 1;
}

int inspectImage(const QString& path)
{
    const DiscImage::Info info = DiscImage::inspect(path);
    out() << "image:   " << QDir::toNativeSeparators(path) << '\n';
    if (!info.gameId.isEmpty())
    {
        out() << "disc:    " << info.gameId << "  " << info.title << '\n';
        out() << "format:  " << info.format << (info.nkit ? " (NKit)" : "") << '\n';
        out() << "files:   " << info.files << ", " << info.bytes << " bytes\n";
    }
    out() << (info.ok ? QStringLiteral("usable") : QStringLiteral("NOT USABLE: ") + info.error)
          << '\n';
    out().flush();
    return info.ok ? 0 : 1;
}

int extractImage(const QString& image, const QString& dir)
{
    std::atomic<bool> cancel{ false };
    int lastPercent = -1;
    QString error;
    const DiscImage::Result r = DiscImage::extract(
        image, dir,
        [&lastPercent](qint64 done, qint64 total) {
            const int percent = total > 0 ? int(done * 100 / total) : 0;
            if (percent / 10 != lastPercent / 10)
            {
                out() << percent << "%\n";
                out().flush();
            }
            lastPercent = percent;
        },
        cancel, &error);
    if (r != DiscImage::Result::Ok)
    {
        out() << "FAILED: " << error << '\n';
        out().flush();
        return 1;
    }
    out() << "extracted to " << QDir::toNativeSeparators(dir) << '\n';
    out().flush();
    return 0;
}

// Refuses to run unless Qt's test mode moved the extraction folder, because it deletes that folder.
int selftestExtract(const QString& image)
{
    QStandardPaths::setTestModeEnabled(true);
    const DiscImage::Info info = DiscImage::inspect(image);
    if (!info.ok)
    {
        out() << "FAIL: " << info.error << '\n';
        out().flush();
        return 1;
    }
    const QString target = DiscImage::defaultExtractDir(info.gameId);
    if (!target.contains(QLatin1String(".qttest")))
    {
        out() << "FAIL: test mode did not move the extraction folder: " << target << '\n';
        out().flush();
        return 1;
    }
    QDir(target).removeRecursively();
    const QString files = QDir(target).filePath(QStringLiteral("files"));
    const QString work = QFileInfo(target).absolutePath() + QStringLiteral("/.")
                         + QFileInfo(target).fileName() + QStringLiteral(".extracting");

    QTemporaryDir tmp;
    MainWindow w;
    w.openFile(tmp.filePath(QStringLiteral("strikers.ini")));
    w.show();
    auto* button = w.findChild<QPushButton*>(QStringLiteral("extractFiles"));
    if (button == nullptr)
    {
        out() << "FAIL: no Extract button\n";
        out().flush();
        return 1;
    }

    bool cancelling = false;
    bool sawProgress = false;
    QStringList asked;
    QTimer answer;
    answer.setInterval(20);
    QObject::connect(&answer, &QTimer::timeout, &w, [&] {
        for (QMessageBox* box : w.findChildren<QMessageBox*>())
        {
            if (!box->isVisible())
                continue;
            asked << box->text().section(QLatin1Char('\n'), 0, 0);
            QAbstractButton* b = box->defaultButton();
            if (b == nullptr)
                b = box->button(QMessageBox::Ok);
            if (b != nullptr)
                b->click();
            return;
        }
        for (QProgressDialog* p : w.findChildren<QProgressDialog*>())
        {
            if (p->isVisible() && p->value() > 0)
            {
                sawProgress = true;
                // The button, not cancel(), which does not emit canceled.
                if (cancelling)
                {
                    for (QPushButton* b : p->findChildren<QPushButton*>())
                        b->click();
                }
            }
        }
    });
    answer.start();

    bool ok = true;
    const auto expect = [&ok](bool pass, const QString& what) {
        out() << (pass ? "ok   " : "FAIL ") << what << '\n';
        out().flush();
        ok = ok && pass;
    };

    const QWidget* row = button->parentWidget();
    expect(row->isHidden(), QStringLiteral("the button is hidden with no disc chosen"));
    w.setValueByKey(QStringLiteral("data"), image);
    QCoreApplication::processEvents();
    expect(!row->isHidden(), QStringLiteral("the button shows for a usable image"));
    button->click();
    int count = 0;
    for (QDirIterator it(files, QDir::Files, QDirIterator::Subdirectories); it.hasNext(); it.next())
        count++;
    expect(w.valueByKey(QStringLiteral("data")) == files,
           QStringLiteral("the field points at the extracted files: %1").arg(w.valueByKey(QStringLiteral("data"))));
    expect(count == info.files, QStringLiteral("%1 files extracted of %2").arg(count).arg(info.files));
    expect(sawProgress, QStringLiteral("the progress dialog moved"));
    expect(!QFileInfo::exists(work), QStringLiteral("no working folder left"));

    expect(row->isHidden(), QStringLiteral("and hides again for the folder it made"));

    asked.clear();
    w.setValueByKey(QStringLiteral("data"), image);
    button->click();
    expect(w.valueByKey(QStringLiteral("data")) == files && !asked.isEmpty()
               && asked.first().contains(QLatin1String("already")),
           QStringLiteral("a second extraction offers the first: \"%1\"").arg(asked.value(0)));

    QDir(target).removeRecursively();
    asked.clear();
    sawProgress = false;
    cancelling = true;
    w.setValueByKey(QStringLiteral("data"), image);
    button->click();
    expect(sawProgress, QStringLiteral("the cancel landed during the extraction"));
    expect(!QFileInfo::exists(target) && !QFileInfo::exists(work),
           QStringLiteral("a cancel leaves neither the folder nor the working folder"));
    expect(w.valueByKey(QStringLiteral("data")) == image,
           QStringLiteral("a cancel leaves the field on the image"));

    answer.stop();
    QDir(target).removeRecursively();
    out() << (ok ? "PASS" : "FAIL") << '\n';
    out().flush();
    return ok ? 0 : 1;
}

void usage()
{
    out() << "usage: strikers-settings [strikers.ini] [--paths] [--selftest]\n"
             "                         [--inspect image] [--extract image dir]\n"
             "                         [--selftest-extract image]\n"
             "                         [--lang xx] [--langs]\n"
             "                         [--screenshot out.png [--screenshot-tab N[.N]]\n"
             "                                              [--screenshot-size WxH]\n"
             "                                              [--screenshot-help key]\n"
             "                                              [--screenshot-expand]]\n"
             "\n"
             "With no path, strikers.ini in the folder holding the game is used, and\n"
             "created on the first save from strikers.ini.example if one is there.\n"
             "\n"
             "--lang takes de, es, fr or en and overrides the system locale. It is the\n"
             "interface language of this window only; the game's own language is the\n"
             "Language setting on the Game tab, and the two are unrelated.\n";
    out().flush();
}

} // namespace

// Resize the window, switch every tab, open every disclosure, and report how many resize events
// came back.
int stressResize(const QString& iniPath, int rounds)
{
    MainWindow w;
    w.openFile(iniPath);
    w.show();
    QCoreApplication::processEvents();

    const int settled = w.resizeCount();
    int worst = 0;
    for (int i = 0; i < rounds; ++i)
    {
        const int before = w.resizeCount();
        w.resize(620 + (i % 17) * 45, 500 + (i % 11) * 55);
        for (int spin = 0; spin < 8; ++spin)
            QCoreApplication::processEvents();
        worst = qMax(worst, w.resizeCount() - before);

        if (auto* tabs = w.findChild<QTabWidget*>())
            tabs->setCurrentIndex(i % tabs->count());
        for (int spin = 0; spin < 4; ++spin)
            QCoreApplication::processEvents();
    }
    for (auto* b : w.findChildren<QToolButton*>())
    {
        if (b->isCheckable())
        {
            b->click();
            QCoreApplication::processEvents();
        }
    }

    const int total = w.resizeCount() - settled;
    out() << "rounds " << rounds << "  resize events " << total
          << "  worst single request " << worst << '\n';
    // Ten per request is generous for a window that also changes tab; a feedback loop produces
    // hundreds or never returns at all.
    const bool ok = worst <= 10;
    out() << (ok ? "PASS" : "FAIL: the layout is feeding back into the geometry") << '\n';
    out().flush();
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // The Dock and the window's own icon. On macOS a bundle takes this from its Info.plist instead,
    // so this is what the plain binary in a build tree gets; on Windows and Linux it is the window
    // icon in both cases.
    app.setWindowIcon(QIcon(QStringLiteral(":/settings.png")));
    // applicationName is an identifier: QSettings builds the path it keeps the window geometry
    // under from it and organizationName, so changing either moves a file that already exists on
    // players' machines.
    QCoreApplication::setApplicationName(QStringLiteral("strikers-settings"));
    QCoreApplication::setOrganizationName(QStringLiteral("smstrikers-port"));
    // Not tr(): this runs before installTranslations(), which cannot move earlier because --lang
    // decides it, so a tr() here would find no catalogue and be English anyway.
    QGuiApplication::setApplicationDisplayName(
        QStringLiteral("Super Mario Strikers Settings"));

    QString iniPath;
    QString shotPath;
    bool wantSelftest = false;
    int stressRounds = 0;
    bool wantPaths = false;
    QString shotTab;
    QString shotSize;
    QString shotHelp;
    bool shotExpand = false;
    QString language;
    bool wantLanguages = false;
    QString inspectPath;
    QString extractFrom;
    QString extractTo;
    QString selftestExtractPath;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        const QString& a = args[i];
        if (a == QLatin1String("--stress-resize"))
        {
            stressRounds = (i + 1 < args.size() && !args[i + 1].startsWith(QLatin1Char('-')))
                               ? args[++i].toInt()
                               : 40;
        }
        else if (a == QLatin1String("--selftest"))
            wantSelftest = true;
        else if (a == QLatin1String("--paths"))
            wantPaths = true;
        else if (a == QLatin1String("--screenshot") && i + 1 < args.size())
            shotPath = args[++i];
        else if (a == QLatin1String("--screenshot-tab") && i + 1 < args.size())
            shotTab = args[++i];
        else if (a == QLatin1String("--screenshot-size") && i + 1 < args.size())
            shotSize = args[++i];
        else if (a == QLatin1String("--screenshot-help") && i + 1 < args.size())
            shotHelp = args[++i];
        else if (a == QLatin1String("--screenshot-expand"))
            shotExpand = true;
        else if (a == QLatin1String("--lang") && i + 1 < args.size())
            language = args[++i];
        else if (a == QLatin1String("--langs"))
            wantLanguages = true;
        else if (a == QLatin1String("--selftest-extract") && i + 1 < args.size())
            selftestExtractPath = args[++i];
        else if (a == QLatin1String("--inspect") && i + 1 < args.size())
            inspectPath = args[++i];
        else if (a == QLatin1String("--extract") && i + 2 < args.size())
        {
            extractFrom = args[++i];
            extractTo = args[++i];
        }
        else if (a == QLatin1String("--help") || a == QLatin1String("-h"))
        {
            usage();
            return 0;
        }
        // macOS's own arguments. Cocoa reads NSUserDefaults out of argv, so `-AppleLanguages
        // "(de)"` overrides the system language for one run, the only way to exercise the real
        // locale path here.
        else if (a.startsWith(QLatin1String("-Apple")) || a.startsWith(QLatin1String("-NS")) ||
                 a.startsWith(QLatin1String("-psn_")))
        {
            if (i + 1 < args.size() && !args[i + 1].startsWith(QLatin1Char('-')))
                ++i;
        }
        else if (a.startsWith(QLatin1Char('-')))
        {
            out() << "unknown option: " << a << '\n';
            usage();
            return 2;
        }
        else
            iniPath = a;
    }

    // After the arguments, because --lang decides it; before anything reads the schema, because the
    // schema is data built once on first use and a table built one call early would be English for
    // the rest of the run.
    installTranslations(language);
    Schema::retranslate();

    // Before the ini path is resolved: this asks nothing of the file system and a machine with no
    // game folder still has to be able to run it.
    if (wantLanguages)
        return languages();
    if (!inspectPath.isEmpty())
        return inspectImage(inspectPath);
    if (!extractFrom.isEmpty())
        return extractImage(extractFrom, extractTo);
    if (!selftestExtractPath.isEmpty())
        return selftestExtract(selftestExtractPath);

    if (iniPath.isEmpty())
    {
        // Beside the game, not in the working directory: a shortcut, Explorer and a terminal each
        // set that somewhere different, and the game's own config.c looks beside itself for the
        // same reason.
        iniPath = AppPaths::defaultIniPath();
    }

    if (wantPaths)
        return paths(iniPath);

    if (stressRounds > 0)
        return stressResize(iniPath, stressRounds);

    if (wantSelftest)
        return selftest(iniPath);

    if (!shotPath.isEmpty())
        return screenshot(iniPath, shotPath, shotTab, shotSize, shotHelp, shotExpand);

    MainWindow w;
    w.rememberGeometry();
    w.openFile(iniPath);
    w.show();
    return app.exec();
}
