#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QDebug>

struct Task {
    QString id;
    QString content;
    QDateTime reminderTime;
    bool isCompleted;
    bool reminderShown;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["content"] = content;
        obj["reminderTime"] = reminderTime.toString(Qt::ISODate);
        obj["isCompleted"] = isCompleted;
        obj["reminderShown"] = reminderShown;
        return obj;
    }

    static Task fromJson(const QJsonObject &obj) {
        Task t;
        t.id = obj["id"].toString();
        t.content = obj["content"].toString();
        t.reminderTime = QDateTime::fromString(obj["reminderTime"].toString(), Qt::ISODate);
        t.isCompleted = obj["isCompleted"].toBool();
        t.reminderShown = obj["reminderShown"].toBool();
        return t;
    }
};

class TaskManager : public QObject {
    Q_OBJECT

public:
    static TaskManager& instance() {
        static TaskManager _instance;
        return _instance;
    }

    void addTask(const QString &content, const QDateTime &time) {
        Task t;
        t.id = QString::number(QDateTime::currentMSecsSinceEpoch());
        t.content = content;
        t.reminderTime = time;
        t.isCompleted = false;
        t.reminderShown = false;
        m_tasks.append(t);
        saveTasks();
        emit tasksChanged();
    }

    void completeTask(const QString &id, bool completed) {
        for (auto &t : m_tasks) {
            if (t.id == id) {
                t.isCompleted = completed;
                saveTasks();
                emit tasksChanged();
                break;
            }
        }
    }

    void removeTask(const QString &id) {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].id == id) {
                m_tasks.removeAt(i);
                saveTasks();
                emit tasksChanged();
                break;
            }
        }
    }

    QList<Task> getTasks() const {
        return m_tasks;
    }

signals:
    void tasksChanged();
    void taskReminder(const QString &content, const QString &taskId);

private:
    TaskManager() {
        loadTasks();
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &TaskManager::checkReminders);
        m_timer->start(10000); // Check every 10 seconds
    }

    QList<Task> m_tasks;
    QTimer *m_timer;

    void loadTasks() {
        QString path = getDataPath();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonArray arr = doc.array();
            for (const auto &val : arr) {
                m_tasks.append(Task::fromJson(val.toObject()));
            }
        }
    }

    void saveTasks() {
        QString path = getDataPath();
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonArray arr;
            for (const auto &t : m_tasks) {
                arr.append(t.toJson());
            }
            file.write(QJsonDocument(arr).toJson());
        }
    }

    QString getDataPath() {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tasks.json";
        QDir().mkpath(QFileInfo(path).path());
        return path;
    }

    void checkReminders() {
        QDateTime now = QDateTime::currentDateTime();
        bool changed = false;
        for (auto &t : m_tasks) {
            if (!t.isCompleted && !t.reminderShown && t.reminderTime.isValid() && t.reminderTime <= now) {
                t.reminderShown = true;
                emit taskReminder(t.content, t.id);
                changed = true;
            }
        }
        if (changed) {
            saveTasks();
        }
    }
};
