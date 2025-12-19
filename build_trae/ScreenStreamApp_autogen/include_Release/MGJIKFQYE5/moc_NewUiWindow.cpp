/****************************************************************************
** Meta object code from reading C++ file 'NewUiWindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/NewUi/NewUiWindow.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'NewUiWindow.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11NewUiWindowE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN11NewUiWindowE = QtMocHelpers::stringData(
    "NewUiWindow",
    "startWatchingRequested",
    "",
    "targetId",
    "systemSettingsRequested",
    "clearMarksRequested",
    "toggleStreamingIslandRequested",
    "kickViewerRequested",
    "viewerId",
    "closeRoomRequested",
    "talkToggleRequested",
    "enabled",
    "onTimerTimeout",
    "onTalkSpinnerTimeout",
    "onStreamLog",
    "msg",
    "onUserListUpdated",
    "users",
    "onLoginConnected"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN11NewUiWindowE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   86,    2, 0x06,    1 /* Public */,
       4,    0,   89,    2, 0x06,    3 /* Public */,
       5,    0,   90,    2, 0x06,    4 /* Public */,
       6,    0,   91,    2, 0x06,    5 /* Public */,
       7,    1,   92,    2, 0x06,    6 /* Public */,
       9,    0,   95,    2, 0x06,    8 /* Public */,
      10,    2,   96,    2, 0x06,    9 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      12,    0,  101,    2, 0x08,   12 /* Private */,
      13,    0,  102,    2, 0x08,   13 /* Private */,
      14,    1,  103,    2, 0x08,   14 /* Private */,
      16,    1,  106,    2, 0x08,   16 /* Private */,
      18,    0,  109,    2, 0x08,   18 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    3,   11,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   15,
    QMetaType::Void, QMetaType::QJsonArray,   17,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject NewUiWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN11NewUiWindowE.offsetsAndSizes,
    qt_meta_data_ZN11NewUiWindowE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN11NewUiWindowE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<NewUiWindow, std::true_type>,
        // method 'startWatchingRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'systemSettingsRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'clearMarksRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toggleStreamingIslandRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'kickViewerRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'closeRoomRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'talkToggleRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onTimerTimeout'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onTalkSpinnerTimeout'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStreamLog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onUserListUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QJsonArray &, std::false_type>,
        // method 'onLoginConnected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void NewUiWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<NewUiWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->startWatchingRequested((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->systemSettingsRequested(); break;
        case 2: _t->clearMarksRequested(); break;
        case 3: _t->toggleStreamingIslandRequested(); break;
        case 4: _t->kickViewerRequested((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->closeRoomRequested(); break;
        case 6: _t->talkToggleRequested((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->onTimerTimeout(); break;
        case 8: _t->onTalkSpinnerTimeout(); break;
        case 9: _t->onStreamLog((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->onUserListUpdated((*reinterpret_cast< std::add_pointer_t<QJsonArray>>(_a[1]))); break;
        case 11: _t->onLoginConnected(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (NewUiWindow::*)(const QString & );
            if (_q_method_type _q_method = &NewUiWindow::startWatchingRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (NewUiWindow::*)();
            if (_q_method_type _q_method = &NewUiWindow::systemSettingsRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (NewUiWindow::*)();
            if (_q_method_type _q_method = &NewUiWindow::clearMarksRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (NewUiWindow::*)();
            if (_q_method_type _q_method = &NewUiWindow::toggleStreamingIslandRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (NewUiWindow::*)(const QString & );
            if (_q_method_type _q_method = &NewUiWindow::kickViewerRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (NewUiWindow::*)();
            if (_q_method_type _q_method = &NewUiWindow::closeRoomRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (NewUiWindow::*)(const QString & , bool );
            if (_q_method_type _q_method = &NewUiWindow::talkToggleRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
    }
}

const QMetaObject *NewUiWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *NewUiWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN11NewUiWindowE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int NewUiWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void NewUiWindow::startWatchingRequested(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void NewUiWindow::systemSettingsRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void NewUiWindow::clearMarksRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void NewUiWindow::toggleStreamingIslandRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void NewUiWindow::kickViewerRequested(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void NewUiWindow::closeRoomRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void NewUiWindow::talkToggleRequested(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
