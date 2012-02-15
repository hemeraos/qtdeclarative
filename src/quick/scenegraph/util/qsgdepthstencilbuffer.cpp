/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtDeclarative module of the Qt Toolkit.
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

#include "qsgdepthstencilbuffer_p.h"

QT_BEGIN_NAMESPACE

QSGDepthStencilBuffer::QSGDepthStencilBuffer(QOpenGLContext *context, const Format &format)
    : m_functions(context)
    , m_manager(0)
    , m_format(format)
    , m_depthBuffer(0)
    , m_stencilBuffer(0)
{
    // 'm_manager' is set by QSGDepthStencilBufferManager::insertBuffer().
}

QSGDepthStencilBuffer::~QSGDepthStencilBuffer()
{
    if (m_manager)
        m_manager->m_buffers.remove(m_format);
}

void QSGDepthStencilBuffer::attach()
{
    m_functions.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, m_depthBuffer);
    m_functions.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, m_stencilBuffer);
}

void QSGDepthStencilBuffer::detach()
{
    m_functions.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, 0);
    m_functions.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, 0);
}


QSGDefaultDepthStencilBuffer::QSGDefaultDepthStencilBuffer(QOpenGLContext *context, const Format &format)
    : QSGDepthStencilBuffer(context, format)
{
    const GLsizei width = format.size.width();
    const GLsizei height = format.size.height();

    if (format.attachments == (DepthAttachment | StencilAttachment)
            && m_functions.hasOpenGLExtension(QOpenGLExtensions::PackedDepthStencil))
    {
        m_functions.glGenRenderbuffers(1, &m_depthBuffer);
        m_functions.glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
        if (format.samples && m_functions.hasOpenGLExtension(QOpenGLExtensions::FramebufferMultisample)) {
            m_functions.glRenderbufferStorageMultisample(GL_RENDERBUFFER, format.samples,
                GL_DEPTH24_STENCIL8, width, height);
        } else {
            m_functions.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        }
        m_stencilBuffer = m_depthBuffer;
    }
    if (!m_depthBuffer && (format.attachments & DepthAttachment)) {
        m_functions.glGenRenderbuffers(1, &m_depthBuffer);
        m_functions.glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
#ifdef QT_OPENGL_ES
        const GLenum internalFormat = m_functions.hasOpenGLExtension(QOpenGLExtensions::Depth24)
                ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16;
#else
        const GLenum internalFormat = GL_DEPTH_COMPONENT;
#endif
        if (format.samples && m_functions.hasOpenGLExtension(QOpenGLExtensions::FramebufferMultisample)) {
            m_functions.glRenderbufferStorageMultisample(GL_RENDERBUFFER, format.samples,
                internalFormat, width, height);
        } else {
            m_functions.glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
        }
    }
    if (!m_stencilBuffer && (format.attachments & StencilAttachment)) {
        m_functions.glGenRenderbuffers(1, &m_stencilBuffer);
        m_functions.glBindRenderbuffer(GL_RENDERBUFFER, m_stencilBuffer);
#ifdef QT_OPENGL_ES
        const GLenum internalFormat = GL_STENCIL_INDEX8;
#else
        const GLenum internalFormat = GL_STENCIL_INDEX;
#endif
        if (format.samples && m_functions.hasOpenGLExtension(QOpenGLExtensions::FramebufferMultisample)) {
            m_functions.glRenderbufferStorageMultisample(GL_RENDERBUFFER, format.samples,
                internalFormat, width, height);
        } else {
            m_functions.glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
        }
    }
}

QSGDefaultDepthStencilBuffer::~QSGDefaultDepthStencilBuffer()
{
    free();
}

void QSGDefaultDepthStencilBuffer::free()
{
    if (m_depthBuffer)
        m_functions.glDeleteRenderbuffers(1, &m_depthBuffer);
    if (m_stencilBuffer && m_stencilBuffer != m_depthBuffer)
        m_functions.glDeleteRenderbuffers(1, &m_stencilBuffer);
    m_depthBuffer = m_stencilBuffer = 0;
}


QSGDepthStencilBufferManager::~QSGDepthStencilBufferManager()
{
    for (Hash::const_iterator it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it) {
        QSGDepthStencilBuffer *buffer = it.value().data();
        buffer->free();
        buffer->m_manager = 0;
    }
}

QSharedPointer<QSGDepthStencilBuffer> QSGDepthStencilBufferManager::bufferForFormat(const QSGDepthStencilBuffer::Format &fmt)
{
    Hash::const_iterator it = m_buffers.constFind(fmt);
    if (it != m_buffers.constEnd())
        return it.value().toStrongRef();
    return QSharedPointer<QSGDepthStencilBuffer>();
}

void QSGDepthStencilBufferManager::insertBuffer(const QSharedPointer<QSGDepthStencilBuffer> &buffer)
{
    Q_ASSERT(buffer->m_manager == 0);
    Q_ASSERT(!m_buffers.contains(buffer->m_format));
    buffer->m_manager = this;
    m_buffers.insert(buffer->m_format, buffer.toWeakRef());
}

uint qHash(const QSGDepthStencilBuffer::Format &format)
{
    return qHash(qMakePair(format.size.width(), format.size.height()))
            ^ (uint(format.samples) << 12) ^ (uint(format.attachments) << 28);
}

QT_END_NAMESPACE
