#pragma once
// Test-only Qt Widgets driver. Supports the single-button gestures and
// numeric text input used by these tests, not general OS input simulation.
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

class WidgetEvents {
    Qt::MouseButtons buttons_=Qt::NoButton;
    void mouse(QWidget* w,QEvent::Type type,Qt::MouseButton button,Qt::KeyboardModifiers mods,QPoint pos){
        if(type==QEvent::MouseButtonPress){
            // A new gesture also supersedes a prior cancelled drag.
            buttons_=button;
            if(w->focusPolicy()&Qt::ClickFocus)w->setFocus(Qt::MouseFocusReason);
        }else if(type==QEvent::MouseButtonRelease)buttons_=Qt::NoButton;
        const QPointF global(w->mapToGlobal(pos));
        QMouseEvent event(type,QPointF(pos),global,button,buttons_,mods);
        QCoreApplication::sendEvent(w,&event);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
public:
    static void qWait(int ms){
        if(ms>0){
            QEventLoop loop;
            QTimer::singleShot(ms,Qt::PreciseTimer,&loop,&QEventLoop::quit);
            loop.exec();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void mousePress(QWidget* w,Qt::MouseButton b,Qt::KeyboardModifiers m=Qt::NoModifier,QPoint p={}){
        mouse(w,QEvent::MouseButtonPress,b,m,p.isNull()?w->rect().center():p);
    }
    void mouseRelease(QWidget* w,Qt::MouseButton b,Qt::KeyboardModifiers m=Qt::NoModifier,QPoint p={}){
        mouse(w,QEvent::MouseButtonRelease,b,m,p.isNull()?w->rect().center():p);
    }
    void mouseClick(QWidget* w,Qt::MouseButton b,Qt::KeyboardModifiers m=Qt::NoModifier,QPoint p={}){
        mousePress(w,b,m,p);mouseRelease(w,b,m,p);
    }
    void mouseMove(QWidget* w,QPoint p={},int delay=0,Qt::KeyboardModifiers m=Qt::NoModifier){
        qWait(delay);mouse(w,QEvent::MouseMove,Qt::NoButton,m,p.isNull()?w->rect().center():p);
    }
    static void keyClick(QWidget* w,Qt::Key key,Qt::KeyboardModifiers m=Qt::NoModifier,QString value={}){
        QKeyEvent press(QEvent::KeyPress,key,m,value);
        QCoreApplication::sendEvent(w,&press);
        QKeyEvent release(QEvent::KeyRelease,key,m,value);
        QCoreApplication::sendEvent(w,&release);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
    static void keyClicks(QWidget* w,const QString& value){
        for(auto c:value){
            Qt::Key key;
            if(c>=u'0'&&c<=u'9')key=Qt::Key(Qt::Key_0+c.unicode()-u'0');
            else if(c==u'-')key=Qt::Key_Minus;
            else if(c==u'.')key=Qt::Key_Period;
            else qFatal("WidgetEvents::keyClicks supports numeric ASCII input only");
            keyClick(w,key,Qt::NoModifier,QString(c));
        }
    }
};
