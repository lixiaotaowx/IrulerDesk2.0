/****************************************************************************
** Meta object code from reading C++ file 'VideoDisplayWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/video_components/VideoDisplayWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VideoDisplayWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN18VideoDisplayWidgetE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN18VideoDisplayWidgetE = QtMocHelpers::stringData(
    "VideoDisplayWidget",
    "connectionStatusChanged",
    "",
    "status",
    "frameReceived",
    "statsUpdated",
    "VideoStats",
    "stats",
    "receivingStopped",
    "viewerId",
    "targetId",
    "fullscreenToggleRequested",
    "annotationColorChanged",
    "colorId",
    "audioOutputSelectionChanged",
    "followSystem",
    "deviceId",
    "micInputSelectionChanged",
    "avatarUpdateReceived",
    "userId",
    "iconId",
    "renderFrame",
    "frameData",
    "frameSize",
    "renderFrameWithTimestamp",
    "captureTimestamp",
    "updateConnectionStatus",
    "onMousePositionReceived",
    "position",
    "timestamp",
    "name",
    "sendUndo",
    "sendClear",
    "sendCloseOverlay",
    "sendLike",
    "captureToImage",
    "notifyTargetOffline",
    "reason",
    "clearOfflineReminder",
    "onStartStopClicked",
    "updateStatsDisplay"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN18VideoDisplayWidgetE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      24,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       9,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  158,    2, 0x06,    1 /* Public */,
       4,    0,  161,    2, 0x06,    3 /* Public */,
       5,    1,  162,    2, 0x06,    4 /* Public */,
       8,    2,  165,    2, 0x06,    6 /* Public */,
      11,    0,  170,    2, 0x06,    9 /* Public */,
      12,    1,  171,    2, 0x06,   10 /* Public */,
      14,    2,  174,    2, 0x06,   12 /* Public */,
      17,    2,  179,    2, 0x06,   15 /* Public */,
      18,    2,  184,    2, 0x06,   18 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      21,    2,  189,    2, 0x0a,   21 /* Public */,
      24,    3,  194,    2, 0x0a,   24 /* Public */,
      26,    1,  201,    2, 0x0a,   28 /* Public */,
      27,    3,  204,    2, 0x0a,   30 /* Public */,
      27,    2,  211,    2, 0x2a,   34 /* Public | MethodCloned */,
      31,    0,  216,    2, 0x0a,   37 /* Public */,
      32,    0,  217,    2, 0x0a,   38 /* Public */,
      33,    0,  218,    2, 0x0a,   39 /* Public */,
      34,    0,  219,    2, 0x0a,   40 /* Public */,
      35,    0,  220,    2, 0x10a,   41 /* Public | MethodIsConst  */,
      36,    1,  221,    2, 0x0a,   42 /* Public */,
      36,    0,  224,    2, 0x2a,   44 /* Public | MethodCloned */,
      38,    0,  225,    2, 0x0a,   45 /* Public */,
      39,    0,  226,    2, 0x08,   46 /* Private */,
      40,    0,  227,    2, 0x08,   47 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 6,    7,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    9,   10,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   13,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   15,   16,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   15,   16,
    QMetaType::Void, QMetaType::QString, QMetaType::Int,   19,   20,

 // slots: parameters
    QMetaType::Void, QMetaType::QByteArray, QMetaType::QSize,   22,   23,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::QSize, QMetaType::LongLong,   22,   23,   25,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QPoint, QMetaType::LongLong, QMetaType::QString,   28,   29,   30,
    QMetaType::Void, QMetaType::QPoint, QMetaType::LongLong,   28,   29,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::QImage,
    QMetaType::Void, QMetaType::QString,   37,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject VideoDisplayWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN18VideoDisplayWidgetE.offsetsAndSizes,
    qt_meta_data_ZN18VideoDisplayWidgetE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN18VideoDisplayWidgetE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<VideoDisplayWidget, std::true_type>,
        // method 'connectionStatusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'frameReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'statsUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const VideoStats &, std::false_type>,
        // method 'receivingStopped'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'fullscreenToggleRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'annotationColorChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'audioOutputSelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'micInputSelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'avatarUpdateReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'renderFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QSize &, std::false_type>,
        // method 'renderFrameWithTimestamp'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QSize &, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'updateConnectionStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onMousePositionReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onMousePositionReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'sendUndo'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sendClear'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sendCloseOverlay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sendLike'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'captureToImage'
        QtPrivate::TypeAndForceComplete<QImage, std::false_type>,
        // method 'notifyTargetOffline'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'notifyTargetOffline'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'clearOfflineReminder'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStartStopClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateStatsDisplay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void VideoDisplayWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VideoDisplayWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->connectionStatusChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->frameReceived(); break;
        case 2: _t->statsUpdated((*reinterpret_cast< std::add_pointer_t<VideoStats>>(_a[1]))); break;
        case 3: _t->receivingStopped((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 4: _t->fullscreenToggleRequested(); break;
        case 5: _t->annotationColorChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->audioOutputSelectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 7: _t->micInputSelectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 8: _t->avatarUpdateReceived((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 9: _t->renderFrame((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QSize>>(_a[2]))); break;
        case 10: _t->renderFrameWithTimestamp((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QSize>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[3]))); break;
        case 11: _t->updateConnectionStatus((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->onMousePositionReceived((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 13: _t->onMousePositionReceived((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2]))); break;
        case 14: _t->sendUndo(); break;
        case 15: _t->sendClear(); break;
        case 16: _t->sendCloseOverlay(); break;
        case 17: _t->sendLike(); break;
        case 18: { QImage _r = _t->captureToImage();
            if (_a[0]) *reinterpret_cast< QImage*>(_a[0]) = std::move(_r); }  break;
        case 19: _t->notifyTargetOffline((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 20: _t->notifyTargetOffline(); break;
        case 21: _t->clearOfflineReminder(); break;
        case 22: _t->onStartStopClicked(); break;
        case 23: _t->updateStatsDisplay(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (VideoDisplayWidget::*)(const QString & );
            if (_q_method_type _q_method = &VideoDisplayWidget::connectionStatusChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)();
            if (_q_method_type _q_method = &VideoDisplayWidget::frameReceived; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)(const VideoStats & );
            if (_q_method_type _q_method = &VideoDisplayWidget::statsUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)(const QString & , const QString & );
            if (_q_method_type _q_method = &VideoDisplayWidget::receivingStopped; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)();
            if (_q_method_type _q_method = &VideoDisplayWidget::fullscreenToggleRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)(int );
            if (_q_method_type _q_method = &VideoDisplayWidget::annotationColorChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)(bool , const QString & );
            if (_q_method_type _q_method = &VideoDisplayWidget::audioOutputSelectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)(bool , const QString & );
            if (_q_method_type _q_method = &VideoDisplayWidget::micInputSelectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (VideoDisplayWidget::*)(const QString & , int );
            if (_q_method_type _q_method = &VideoDisplayWidget::avatarUpdateReceived; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
    }
}

const QMetaObject *VideoDisplayWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VideoDisplayWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN18VideoDisplayWidgetE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int VideoDisplayWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 24)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 24;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 24)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 24;
    }
    return _id;
}

// SIGNAL 0
void VideoDisplayWidget::connectionStatusChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void VideoDisplayWidget::frameReceived()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void VideoDisplayWidget::statsUpdated(const VideoStats & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void VideoDisplayWidget::receivingStopped(const QString & _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void VideoDisplayWidget::fullscreenToggleRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void VideoDisplayWidget::annotationColorChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void VideoDisplayWidget::audioOutputSelectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void VideoDisplayWidget::micInputSelectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void VideoDisplayWidget::avatarUpdateReceived(const QString & _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}
QT_WARNING_POP
