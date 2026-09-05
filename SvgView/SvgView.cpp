
// Copyright 2017 the Resvg Authors
// SPDX-License-Identifier: Apache-2.0 OR MIT

#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QtSvg/QSvgRenderer>          // <-- Qt‑SVG renderer

#include "svgview.h"

///////////////////////////////////////////////////////////////
// Worker – does all heavy work in a background thread
///////////////////////////////////////////////////////////////
class SvgViewWorker : public QObject
{
    Q_OBJECT
public:
    explicit SvgViewWorker(QObject *parent = nullptr)
        : QObject(parent), m_renderer(nullptr), m_dpiRatio(qApp->screens().first()->devicePixelRatio())
    {}

    QRect viewBox() const
    {
        QMutexLocker lock(&m_mutex);
        return m_renderer ? m_renderer->bounds().toRect() : QRect();
    }

    QString loadData(const QByteArray &data)
    {
        QMutexLocker lock(&m_mutex);
        delete m_renderer;
        m_renderer = new QSvgRenderer(data);
        if (!m_renderer->isValid())
            return tr("Invalid SVG data");
        return QString();
    }

    QString loadFile(const QString &path)
    {
        QMutexLocker lock(&m_mutex);
        delete m_renderer;
        m_renderer = new QSvgRenderer(path);
        if (!m_renderer->isValid())
            return tr("Failed to load SVG file");
        return QString();
    }

    void render(const QSize &viewSize)
    {
        Q_ASSERT(QThread::currentThread() != qApp->thread());

        QMutexLocker lock(&m_mutex);
        if (!m_renderer || !m_renderer->isValid())
            return;

        QElapsedTimer timer;
        timer.start();

        QSize svgSize = m_renderer->defaultSize();
        if (!svgSize.isValid() || svgSize.isEmpty())
            svgSize = viewSize;               // fallback

        // Scale respecting aspect ratio
        QSize target = svgSize.scaled(viewSize, Qt::KeepAspectRatio);
        target = QSize(int(target.width() * m_dpiRatio),
                      int(target.height() * m_dpiRatio));

        QImage img(target, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(m_dpiRatio, m_dpiRatio);
        m_renderer->render(&painter);
        painter.end();

        img.setDevicePixelRatio(m_dpiRatio);
        emit rendered(img);

        qDebug() << "Render in" << timer.elapsed() << "ms";
    }

signals:
    void rendered(const QImage &img);

private:
    mutable QMutex m_mutex;
    QSvgRenderer *m_renderer;
    qreal m_dpiRatio;
};

///////////////////////////////////////////////////////////////
// SvgView implementation
///////////////////////////////////////////////////////////////
static QImage genCheckedTexture()
{
    const int l = 20;
    QImage pix(l, l, QImage::Format_RGB32);
    int b = l / 2;
    pix.fill(QColor("#c0c0c0"));

    QPainter p(&pix);
    p.fillRect(QRect(0, 0, b, b), QColor("#808080"));
    p.fillRect(QRect(b, b, b, b), QColor("#808080"));
    p.end();

    return pix;
}

SvgView::SvgView(QWidget *parent)
    : QWidget(parent)
    , m_checkboardImg(genCheckedTexture())
    , m_worker(new SvgViewWorker())
    , m_resizeTimer(new QTimer(this))
{
    setAcceptDrops(true);
    setMinimumSize(10, 10);

    QThread *th = new QThread(this);
    m_worker->moveToThread(th);
    th->start();

    const auto *screen = qApp->screens().first();
    m_dpiRatio = screen->devicePixelRatio();

    connect(m_worker, &SvgViewWorker::rendered,
            this, &SvgView::onRendered);
    connect(m_resizeTimer, &QTimer::timeout,
            this, &SvgView::requestUpdate);

    m_resizeTimer->setSingleShot(true);
}

SvgView::~SvgView()
{
    QThread *th = m_worker->thread();
    th->quit();
    th->wait(10000);
    delete m_worker;
}

void SvgView::init()
{
    // No special init required for Qt SVG
}

void SvgView::setFitToView(bool flag)
{
    m_isFitToView = flag;
    requestUpdate();
}

void SvgView::setBackground(Background background)
{
    m_background = background;
    update();
}

void SvgView::setDrawImageBorder(bool flag)
{
    m_isDrawImageBorder = flag;
    update();
}

// -----------------------------------------------------------------
// Loading helpers – forward to the worker
// -----------------------------------------------------------------
void SvgView::loadData(const QByteArray &ba)
{
    const QString err = m_worker->loadData(ba);
    afterLoad(err);
}

void SvgView::loadFile(const QString &path)
{
    const QString err = m_worker->loadFile(path);
    afterLoad(err);
}

void SvgView::afterLoad(const QString &errMsg)
{
    m_img = QImage();

    if (errMsg.isEmpty()) {
        m_isHasImage = true;
        requestUpdate();
    } else {
        emit loadError(errMsg);
        m_isHasImage = false;
        update();
    }
}

// -----------------------------------------------------------------
// UI helpers
// -----------------------------------------------------------------
void SvgView::drawSpinner(QPainter &p)
{
    const int outerRadius = 20;
    const int innerRadius = outerRadius * 0.45;

    const int capsuleHeight = outerRadius - innerRadius;
    const int capsuleWidth  = capsuleHeight * 0.35;
    const int capsuleRadius = capsuleWidth / 2;

    for (int i = 0; i < 12; ++i) {
        QColor color = Qt::black;
        color.setAlphaF(1.0f - (i / 12.0f));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.save();
        p.translate(width() / 2, height() / 2);
        p.rotate(m_angle - i * 30.0f);
        p.drawRoundedRect(-capsuleWidth * 0.5,
                          -(innerRadius + capsuleHeight),
                          capsuleWidth, capsuleHeight,
                          capsuleRadius, capsuleRadius);
        p.restore();
    }
}

void SvgView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = contentsRect();
    p.setClipRect(r);

    switch (m_background) {
    case Background::None: break;
    case Background::White:
        p.fillRect(r, Qt::white);
        break;
    case Background::CheckBoard:
        p.fillRect(r, QBrush(m_checkboardImg));
        break;
    }

    if (m_img.isNull() && !m_timer.isActive()) {
        p.setPen(Qt::black);
        p.drawText(rect(), Qt::AlignCenter, "Drop an SVG image here.");
    } else if (m_timer.isActive()) {
        drawSpinner(p);
    } else {
        const QRect imgRect(0, 0,
                           m_img.width() / m_dpiRatio,
                           m_img.height() / m_dpiRatio);

        p.translate(r.x() + (r.width() - imgRect.width()) / 2,
                    r.y() + (r.height() - imgRect.height()) / 2);
        p.drawImage(0, 0, m_img);

        if (m_isDrawImageBorder) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Qt::green);
            p.setBrush(Qt::NoBrush);
            p.drawRect(imgRect);
        }
    }

    QWidget::paintEvent(nullptr);
}

// -----------------------------------------------------------------
// Drag & drop
// -----------------------------------------------------------------
void SvgView::dragEnterEvent(QDragEnterEvent *event) { event->accept(); }
void SvgView::dragMoveEvent(QDragMoveEvent *event) { event->accept(); }

void SvgView::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasUrls()) {
        event->ignore();
        return;
    }

    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile())
            continue;

        QString path = url.toLocalFile();
        QFileInfo fi(path);
        if (fi.isFile()) {
            QString suffix = fi.suffix().toLower();
            if (suffix == "svg" || suffix == "svgz") {
                loadFile(path);
            } else {
                QMessageBox::warning(this, tr("Warning"),
                                     tr("You can drop only SVG and SVGZ files."));
            }
        }
    }
    event->acceptProposedAction();
}

// -----------------------------------------------------------------
// Resize handling
// -----------------------------------------------------------------
void SvgView::resizeEvent(QResizeEvent *) { m_resizeTimer->start(200); }

void SvgView::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timer.timerId()) {
        m_angle = (m_angle + 30) % 360;
        update();
    } else {
        QWidget::timerEvent(event);
    }
}

// -----------------------------------------------------------------
// Rendering request
// -----------------------------------------------------------------
void SvgView::requestUpdate()
{
    if (!m_isHasImage)
        return;

    const QSize targetSize = m_isFitToView ? size() : m_worker->viewBox().size();

    if (targetSize * m_dpiRatio == m_img.size())
        return;

    m_timer.start(100, this);

    // Trigger rendering in the worker thread
    QTimer::singleShot(1, m_worker, [=]() {
        m_worker->render(targetSize);
    });
}

// -----------------------------------------------------------------
// Slot called from the worker when rendering finishes
// -----------------------------------------------------------------
void SvgView::onRendered(const QImage &img)
{
    m_timer.stop();
    m_img = img;
    update();
}
