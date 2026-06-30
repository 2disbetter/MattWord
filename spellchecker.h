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

    // Returns true if `word` would currently be flagged as misspelled
    // (respects the ignore list and the user's custom dictionary).
    bool checkMisspelled(const QString &word);

    // Stop flagging `word` for this session only.
    void ignoreWord(const QString &word);

    // Stop flagging `word` permanently (saved across restarts).
    void addWordToDictionary(const QString &word);

protected:
    void highlightBlock(const QString &text) override;

private slots:
    void onTextChanged();
    void onContentsChange(int from, int charsRemoved, int charsAdded);
    void performSpellCheck();

private:
    bool isWordMisspelled(const QStringList &words, QHash<QString, bool> &results);
    bool isWordIgnoredOrAdded(const QString &word) const;
    void loadAddedWords();
    void saveAddedWords();

    QTimer *debounceTimer;
    QHash<QString, bool> spellCache;
    QSet<int> modifiedBlocks;
    QMetaObject::Connection contentsChangedConnection;
    bool spellCheckingEnabled = true; // New flag to track spell-checking state
    bool aspellAvailable = false;     // Whether aspell was found at startup
                                      // (false e.g. on a stock Windows install)
    QSet<QString> ignoredWords;       // session-only, lowercased
    QSet<QString> addedWords;         // persistent custom dictionary, lowercased
};

#endif // SPELLCHECKER_H
