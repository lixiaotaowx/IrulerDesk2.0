#ifndef FIRSTLAUNCHWIZARD_H
#define FIRSTLAUNCHWIZARD_H

#include <QDialog>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QRadioButton>
#include <QScrollArea>
#include <QList>
#include <QScreen>
#include <QGuiApplication>
#include <QPixmap>

class FirstLaunchWizard : public QDialog {
    Q_OBJECT
public:
    explicit FirstLaunchWizard(QWidget* parent = nullptr);
    QString userName() const;
    int iconId() const;
    int screenIndex() const;

private:
    void setupStyle();
    void buildPages();
    void buildUserPage();
    void buildAvatarPage();
    void buildScreenPage();
    void selectAvatar(int id);
    void selectScreen(int idx);
    void updateNav();

    QStackedWidget* m_stack = nullptr;
    QWidget* m_userPage = nullptr;
    QWidget* m_avatarPage = nullptr;
    QWidget* m_screenPage = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QGridLayout* m_avatarGrid = nullptr;
    QList<QAbstractButton*> m_avatarButtons;
    QButtonGroup* m_avatarGroup = nullptr;
    int m_selectedIconId = -1;

    QHBoxLayout* m_screenRow = nullptr;
    QList<QAbstractButton*> m_screenButtons;
    QButtonGroup* m_screenGroup = nullptr;
    int m_selectedScreenIndex = -1;

    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QPushButton* m_finishBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
};

#endif // FIRSTLAUNCHWIZARD_H
