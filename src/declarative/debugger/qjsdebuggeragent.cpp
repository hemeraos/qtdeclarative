/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
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
** $QT_END_LICENSE$
**
****************************************************************************/

#include "private/qjsdebuggeragent_p.h"
#include "private/qdeclarativedebughelper_p.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qset.h>
#include <QtCore/qurl.h>
#include <QtScript/qscriptcontextinfo.h>
#include <QtScript/qscriptengine.h>
#include <QtScript/qscriptvalueiterator.h>

QT_BEGIN_NAMESPACE

class QJSDebuggerAgentPrivate
{
public:
    QJSDebuggerAgentPrivate(QJSDebuggerAgent *q)
        : q(q), state(NoState), isInitialized(false)
    {}

    void continueExec();
    void recordKnownObjects(const QList<JSAgentWatchData> &);
    QList<JSAgentWatchData> getLocals(QScriptContext *);
    void positionChange(qint64 scriptId, int lineNumber, int columnNumber);
    QScriptEngine *engine() { return q->engine(); }
    void stopped();

public:
    QJSDebuggerAgent *q;
    JSDebuggerState state;
    int stepDepth;
    int stepCount;

    QEventLoop loop;
    QHash<qint64, QString> filenames;
    JSAgentBreakpoints breakpoints;
    // breakpoints by filename (without path)
    QHash<QString, JSAgentBreakpointData> fileNameToBreakpoints;
    QStringList watchExpressions;
    QSet<qint64> knownObjectIds;
    bool isInitialized;
};

namespace {

class SetupExecEnv
{
public:
    SetupExecEnv(QJSDebuggerAgentPrivate *a)
        : agent(a),
          previousState(a->state),
          hadException(a->engine()->hasUncaughtException())
    {
        agent->state = StoppedState;
    }

    ~SetupExecEnv()
    {
        if (!hadException && agent->engine()->hasUncaughtException())
            agent->engine()->clearExceptions();
        agent->state = previousState;
    }

private:
    QJSDebuggerAgentPrivate *agent;
    JSDebuggerState previousState;
    bool hadException;
};

} // anonymous namespace

static JSAgentWatchData fromScriptValue(const QString &expression,
                                        const QScriptValue &value)
{
    static const QString arrayStr = QCoreApplication::translate
            ("Debugger::JSAgentWatchData", "[Array of length %1]");
    static const QString undefinedStr = QCoreApplication::translate
            ("Debugger::JSAgentWatchData", "<undefined>");

    JSAgentWatchData data;
    data.exp = expression.toUtf8();
    data.name = data.exp;
    data.hasChildren = false;
    data.value = value.toString().toUtf8();
    data.objectId = value.objectId();
    if (value.isArray()) {
        data.type = "Array";
        data.value = arrayStr.arg(value.property(QLatin1String("length")).toString()).toUtf8();
        data.hasChildren = true;
    } else if (value.isBool()) {
        data.type = "Bool";
        // data.value = value.toBool() ? "true" : "false";
    } else if (value.isDate()) {
        data.type = "Date";
        data.value = value.toDateTime().toString().toUtf8();
    } else if (value.isError()) {
        data.type = "Error";
    } else if (value.isFunction()) {
        data.type = "Function";
    } else if (value.isUndefined()) {
        data.type = undefinedStr.toUtf8();
    } else if (value.isNumber()) {
        data.type = "Number";
    } else if (value.isRegExp()) {
        data.type = "RegExp";
    } else if (value.isString()) {
        data.type = "String";
    } else if (value.isVariant()) {
        data.type = "Variant";
    } else if (value.isQObject()) {
        const QObject *obj = value.toQObject();
        data.type = "Object";
        data.value += '[';
        data.value += obj->metaObject()->className();
        data.value += ']';
        data.hasChildren = true;
    } else if (value.isObject()) {
        data.type = "Object";
        data.hasChildren = true;
        data.value = "[Object]";
    } else if (value.isNull()) {
        data.type = "<null>";
    } else {
        data.type = "<unknown>";
    }
    return data;
}

static QList<JSAgentWatchData> expandObject(const QScriptValue &object)
{
    QList<JSAgentWatchData> result;
    QScriptValueIterator it(object);
    while (it.hasNext()) {
        it.next();
        if (it.flags() & QScriptValue::SkipInEnumeration)
            continue;
        if (/*object.isQObject() &&*/ it.value().isFunction()) {
            // Cosmetics: skip all functions and slot, there are too many of them,
            // and it is not useful information in the debugger.
            continue;
        }
        JSAgentWatchData data = fromScriptValue(it.name(), it.value());
        result.append(data);
    }
    if (result.isEmpty()) {
        JSAgentWatchData data;
        data.name = "<no initialized data>";
        data.hasChildren = false;
        data.value = " ";
        data.objectId = 0;
        result.append(data);
    }
    return result;
}

static QString fileName(const QString &fileUrl)
{
    int lastDelimiterPos = fileUrl.lastIndexOf(QLatin1Char('/'));
    return fileUrl.mid(lastDelimiterPos, fileUrl.size() - lastDelimiterPos);
}

void QJSDebuggerAgentPrivate::recordKnownObjects(const QList<JSAgentWatchData>& list)
{
    foreach (const JSAgentWatchData &data, list)
        knownObjectIds << data.objectId;
}

QList<JSAgentWatchData> QJSDebuggerAgentPrivate::getLocals(QScriptContext *ctx)
{
    QList<JSAgentWatchData> locals;
    if (ctx) {
        QScriptValue activationObject = ctx->activationObject();
        QScriptValue thisObject = ctx->thisObject();
        locals = expandObject(activationObject);
        if (thisObject.isObject()
                && thisObject.objectId() != engine()->globalObject().objectId()
                && QScriptValueIterator(thisObject).hasNext())
            locals.prepend(fromScriptValue(QLatin1String("this"), thisObject));
        recordKnownObjects(locals);
        knownObjectIds << activationObject.objectId();
    }
    return locals;
}

/*!
  Constructs a new agent for the given \a engine. The agent will
  report debugging-related events (e.g. step completion) to the given
  \a backend.
*/
QJSDebuggerAgent::QJSDebuggerAgent(QScriptEngine *engine, QObject *parent)
    : QObject(parent)
    , QScriptEngineAgent(engine)
    , d(new QJSDebuggerAgentPrivate(this))
{
    QJSDebuggerAgent::engine()->setAgent(this);
}

QJSDebuggerAgent::QJSDebuggerAgent(QDeclarativeEngine *engine, QObject *parent)
    : QObject(parent)
    , QScriptEngineAgent(QDeclarativeDebugHelper::getScriptEngine(engine))
    , d(new QJSDebuggerAgentPrivate(this))
{
    QJSDebuggerAgent::engine()->setAgent(this);
}

/*!
  Destroys this QJSDebuggerAgent.
*/
QJSDebuggerAgent::~QJSDebuggerAgent()
{
    engine()->setAgent(0);
    delete d;
}

/*!
  Indicates whether the agent got the list of breakpoints.
  */
bool QJSDebuggerAgent::isInitialized() const
{
    return d->isInitialized;
}

void QJSDebuggerAgent::setBreakpoints(const JSAgentBreakpoints &breakpoints)
{
    d->breakpoints = breakpoints;

    d->fileNameToBreakpoints.clear();
    foreach (const JSAgentBreakpointData &bp, breakpoints)
        d->fileNameToBreakpoints.insertMulti(fileName(QString::fromUtf8(bp.fileUrl)), bp);

    d->isInitialized = true;
}

void QJSDebuggerAgent::setWatchExpressions(const QStringList &watchExpressions)
{
    d->watchExpressions = watchExpressions;
}

void QJSDebuggerAgent::stepOver()
{
    d->stepDepth = 0;
    d->state = SteppingOverState;
    d->continueExec();
}

void QJSDebuggerAgent::stepInto()
{
    d->stepDepth = 0;
    d->state = SteppingIntoState;
    d->continueExec();
}

void QJSDebuggerAgent::stepOut()
{
    d->stepDepth = 0;
    d->state = SteppingOutState;
    d->continueExec();
}

void QJSDebuggerAgent::continueExecution()
{
    d->state = NoState;
    d->continueExec();
}

JSAgentWatchData QJSDebuggerAgent::executeExpression(const QString &expr)
{
    SetupExecEnv execEnv(d);

    JSAgentWatchData data = fromScriptValue(expr, engine()->evaluate(expr));
    d->knownObjectIds << data.objectId;
    return data;
}

QList<JSAgentWatchData> QJSDebuggerAgent::expandObjectById(quint64 objectId)
{
    SetupExecEnv execEnv(d);

    QScriptValue v;
    if (d->knownObjectIds.contains(objectId))
        v = engine()->objectById(objectId);

    QList<JSAgentWatchData> result = expandObject(v);
    d->recordKnownObjects(result);
    return result;
}

QList<JSAgentWatchData> QJSDebuggerAgent::locals()
{
    SetupExecEnv execEnv(d);
    return d->getLocals(engine()->currentContext());
}

QList<JSAgentWatchData> QJSDebuggerAgent::localsAtFrame(int frameId)
{
    SetupExecEnv execEnv(d);

    int deep = 0;
    QScriptContext *ctx = engine()->currentContext();
    while (ctx && deep < frameId) {
        ctx = ctx->parentContext();
        deep++;
    }

    return d->getLocals(ctx);
}

QList<JSAgentStackData> QJSDebuggerAgent::backtrace()
{
    SetupExecEnv execEnv(d);

    QList<JSAgentStackData> backtrace;

    for (QScriptContext *ctx = engine()->currentContext(); ctx; ctx = ctx->parentContext()) {
        QScriptContextInfo info(ctx);

        JSAgentStackData frame;
        frame.functionName = info.functionName().toUtf8();
        if (frame.functionName.isEmpty()) {
            if (ctx->parentContext()) {
                switch (info.functionType()) {
                case QScriptContextInfo::ScriptFunction:
                    frame.functionName = "<anonymous>";
                    break;
                case QScriptContextInfo::NativeFunction:
                    frame.functionName = "<native>";
                    break;
                case QScriptContextInfo::QtFunction:
                case QScriptContextInfo::QtPropertyFunction:
                    frame.functionName = "<native slot>";
                    break;
                }
            } else {
                frame.functionName = "<global>";
            }
        }
        frame.lineNumber = info.lineNumber();
        // if the line number is unknown, fallback to the function line number
        if (frame.lineNumber == -1)
            frame.lineNumber = info.functionStartLineNumber();

        frame.fileUrl = info.fileName().toUtf8();
        backtrace.append(frame);
    }

    return backtrace;
}

QList<JSAgentWatchData> QJSDebuggerAgent::watches()
{
    SetupExecEnv execEnv(d);

    QList<JSAgentWatchData> watches;
    foreach (const QString &expr, d->watchExpressions)
        watches << fromScriptValue(expr, engine()->evaluate(expr));
    d->recordKnownObjects(watches);
    return watches;
}

void QJSDebuggerAgent::setProperty(qint64 objectId,
                                   const QString &property,
                                   const QString &value)
{
    SetupExecEnv execEnv(d);

    if (d->knownObjectIds.contains(objectId)) {
        QScriptValue object = engine()->objectById(objectId);
        if (object.isObject()) {
            QScriptValue result = engine()->evaluate(value);
            object.setProperty(property, result);
        }
    }
}

/*!
  \reimp
*/
void QJSDebuggerAgent::scriptLoad(qint64 id, const QString &program,
                                  const QString &fileName, int)
{
    Q_UNUSED(program);
    d->filenames.insert(id, fileName);
}

/*!
  \reimp
*/
void QJSDebuggerAgent::scriptUnload(qint64 id)
{
    d->filenames.remove(id);
}

/*!
  \reimp
*/
void QJSDebuggerAgent::contextPush()
{
}

/*!
  \reimp
*/
void QJSDebuggerAgent::contextPop()
{
}

/*!
  \reimp
*/
void QJSDebuggerAgent::functionEntry(qint64 scriptId)
{
    Q_UNUSED(scriptId);
    d->stepDepth++;
}

/*!
  \reimp
*/
void QJSDebuggerAgent::functionExit(qint64 scriptId, const QScriptValue &returnValue)
{
    Q_UNUSED(scriptId);
    Q_UNUSED(returnValue);
    d->stepDepth--;
}

/*!
  \reimp
*/
void QJSDebuggerAgent::positionChange(qint64 scriptId, int lineNumber, int columnNumber)
{
    d->positionChange(scriptId, lineNumber, columnNumber);
}

void QJSDebuggerAgentPrivate::positionChange(qint64 scriptId, int lineNumber, int columnNumber)
{
    Q_UNUSED(columnNumber);

    if (state == StoppedState)
        return; //no re-entrency

    // check breakpoints
    if (!breakpoints.isEmpty()) {
        QHash<qint64, QString>::const_iterator it = filenames.constFind(scriptId);
        QScriptContext *ctx = engine()->currentContext();
        QScriptContextInfo info(ctx);
        if (it == filenames.constEnd()) {
            // It is possible that the scripts are loaded before the agent is attached
            QString filename = info.fileName();

            JSAgentStackData frame;
            frame.functionName = info.functionName().toUtf8();

            QPair<QString, qint32> key = qMakePair(filename, lineNumber);
            it = filenames.insert(scriptId, filename);
        }

        const QString filePath = it.value();
        JSAgentBreakpoints bps = fileNameToBreakpoints.values(fileName(filePath)).toSet();

        foreach (const JSAgentBreakpointData &bp, bps) {
            if (bp.lineNumber == lineNumber) {
                stopped();
                return;
            }
        }
    }

    switch (state) {
    case NoState:
    case StoppedState:
        // Do nothing
        break;
    case SteppingOutState:
        if (stepDepth >= 0)
            break;
        //fallthough
    case SteppingOverState:
        if (stepDepth > 0)
            break;
        //fallthough
    case SteppingIntoState:
        stopped();
        break;
    }

}

/*!
  \reimp
*/
void QJSDebuggerAgent::exceptionThrow(qint64 scriptId,
                                     const QScriptValue &exception,
                                     bool hasHandler)
{
    Q_UNUSED(scriptId);
    Q_UNUSED(exception);
    Q_UNUSED(hasHandler);
//    qDebug() << Q_FUNC_INFO << exception.toString() << hasHandler;
#if 0 //sometimes, we get exceptions that we should just ignore.
    if (!hasHandler && state != StoppedState)
        stopped(true, exception);
#endif
}

/*!
  \reimp
*/
void QJSDebuggerAgent::exceptionCatch(qint64 scriptId, const QScriptValue &exception)
{
    Q_UNUSED(scriptId);
    Q_UNUSED(exception);
}

bool QJSDebuggerAgent::supportsExtension(Extension extension) const
{
    return extension == QScriptEngineAgent::DebuggerInvocationRequest;
}

QVariant QJSDebuggerAgent::extension(Extension extension, const QVariant &argument)
{
    if (extension == QScriptEngineAgent::DebuggerInvocationRequest) {
        d->stopped();
        return QVariant();
    }
    return QScriptEngineAgent::extension(extension, argument);
}

void QJSDebuggerAgentPrivate::stopped()
{
    bool becauseOfException = false;
    const QScriptValue &exception = QScriptValue();

    knownObjectIds.clear();
    state = StoppedState;

    emit q->stopped(becauseOfException, exception.toString());

    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

void QJSDebuggerAgentPrivate::continueExec()
{
    loop.quit();
}

QT_END_NAMESPACE
