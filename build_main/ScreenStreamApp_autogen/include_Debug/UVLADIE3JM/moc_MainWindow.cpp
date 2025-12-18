/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/MainWindow.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN10MainWindowE = QtMocHelpers::stringData(
    "MainWindow",
    "appInitialized",
    "",
    "appReady",
    "startStreaming",
    "stopStreaming",
    "updateStatus",
    "onCaptureProcessFinished",
    "exitCode",
    "QProcess::ExitStatus",
    "exitStatus",
    "onPlayerProcessFinished",
    "onLoginWebSocketConnected",
    "onLoginWebSocketDisconnected",
    "onLoginWebSocketTextMessageReceived",
    "message",
    "onLoginWebSocketError",
    "QAbstractSocket::SocketError",
    "error",
    "onUserCleanupTimerTimeout",
    "showContextMenu",
    "pos",
    "onContextMenuOption1",
    "onContextMenuOption2",
    "onWatchButtonClicked",
    "onUserImageClicked",
    "userId",
    "userName",
    "showMainList",
    "onSetAvatarRequested",
    "onSystemSettingsRequested",
    "onToggleStreamingIsland",
    "onScreenSelected",
    "index",
    "onAvatarSelected",
    "iconId",
    "onClearMarksRequested",
    "onExitRequested",
    "onHideRequested",
    "onAnnotationColorChanged",
    "colorId",
    "onUserNameChanged",
    "name",
    "onMicToggleRequested",
    "enabled",
    "onSpeakerToggleRequested",
    "onLocalQualitySelected",
    "quality",
    "onAudioOutputSelectionChanged",
    "followSystem",
    "deviceId",
    "onMicInputSelectionChanged",
    "onManualApprovalEnabledChanged"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN10MainWindowE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      34,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  218,    2, 0x06,    1 /* Public */,
       3,    0,  219,    2, 0x06,    2 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    0,  220,    2, 0x08,    3 /* Private */,
       5,    0,  221,    2, 0x08,    4 /* Private */,
       6,    0,  222,    2, 0x08,    5 /* Private */,
       7,    2,  223,    2, 0x08,    6 /* Private */,
      11,    2,  228,    2, 0x08,    9 /* Private */,
      12,    0,  233,    2, 0x08,   12 /* Private */,
      13,    0,  234,    2, 0x08,   13 /* Private */,
      14,    1,  235,    2, 0x08,   14 /* Private */,
      16,    1,  238,    2, 0x08,   16 /* Private */,
      19,    0,  241,    2, 0x08,   18 /* Private */,
      20,    1,  242,    2, 0x08,   19 /* Private */,
      22,    0,  245,    2, 0x08,   21 /* Private */,
      23,    0,  246,    2, 0x08,   22 /* Private */,
      24,    0,  247,    2, 0x08,   23 /* Private */,
      25,    2,  248,    2, 0x08,   24 /* Private */,
      28,    0,  253,    2, 0x08,   27 /* Private */,
      29,    0,  254,    2, 0x08,   28 /* Private */,
      30,    0,  255,    2, 0x08,   29 /* Private */,
      31,    0,  256,    2, 0x08,   30 /* Private */,
      32,    1,  257,    2, 0x08,   31 /* Private */,
      34,    1,  260,    2, 0x08,   33 /* Private */,
      36,    0,  263,    2, 0x08,   35 /* Private */,
      37,    0,  264,    2, 0x08,   36 /* Private */,
      38,    0,  265,    2, 0x08,   37 /* Private */,
      39,    1,  266,    2, 0x08,   38 /* Private */,
      41,    1,  269,    2, 0x08,   40 /* Private */,
      43,    1,  272,    2, 0x08,   42 /* Private */,
      45,    1,  275,    2, 0x08,   44 /* Private */,
      46,    1,  278,    2, 0x0a,   46 /* Public */,
      48,    2,  281,    2, 0x0a,   48 /* Public */,
      51,    2,  286,    2, 0x0a,   51 /* Public */,
      52,    1,  291,    2, 0x0a,   54 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 9,    8,   10,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 9,    8,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   15,
    QMetaType::Void, 0x80000000 | 17,   18,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QPoint,   21,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   26,   27,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   33,
    QMetaType::Void, QMetaType::Int,   35,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   40,
    QMetaType::Void, QMetaType::QString,   42,
    QMetaType::Void, QMetaType::Bool,   44,
    QMetaType::Void, QMetaType::Bool,   44,
    QMetaType::Void, QMetaType::QString,   47,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   49,   50,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   49,   50,
    QMetaType::Void, QMetaType::Bool,   44,

       0        // eod
};

Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_ZN10MainWindowE.offsetsAndSizes,
    qt_meta_data_ZN10MainWindowE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN10MainWindowE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>,
        // method 'appInitialized'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'appReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'startStreaming'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stopStreaming'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCaptureProcessFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QProcess::ExitStatus, std::false_type>,
        // method 'onPlayerProcessFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QProcess::ExitStatus, std::false_type>,
        // method 'onLoginWebSocketConnected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onLoginWebSocketDisconnected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onLoginWebSocketTextMessageReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onLoginWebSocketError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QAbstractSocket::SocketError, std::false_type>,
        // method 'onUserCleanupTimerTimeout'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showContextMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>,
        // method 'onContextMenuOption1'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onContextMenuOption2'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onWatchButtonClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onUserImageClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'showMainList'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSetAvatarRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSystemSettingsRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onToggleStreamingIsland'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onScreenSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onAvatarSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onClearMarksRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onExitRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onHideRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onAnnotationColorChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onUserNameChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onMicToggleRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onSpeakerToggleRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onLocalQualitySelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onAudioOutputSelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onMicInputSelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onManualApprovalEnabledChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->appInitialized(); break;
        case 1: _t->appReady(); break;
        case 2: _t->startStreaming(); break;
        case 3: _t->stopStreaming(); break;
        case 4: _t->updateStatus(); break;
        case 5: _t->onCaptureProcessFinished((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        case 6: _t->onPlayerProcessFinished((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        case 7: _t->onLoginWebSocketConnected(); break;
        case 8: _t->onLoginWebSocketDisconnected(); break;
        case 9: _t->onLoginWebSocketTextMessageReceived((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->onLoginWebSocketError((*reinterpret_cast< std::add_pointer_t<QAbstractSocket::SocketError>>(_a[1]))); break;
        case 11: _t->onUserCleanupTimerTimeout(); break;
        case 12: _t->showContextMenu((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 13: _t->onContextMenuOption1(); break;
        case 14: _t->onContextMenuOption2(); break;
        case 15: _t->onWatchButtonClicked(); break;
        case 16: _t->onUserImageClicked((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 17: _t->showMainList(); break;
        case 18: _t->onSetAvatarRequested(); break;
        case 19: _t->onSystemSettingsRequested(); break;
        case 20: _t->onToggleStreamingIsland(); break;
        case 21: _t->onScreenSelected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 22: _t->onAvatarSelected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 23: _t->onClearMarksRequested(); break;
        case 24: _t->onExitRequested(); break;
        case 25: _t->onHideRequested(); break;
        case 26: _t->onAnnotationColorChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 27: _t->onUserNameChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 28: _t->onMicToggleRequested((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 29: _t->onSpeakerToggleRequested((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 30: _t->onLocalQualitySelected((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 31: _t->onAudioOutputSelectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 32: _t->onMicInputSelectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 33: _t->onManualApprovalEnabledChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QAbstractSocket::SocketError >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (MainWindow::*)();
            if (_q_method_type _q_method = &MainWindow::appInitialized; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (MainWindow::*)();
            if (_q_method_type _q_method = &MainWindow::appReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN10MainWindowE.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 34)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 34;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 34)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 34;
    }
    return _id;
}

// SIGNAL 0
void MainWindow::appInitialized()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void MainWindow::appReady()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
