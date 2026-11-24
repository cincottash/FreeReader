// The welcome on the first start: the reader's own language (the app's text switches to it as soon as it is
// chosen, the welcome first), and the languages they read, each with its pack to download (its dictionary, its
// definitions and the offline translator). Every pack offered is ticked, so one click gets everything; nothing is
// downloaded before that click. Only complete packs are offered. Everything here can be changed later in
// Settings > General.
#pragma once

#include <QDialog>
#include <QHash>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
struct Settings;

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    // `installed(language)`: whether that language's pack is on this computer already. `dataFolder`: where a
    // corrected interface file may sit (InterfaceLanguage.h).
    WelcomeDialog(Settings& settings, std::function<bool(const QString&)> installed, const QString& dataFolder, QWidget* parent);
    QString readerLanguage() const;
    QStringList chosen() const;                  // the languages ticked whose packs aren't installed yet
    void choose(const QString& language, bool on);
    void setReaderLanguage(const QString& code);
    void finish(bool download);                  // the two buttons (tests)

signals:
    void downloadRequested(const QStringList& languages);

private:
    void build();                                // every widget, in the interface's language now
    void languageChosen();
    void fitHeight();
    void fillPacks();
    void updateButtons();

    Settings& m_settings;
    std::function<bool(const QString&)> m_installed;
    QString m_dataFolder;
    QString m_language;                          // the one chosen
    QWidget* m_content = nullptr;
    QComboBox* m_languageBox = nullptr;
    QVBoxLayout* m_packs = nullptr;
    struct Row {
        QString language;
        QCheckBox* box = nullptr;
        bool installed = false;
    };
    std::vector<Row> m_rows;
    QHash<QString, bool> m_ticks;                // what the reader ticked or unticked, kept across a language switch
    QPushButton* m_start = nullptr;
    QPushButton* m_later = nullptr;
};
