#ifndef SPELLCHECKER_H
#define SPELLCHECKER_H

#include <QSyntaxHighlighter>
#include <QTimer>
#include <QProcess>
#include <QHash>
#include <QSet>

class SpellHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SpellHighlighter(QTextDocument *parent = nullptr);
    void disableSpellChecking();
    void enableSpellChecking();

protected:
    void highlightBlock(const QString &text) override;

private slots:
    void onTextChanged();
    void onContentsChange(int from, int charsRemoved, int charsAdded);
    void performSpellCheck();

private:
    bool isWordMisspelled(const QStringList &words, QHash<QString, bool> &results);
    QTimer *debounceTimer;
    QHash<QString, bool> spellCache;
    QSet<int> modifiedBlocks;
    QMetaObject::Connection contentsChangedConnection;
    bool spellCheckingEnabled = true; // New flag to track spell-checking state
};

#endif // SPELLCHECKER_H
