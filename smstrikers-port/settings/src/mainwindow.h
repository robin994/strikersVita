// The settings window. One idea holds the whole thing together: every control registers a pair of
// closures that turn it into the string the ini file wants and back.

#pragma once

#include "inifile.h"
#include "schema.h"

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

class InfoButton;
class KeyCaptureButton;
class SettingsPage;
class QCheckBox;
class QComboBox;
class QDir;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QThread;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Read `path`. A missing file is not an error, and it is not an empty document either:
    // strikers.ini.example beside the application is loaded as the starting text so the player's
    // own file inherits every comment the release shipped.
    void openFile(const QString& path);
    QString filePath() const { return m_path; }

    bool saveTo(const QString& path, QString* error);

    // For --selftest: reach a control by key without a mouse.
    bool setValueByKey(const QString& key, const QString& value);
    QString valueByKey(const QString& key) const;

    // For --screenshot-help: open one setting's info panel, so a headless run can show what the
    // explanation actually looks like.
    bool showHelpFor(const QString& key);

    // Restore the size and position the window had last time, and write them back on the way out.
    void rememberGeometry();

    // Resize events seen so far. --stress-resize reads it: a window whose layout feeds back into
    // its own geometry answers one resize with many.
    int resizeCount() const { return m_resizes; }

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSave();
    void onRevert();
    void onResetAll();
    void onPlay();
    void onChooseImage();
    void onChooseFolder();
    void onExtract();

private:
    struct Control
    {
        QString key;
        QString section;
        QString def;
        std::function<QString()> get;
        std::function<void(const QString&)> set;
    };
    QWidget* buildDisplayTab();
    QWidget* buildAudioTab();
    QWidget* buildInputTab();
    QWidget* buildKeyboardPage();
    QWidget* buildGamepadPage();
    QWidget* buildGameTab();
    QWidget* buildAdvancedBox();

    // Widget factories that also register the control
    void addSwitch(SettingsPage* page, const Setting& s);
    QComboBox* addChoice(SettingsPage* page, const Setting& s);
    InfoButton* infoFor(const Setting& s);

    void registerControl(const Setting& s, std::function<QString()> get,
                         std::function<void(const QString&)> set);

    void loadIntoUi();
    void collectFromUi();

    void refreshAdvancedTable();
    void applyAdvancedTable();

    void updateConflicts();
    void updateDataState();
    void updateLanguageState(const QString& gameId);
    void updateTexturePacks();
    QString userFolder() const;
    // A path from the file as the game reads it: relative to the folder Play starts it in.
    static QString gamePath(const QString& path);
    void openFolder(const QString& path);
    QString extractTarget(const QString& gameId, qint64 bytes);
    void updatePlayButton();
    void setDirty(bool dirty);
    void markDirty() { setDirty(true); }
    void showPath(const QString& text);
    void fitToCurrentTab();

    IniFile m_ini;
    QString m_path;
    bool m_dirty = false;

    QVector<Control> m_controls;
    QHash<QString, int> m_controlByKey;  // normalised key -> index into m_controls
    QHash<QString, InfoButton*> m_infoByKey;

    QTabWidget* m_tabs = nullptr;
    int m_gameTab = 0;

    QVector<KeyCaptureButton*> m_keyButtons;
    QLabel* m_conflictLabel = nullptr;

    QLineEdit* m_dataEdit = nullptr;
    QLabel* m_dataState = nullptr;
    QWidget* m_extractRow = nullptr;
    QPushButton* m_extractButton = nullptr;
    QThread* m_extractThread = nullptr;
    std::atomic<bool> m_extractCancel{ false };
    QComboBox* m_language = nullptr;
    QLabel* m_languageState = nullptr;

    QCheckBox* m_texturesBox = nullptr;
    QString m_texturesFolder;  // a folder the file names, kept while the switch is on
    QLabel* m_packList = nullptr;
    QWidget* m_packChoiceRow = nullptr;
    QComboBox* m_packChoice = nullptr;
    QString m_packWanted;  // the file's pack, kept when this machine does not have it
    bool m_fillingPacks = false;
    QCheckBox* m_dumpBox = nullptr;
    QString m_dumpFolder;

    QTableWidget* m_advanced = nullptr;

    QPushButton* m_saveButton = nullptr;
    QPushButton* m_playButton = nullptr;
    QLabel* m_pathLabel = nullptr;
    QString m_pathText;

    bool m_loading = false;
    bool m_rememberGeometry = false;
    bool m_fitting = false;
    int m_resizes = 0;
};
