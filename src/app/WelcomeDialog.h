// The welcome on the first start (in English: a language has to come first): the reader's own language, and the
// languages they read, each with its pack to download (its dictionary, its definitions and the offline translator).
// Only complete packs are offered. Everything here can be changed later in Settings > General.
#pragma once

#include <QDialog>

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
    // `installed(language)`: whether that language's pack is on this computer already.
    WelcomeDialog(Settings& settings, std::function<bool(const QString&)> installed, QWidget* parent);
    QString readerLanguage() const;
    QStringList chosen() const;                  // the languages ticked whose packs aren't installed yet
    void choose(const QString& language, bool on);
    void setReaderLanguage(const QString& code);
    void finish(bool download);                  // the two buttons (tests)

signals:
    void downloadRequested(const QStringList& languages);

private:
    void fillPacks();
    void updateButtons();

    Settings& m_settings;
    std::function<bool(const QString&)> m_installed;
    QComboBox* m_language;
    QVBoxLayout* m_packs;
    struct Row {
        QString language;
        QCheckBox* box = nullptr;
        bool installed = false;
    };
    std::vector<Row> m_rows;
    QPushButton* m_start;
    QPushButton* m_later;
};
