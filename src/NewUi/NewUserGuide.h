#ifndef NEW_USER_GUIDE_H
#define NEW_USER_GUIDE_H

#include <QWidget>
#include <QList>
#include <QRect>
#include <QPushButton>

class NewUiWindow;
class QLabel;

class NewUserGuide : public QWidget {
    Q_OBJECT

public:
    explicit NewUserGuide(NewUiWindow *parent);
    ~NewUserGuide() override = default;

    void calculateTargets();
    void resetToStart();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    NewUiWindow *m_parentWindow;
    QPushButton *m_dismissBtn;
    QPushButton *m_fakeRemoteBtn; // Fake button for remote control guide

    struct GuideItem {
        QRect rect;
        QString description;
        QString detailText; // Detailed description for the center box
        enum Position { Left, Right, Top, Bottom };
        Position position;

        // Layout info
        QPoint lineStart;
        QPoint lineEnd;
        QRect textRect;
    };
    QList<GuideItem> m_items;
    int m_currentIndex = 0;
    QWidget *m_localCardTarget = nullptr;
    QString m_localCardStyle;
    QWidget *m_fakeContextMenu = nullptr;
    QLabel *m_fakePrivacyItem = nullptr;
    QLabel *m_fakeCameraItem = nullptr;

    void layoutItems();
    void nextStep();
    void applyLocalCardDemoStyle(bool active);
    void restoreLocalCardStyle();
    void showFakeContextMenu(const QRect &targetRect, bool highlightPrivacy, bool highlightCamera);
    void hideFakeContextMenu();
};

#endif // NEW_USER_GUIDE_H
