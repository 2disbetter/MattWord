#include "spellchecker.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QDebug>
#include <QMessageBox>
#include <QElapsedTimer>

SpellHighlighter::SpellHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent) {
    debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(500);
    connect(debounceTimer, &QTimer::timeout, this, &SpellHighlighter::performSpellCheck);
    contentsChangedConnection = connect(parent, &QTextDocument::contentsChanged, this, &SpellHighlighter::onTextChanged);
    connect(parent, &QTextDocument::contentsChange, this, &SpellHighlighter::onContentsChange);

    QProcess testAspell;
    testAspell.start("aspell", QStringList() << "-a" << "-l" << "en_US");
    if (!testAspell.waitForStarted(1000)) {
        QMessageBox::warning(nullptr, tr("Spell Check Error"),
            tr("Aspell is not installed or not found in PATH. Spell checking is disabled."));
    } else {
        testAspell.write("test\n");
        testAspell.closeWriteChannel();
        testAspell.waitForFinished(1000);
        QString output = QString::fromUtf8(testAspell.readAllStandardOutput()).trimmed();
        QString error = QString::fromUtf8(testAspell.readAllStandardError()).trimmed();
        if (output.isEmpty() && !error.isEmpty()) {
            QMessageBox::warning(nullptr, tr("Spell Check Error"),
                tr("Aspell dictionary missing: ") + error + tr("\nInstall aspell-en to fix."));
        }
    }
}

void SpellHighlighter::disableSpellChecking() {
    spellCheckingEnabled = false;
    QObject::disconnect(contentsChangedConnection);
    rehighlight(); // Force rehighlight to clear underlines
}

void SpellHighlighter::enableSpellChecking() {
    spellCheckingEnabled = true;
    contentsChangedConnection = QObject::connect(document(), &QTextDocument::contentsChanged,
                                                this, &SpellHighlighter::onTextChanged);
    rehighlight(); // Force rehighlight to restore underlines
}

void SpellHighlighter::onTextChanged() {
    spellCache.clear();
    if (spellCheckingEnabled) {
        debounceTimer->start();
    }
}

void SpellHighlighter::onContentsChange(int from, int charsRemoved, int charsAdded) {
    QTextBlock startBlock = document()->findBlock(from);
    QTextBlock endBlock = document()->findBlock(from + charsAdded);
    if (!endBlock.isValid()) {
        endBlock = document()->lastBlock();
    }

    modifiedBlocks.clear();
    for (QTextBlock block = startBlock; block.isValid() && block.blockNumber() <= endBlock.blockNumber(); block = block.next()) {
        modifiedBlocks.insert(block.blockNumber());
    }

    // Check if a space was added
    QString text = document()->toPlainText();
    if (spellCheckingEnabled && charsAdded == 1 && from < text.length() && text[from] == ' ') {
        QTextBlock block = document()->findBlock(from);
        if (block.isValid()) {
            rehighlightBlock(block);
        }
    } else if (spellCheckingEnabled) {
        debounceTimer->start();
    }
}

void SpellHighlighter::performSpellCheck() {
    if (!spellCheckingEnabled) return;

    QElapsedTimer timer;
    timer.start();

    for (int blockNumber : modifiedBlocks) {
        QTextBlock block = document()->findBlockByNumber(blockNumber);
        if (block.isValid()) {
            rehighlightBlock(block);
        }
    }

    modifiedBlocks.clear();
    // qDebug() << "Spell check took:" << timer.elapsed() << "ms";
}

void SpellHighlighter::highlightBlock(const QString &text) {
    QElapsedTimer timer;
    timer.start();

    QString plainText = currentBlock().text();
    QRegularExpression wordRx("\\b[a-zA-Z]{2,}\\b");
    QRegularExpressionMatchIterator i = wordRx.globalMatch(plainText);
    QTextCharFormat misspelledFormat;
    misspelledFormat.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    misspelledFormat.setUnderlineColor(Qt::red);
    QTextCharFormat clearFormat;
    clearFormat.setUnderlineStyle(QTextCharFormat::NoUnderline);

    // Always clear existing formatting
    setFormat(0, plainText.length(), clearFormat);

    // Skip spell-checking if disabled
    if (!spellCheckingEnabled) {
        // qDebug() << "Spell checking disabled, skipping highlight for block" << currentBlock().blockNumber();
        return;
    }

    QStringList wordsToCheck;
    QHash<QString, QPair<int, int>> wordPositions;
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString word = match.captured();
        if (word.length() >= 2 && !word.contains(QRegularExpression("[0-9]"))) {
            wordsToCheck.append(word);
            wordPositions[word] = {match.capturedStart(), match.capturedLength()};
        }
    }

    if (!wordsToCheck.isEmpty()) {
        QHash<QString, bool> results;
        isWordMisspelled(wordsToCheck, results);
        for (const QString &word : wordsToCheck) {
            bool isMisspelled = results.value(word, false);
            spellCache.insert(word, isMisspelled);
            // qDebug() << "Word:" << word << "Misspelled:" << isMisspelled;
            if (isMisspelled) {
                auto [start, length] = wordPositions[word];
                setFormat(start, length, misspelledFormat);
            }
        }
    }

    // qDebug() << "highlightBlock for block" << currentBlock().blockNumber() << "took:" << timer.elapsed() << "ms";
}

bool SpellHighlighter::isWordMisspelled(const QStringList &words, QHash<QString, bool> &results) {
    if (words.isEmpty()) return false;

    QElapsedTimer timer;
    timer.start();

    QProcess aspell;
    aspell.start("aspell", QStringList() << "-a" << "-l" << "en_US");
    if (!aspell.waitForStarted(1000)) {
        // qDebug() << "Aspell failed to start. Error:" << aspell.errorString();
        for (const QString &word : words) results[word] = false;
        return false;
    }

    QByteArray input;
    for (const QString &word : words) {
        input += word.toUtf8() + "\n";
    }
    aspell.write(input);
    aspell.closeWriteChannel();

    if (!aspell.waitForFinished(1000)) {
        // qDebug() << "Aspell failed to finish. Error:" << aspell.errorString();
        for (const QString &word : words) results[word] = false;
        return false;
    }

    QString output = QString::fromUtf8(aspell.readAllStandardOutput()).trimmed();
    QString error = QString::fromUtf8(aspell.readAllStandardError()).trimmed();
    if (!error.isEmpty()) {
        // qDebug() << "Aspell stderr:" << error;
    }

    if (output.isEmpty()) {
        // qDebug() << "Aspell produced no output for input:" << input;
        for (const QString &word : words) results[word] = false;
        return false;
    }

    int bannerEnd = output.indexOf('\n');
    if (bannerEnd == -1) {
        // qDebug() << "Invalid aspell output format:" << output;
        for (const QString &word : words) results[word] = false;
        return false;
    }

    QStringList resultLines = output.mid(bannerEnd + 1).split('\n', Qt::SkipEmptyParts);
    for (int i = 0; i < words.size() && i < resultLines.size(); ++i) {
        bool isMisspelled = !resultLines[i].startsWith('*');
        results[words[i]] = isMisspelled;
    }

    // qDebug() << "isWordMisspelled for" << words.size() << "words took:" << timer.elapsed() << "ms";
    return true;
}
