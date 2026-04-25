#ifndef DRAWINGCANVAS_H
#define DRAWINGCANVAS_H

#include <QWidget>
#include <QDialog>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QList>
#include <QPainterPath>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QPixmap>
#include <QUndoStack>
#include <QUndoCommand>

// ─── Stroke data ───────────────────────────────────────────────────────────────

struct StrokePoint {
    QPointF pos;
    qreal   pressure; // 0.0 – 1.0
};

struct Stroke {
    QList<StrokePoint> points;
    QColor  color;
    qreal   baseWidth;  // width before pressure scaling
    bool    isEraser;
};

// ─── Canvas widget ─────────────────────────────────────────────────────────────

class DrawingCanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit DrawingCanvasWidget(QWidget *parent = nullptr);

    // Tool modes
    enum Tool { Pen, Highlighter, Eraser };

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    void setPenColor(const QColor &color);
    QColor penColor() const { return m_penColor; }

    void setPenWidth(qreal width);
    qreal penWidth() const { return m_penWidth; }

    void setHighlighterColor(const QColor &color);

    void clearCanvas();
    void undoStroke();
    void redoStroke();

    bool isModified() const { return !m_strokes.isEmpty(); }

    QImage toImage() const;

    // Canvas size
    QSize canvasSize() const { return m_canvasSize; }
    void  setCanvasSize(const QSize &size);

signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void modified();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void beginStroke(const QPointF &pos, qreal pressure);
    void continueStroke(const QPointF &pos, qreal pressure);
    void endStroke();
    void renderStrokeToCache(const Stroke &stroke);
    void rebuildCache();
    void drawStroke(QPainter &painter, const Stroke &stroke) const;
    void emitUndoRedoState();

    Tool    m_tool        = Pen;
    QColor  m_penColor    = Qt::black;
    QColor  m_highlighterColor = QColor(255, 255, 0, 80);
    qreal   m_penWidth    = 3.0;
    QSize   m_canvasSize  = QSize(800, 600);

    QList<Stroke>  m_strokes;       // committed strokes
    QList<Stroke>  m_redoStrokes;   // for redo
    Stroke         m_currentStroke; // stroke being drawn right now
    bool           m_drawing = false;
    bool           m_usingTablet = false;

    QPixmap m_cache;  // rendered committed strokes for fast compositing
};

// ─── Drawing dialog ────────────────────────────────────────────────────────────

class DrawingDialog : public QDialog {
    Q_OBJECT

public:
    explicit DrawingDialog(QWidget *parent = nullptr, const QSize &canvasSize = QSize(800, 600));

    QImage resultImage() const;

private slots:
    void onToolPen();
    void onToolHighlighter();
    void onToolEraser();
    void onPickColor();
    void onPickHighlighterColor();
    void onWidthChanged(int value);
    void onClear();

private:
    DrawingCanvasWidget *m_canvas;
    QPushButton *m_colorBtn;
    QPushButton *m_highlighterColorBtn;
    QSpinBox    *m_widthSpin;
    QAction     *m_penAct;
    QAction     *m_highlighterAct;
    QAction     *m_eraserAct;
    QAction     *m_undoAct;
    QAction     *m_redoAct;

    void updateColorButtonIcon();
    void updateHighlighterColorButtonIcon();
};

#endif // DRAWINGCANVAS_H
