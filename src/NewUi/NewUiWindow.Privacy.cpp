
#include "NewUiWindow.h"
#include <QPainter>
#include <QPixmap>

void NewUiWindow::togglePrivacyMode(bool enable)
{
    m_isPrivacyMode = enable;
    
    // Update local card UI or state if needed
    // The main visual update happens in buildLocalScreenFrame
    
    // Force immediate frame update to reflect change
    publishLocalScreenFrame(true);
}
