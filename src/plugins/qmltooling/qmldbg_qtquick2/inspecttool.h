/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef INSPECTTOOL_H
#define INSPECTTOOL_H

#include "abstracttool.h"

#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

QT_FORWARD_DECLARE_CLASS(QQuickView)
QT_FORWARD_DECLARE_CLASS(QQuickItem)

namespace QmlJSDebugger {
namespace QtQuick2 {

class QQuickViewInspector;
class HoverHighlight;

class InspectTool : public AbstractTool
{
    Q_OBJECT
public:
    enum ZoomDirection {
        ZoomIn,
        ZoomOut
    };

    InspectTool(QQuickViewInspector *inspector, QQuickView *view);
    ~InspectTool();

    void enable(bool enable);

    void leaveEvent(QEvent *);

    void mousePressEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void mouseDoubleClickEvent(QMouseEvent *);

    void hoverMoveEvent(QMouseEvent *);
    void wheelEvent(QWheelEvent *);

    void keyPressEvent(QKeyEvent *) {}
    void keyReleaseEvent(QKeyEvent *);

    void touchEvent(QTouchEvent *event);

private:
    QQuickViewInspector *inspector() const;
    qreal nextZoomScale(ZoomDirection direction);
    void scaleView(const qreal &factor, const QPointF &newcenter, const QPointF &oldcenter);
    void zoomIn();
    void zoomOut();
    void initializeDrag(const QPointF &pos);
    void dragItemToPosition();
    void moveItem(bool valid);
    void selectNextItem();
    void selectItem();

private slots:
    void zoomTo100();

private:
    bool m_originalSmooth;
    bool m_dragStarted;
    bool m_pinchStarted;
    bool m_didPressAndHold;
    bool m_tapEvent;
    QPointer<QQuickItem> m_rootItem;
    QPointF m_adjustedOrigin;
    QPointF m_dragStartPosition;
    QPointF m_mousePosition;
    QPointF m_originalPosition;
    qreal m_currentScale;
    qreal m_smoothScaleFactor;
    qreal m_minScale;
    qreal m_maxScale;
    qreal m_originalScale;
    ulong m_touchTimestamp;
    QTimer m_pressAndHoldTimer;

    HoverHighlight *m_hoverHighlight;
    QQuickItem *m_lastItem;
    QQuickItem *m_lastClickedItem;
};

} // namespace QtQuick2
} // namespace QmlJSDebugger

#endif // INSPECTTOOL_H
