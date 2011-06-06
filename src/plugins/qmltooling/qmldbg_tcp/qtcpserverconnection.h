/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the QtDeclarative module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** No Commercial Usage
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QTCPSERVERCONNECTION_H
#define QTCPSERVERCONNECTION_H

#include <QtDeclarative/private/qdeclarativedebugserverconnection_p.h>

QT_BEGIN_NAMESPACE

class QDeclarativeDebugServer;
class QTcpServerConnectionPrivate;
class QTcpServerConnection : public QObject, public QDeclarativeDebugServerConnection
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QTcpServerConnection)
    Q_DISABLE_COPY(QTcpServerConnection)
    Q_INTERFACES(QDeclarativeDebugServerConnection)


public:
    QTcpServerConnection();
    ~QTcpServerConnection();

    void setServer(QDeclarativeDebugServer *server);
    void setPort(int port, bool bock);

    bool isConnected() const;
    void send(const QByteArray &message);
    void disconnect();
    bool waitForMessage();

    void listen();
    void waitForConnection();

private Q_SLOTS:
    void readyRead();
    void newConnection();

private:
    QTcpServerConnectionPrivate *d_ptr;
};

QT_END_NAMESPACE

#endif // QTCPSERVERCONNECTION_H
