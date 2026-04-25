#include "mainwindow.h"
#include "drawingcanvas.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QTextBlock>
#include <QImageReader>
#include <QBuffer>
#include <QPageLayout>
#include <QtMath>
#include <QUuid>
#include <QRegularExpression>
#include <QApplication>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QKeyEvent>
#include <QVariant>

MyTextEdit::MyTextEdit(QWidget *parent) : QTextEdit(parent) {
    setAcceptRichText(true);
    setAutoFormatting(QTextEdit::AutoNone);
}

void MyTextEdit::setMyViewportMargins(int left, int top, int right, int bottom) {
    setViewportMargins(left, top, right, bottom);
}

void MyTextEdit::paintEvent(QPaintEvent *event) {
    QElapsedTimer timer;
    timer.start();
    QTextEdit::paintEvent(event);
    // qDebug() << "paintEvent took:" << timer.elapsed() << "ms";
}

bool MyTextEdit::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::UpdateRequest) {
        // qDebug() << "Viewport update requested";
    }
    return QTextEdit::viewportEvent(event);
}

void MyTextEdit::keyPressEvent(QKeyEvent *event) {
    QTextEdit::keyPressEvent(event);
    if (event->key() == Qt::Key_Space) {
        QTextBlock block = textCursor().block();
        if (block.isValid()) {
            SpellHighlighter *highlighter = qobject_cast<SpellHighlighter*>(document()->findChild<QSyntaxHighlighter*>());
            if (highlighter) {
                highlighter->rehighlightBlock(block);
            }
        }
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    editor = new MyTextEdit(this);
    editor->setAcceptRichText(true);
    editor->setAutoFormatting(QTextEdit::AutoNone);
    spellHighlighter = new SpellHighlighter(editor->document());
    setCentralWidget(editor);

    connect(editor->document(), &QTextDocument::documentLayoutChanged, this, &MainWindow::onDocumentLayoutChanged);

    // File Menu
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *newAct = fileMenu->addAction(tr("&New"), this, &MainWindow::newFile);
    newAct->setShortcut(QKeySequence::New);
    QAction *openAct = fileMenu->addAction(tr("&Open"), this, &MainWindow::openFile);
    openAct->setShortcut(QKeySequence::Open);
    QAction *saveAct = fileMenu->addAction(tr("&Save"), this, &MainWindow::saveFile);
    saveAct->setShortcut(QKeySequence::Save);
    QAction *saveAsAct = fileMenu->addAction(tr("Save &As"), this, &MainWindow::saveAsFile);
    fileMenu->addSeparator();
    QAction *pageSetupAct = fileMenu->addAction(tr("Page &Setup"), this, &MainWindow::pageSetup);
    fileMenu->addSeparator();
    QAction *printAct = fileMenu->addAction(tr("&Print"), this, &MainWindow::print);
    printAct->setShortcut(QKeySequence::Print);
    fileMenu->addSeparator();
    QAction *exitAct = fileMenu->addAction(tr("E&xit"), this, &MainWindow::exitApp);
    exitAct->setShortcut(QKeySequence::Quit);

    // Edit Menu
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAct = editMenu->addAction(tr("&Undo"), this, &MainWindow::undo);
    undoAct->setShortcut(QKeySequence::Undo);
    QAction *redoAct = editMenu->addAction(tr("&Redo"), this, &MainWindow::redo);
    redoAct->setShortcut(QKeySequence::Redo);
    editMenu->addSeparator();
    QAction *cutAct = editMenu->addAction(tr("Cu&t"), this, &MainWindow::cut);
    cutAct->setShortcut(QKeySequence::Cut);
    QAction *copyAct = editMenu->addAction(tr("&Copy"), this, &MainWindow::copy);
    copyAct->setShortcut(QKeySequence::Copy);
    QAction *pasteAct = editMenu->addAction(tr("&Paste"), this, &MainWindow::paste);
    pasteAct->setShortcut(QKeySequence::Paste);

    // Format Menu
    QMenu *formatMenu = menuBar()->addMenu(tr("F&ormat"));
    QAction *boldAct = formatMenu->addAction(tr("&Bold"), this, &MainWindow::bold);
    boldAct->setShortcut(QKeySequence::Bold);
    boldAct->setCheckable(true);
    QAction *italicAct = formatMenu->addAction(tr("&Italic"), this, &MainWindow::italic);
    italicAct->setShortcut(QKeySequence::Italic);
    italicAct->setCheckable(true);
    QAction *underlineAct = formatMenu->addAction(tr("&Underline"), this, &MainWindow::underline);
    underlineAct->setShortcut(QKeySequence::Underline);
    underlineAct->setCheckable(true);

    // Insert Menu
    QMenu *insertMenu = menuBar()->addMenu(tr("&Insert"));
    QAction *imageAct = insertMenu->addAction(tr("I&mage"), this, &MainWindow::insertImage);
    QAction *drawingAct = insertMenu->addAction(tr("&Drawing"), this, &MainWindow::insertDrawing);

    // View Menu for theme selection
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    QAction *lightThemeAct = themeMenu->addAction(tr("Light (Black on White)"), this, &MainWindow::setLightTheme);
    QAction *darkThemeAct = themeMenu->addAction(tr("Dark (White on Black)"), this, &MainWindow::setDarkTheme);

    // Toolbar
    QToolBar *toolBar = addToolBar(tr("Tools"));
    toolBar->addAction(newAct);
    toolBar->addAction(openAct);
    toolBar->addAction(saveAct);
    toolBar->addSeparator();
    toolBar->addAction(undoAct);
    toolBar->addAction(redoAct);
    toolBar->addSeparator();
    toolBar->addAction(boldAct);
    toolBar->addAction(italicAct);
    toolBar->addAction(underlineAct);
    toolBar->addSeparator();
    toolBar->addAction(imageAct);
    toolBar->addAction(drawingAct);

    setWindowTitle(tr("Qt Rich Text Editor"));
    resize(800, 600);

    applyPageSetup();
    setLightTheme();
}

MainWindow::~MainWindow() {}

void MainWindow::newFile() {
    editor->clear();
    currentFilePath.clear();
}

void MainWindow::openFile() {
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open File"), "", tr("HTML Files (*.html *.htm);;All Files (*)"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot open file: ") + file.errorString());
        return;
    }
    QTextStream in(&file);
    QString html = in.readAll();
    file.close();

    // ----------------------------------------------------------------
    // Pre-process: Qt's setHtml() does NOT support data: URIs in <img>
    // src attributes — it renders the raw URI as text instead of an
    // image.  We therefore:
    //   1. Find every data URI in the HTML.
    //   2. Replace it with a plain "myimage/..." resource name.
    //   3. Decode the base64 bytes and keep them in a map.
    //   4. After setHtml(), register each image as a document resource
    //      so Qt's layout engine can find and render it.
    // ----------------------------------------------------------------
    QHash<QString, QByteArray> imageResources;
    // Matches:  src="data:image/png;base64,<base64data>"
    //   cap(1) = format extension ("png", "jpg", …)
    //   cap(2) = raw base64 string
    QRegularExpression dataUriRx(
        "src=\"data:image/([a-zA-Z]+);base64,([^\"]+)\"",
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = dataUriRx.globalMatch(html);
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext())
        matches.append(it.next());

    // Process in reverse so earlier replacements don't shift later offsets
    int counter = 0;
    for (int idx = matches.size() - 1; idx >= 0; --idx) {
        const auto &match = matches[idx];
        QString fmt  = match.captured(1);               // e.g. "png"
        QString b64  = match.captured(2);               // raw base64

        QString resourceName =
            QString("myimage/loaded_%1.%2").arg(counter++).arg(fmt);

        QByteArray ba = QByteArray::fromBase64(b64.toLatin1());
        imageResources[resourceName] = ba;

        // Replace  src="data:image/png;base64,…"  with  src="myimage/loaded_N.png"
        html.replace(match.capturedStart(), match.capturedLength(),
                     QString("src=\"%1\"").arg(resourceName));
    }

    // Load the (now resource-name-based) HTML
    spellHighlighter->disableSpellChecking();
    editor->document()->blockSignals(true);
    editor->setUpdatesEnabled(false);

    editor->setHtml(html);

    // Register every image so the layout engine can render them.
    // Must happen AFTER setHtml() because setHtml() clears the document.
    for (auto it2 = imageResources.constBegin(); it2 != imageResources.constEnd(); ++it2) {
        editor->document()->addResource(
            QTextDocument::ImageResource, QUrl(it2.key()), it2.value());
    }

    // Force the layout to re-evaluate now that resources are present
    editor->document()->markContentsDirty(0, editor->document()->characterCount());

    editor->document()->blockSignals(false);
    editor->setUpdatesEnabled(true);
    editor->viewport()->update();

    spellHighlighter->enableSpellChecking();

    currentFilePath = filePath;
}

void MainWindow::saveFile() {
    if (currentFilePath.isEmpty()) {
        saveAsFile();
    } else {
        saveToFile(currentFilePath);
    }
}

void MainWindow::saveAsFile() {
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Save As"), "", tr("HTML Files (*.html *.htm);;All Files (*)"));
    if (filePath.isEmpty()) return;
    saveToFile(filePath);
    currentFilePath = filePath;
}

void MainWindow::saveToFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot write file %1").arg(filePath));
        return;
    }

    QString html = editor->toHtml();

    // ----------------------------------------------------------------
    // Embed every internal image resource as a base64 data URI so the
    // file is self-contained and images survive a save/reload cycle.
    //
    // Two bugs fixed vs. the original code:
    //
    //  1. Qt may internally decode a stored QByteArray resource into a
    //     QImage for rendering.  When we later call document()->resource()
    //     it may come back as QImage rather than QByteArray.  We handle
    //     both cases.
    //
    //  2. The original code iterated matches *forwards* and then called
    //     html.replace(offset, len, newText).  Once the first replacement
    //     changes the string length every subsequent stored offset is
    //     wrong.  We collect all matches first, then process them in
    //     *reverse* order so earlier replacements don't shift later ones.
    // ----------------------------------------------------------------
    QRegularExpression imgRx(
        "<img\\s+[^>]*src\\s*=\\s*\"(myimage/[^\"]+)\"[^>]*>");

    QRegularExpressionMatchIterator it = imgRx.globalMatch(html);
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext())
        matches.append(it.next());

    for (int idx = matches.size() - 1; idx >= 0; --idx) {
        const auto &match = matches[idx];
        QString resourceName = match.captured(1);

        QVariant resource = editor->document()->resource(
            QTextDocument::ImageResource, QUrl(resourceName));

        QByteArray ba;
        if (resource.typeId() == QMetaType::QByteArray) {
            // Resource is still in its original encoded form
            ba = resource.toByteArray();
        } else if (resource.canConvert<QImage>()) {
            // Qt decoded it to QImage internally for rendering — re-encode
            QImage img = resource.value<QImage>();
            QBuffer buf(&ba);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            buf.close();
        } else {
            qDebug() << "saveToFile: resource not found or unknown type:" << resourceName;
            continue;
        }

        QString base64  = QString::fromLatin1(ba.toBase64());
        QString dataUrl = "data:image/png;base64," + base64;

        // Splice the data URI into the tag in-place
        QString newTag = match.captured();
        newTag.replace(resourceName, dataUrl);
        html.replace(match.capturedStart(), match.capturedLength(), newTag);
    }

    QTextStream out(&file);
    out << html;
    file.close();
    currentFilePath = filePath;
}

void MainWindow::undo()  { editor->undo(); }
void MainWindow::redo()  { editor->redo(); }
void MainWindow::cut()   { editor->cut(); }
void MainWindow::copy()  { editor->copy(); }
void MainWindow::paste() { editor->paste(); }

void MainWindow::bold() {
    QTextCharFormat fmt;
    fmt.setFontWeight(editor->fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
    editor->mergeCurrentCharFormat(fmt);
}

void MainWindow::italic() {
    QTextCharFormat fmt;
    fmt.setFontItalic(!editor->fontItalic());
    editor->mergeCurrentCharFormat(fmt);
}

void MainWindow::underline() {
    QTextCharFormat fmt;
    fmt.setFontUnderline(!editor->fontUnderline());
    editor->mergeCurrentCharFormat(fmt);
}

void MainWindow::insertImage() {
    QElapsedTimer timer;
    timer.start();

    QString imagePath = QFileDialog::getOpenFileName(
        this, tr("Insert Image"), "", tr("Image Files (*.png *.jpg *.bmp)"));
    if (imagePath.isEmpty()) return;

    QImage image(imagePath);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Image Load Error"), tr("Failed to load image."));
        return;
    }

    QStringList options;
    options << "200" << "300" << "400";
    bool ok;
    QString widthStr = QInputDialog::getItem(
        this, tr("Select Image Width"), tr("Width (px):"), options, 0, false, &ok);
    if (!ok || widthStr.isEmpty()) return;
    int selectedWidth = widthStr.toInt();

    if (image.width() > selectedWidth)
        image = image.scaledToWidth(selectedWidth, Qt::FastTransformation);

    editor->setUpdatesEnabled(false);
    editor->document()->blockSignals(true);
    spellHighlighter->disableSpellChecking();

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    buffer.close();

    QString resourceName = "myimage/" + QFileInfo(imagePath).fileName();
    editor->document()->addResource(QTextDocument::ImageResource, QUrl(resourceName), ba);

    QTextImageFormat imageFormat;
    imageFormat.setName(resourceName);
    imageFormat.setWidth(selectedWidth);
    editor->textCursor().insertImage(imageFormat);

    editor->document()->blockSignals(false);
    editor->setUpdatesEnabled(true);
    editor->document()->markContentsDirty(0, editor->document()->characterCount());
    spellHighlighter->enableSpellChecking();
}

void MainWindow::insertDrawing() {
    DrawingDialog dlg(this, QSize(700, 500));
    if (dlg.exec() != QDialog::Accepted) return;

    QImage image = dlg.resultImage();
    if (image.isNull()) return;

    QStringList options;
    options << "200" << "300" << "400" << "500" << "600" << "700";
    bool ok;
    QString widthStr = QInputDialog::getItem(
        this, tr("Select Drawing Width"), tr("Width in document (px):"), options, 3, false, &ok);
    if (!ok || widthStr.isEmpty()) return;
    int selectedWidth = widthStr.toInt();

    if (image.width() > selectedWidth)
        image = image.scaledToWidth(selectedWidth, Qt::SmoothTransformation);

    editor->setUpdatesEnabled(false);
    editor->document()->blockSignals(true);
    spellHighlighter->disableSpellChecking();

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    buffer.close();

    QString resourceName =
        "myimage/drawing_" +
        QUuid::createUuid().toString(QUuid::Id128).left(8) + ".png";
    editor->document()->addResource(QTextDocument::ImageResource, QUrl(resourceName), ba);

    QTextImageFormat imageFormat;
    imageFormat.setName(resourceName);
    imageFormat.setWidth(selectedWidth);
    editor->textCursor().insertImage(imageFormat);

    editor->document()->blockSignals(false);
    editor->setUpdatesEnabled(true);
    editor->document()->markContentsDirty(0, editor->document()->characterCount());
    spellHighlighter->enableSpellChecking();
}

void MainWindow::print() {
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    printer.setPageSize(QPageSize(pageSizeId));
    printer.setPageMargins(
        QMarginsF(leftMargin / 72.0, topMargin / 72.0,
                  rightMargin / 72.0, bottomMargin / 72.0),
        QPageLayout::Inch);

    spellHighlighter->disableSpellChecking();
    editor->print(&printer);
    spellHighlighter->enableSpellChecking();
}

void MainWindow::pageSetup() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Page Setup"));
    QFormLayout *layout = new QFormLayout(&dialog);

    QComboBox *pageSizeCombo = new QComboBox(&dialog);
    pageSizeCombo->addItem(tr("Letter (8.5 x 11 in)"), QPageSize::Letter);
    pageSizeCombo->addItem(tr("A4 (210 x 297 mm)"), QPageSize::A4);
    pageSizeCombo->setCurrentIndex(pageSizeCombo->findData(pageSizeId));
    pageSizeCombo->setToolTip(tr("Select the page size for printing"));
    layout->addRow(tr("Page Size:"), pageSizeCombo);

    auto makeSpin = [&](double val, const QString &tip) {
        QDoubleSpinBox *s = new QDoubleSpinBox(&dialog);
        s->setRange(0.0, 10.0);
        s->setSingleStep(0.1);
        s->setSuffix(" in");
        s->setToolTip(tip);
        s->setValue(val);
        return s;
    };

    QDoubleSpinBox *leftSpin   = makeSpin(leftMargin   / 72.0, tr("Left margin in inches"));
    QDoubleSpinBox *topSpin    = makeSpin(topMargin    / 72.0, tr("Top margin in inches"));
    QDoubleSpinBox *rightSpin  = makeSpin(rightMargin  / 72.0, tr("Right margin in inches"));
    QDoubleSpinBox *bottomSpin = makeSpin(bottomMargin / 72.0, tr("Bottom margin in inches"));
    layout->addRow(tr("Left Margin:"),   leftSpin);
    layout->addRow(tr("Top Margin:"),    topSpin);
    layout->addRow(tr("Right Margin:"),  rightSpin);
    layout->addRow(tr("Bottom Margin:"), bottomSpin);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        pageSizeId   = static_cast<QPageSize::PageSizeId>(pageSizeCombo->currentData().toInt());
        leftMargin   = leftSpin->value()   * 72.0;
        topMargin    = topSpin->value()    * 72.0;
        rightMargin  = rightSpin->value()  * 72.0;
        bottomMargin = bottomSpin->value() * 72.0;
        applyPageSetup();
    }
}

void MainWindow::applyPageSetup() {
    editor->setMyViewportMargins(
        qRound(leftMargin), qRound(topMargin),
        qRound(rightMargin), qRound(bottomMargin));
}

void MainWindow::setLightTheme() {
    editor->setStyleSheet(
        "QTextEdit { color: black; background-color: white; "
        "border: 1px solid #ddd; border-radius: 4px; padding: 0px; }");
}

void MainWindow::setDarkTheme() {
    editor->setStyleSheet(
        "QTextEdit { color: white; background-color: black; "
        "border: 1px solid #ddd; border-radius: 4px; padding: 0px; }");
}

void MainWindow::exitApp() {
    qApp->quit();
}

void MainWindow::onDocumentLayoutChanged() {
    // qDebug() << "Document layout changed";
}
