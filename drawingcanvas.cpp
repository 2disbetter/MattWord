#include "drawingcanvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QApplication>
#include <QScrollArea>
#include <QStyle>
#include <QScreen>

// ═══════════════════════════════════════════════════════════════════════════════
//  DrawingCanvasWidget
// ═══════════════════════════════════════════════════════════════════════════════

DrawingCanvasWidget::DrawingCanvasWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TabletTracking);
    setAttribute(Qt::WA_StaticContents);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    m_cache = QPixmap(m_canvasSize);
    m_cache.fill(Qt::white);
    setMinimumSize(m_canvasSize);
    setMaximumSize(m_canvasSize);
}

void DrawingCanvasWidget::setCanvasSize(const QSize &size) {
    m_canvasSize = size;
    setMinimumSize(size);
    setMaximumSize(size);
    rebuildCache();
    update();
}

// ─── Tool / property setters ───────────────────────────────────────────────────

void DrawingCanvasWidget::setTool(Tool tool) {
    m_tool = tool;
    if (tool == Eraser) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void DrawingCanvasWidget::setPenColor(const QColor &color) {
    m_penColor = color;
}

void DrawingCanvasWidget::setPenWidth(qreal width) {
    m_penWidth = qBound(1.0, width, 50.0);
}

void DrawingCanvasWidget::setHighlighterColor(const QColor &color) {
    // Keep alpha at a fixed semi-transparent level for highlighter feel
    m_highlighterColor = color;
    m_highlighterColor.setAlpha(80);
}

// ─── Undo / redo / clear ───────────────────────────────────────────────────────

void DrawingCanvasWidget::clearCanvas() {
    if (m_strokes.isEmpty()) return;
    m_redoStrokes.clear();
    m_strokes.clear();
    rebuildCache();
    update();
    emitUndoRedoState();
}

void DrawingCanvasWidget::undoStroke() {
    if (m_strokes.isEmpty()) return;
    m_redoStrokes.append(m_strokes.takeLast());
    rebuildCache();
    update();
    emitUndoRedoState();
}

void DrawingCanvasWidget::redoStroke() {
    if (m_redoStrokes.isEmpty()) return;
    m_strokes.append(m_redoStrokes.takeLast());
    renderStrokeToCache(m_strokes.last());
    update();
    emitUndoRedoState();
}

void DrawingCanvasWidget::emitUndoRedoState() {
    emit canUndoChanged(!m_strokes.isEmpty());
    emit canRedoChanged(!m_redoStrokes.isEmpty());
    if (!m_strokes.isEmpty()) emit modified();
}

// ─── Image export ──────────────────────────────────────────────────────────────

QImage DrawingCanvasWidget::toImage() const {
    QImage img(m_canvasSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const Stroke &s : m_strokes) {
        drawStroke(p, s);
    }
    p.end();
    return img;
}

// ─── Input handling ────────────────────────────────────────────────────────────

void DrawingCanvasWidget::tabletEvent(QTabletEvent *event) {
    QPointF pos = event->position();
    qreal pressure = event->pressure();

    switch (event->type()) {
    case QEvent::TabletPress:
        m_usingTablet = true;
        beginStroke(pos, pressure);
        event->accept();
        break;
    case QEvent::TabletMove:
        if (m_drawing) {
            continueStroke(pos, pressure);
            event->accept();
        }
        break;
    case QEvent::TabletRelease:
        if (m_drawing) {
            endStroke();
            event->accept();
        }
        m_usingTablet = false;
        break;
    default:
        break;
    }
}

void DrawingCanvasWidget::mousePressEvent(QMouseEvent *event) {
    if (m_usingTablet) return;  // ignore mouse when tablet is active
    if (event->button() == Qt::LeftButton) {
        beginStroke(event->position(), 1.0);
    }
}

void DrawingCanvasWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_usingTablet) return;
    if (m_drawing) {
        continueStroke(event->position(), 1.0);
    }
}

void DrawingCanvasWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_usingTablet) return;
    if (event->button() == Qt::LeftButton && m_drawing) {
        endStroke();
    }
}

void DrawingCanvasWidget::beginStroke(const QPointF &pos, qreal pressure) {
    m_drawing = true;
    m_currentStroke = Stroke();

    switch (m_tool) {
    case Pen:
        m_currentStroke.color     = m_penColor;
        m_currentStroke.baseWidth = m_penWidth;
        m_currentStroke.isEraser  = false;
        break;
    case Highlighter:
        m_currentStroke.color     = m_highlighterColor;
        m_currentStroke.baseWidth = m_penWidth * 4.0;  // highlighter is wider
        m_currentStroke.isEraser  = false;
        break;
    case Eraser:
        m_currentStroke.color     = Qt::white;
        m_currentStroke.baseWidth = m_penWidth * 3.0;
        m_currentStroke.isEraser  = true;
        break;
    }

    m_currentStroke.points.append({pos, pressure});
    update();
}

void DrawingCanvasWidget::continueStroke(const QPointF &pos, qreal pressure) {
    if (!m_drawing) return;

    // Skip if too close to the last point (reduces jitter)
    if (!m_currentStroke.points.isEmpty()) {
        QPointF last = m_currentStroke.points.last().pos;
        if (QLineF(last, pos).length() < 1.5) return;
    }

    m_currentStroke.points.append({pos, pressure});
    update();
}

void DrawingCanvasWidget::endStroke() {
    if (!m_drawing) return;
    m_drawing = false;

    if (m_currentStroke.points.size() >= 2) {
        m_strokes.append(m_currentStroke);
        m_redoStrokes.clear();
        renderStrokeToCache(m_currentStroke);
        emitUndoRedoState();
    }
    m_currentStroke.points.clear();
    update();
}

// ─── Rendering ─────────────────────────────────────────────────────────────────

void DrawingCanvasWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    // Draw the cached committed strokes
    p.drawPixmap(0, 0, m_cache);

    // Draw the live stroke on top
    if (m_drawing && m_currentStroke.points.size() >= 2) {
        p.setRenderHint(QPainter::Antialiasing, true);
        drawStroke(p, m_currentStroke);
    }
}

void DrawingCanvasWidget::drawStroke(QPainter &painter, const Stroke &stroke) const {
    if (stroke.points.size() < 2) return;

    painter.save();

    if (stroke.isEraser) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
    } else if (stroke.color.alpha() < 255) {
        // Highlighter: use SourceOver for translucency
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    const auto &pts = stroke.points;

    // Draw variable-width segments using quads for pressure response
    for (int i = 0; i < pts.size() - 1; ++i) {
        const StrokePoint &p0 = pts[i];
        const StrokePoint &p1 = pts[i + 1];

        // Interpolate pressure → width
        qreal w0 = stroke.baseWidth * qMax(0.2, p0.pressure);
        qreal w1 = stroke.baseWidth * qMax(0.2, p1.pressure);
        qreal avgWidth = (w0 + w1) * 0.5;

        QPen pen(stroke.color, avgWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);

        // Smoothing: use cubic Bézier between midpoints
        if (i == 0) {
            painter.drawLine(p0.pos, p1.pos);
        } else {
            QPointF mid0 = (pts[i - 1].pos + p0.pos) * 0.5;
            QPointF mid1 = (p0.pos + p1.pos) * 0.5;
            QPainterPath path;
            path.moveTo(mid0);
            path.quadTo(p0.pos, mid1);
            painter.drawPath(path);
        }
    }

    // Draw round caps at stroke endpoints for a polished look
    if (!pts.isEmpty()) {
        qreal startW = stroke.baseWidth * qMax(0.2, pts.first().pressure);
        painter.setPen(Qt::NoPen);
        painter.setBrush(stroke.color);
        painter.drawEllipse(pts.first().pos, startW * 0.45, startW * 0.45);

        qreal endW = stroke.baseWidth * qMax(0.2, pts.last().pressure);
        painter.drawEllipse(pts.last().pos, endW * 0.45, endW * 0.45);
    }

    painter.restore();
}

void DrawingCanvasWidget::renderStrokeToCache(const Stroke &stroke) {
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing, true);
    drawStroke(p, stroke);
    p.end();
}

void DrawingCanvasWidget::rebuildCache() {
    m_cache = QPixmap(m_canvasSize);
    m_cache.fill(Qt::white);
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const Stroke &s : m_strokes) {
        drawStroke(p, s);
    }
    p.end();
}

void DrawingCanvasWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DrawingDialog
// ═══════════════════════════════════════════════════════════════════════════════

DrawingDialog::DrawingDialog(QWidget *parent, const QSize &canvasSize)
    : QDialog(parent)
{
    setWindowTitle(tr("Drawing Canvas"));
    setMinimumSize(500, 400);

    // ── Canvas ──
    m_canvas = new DrawingCanvasWidget(this);
    m_canvas->setCanvasSize(canvasSize);

    // Wrap canvas in a scroll area so large canvases are usable
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(m_canvas);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setWidgetResizable(false);
    scrollArea->setStyleSheet("QScrollArea { background-color: #e0e0e0; border: none; }");

    // ── Toolbar ──
    QToolBar *toolbar = new QToolBar(tr("Drawing Tools"), this);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setMovable(false);
    toolbar->setStyleSheet(
        "QToolBar { spacing: 4px; padding: 4px; background: #f5f5f5; border-bottom: 1px solid #ccc; }"
        "QToolButton { padding: 4px 8px; border-radius: 4px; }"
        "QToolButton:checked { background: #d0d0d0; border: 1px solid #aaa; }"
        "QToolButton:hover { background: #e0e0e0; }"
    );

    // Tool actions in an exclusive group
    QActionGroup *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    m_penAct = toolbar->addAction(tr("✏ Pen"));
    m_penAct->setCheckable(true);
    m_penAct->setChecked(true);
    m_penAct->setToolTip(tr("Pen tool – draw with pressure sensitivity"));
    toolGroup->addAction(m_penAct);

    m_highlighterAct = toolbar->addAction(tr("🖍 Highlighter"));
    m_highlighterAct->setCheckable(true);
    m_highlighterAct->setToolTip(tr("Highlighter – semi-transparent wide strokes"));
    toolGroup->addAction(m_highlighterAct);

    m_eraserAct = toolbar->addAction(tr("⬜ Eraser"));
    m_eraserAct->setCheckable(true);
    m_eraserAct->setToolTip(tr("Eraser – remove strokes"));
    toolGroup->addAction(m_eraserAct);

    toolbar->addSeparator();

    // Color button (pen)
    m_colorBtn = new QPushButton(this);
    m_colorBtn->setFixedSize(28, 28);
    m_colorBtn->setToolTip(tr("Pen color"));
    m_colorBtn->setCursor(Qt::PointingHandCursor);
    updateColorButtonIcon();
    toolbar->addWidget(m_colorBtn);

    // Color button (highlighter)
    m_highlighterColorBtn = new QPushButton(this);
    m_highlighterColorBtn->setFixedSize(28, 28);
    m_highlighterColorBtn->setToolTip(tr("Highlighter color"));
    m_highlighterColorBtn->setCursor(Qt::PointingHandCursor);
    updateHighlighterColorButtonIcon();
    toolbar->addWidget(m_highlighterColorBtn);

    toolbar->addSeparator();

    // Width spinner
    QLabel *widthLabel = new QLabel(tr(" Size:"), this);
    toolbar->addWidget(widthLabel);

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(1, 50);
    m_widthSpin->setValue(3);
    m_widthSpin->setSuffix(tr(" px"));
    m_widthSpin->setToolTip(tr("Brush size in pixels"));
    toolbar->addWidget(m_widthSpin);

    toolbar->addSeparator();

    // Undo / Redo
    m_undoAct = toolbar->addAction(tr("↩ Undo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_undoAct->setEnabled(false);
    m_undoAct->setToolTip(tr("Undo last stroke (Ctrl+Z)"));

    m_redoAct = toolbar->addAction(tr("↪ Redo"));
    m_redoAct->setShortcut(QKeySequence::Redo);
    m_redoAct->setEnabled(false);
    m_redoAct->setToolTip(tr("Redo undone stroke (Ctrl+Y)"));

    toolbar->addSeparator();

    QAction *clearAct = toolbar->addAction(tr("🗑 Clear"));
    clearAct->setToolTip(tr("Clear entire canvas"));

    // ── Bottom buttons ──
    QDialogButtonBox *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(tr("Insert Drawing"));

    // ── Layout ──
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 8);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(scrollArea, 1);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(12, 8, 12, 0);
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnBox);
    mainLayout->addLayout(bottomLayout);

    // ── Connections ──
    connect(m_penAct,         &QAction::triggered, this, &DrawingDialog::onToolPen);
    connect(m_highlighterAct, &QAction::triggered, this, &DrawingDialog::onToolHighlighter);
    connect(m_eraserAct,      &QAction::triggered, this, &DrawingDialog::onToolEraser);
    connect(m_colorBtn,       &QPushButton::clicked, this, &DrawingDialog::onPickColor);
    connect(m_highlighterColorBtn, &QPushButton::clicked, this, &DrawingDialog::onPickHighlighterColor);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DrawingDialog::onWidthChanged);
    connect(m_undoAct,   &QAction::triggered, m_canvas, &DrawingCanvasWidget::undoStroke);
    connect(m_redoAct,   &QAction::triggered, m_canvas, &DrawingCanvasWidget::redoStroke);
    connect(clearAct,    &QAction::triggered, this, &DrawingDialog::onClear);
    connect(btnBox,      &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox,      &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_canvas, &DrawingCanvasWidget::canUndoChanged, m_undoAct, &QAction::setEnabled);
    connect(m_canvas, &DrawingCanvasWidget::canRedoChanged, m_redoAct, &QAction::setEnabled);

    // Resize dialog to fit nicely on screen
    QScreen *screen = QApplication::primaryScreen();
    if (screen) {
        QSize avail = screen->availableSize();
        int w = qMin(canvasSize.width() + 60, avail.width() - 100);
        int h = qMin(canvasSize.height() + 140, avail.height() - 100);
        resize(w, h);
    }
}

QImage DrawingDialog::resultImage() const {
    return m_canvas->toImage();
}

// ─── Slots ─────────────────────────────────────────────────────────────────────

void DrawingDialog::onToolPen() {
    m_canvas->setTool(DrawingCanvasWidget::Pen);
}

void DrawingDialog::onToolHighlighter() {
    m_canvas->setTool(DrawingCanvasWidget::Highlighter);
}

void DrawingDialog::onToolEraser() {
    m_canvas->setTool(DrawingCanvasWidget::Eraser);
}

void DrawingDialog::onPickColor() {
    QColor c = QColorDialog::getColor(m_canvas->penColor(), this, tr("Pen Color"));
    if (c.isValid()) {
        m_canvas->setPenColor(c);
        updateColorButtonIcon();
    }
}

void DrawingDialog::onPickHighlighterColor() {
    // Show color dialog without alpha — we manage alpha ourselves
    QColor base = m_canvas->penColor(); // just a starting point
    QColor c = QColorDialog::getColor(QColor(255, 255, 0), this, tr("Highlighter Color"));
    if (c.isValid()) {
        m_canvas->setHighlighterColor(c);
        updateHighlighterColorButtonIcon();
    }
}

void DrawingDialog::onWidthChanged(int value) {
    m_canvas->setPenWidth(value);
}

void DrawingDialog::onClear() {
    m_canvas->clearCanvas();
}

void DrawingDialog::updateColorButtonIcon() {
    QPixmap px(24, 24);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(m_canvas->penColor());
    p.setPen(QPen(Qt::gray, 1));
    p.drawRoundedRect(2, 2, 20, 20, 4, 4);
    p.end();
    m_colorBtn->setIcon(QIcon(px));
    m_colorBtn->setIconSize(QSize(24, 24));
}

void DrawingDialog::updateHighlighterColorButtonIcon() {
    QPixmap px(24, 24);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    // Draw with the semi-transparent highlighter color on a white background
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(2, 2, 20, 20, 4, 4);
    QColor hlColor = QColor(255, 255, 0, 80); // default; ideally track the real one
    p.setBrush(hlColor);
    p.drawRoundedRect(2, 2, 20, 20, 4, 4);
    p.setPen(QPen(Qt::gray, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(2, 2, 20, 20, 4, 4);
    p.end();
    m_highlighterColorBtn->setIcon(QIcon(px));
    m_highlighterColorBtn->setIconSize(QSize(24, 24));
}
