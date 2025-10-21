#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QTextDocument>
#include <QUrl>
#include <QDebug>
#include <QInputDialog>
#include <QPrinter>
#include <QPrintDialog>
#include <QDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QTextFrame>
#include <QPageSize>
#include <QComboBox>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QUuid>
#include "spellchecker.h"
#include <QElapsedTimer>

// Subclass QTextEdit to expose viewport margins and log paint/update events
class MyTextEdit : public QTextEdit {
    Q_OBJECT
public:
    MyTextEdit(QWidget *parent = nullptr) : QTextEdit(parent) {}
    void setMyViewportMargins(int left, int top, int right, int bottom) {
        setViewportMargins(left, top, right, bottom);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QElapsedTimer timer;
        timer.start();
        QTextEdit::paintEvent(event);
        // qDebug() << "paintEvent took:" << timer.elapsed() << "ms";
    }

    bool viewportEvent(QEvent *event) override {
        if (event->type() == QEvent::UpdateRequest) {
            // qDebug() << "Viewport update requested";
        }
        return QTextEdit::viewportEvent(event);
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void newFile();
    void openFile();
    void saveFile();
    void saveAsFile();
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void bold();
    void italic();
    void underline();
    void insertImage();
    void print();
    void pageSetup();
    void setLightTheme();
    void setDarkTheme();
    void exitApp();
    void onDocumentLayoutChanged();

private:
    MyTextEdit *editor;
    SpellHighlighter *spellHighlighter;
    QString currentFilePath;

    // Page and margin settings (in points; 1 inch = 72 points)
    QPageSize::PageSizeId pageSizeId = QPageSize::Letter;
    double leftMargin = 72.0;
    double topMargin = 72.0;
    double rightMargin = 72.0;
    double bottomMargin = 72.0;

    void saveToFile(const QString &filePath);
    void applyPageSetup();
};

#endif // MAINWINDOW_H