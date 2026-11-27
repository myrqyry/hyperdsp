#pragma once
// Test-only driver for Qt Widgets. OBS's pinned Qt SDKs ship Widgets but
// may omit QtTest. Deliver actual input events to the real widgets rather
// than calling their slots or bypassing the canvas interaction handlers.
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QThread>
#include <QWidget>

namespace widget_events {
inline void qWait(int ms){
    QElapsedTimer timer;timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        if(timer.elapsed()<ms)QThread::msleep(1);
    }while(timer.elapsed()<ms);
}
inline Qt::MouseButtons buttons=Qt::NoButton;
inline Qt::KeyboardModifiers modifiers=Qt::NoModifier;
inline void mouse(QWidget* w,QEvent::Type type,Qt::MouseButton button,Qt::KeyboardModifiers mods,QPoint pos){
    if(type==QEvent::MouseButtonPress){
        buttons|=button;modifiers=mods;
        if(w->focusPolicy()&Qt::ClickFocus)w->setFocus(Qt::MouseFocusReason);
    }else if(type==QEvent::MouseButtonRelease)buttons&=~Qt::MouseButtons(button);
    QMouseEvent event(type,QPointF(pos),QPointF(w->mapToGlobal(pos)),button,buttons,mods);
    QCoreApplication::sendEvent(w,&event);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}
inline void mousePress(QWidget* w,Qt::MouseButton b,Qt::KeyboardModifiers m=Qt::NoModifier,QPoint p={}){
    mouse(w,QEvent::MouseButtonPress,b,m,p.isNull()?w->rect().center():p);
}
inline void mouseRelease(QWidget* w,Qt::MouseButton b,Qt::KeyboardModifiers m=Qt::NoModifier,QPoint p={}){
    mouse(w,QEvent::MouseButtonRelease,b,m,p.isNull()?w->rect().center():p);
}
inline void mouseClick(QWidget* w,Qt::MouseButton b,Qt::KeyboardModifiers m=Qt::NoModifier,QPoint p={}){
    mousePress(w,b,m,p);mouseRelease(w,b,m,p);
}
inline void mouseMove(QWidget* w,QPoint p={},int delay=0){
    qWait(delay);mouse(w,QEvent::MouseMove,Qt::NoButton,modifiers,p.isNull()?w->rect().center():p);
}
inline void keyClick(QWidget* w,Qt::Key key,QString value={}){
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier,value);
    QCoreApplication::sendEvent(w,&press);
    QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier,value);
    QCoreApplication::sendEvent(w,&release);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}
inline void keyClicks(QWidget* w,const QString& value){
    for(auto c:value)keyClick(w,Qt::Key(c.toUpper().unicode()),QString(c));
}
}
