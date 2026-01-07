#ifndef FIRSTLAUNCHWIZARD_H
#define FIRSTLAUNCHWIZARD_H

#include <QDialog>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QScrollArea>
#include <QList>
#include <QScreen>
#include <QGuiApplication>
#include <QPixmap>
#include <QCloseEvent>

class FirstLaunchWizard : public QDialog {
    Q_OBJECT
public:
    explicit FirstLaunchWizard(QWidget* parent = nullptr);
    QString userName() const;
    int screenIndex() const;
    bool exitRequested() const;
protected:
    void closeEvent(QCloseEvent* e) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupStyle();
    void buildPages();
    void buildUserPage();
    void buildScreenPage();
    void selectScreen(int idx);
    void updateNav();

    QStackedWidget* m_stack = nullptr;
    QWidget* m_userPage = nullptr;
    QWidget* m_screenPage = nullptr;

    QLineEdit* m_nameEdit = nullptr;

    QHBoxLayout* m_screenRow = nullptr;
    QList<QAbstractButton*> m_screenButtons;
    QButtonGroup* m_screenGroup = nullptr;
    int m_selectedScreenIndex = -1;

    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QPushButton* m_finishBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
    bool m_exitRequested = false;
};

#endif // FIRSTLAUNCHWIZARD_H
