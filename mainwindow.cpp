#include "mainwindow.h"
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
        // Trigger spell check for the current block
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

    // Connect document layout changed signal for debugging
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
    QString filePath = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("HTML Files (*.html *.htm);;All Files (*)"));
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot open file: ") + file.errorString());
        return;
    }
    QTextStream in(&file);
    QString html = in.readAll();
    editor->setHtml(html);
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
    QString filePath = QFileDialog::getSaveFileName(this, tr("Save As"), "", tr("HTML Files (*.html *.htm);;All Files (*)"));
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

    QRegularExpression imgRx("<img\\s+[^>]*src\\s*=\\s*\"(myimage/[^\"]+)\"[^>]*>");
    QRegularExpressionMatchIterator i = imgRx.globalMatch(html);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString resourceName = match.captured(1);

        // Load the image data from the document's resource
        QVariant resource = editor->document()->resource(QTextDocument::ImageResource, QUrl(resourceName));
        if (resource.typeId() != QMetaType::QByteArray) {
            qDebug() << "Resource not found or invalid:" << resourceName;
            continue;
        }

        QByteArray ba = resource.toByteArray();
        QString base64 = ba.toBase64();

        // Replace with data URL (PNG since we saved as PNG in insertImage)
        QString dataUrl = QString("data:image/png;base64,%1").arg(base64);
        html.replace(match.capturedStart(), match.capturedLength(),
                     match.captured().replace(match.captured(1), dataUrl));
    }

    QTextStream out(&file);
    out << html;
    file.close();
    currentFilePath = filePath;
}

void MainWindow::undo() {
    editor->undo();
}

void MainWindow::redo() {
    editor->redo();
}

void MainWindow::cut() {
    editor->cut();
}

void MainWindow::copy() {
    editor->copy();
}

void MainWindow::paste() {
    editor->paste();
}

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

    QString imagePath = QFileDialog::getOpenFileName(this, tr("Insert Image"), "", tr("Image Files (*.png *.jpg *.bmp)"));
    if (imagePath.isEmpty()) return;

    QImage image(imagePath);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Image Load Error"), tr("Failed to load image."));
        return;
    }

    // Prompt user for width selection
    QStringList options;
    options << "200" << "300" << "400";
    bool ok;
    QString widthStr = QInputDialog::getItem(this, tr("Select Image Width"), tr("Width (px):"), options, 0, false, &ok);
    if (!ok || widthStr.isEmpty()) {
        return;
    }
    int selectedWidth = widthStr.toInt();

    // Resize images to selected width if larger
    if (image.width() > selectedWidth) {
        image = image.scaledToWidth(selectedWidth, Qt::FastTransformation);
        // qDebug() << "Image resized to width:" << image.width();
    }

    // Disable updates and signals to prevent repaints and spell checking
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

    // Re-enable updates and signals
    editor->document()->blockSignals(false);
    editor->setUpdatesEnabled(true);
    editor->document()->markContentsDirty(0, editor->document()->characterCount());
    spellHighlighter->enableSpellChecking();

    // qDebug() << "Image insertion took:" << timer.elapsed() << "ms";
    // qDebug() << "Document has" << editor->document()->blockCount() << "blocks and" << editor->document()->characterCount() << "characters";
}

void MainWindow::print() {
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    printer.setPageSize(QPageSize(pageSizeId));
    printer.setPageMargins(QMarginsF(leftMargin / 72.0, topMargin / 72.0, rightMargin / 72.0, bottomMargin / 72.0), QPageLayout::Inch);

    // Temporarily disable spell highlighting for printing
    spellHighlighter->disableSpellChecking();

    editor->print(&printer);

    // Re-enable spell highlighting
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
    pageSizeCombo->setToolTip(tr("Select the page size for printing (does not affect editor view width)"));
    layout->addRow(tr("Page Size:"), pageSizeCombo);
    QDoubleSpinBox *leftSpin = new QDoubleSpinBox(&dialog);
    leftSpin->setRange(0.0, 10.0);
    leftSpin->setSingleStep(0.1);
    leftSpin->setSuffix(" in");
    leftSpin->setToolTip(tr("Set the left page margin in inches (visible as padding in the editor)"));
    leftSpin->setValue(leftMargin / 72.0);
    layout->addRow(tr("Left Margin:"), leftSpin);
    QDoubleSpinBox *topSpin = new QDoubleSpinBox(&dialog);
    topSpin->setRange(0.0, 10.0);
    topSpin->setSingleStep(0.1);
    topSpin->setSuffix(" in");
    topSpin->setToolTip(tr("Set the top page margin in inches (visible as padding at the top of the editor)"));
    topSpin->setValue(topMargin / 72.0);
    layout->addRow(tr("Top Margin:"), topSpin);
    QDoubleSpinBox *rightSpin = new QDoubleSpinBox(&dialog);
    rightSpin->setRange(0.0, 10.0);
    rightSpin->setSingleStep(0.1);
    rightSpin->setSuffix(" in");
    rightSpin->setToolTip(tr("Set the right page margin in inches (visible as padding in the editor)"));
    rightSpin->setValue(rightMargin / 72.0);
    layout->addRow(tr("Right Margin:"), rightSpin);
    QDoubleSpinBox *bottomSpin = new QDoubleSpinBox(&dialog);
    bottomSpin->setRange(0.0, 10.0);
    bottomSpin->setSingleStep(0.1);
    bottomSpin->setSuffix(" in");
    bottomSpin->setToolTip(tr("Set the bottom page margin in inches (visible as padding at the bottom of the editor)"));
    bottomSpin->setValue(bottomMargin / 72.0);
    layout->addRow(tr("Bottom Margin:"), bottomSpin);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        pageSizeId = static_cast<QPageSize::PageSizeId>(pageSizeCombo->currentData().toInt());
        leftMargin = leftSpin->value() * 72.0;
        topMargin = topSpin->value() * 72.0;
        rightMargin = rightSpin->value() * 72.0;
        bottomMargin = bottomSpin->value() * 72.0;
        applyPageSetup();
    }
}

void MainWindow::applyPageSetup() {
    editor->setMyViewportMargins(qRound(leftMargin), qRound(topMargin), qRound(rightMargin), qRound(bottomMargin));
}

void MainWindow::setLightTheme() {
    editor->setStyleSheet("QTextEdit { color: black; background-color: white; border: 1px solid #ddd; border-radius: 4px; padding: 0px; }");
}

void MainWindow::setDarkTheme() {
    editor->setStyleSheet("QTextEdit { color: white; background-color: black; border: 1px solid #ddd; border-radius: 4px; padding: 0px; }");
}

void MainWindow::exitApp() {
    qApp->quit();
}

void MainWindow::onDocumentLayoutChanged() {
    QElapsedTimer timer;
    timer.start();
    // qDebug() << "Document layout update took:" << timer.elapsed() << "ms";
}
