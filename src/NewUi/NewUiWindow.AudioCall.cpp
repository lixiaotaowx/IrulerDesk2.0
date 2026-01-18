#include "NewUiWindow.h"

#include "../common/AppConfig.h"

#include <QAbstractButton>
#include <QAction>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayoutItem>
#include <QLabel>
#include <QMenu>
#include <QMetaType>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

static QString toJsStringLiteral(const QString &value)
{
    const QJsonArray a{value};
    const QString j = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
    if (j.size() >= 2 && j.startsWith('[') && j.endsWith(']')) {
        return j.mid(1, j.size() - 2);
    }
    return QStringLiteral("\"\"");
}

static QString readTextFileUtf8(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

static QString buildJanusAudioHtml()
{
    const QString wsUrlLiteral = toJsStringLiteral(AppConfig::janusWsUrl());
    const QString templatePath = AppConfig::janusAudioHtmlPath();
    QString html = readTextFileUtf8(templatePath);
    if (html.isEmpty()) {
        html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\" /></head><body>IrulerJanusAudio.html missing</body></html>");
    }
    return html.replace(QStringLiteral("__WS_URL__"), wsUrlLiteral);
}

namespace {
class JanusLogPage final : public QWebEnginePage
{
public:
    explicit JanusLogPage(QObject *parent = nullptr) : QWebEnginePage(parent) {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString &message,
                                  int lineNumber,
                                  const QString &sourceID) override
    {
        if (level == QWebEnginePage::ErrorMessageLevel) {
            // qInfo().noquote() << message << "line=" << lineNumber << "src=" << sourceID;
        } else {
            // qInfo().noquote() << message;
        }
        QWebEnginePage::javaScriptConsoleMessage(level, message, lineNumber, sourceID);
    }
};
}

void NewUiWindow::ensureJanusAudioLoaded()
{
    if (m_janusAudioLoaded) {
        return;
    }
    if (!m_function1WebView || !m_function1WebView->page()) {
        return;
    }

    auto *page = m_function1WebView->page();
    if (!dynamic_cast<JanusLogPage*>(page)) {
        auto *p = new JanusLogPage(m_function1WebView);
        m_function1WebView->setPage(p);
        page = p;
    }
    if (m_function1WebView->settings()) {
        m_function1WebView->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    }
    page->setAudioMuted(false);
    connect(page, &QWebEnginePage::featurePermissionRequested, this,
            [page](const QUrl &securityOrigin, QWebEnginePage::Feature feature) {
                if (feature == QWebEnginePage::MediaAudioCapture ||
                    feature == QWebEnginePage::MediaAudioVideoCapture) {
                    page->setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionGrantedByUser);
                }
            });

    connect(m_function1WebView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            return;
        }
        if (m_janusIgnoreAlone) {
             const QString ignoreJs = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.setIgnoreAlone(true);");
             if (m_function1WebView && m_function1WebView->page()) {
                 m_function1WebView->page()->runJavaScript(ignoreJs);
             }
        }
        applyJanusAudioState();
    });

    const QString templatePath = AppConfig::janusAudioHtmlPath();
    if (!templatePath.isEmpty()) {
        const QFileInfo fi(templatePath);
        /*
        // qInfo().noquote() << "[KickDiag] janus template"
                          << " path=" << fi.absoluteFilePath()
                          << " bytes=" << fi.size();
        */
    } else {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString expected = QDir(appDir).filePath(QStringLiteral("src/web/IrulerJanusAudio.html"));
        const QFileInfo expectedFi(QDir::cleanPath(expected));
        // qInfo().noquote()
        //     << QStringLiteral("[KickDiag] janus template missing app_dir=%1 cwd=%2 expect=%3 exist=%4")
        //            .arg(appDir,
        //                 QDir::currentPath(),
        //                 expectedFi.absoluteFilePath(),
        //                 expectedFi.exists() && expectedFi.isFile() ? QStringLiteral("true") : QStringLiteral("false"));
    }
    m_function1WebView->setHtml(buildJanusAudioHtml(), QUrl(QStringLiteral("http://localhost/")));
    m_janusAudioLoaded = true;
}

void NewUiWindow::applyJanusAudioState()
{
    if (!m_function1WebView || !m_function1WebView->page()) {
        return;
    }
    if (m_janusDesiredRoomOwnerId.isEmpty()) {
        return;
    }
    const QString desiredOwnerId = m_janusDesiredRoomOwnerId;
    const qint64 room = AppConfig::janusAudioRoomForUserId(desiredOwnerId);
    const QString display = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
    const bool muted = !m_globalMicEnabled;

    const QString readyJs = QStringLiteral(
        "(() => { try { return !!(window.__JANUS_READY__ && window.IrulerJanusAudio); } catch (e) { return false; } })();");

    m_function1WebView->page()->runJavaScript(
        readyJs,
        [this, desiredOwnerId, room, display, muted](const QVariant &ret) {
            if (!this) {
                return;
            }
            if (m_janusDesiredRoomOwnerId != desiredOwnerId) {
                return;
            }
            if (!ret.toBool()) {
                return;
            }

            if (m_janusActiveRoomOwnerId == desiredOwnerId) {
                const QString setMutedJs = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.setMuted(%1);")
                                               .arg(muted ? QStringLiteral("true") : QStringLiteral("false"));
                m_function1WebView->page()->runJavaScript(setMutedJs);
                return;
            }

            /*
            // qInfo().noquote() << "[KickDiag] janus state"
                              << " owner=" << desiredOwnerId
                              << " room=" << room
                              << " muted=" << (muted ? "true" : "false");
            */
            const QString switchJs = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.switchRoom(%1, %2, %3);")
                                         .arg(QString::number(room),
                                              toJsStringLiteral(display),
                                              muted ? QStringLiteral("true") : QStringLiteral("false"));
            m_function1WebView->page()->runJavaScript(switchJs);

            // [Fix] Ensure ignoreAlone state is synced after switch
            if (m_janusIgnoreAlone) {
                const QString ignoreJs = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.setIgnoreAlone(true);");
                m_function1WebView->page()->runJavaScript(ignoreJs);
            }

            m_janusActiveRoomOwnerId = desiredOwnerId;
        });
}

void NewUiWindow::stopJanusEnsure()
{
    m_janusEnsureOwnerId.clear();
    m_janusEnsureAttempt = 0;
    m_janusEnsureStartAtMs = 0;
    if (m_janusEnsureTimer) {
        m_janusEnsureTimer->stop();
    }
}

void NewUiWindow::scheduleJanusEnsure(const QString &desiredOwnerId)
{
    if (desiredOwnerId.isEmpty()) {
        stopJanusEnsure();
        return;
    }

    m_janusEnsureOwnerId = desiredOwnerId;
    m_janusEnsureAttempt = 0;
    m_janusEnsureStartAtMs = QDateTime::currentMSecsSinceEpoch();

    if (!m_janusEnsureTimer) {
        m_janusEnsureTimer = new QTimer(this);
        m_janusEnsureTimer->setSingleShot(true);
        connect(m_janusEnsureTimer, &QTimer::timeout, this, [this]() {
            const QString desiredOwnerId = m_janusEnsureOwnerId;
            if (desiredOwnerId.isEmpty()) {
                return;
            }
            if (!m_function1WebView || !m_function1WebView->page()) {
                m_janusEnsureAttempt++;
                if (m_janusEnsureAttempt <= 60 && m_janusEnsureTimer) {
                    m_janusEnsureTimer->start(400);
                }
                return;
            }

            ensureJanusAudioLoaded();

            const qint64 expectedRoom = AppConfig::janusAudioRoomForUserId(desiredOwnerId);
            const QString getStateJs = QStringLiteral(
                "(() => { try { return (window.IrulerJanusAudio && IrulerJanusAudio.getState) ? IrulerJanusAudio.getState() : null; } catch (e) { return null; } })();");

            m_function1WebView->page()->runJavaScript(getStateJs, [this, desiredOwnerId, expectedRoom](const QVariant &ret) {
                if (!this) {
                    return;
                }
                if (m_janusEnsureOwnerId != desiredOwnerId) {
                    return;
                }

                qint64 room = 0;
                qint64 sessionId = 0;
                qint64 wsState = -1;
                bool parsed = false;

                const QVariantMap map = ret.toMap();
                if (!map.isEmpty()) {
                    parsed = true;
                    room = map.value(QStringLiteral("room")).toLongLong();
                    sessionId = map.value(QStringLiteral("sessionId")).toLongLong();
                    wsState = map.value(QStringLiteral("wsState")).toLongLong();
                }

                const bool inRoom = parsed && (room == expectedRoom) && (sessionId > 0) && (wsState == 1);
                if (inRoom) {
                    stopJanusEnsure();
                    return;
                }

                // [Fix] Only retry the switch command periodically to avoid interrupting the join process
                // JS join can take a few seconds. If we reset every 350ms/900ms via applyJanusAudioState(), it never finishes.
                // m_janusEnsureAttempt 0 is skipped because we just called applyJanusAudioState() before scheduling.
                const bool shouldRetryCommand = (m_janusActiveRoomOwnerId != desiredOwnerId) || 
                                              (m_janusEnsureAttempt > 0 && m_janusEnsureAttempt % 10 == 0);

                if (shouldRetryCommand) {
                    m_janusActiveRoomOwnerId.clear();
                    applyJanusAudioState();
                }

                m_janusEnsureAttempt++;
                if (m_janusEnsureAttempt > 60) {
                    return;
                }
                const int delayMs = (m_janusEnsureAttempt <= 8) ? 350 : 900;
                if (m_janusEnsureTimer) {
                    m_janusEnsureTimer->start(delayMs);
                }
            });
        });
    }

    if (m_janusEnsureTimer) {
        m_janusEnsureTimer->stop();
        m_janusEnsureTimer->start(200);
    }
}

void NewUiWindow::janusSwitchToUserRoom(const QString &userId)
{
    if (userId.isEmpty()) {
        return;
    }
    if (m_janusDesiredRoomOwnerId == userId) {
        ensureJanusAudioLoaded();
        applyJanusAudioState();
        scheduleJanusEnsure(userId);
        return;
    }
    m_janusDesiredRoomOwnerId = userId;
    ensureJanusAudioLoaded();
    applyJanusAudioState();
    scheduleJanusEnsure(userId);
}

void NewUiWindow::janusSwitchToMyRoom()
{
    if (m_myStreamId.isEmpty()) {
        return;
    }
    if (m_janusDesiredRoomOwnerId == m_myStreamId) {
        ensureJanusAudioLoaded();
        applyJanusAudioState();
        scheduleJanusEnsure(m_myStreamId);
        return;
    }
    m_janusDesiredRoomOwnerId = m_myStreamId;
    ensureJanusAudioLoaded();
    applyJanusAudioState();
    scheduleJanusEnsure(m_myStreamId);
}

void NewUiWindow::janusSetMuted(bool muted)
{
    if (!m_function1WebView || !m_function1WebView->page()) {
        return;
    }
    ensureJanusAudioLoaded();
    const QString js = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.setMuted(%1);")
                           .arg(muted ? QStringLiteral("true") : QStringLiteral("false"));
    m_function1WebView->page()->runJavaScript(js);
}

void NewUiWindow::janusStop()
{
    stopJanusEnsure();
    m_janusDesiredRoomOwnerId.clear();
    m_janusActiveRoomOwnerId.clear();
    hideAudioCallUi();
    if (!m_function1WebView || !m_function1WebView->page()) {
        return;
    }
    ensureJanusAudioLoaded();
    const QString js = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.stop && IrulerJanusAudio.stop('hangup');");
    m_function1WebView->page()->runJavaScript(js);
}

void NewUiWindow::showAudioCallUiForSession(const QString &peerId, bool forceEnableMic)
{
    showAudioCallUiInternal(peerId, forceEnableMic);
}

void NewUiWindow::restoreAudioCallUi()
{
    setAudioCallMiniHidden(false);
    hideAudioCallMiniBar();
    if (m_audioCallDialog && !m_audioCallPeerId.isEmpty()) {
        m_audioCallDialog->show();
        m_audioCallDialog->raise();
    }
}

QString NewUiWindow::activeAudioCallPeerId() const
{
    return m_audioCallPeerId;
}

void NewUiWindow::ensureAudioCallUi()
{
    if (m_audioCallDialog) {
        return;
    }

    auto *dlg = new QDialog(nullptr);
    static_cast<QObject*>(dlg)->setParent(this);
    dlg->setWindowTitle(QStringLiteral("通话"));
    dlg->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    dlg->setAttribute(Qt::WA_TranslucentBackground, true);
    dlg->setAttribute(Qt::WA_ShowWithoutActivating, true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    dlg->setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
#endif
    dlg->setFocusPolicy(Qt::NoFocus);
    dlg->setModal(false);
    dlg->setMinimumSize(320, 180);
    dlg->installEventFilter(this);

    auto *outerLayout = new QVBoxLayout(dlg);
    outerLayout->setContentsMargins(10, 10, 10, 10);
    outerLayout->setSpacing(0);

    auto *panel = new QFrame(dlg);
    panel->setObjectName(QStringLiteral("AudioCallPanel"));
    panel->setStyleSheet(QStringLiteral("QFrame#AudioCallPanel { background: #3a3a3a; border-radius: 12px; }"));
    panel->setFocusPolicy(Qt::NoFocus);
    panel->installEventFilter(this);
    auto *shadow = new QGraphicsDropShadowEffect(panel);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 160));
    panel->setGraphicsEffect(shadow);
    outerLayout->addWidget(panel);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(12);

    const QString appDir = QCoreApplication::applicationDirPath();

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(0);
    topRow->addStretch(1);
    auto *minBtn = new QPushButton(panel);
    minBtn->setFixedSize(28, 28);
    minBtn->setText(QStringLiteral("-"));
    QFont f = minBtn->font();
    f.setBold(true);
    f.setPointSize(16);
    minBtn->setFont(f);
    minBtn->setFlat(true);
    minBtn->setCursor(Qt::PointingHandCursor);
    minBtn->setToolTip(QStringLiteral("最小化到通话条"));
    minBtn->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; color: rgba(255,255,255,220); }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); border-radius: 6px; }"
        "QPushButton:pressed { background: rgba(255,255,255,0.12); border-radius: 6px; }"));
    connect(minBtn, &QPushButton::clicked, this, [this]() {
        showAudioCallMiniBar();
    });
    topRow->addWidget(minBtn);
    layout->addLayout(topRow);

    auto *area = new QScrollArea(dlg);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setFixedHeight(86);
    area->installEventFilter(this);
    if (area->viewport()) {
        area->viewport()->installEventFilter(this);
    }
    if (area->horizontalScrollBar()) {
        area->horizontalScrollBar()->setStyleSheet(QStringLiteral("QScrollBar:horizontal{height:0px;}"));
    }

    auto *people = new QWidget(area);
    people->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    people->installEventFilter(this);
    auto *peopleLayout = new QHBoxLayout(people);
    peopleLayout->setContentsMargins(0, 0, 0, 0);
    peopleLayout->setSpacing(12);
    peopleLayout->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    peopleLayout->setSizeConstraint(QLayout::SetMinimumSize);
    people->setLayout(peopleLayout);
    area->setWidget(people);

    m_audioCallParticipantsArea = area;
    m_audioCallParticipantsWidget = people;
    m_audioCallParticipantsLayout = peopleLayout;

    layout->addWidget(area);

    layout->addStretch();

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(22);

    const QIcon micOn(appDir + "/maps/logo/Mic_oN2.png");
    const QIcon micOff(appDir + "/maps/logo/Mic_oFF2.png");
    const QIcon hangupIcon(appDir + "/maps/logo/guaduan.png");
    const QIcon spkOn(appDir + "/maps/logo/laba_on2.png");
    const QIcon spkOff(appDir + "/maps/logo/laba_oFF2.png");

    auto styleIconBtn = [](QPushButton *b) {
        b->setFlat(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            "QPushButton { border: none; background: rgba(255,255,255,0.06); border-radius: 28px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.10); }"
            "QPushButton:pressed { background: rgba(255,255,255,0.14); }");
    };

    auto *micBtn = new QPushButton(dlg);
    micBtn->setCheckable(true);
    micBtn->setChecked(m_globalMicEnabled);
    micBtn->setIcon(m_globalMicEnabled ? micOn : micOff);
    micBtn->setIconSize(QSize(44, 44));
    micBtn->setFixedSize(56, 56);
    styleIconBtn(micBtn);

    auto *hangupBtn = new QPushButton(dlg);
    hangupBtn->setIcon(hangupIcon);
    hangupBtn->setIconSize(QSize(44, 44));
    hangupBtn->setFixedSize(56, 56);
    hangupBtn->setDefault(true);
    styleIconBtn(hangupBtn);

    auto *speakerBtn = new QPushButton(dlg);
    speakerBtn->setCheckable(true);
    speakerBtn->setChecked(true);
    speakerBtn->setIcon(spkOn);
    speakerBtn->setIconSize(QSize(44, 44));
    speakerBtn->setFixedSize(56, 56);
    styleIconBtn(speakerBtn);

    btnRow->addStretch();
    btnRow->addWidget(micBtn);
    btnRow->addWidget(hangupBtn);
    btnRow->addWidget(speakerBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto *timer = new QTimer(dlg);
    timer->setInterval(1000);

    connect(micBtn, &QPushButton::toggled, this, [this, micBtn, micOn, micOff](bool enabled) {
        micBtn->setIcon(enabled ? micOn : micOff);
        setGlobalMicCheckedSilently(enabled);
        emit micToggleRequested(enabled);
    });

    connect(speakerBtn, &QPushButton::toggled, this, [this, speakerBtn, spkOn, spkOff](bool enabled) {
        m_audioCallSpeakerEnabled = enabled;
        speakerBtn->setIcon(enabled ? spkOn : spkOff);
        if (m_function1WebView && m_function1WebView->page()) {
            const QString js = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.setSpeakerEnabled && IrulerJanusAudio.setSpeakerEnabled(%1);")
                                   .arg(enabled ? QStringLiteral("true") : QStringLiteral("false"));
            m_function1WebView->page()->runJavaScript(js);
        }
    });

    connect(hangupBtn, &QPushButton::clicked, this, &NewUiWindow::hangupAudioCallUi);
    connect(dlg, &QDialog::rejected, this, &NewUiWindow::hangupAudioCallUi);
    connect(timer, &QTimer::timeout, this, &NewUiWindow::refreshAudioCallParticipants);

    m_audioCallDialog = dlg;
    m_audioCallMuteBtn = micBtn;
    m_audioCallHangupBtn = hangupBtn;
    m_audioCallSpeakerBtn = speakerBtn;
    m_audioCallPollTimer = timer;

    if (!m_audioCallMiniBar) {
        auto *mini = new QDialog(nullptr);
        static_cast<QObject*>(mini)->setParent(this);
        mini->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        mini->setAttribute(Qt::WA_TranslucentBackground, true);
        mini->setAttribute(Qt::WA_ShowWithoutActivating, true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        mini->setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
#endif
        mini->setFocusPolicy(Qt::NoFocus);
        mini->setModal(false);
        mini->installEventFilter(this);

        auto *miniOuter = new QVBoxLayout(mini);
        const int panelSize = 64;
        const int margin = 12;
        mini->setFixedSize(panelSize + margin * 2, panelSize + margin * 2);
        miniOuter->setContentsMargins(margin, margin, margin, margin);
        miniOuter->setSpacing(0);

        auto *miniPanel = new QFrame(mini);
        miniPanel->setObjectName(QStringLiteral("AudioCallMiniPanel"));
        miniPanel->setFixedSize(panelSize, panelSize);
        miniPanel->setStyleSheet(QStringLiteral("QFrame#AudioCallMiniPanel{ background: #3a3a3a; border-radius: 14px; }"));
        miniPanel->setFocusPolicy(Qt::NoFocus);
        miniPanel->installEventFilter(this);
        auto *miniShadow = new QGraphicsDropShadowEffect(miniPanel);
        miniShadow->setBlurRadius(18);
        miniShadow->setOffset(0, 6);
        miniShadow->setColor(QColor(0, 0, 0, 140));
        miniPanel->setGraphicsEffect(miniShadow);
        miniOuter->addWidget(miniPanel);

        auto *miniLayout = new QVBoxLayout(miniPanel);
        miniLayout->setContentsMargins(0, 0, 0, 0);
        miniLayout->setSpacing(0);

        auto *logo = new QLabel(miniPanel);
        logo->setAlignment(Qt::AlignCenter);
        logo->setAttribute(Qt::WA_TransparentForMouseEvents);
        logo->setFixedSize(panelSize, panelSize);
        const QPixmap p(appDir + "/maps/logo/iruler.ico");
        if (!p.isNull()) {
            const int logoSize = 40;
            logo->setPixmap(p.scaled(logoSize, logoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        miniLayout->addWidget(logo, 0, Qt::AlignCenter);

        mini->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(mini, &QDialog::customContextMenuRequested, this, [this](const QPoint &pos) {
            if (!m_audioCallMiniBar || !m_audioCallMiniBar->isVisible()) return;
            QMenu menu(m_audioCallMiniBar);
            QAction *hideAct = menu.addAction(QStringLiteral("隐藏"));
            QAction *picked = menu.exec(m_audioCallMiniBar->mapToGlobal(pos));
            if (picked == hideAct) {
                setAudioCallMiniHidden(true);
            }
        });

        m_audioCallMiniBar = mini;
        m_audioCallMiniLogoLabel = logo;
    }
}

void NewUiWindow::showAudioCallUi(const QString &peerId)
{
    showAudioCallUiInternal(peerId, true);
}

void NewUiWindow::showAudioCallUiInternal(const QString &peerId, bool forceEnableMic)
{
    if (peerId.isEmpty()) {
        return;
    }
    ensureAudioCallUi();
    if (!m_audioCallDialog) {
        return;
    }
    if (forceEnableMic && !m_globalMicEnabled) {
        setGlobalMicCheckedSilently(true);
    }
    m_audioCallSpeakerEnabled = true;
    m_audioCallPeerId = peerId;
    setAudioCallMiniHidden(false);
    hideAudioCallMiniBar();

    updateTalkButtonsAvailability();
    if (m_audioCallMuteBtn) {
        QSignalBlocker blocker(m_audioCallMuteBtn);
        m_audioCallMuteBtn->setChecked(m_globalMicEnabled);
    }
    if (m_audioCallSpeakerBtn) {
        QSignalBlocker blocker(m_audioCallSpeakerBtn);
        m_audioCallSpeakerBtn->setChecked(true);
        const QString appDir = QCoreApplication::applicationDirPath();
        m_audioCallSpeakerBtn->setIcon(QIcon(appDir + "/maps/logo/laba_on2.png"));
    }
    if (m_function1WebView && m_function1WebView->page()) {
        const QString js = QStringLiteral("window.IrulerJanusAudio && IrulerJanusAudio.setSpeakerEnabled && IrulerJanusAudio.setSpeakerEnabled(true);");
        m_function1WebView->page()->runJavaScript(js);
    }
    refreshAudioCallParticipants();
    if (m_audioCallPollTimer && !m_audioCallPollTimer->isActive()) {
        m_audioCallPollTimer->start();
    }
    if (!m_audioCallDialog->isVisible()) {
        const QRect parentRect = this->geometry();
        const int x = parentRect.center().x() - m_audioCallDialog->width() / 2;
        const int y = parentRect.center().y() - m_audioCallDialog->height() / 2;
        m_audioCallDialog->move(x, y);
        m_audioCallDialog->show();
    }
    m_audioCallDialog->raise();
}

void NewUiWindow::hideAudioCallUi()
{
    if (m_audioCallPollTimer) {
        m_audioCallPollTimer->stop();
    }
    m_audioCallPeerId.clear();
    setAudioCallMiniHidden(false);
    updateTalkButtonsAvailability();
    if (m_audioCallDialog) {
        m_audioCallDialog->hide();
    }
    hideAudioCallMiniBar();
}

void NewUiWindow::hangupAudioCallUi()
{
    const QString peerId = m_audioCallPeerId;
    if (!peerId.isEmpty()) {
        setTalkConnected(peerId, false);
        setTalkRemoteActive(peerId, false);
        emit talkToggleRequested(peerId, false);
    } else {
        janusStop();
    }
    hideAudioCallUi();
}

void NewUiWindow::rebuildAudioCallParticipantsUi(const QStringList &names)
{
    if (!m_audioCallParticipantsLayout) {
        return;
    }

    while (QLayoutItem *it = m_audioCallParticipantsLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }

    QStringList finalNames;
    for (const QString &n : names) {
        const QString s = n.trimmed();
        if (!s.isEmpty()) {
            finalNames.append(s);
        }
    }
    if (finalNames.isEmpty()) {
        const QString me = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
        if (!me.isEmpty()) {
            finalNames.append(me);
        }
    }

    auto findUserIdByDisplayName = [this](const QString &displayName) -> QString {
        if (displayName.isEmpty()) return QString();
        const QString meName = m_myUserName.isEmpty() ? m_myStreamId : m_myUserName;
        if (!meName.isEmpty() && displayName == meName) return m_myStreamId;
        if (displayName == m_myStreamId) return m_myStreamId;
        for (auto it = m_userItems.begin(); it != m_userItems.end(); ++it) {
            const QString userId = it.key();
            QListWidgetItem *item = it.value();
            if (!item) continue;
            const QString n = item->data(Qt::UserRole + 1).toString();
            if (!n.isEmpty() && n == displayName) return userId;
            if (userId == displayName) return userId;
        }
        return QString();
    };

    const int cellWidth = 72;
    const int avatarSize = 44;
    const int spacing = m_audioCallParticipantsLayout->spacing();
    const int count = finalNames.size();
    const int totalWidth = (count <= 0) ? 0 : (count * cellWidth + qMax(0, count - 1) * spacing);
    
    // [Fix] Adaptive width: Resize dialog to fit participants
    if (m_audioCallDialog && totalWidth > 0) {
        const int minW = 320;
        const int maxW = 1200; // Safe limit
        const int padding = 60; // Margins
        const int newW = qBound(minW, totalWidth + padding, maxW);
        if (m_audioCallDialog->width() != newW) {
            m_audioCallDialog->resize(newW, m_audioCallDialog->height());
        }
    }

    const int viewportWidth = (m_audioCallParticipantsArea && m_audioCallParticipantsArea->viewport())
                                  ? m_audioCallParticipantsArea->viewport()->width()
                                  : 0;
    const bool shouldCenter = (viewportWidth > 0 && totalWidth > 0 && totalWidth <= viewportWidth);

    m_audioCallParticipantsLayout->setAlignment((shouldCenter ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter);
    if (shouldCenter) {
        m_audioCallParticipantsLayout->addStretch(1);
    }
    for (const QString &name : finalNames) {
        auto *cell = new QWidget(m_audioCallParticipantsWidget);
        cell->setFixedWidth(cellWidth);
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        auto *vl = new QVBoxLayout(cell);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(6);
        // [Fix] Align avatars to Top
        vl->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        auto *av = new QLabel(cell);
        av->setFixedSize(avatarSize, avatarSize);
        av->setAlignment(Qt::AlignCenter);

        QPixmap avatar = QPixmap();
        const QString uid = findUserIdByDisplayName(name);
        cell->setProperty("viewerId", uid);
        
        // [Fix] Crash protection: Use QPointer and safe menu parenting
        QPointer<QWidget> cellPtr = cell;
        
        if (!uid.isEmpty() && uid != m_myStreamId) {
            cell->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(cell, &QWidget::customContextMenuRequested, this, [this, cellPtr](const QPoint &pos) {
                if (!cellPtr) return;
                const QString viewerId = cellPtr->property("viewerId").toString();
                if (viewerId.isEmpty() || viewerId == m_myStreamId) {
                    return;
                }
                // Parent menu to dialog to survive cell destruction
                QMenu menu(m_audioCallDialog); 
                menu.setAttribute(Qt::WA_DeleteOnClose);
                QAction *kickAct = menu.addAction(QStringLiteral("踢出"));
                QAction *picked = menu.exec(cellPtr->mapToGlobal(pos));
                if (picked == kickAct) {
                    emit kickViewerRequested(viewerId);
                }
            });
        }
        if (!uid.isEmpty()) {
            QPixmap cached(avatarCacheFilePath(uid));
            if (!cached.isNull()) {
                avatar = makeCircularPixmap(cached, avatarSize);
            }
        }
        if (avatar.isNull()) {
            avatar = buildHeadAvatarPixmap(avatarSize);
        }
        if (!avatar.isNull()) {
            av->setPixmap(avatar.scaled(avatarSize, avatarSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }

        // [Fix] Add (房主) label to Host
        QString labelText = name;
        if (!uid.isEmpty() && !m_janusDesiredRoomOwnerId.isEmpty() && uid == m_janusDesiredRoomOwnerId) {
             labelText += QStringLiteral("\n(房主)");
        }

        auto *lb = new QLabel(labelText, cell);
        lb->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        lb->setStyleSheet("color: #e0e0e0; font-size: 12px; background: transparent;");
        lb->setFixedWidth(cellWidth);
        // lb->setFixedHeight(32); // Allow height expansion for 2 lines
        lb->setWordWrap(true);

        vl->addWidget(av, 0, Qt::AlignHCenter);
        vl->addWidget(lb, 0, Qt::AlignHCenter);

        m_audioCallParticipantsLayout->addWidget(cell);

        // [Fix] Allow Host to kick others with Crash Protection
        cell->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(cell, &QWidget::customContextMenuRequested, this, [this, cellPtr](const QPoint &pos) {
            if (!cellPtr) return;
            const QString viewerId = cellPtr->property("viewerId").toString();
            const bool iAmHost = (m_myStreamId == m_janusDesiredRoomOwnerId);
            const bool targetIsMe = (viewerId == m_myStreamId);

            if (viewerId.isEmpty() || targetIsMe || !iAmHost) {
                return;
            }
            // Parent menu to dialog to survive cell destruction
            QMenu menu(m_audioCallDialog);
            menu.setAttribute(Qt::WA_DeleteOnClose);
            QAction *kickAct = menu.addAction(QStringLiteral("踢出"));
            QAction *picked = menu.exec(cellPtr->mapToGlobal(pos));
            if (picked == kickAct) {
                emit kickViewerRequested(viewerId);
            }
        });
    }

    if (shouldCenter) {
        m_audioCallParticipantsLayout->addStretch(1);
    }
    if (m_audioCallParticipantsWidget) {
        m_audioCallParticipantsWidget->adjustSize();
    }
}

void NewUiWindow::showAudioCallMiniBar()
{
    if (!m_audioCallDialog || m_audioCallPeerId.isEmpty()) {
        return;
    }
    ensureAudioCallUi();
    if (!m_audioCallMiniBar) {
        return;
    }
    setAudioCallMiniHidden(false);
    m_audioCallMiniBarDragging = false;
    if (m_audioCallDialog->isVisible()) {
        m_audioCallDialog->hide();
    }
    if (!m_audioCallMiniBar->isVisible()) {
        QScreen *screen = QGuiApplication::primaryScreen();
        const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
        const QSize s = m_audioCallMiniBar->size();
        const int x = avail.right() - s.width() - 18;
        const int y = avail.bottom() - s.height() - 18;
        m_audioCallMiniBar->setGeometry(QRect(QPoint(x, y), s));
    }
    m_audioCallMiniBar->show();
    m_audioCallMiniBar->raise();
}

void NewUiWindow::hideAudioCallMiniBar()
{
    if (m_audioCallMiniBar) {
        m_audioCallMiniBar->hide();
    }
}

void NewUiWindow::setAudioCallMiniHidden(bool hidden)
{
    m_audioCallMiniHidden = hidden;
    if (m_audioCallTitleRestoreBtn) {
        const bool visible = (m_audioCallMiniHidden && !m_audioCallPeerId.isEmpty());
        m_audioCallTitleRestoreBtn->setVisible(visible);
    }
    const bool available = (m_audioCallMiniHidden && !m_audioCallPeerId.isEmpty());
    if (available != m_audioCallRestoreAvailable) {
        m_audioCallRestoreAvailable = available;
        emit audioCallRestoreAvailableChanged(available);
    }
    if (m_audioCallMiniHidden) {
        hideAudioCallMiniBar();
    }
}

void NewUiWindow::updateTalkButtonsAvailability()
{
    const bool callBusy = !m_audioCallPeerId.isEmpty();
    const QString activeId = m_audioCallPeerId;
    for (auto it = m_talkButtons.begin(); it != m_talkButtons.end(); ++it) {
        const QString userId = it.key();
        QPushButton *btn = it.value();
        if (!btn) continue;
        const bool allowed = (!callBusy || userId == activeId);
        btn->setCursor(allowed ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
        if (!allowed) {
            btn->setToolTip(QStringLiteral("通话中，无法拨打其它人"));
        } else if (btn->toolTip() == QStringLiteral("通话中，无法拨打其它人")) {
            btn->setToolTip(QString());
        }
    }
}

void NewUiWindow::refreshAudioCallParticipants()
{
    if (!m_function1WebView || !m_function1WebView->page() || !m_audioCallDialog) {
        return;
    }
    const QString js = QStringLiteral(
        "(() => { try {"
        "  if (window.IrulerJanusAudio && IrulerJanusAudio.listParticipantsNow) {"
        "    try { IrulerJanusAudio.listParticipantsNow(); } catch (e) {}"
        "  }"
        "  const ps = (window.IrulerJanusAudio && IrulerJanusAudio.getParticipants) ? IrulerJanusAudio.getParticipants() : [];"
        "  const st = (window.IrulerJanusAudio && IrulerJanusAudio.getState) ? IrulerJanusAudio.getState() : {};"
        "  return {participants: ps, state: st};"
        "} catch (e) { return {participants: [], state: {}}; } })();");
    m_function1WebView->page()->runJavaScript(js, [this](const QVariant &v) {
        if (!this) {
            return;
        }
        QVariantList list;
        QVariantMap state;
        if (v.userType() == QMetaType::QVariantMap) {
            const QVariantMap obj = v.toMap();
            list = obj.value(QStringLiteral("participants")).toList();
            state = obj.value(QStringLiteral("state")).toMap();
        } else if (v.userType() == QMetaType::QVariantList) {
            list = v.toList();
        }

        const int room = state.value(QStringLiteral("room")).toInt();
        const QString stopReason = state.value(QStringLiteral("lastStopReason")).toString();
        // [Fix] Handle both 30s and 3s (quick exit) reasons
        if (room == 0 && (stopReason == QStringLiteral("alone_30s") || stopReason == QStringLiteral("alone_3s"))) {
            hangupAudioCallUi();
            return;
        }

        QStringList names;
        for (const QVariant &it : list) {
            const QString s = it.toString().trimmed();
            if (!s.isEmpty()) {
                names.append(s);
            }
        }

        // [Fix] Auto-close waiting dialog when someone joins
        if (m_isWaitingForAttendees && names.size() > 1) {
            m_isWaitingForAttendees = false;
            janusSetIgnoreAlone(false);
            if (m_inviteWaitDialog) {
                m_inviteWaitDialog->accept();
            }
        }

        rebuildAudioCallParticipantsUi(names);
    });
}

void NewUiWindow::janusSetIgnoreAlone(bool ignore)
{
    m_janusIgnoreAlone = ignore;
    if (!m_function1WebView || !m_function1WebView->page()) return;
    QString js = QStringLiteral("if (window.IrulerJanusAudio && IrulerJanusAudio.setIgnoreAlone) { IrulerJanusAudio.setIgnoreAlone(%1); }")
        .arg(ignore ? "true" : "false");
    m_function1WebView->page()->runJavaScript(js);
}
