/*
* Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>
* Copyright 2014  Hugo Pereira Da Costa <hugo.pereira@free.fr>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of
* the License or (at your option) version 3 or any later version
* accepted by the membership of KDE e.V. (or its successor approved
* by the membership of KDE e.V.), which shall act as a proxy
* defined in Section 14 of version 3 of the license.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "breezedecoration.h"

#include "breezesettingsprovider.h"
#include "config/breezeconfigwidget.h"

#include "breezebutton.h"

#include "breezeboxshadowrenderer.h"

#include <KDecoration3/DecorationButtonGroup>
#include <KDecoration3/DecorationShadow>
#include <KDecoration3/ScaleHelpers>

#include <KColorUtils>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QTimer>

#include <cmath>

K_PLUGIN_FACTORY_WITH_JSON(BreezeDecoFactory, "breezeenhanced.json", registerPlugin<Breeze::Decoration>(); registerPlugin<Breeze::Button>();)

namespace
{
    struct ShadowParams {
        ShadowParams()
            : offset(QPoint(0, 0))
            , radius(0)
            , opacity(0) {}

        ShadowParams(const QPoint &offset, int radius, qreal opacity)
            : offset(offset)
            , radius(radius)
            , opacity(opacity) {}

        QPoint offset;
        int radius;
        qreal opacity;
    };

    struct CompositeShadowParams {
        CompositeShadowParams() = default;

        CompositeShadowParams(
                const QPoint &offset,
                const ShadowParams &shadow1,
                const ShadowParams &shadow2)
            : offset(offset)
            , shadow1(shadow1)
            , shadow2(shadow2) {}

        bool isNone() const {
            return qMax(shadow1.radius, shadow2.radius) == 0;
        }

        QPoint offset;
        ShadowParams shadow1;
        ShadowParams shadow2;
    };

    const CompositeShadowParams s_shadowParams[] = {
        // None
        CompositeShadowParams(),
        // Small
        CompositeShadowParams(
            QPoint(0, 4),
            ShadowParams(QPoint(0, 0), 16, 1),
            ShadowParams(QPoint(0, -2), 8, 0.4)),
        // Medium
        CompositeShadowParams(
            QPoint(0, 8),
            ShadowParams(QPoint(0, 0), 32, 0.9),
            ShadowParams(QPoint(0, -4), 16, 0.3)),
        // Large
        CompositeShadowParams(
            QPoint(0, 12),
            ShadowParams(QPoint(0, 0), 48, 0.8),
            ShadowParams(QPoint(0, -6), 24, 0.2)),
        // Very large
        CompositeShadowParams(
            QPoint(0, 16),
            ShadowParams(QPoint(0, 0), 64, 0.7),
            ShadowParams(QPoint(0, -8), 32, 0.1)),
    };

    inline CompositeShadowParams lookupShadowParams(int size)
    {
        switch (size) {
        case Breeze::InternalSettings::ShadowNone:
            return s_shadowParams[0];
        case Breeze::InternalSettings::ShadowSmall:
            return s_shadowParams[1];
        case Breeze::InternalSettings::ShadowMedium:
            return s_shadowParams[2];
        case Breeze::InternalSettings::ShadowLarge:
            return s_shadowParams[3];
        case Breeze::InternalSettings::ShadowVeryLarge:
            return s_shadowParams[4];
        default:
            // Fallback to the Large size.
            return s_shadowParams[3];
        }
    }
}

namespace Breeze
{

    using KDecoration3::ColorRole;
    using KDecoration3::ColorGroup;

    //________________________________________________________________
    static int g_sDecoCount = 0;
    static int g_shadowSizeEnum = InternalSettings::ShadowLarge;
    static int g_shadowStrength = 255;
    static QColor g_shadowColor = Qt::black;
    static std::shared_ptr<KDecoration3::DecorationShadow> g_sShadow;
    static std::shared_ptr<KDecoration3::DecorationShadow> g_sShadowInactive;

    //________________________________________________________________
    Decoration::Decoration(QObject *parent, const QVariantList &args)
        : KDecoration3::Decoration(parent, args)
    {
        g_sDecoCount++;
    }

    //________________________________________________________________
    Decoration::~Decoration()
    {
        g_sDecoCount--;
        if (g_sDecoCount == 0) {
            // last deco destroyed, clean up shadow
            g_sShadow.reset();
            g_sShadowInactive.reset();
        }
    }

    //________________________________________________________________
    QColor Decoration::titleBarColor() const
    {

        const auto w = window();
        if (hideTitleBar())
            return w->color(ColorGroup::Inactive, ColorRole::TitleBar);
        return w->color(w->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::TitleBar);

    }

    //________________________________________________________________
    QColor Decoration::fontColor() const
    {

        const auto w = window();
        return  w->color(w->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::Foreground);

    }

    //________________________________________________________________
    bool Decoration::init()
    {
        const auto w = window();

        reconfigure();
        updateTitleBar();
        auto s = settings();
        connect(s.get(), &KDecoration3::DecorationSettings::borderSizeChanged, this, &Decoration::recalculateBorders);

        // a change in font might cause the borders to change
        recalculateBorders();
        resetBlurRegion();
        connect(s.get(), &KDecoration3::DecorationSettings::fontChanged, this, &Decoration::recalculateBorders);
        connect(s.get(), &KDecoration3::DecorationSettings::spacingChanged, this, &Decoration::recalculateBorders);

        // buttons
        connect(s.get(), &KDecoration3::DecorationSettings::spacingChanged, this, &Decoration::updateButtonsGeometryDelayed);
        connect(s.get(), &KDecoration3::DecorationSettings::decorationButtonsLeftChanged, this, &Decoration::updateButtonsGeometryDelayed);
        connect(s.get(), &KDecoration3::DecorationSettings::decorationButtonsRightChanged, this, &Decoration::updateButtonsGeometryDelayed);

        // full reconfiguration
        connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, this, &Decoration::reconfigure);
        connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, SettingsProvider::self(), &SettingsProvider::reconfigure, Qt::UniqueConnection);
        connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, this, &Decoration::updateButtonsGeometryDelayed);

        connect(w, &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::recalculateBorders);
        connect(w, &KDecoration3::DecoratedWindow::maximizedHorizontallyChanged, this, &Decoration::recalculateBorders);
        connect(w, &KDecoration3::DecoratedWindow::maximizedVerticallyChanged, this, &Decoration::recalculateBorders);
        connect(w, &KDecoration3::DecoratedWindow::shadedChanged, this, &Decoration::recalculateBorders);

        connect(w, &KDecoration3::DecoratedWindow::captionChanged, this, [this]() {
            // update the caption area
            update(titleBar());
        });

        connect(w, &KDecoration3::DecoratedWindow::activeChanged, this, &Decoration::updateActiveState);
        connect(this, &KDecoration3::Decoration::bordersChanged, this, &Decoration::updateTitleBar);
        connect(w, &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::updateTitleBar);
        connect(w, &KDecoration3::DecoratedWindow::widthChanged, this, &Decoration::updateTitleBar);
        connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::updateTitleBar);
        //connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::setOpaque);

        connect(w, &KDecoration3::DecoratedWindow::widthChanged, this, &Decoration::updateButtonsGeometry);
        connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::updateButtonsGeometry);
        connect(w, &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::updateButtonsGeometry);
        connect(w, &KDecoration3::DecoratedWindow::shadedChanged, this, &Decoration::updateButtonsGeometry);

        connect(s.get(), &KDecoration3::DecorationSettings::borderSizeChanged, this, &Decoration::resetBlurRegion);
        connect(s.get(), &KDecoration3::DecorationSettings::spacingChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::maximizedHorizontallyChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::maximizedVerticallyChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::shadedChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::widthChanged, this, &Decoration::resetBlurRegion);
        connect(w, &KDecoration3::DecoratedWindow::heightChanged, this, [this]() {
            if (!hasNoSideBorders()) resetBlurRegion();
        });

        connect(window(), &KDecoration3::DecoratedWindow::nextScaleChanged, this, &Decoration::updateScale);

        createButtons();
        updateShadow();

        return true;
    }

    //________________________________________________________________
    void Decoration::updateTitleBar()
    {
        auto s = settings();
        const auto w = window();
        const bool maximized = isMaximized();
        const qreal width =  maximized ? w->width() : w->width() - 2*s->largeSpacing()*Metrics::TitleBar_SideMargin;
        const qreal height = (maximized || isTopEdge()) ? borderTop() : borderTop() - s->smallSpacing()*Metrics::TitleBar_TopMargin;
        const qreal x = maximized ? 0 : s->largeSpacing()*Metrics::TitleBar_SideMargin;
        const qreal y = (maximized || isTopEdge()) ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
        setTitleBar(QRectF(x, y, width, height));
    }

    //________________________________________________________________
    void Decoration::updateActiveState()
    {
        updateShadow(); // active and inactive shadows are different
        update();
    }

    //________________________________________________________________
    qreal Decoration::borderSize(bool bottom, qreal scale) const
    {
        const qreal pixelSize = KDecoration3::pixelSize(scale);
        const qreal baseSize = std::max<qreal>(pixelSize, KDecoration3::snapToPixelGrid(settings()->smallSpacing(), scale));
        if (m_internalSettings && (m_internalSettings->mask() & BorderSize))
        {
            switch (m_internalSettings->borderSize()) {
                case InternalSettings::BorderNone: return 0;
                case InternalSettings::BorderNoSides:
                    if (bottom)
                        return KDecoration3::snapToPixelGrid(std::max(4.0, baseSize), scale);
                    else
                        return 0;
                default:
                case InternalSettings::BorderTiny:
                    if (bottom)
                        return KDecoration3::snapToPixelGrid(std::max(4.0, baseSize), scale);
                    else
                        return baseSize;
                case InternalSettings::BorderNormal: return baseSize*2;
                case InternalSettings::BorderLarge: return baseSize*3;
                case InternalSettings::BorderVeryLarge: return baseSize*4;
                case InternalSettings::BorderHuge: return baseSize*5;
                case InternalSettings::BorderVeryHuge: return baseSize*6;
                case InternalSettings::BorderOversized: return baseSize*10;
            }

        } else {

            switch (settings()->borderSize()) {
                case KDecoration3::BorderSize::None: return 0;
                case KDecoration3::BorderSize::NoSides:
                    if (bottom)
                        return KDecoration3::snapToPixelGrid(std::max(4.0, baseSize), scale);
                    else
                        return 0;
                default:
                case KDecoration3::BorderSize::Tiny:
                    if (bottom)
                        return KDecoration3::snapToPixelGrid(std::max(4.0, baseSize), scale);
                    else
                        return baseSize;
                case KDecoration3::BorderSize::Normal: return baseSize*2;
                case KDecoration3::BorderSize::Large: return baseSize*3;
                case KDecoration3::BorderSize::VeryLarge: return baseSize*4;
                case KDecoration3::BorderSize::Huge: return baseSize*5;
                case KDecoration3::BorderSize::VeryHuge: return baseSize*6;
                case KDecoration3::BorderSize::Oversized: return baseSize*10;

            }

        }
    }

    //________________________________________________________________
    void Decoration::reconfigure()
    {

        m_internalSettings = SettingsProvider::self()->internalSettings(this);

        setScaledCornerRadius();

        // borders
        recalculateBorders();

        // blur region
        resetBlurRegion();

        // shadow
        updateShadow();

    }

    //________________________________________________________________
    QMarginsF Decoration::bordersFor(qreal scale) const
    {
        const auto w = window();
        auto s = settings();

        // left, right and bottom borders
        const qreal left = isLeftEdge() ? 0 : borderSize(false, scale);
        const qreal right = isRightEdge() ? 0 : borderSize(false, scale);
        const qreal bottom = (w->isShaded() || isBottomEdge()) ? 0 : borderSize(true, scale);

        qreal top = 0;
        if (hideTitleBar())
            top = bottom;
        else
        {
            QFont f; f.fromString(m_internalSettings->titleBarFont());
            QFontMetricsF fm(f);
            top += KDecoration3::snapToPixelGrid(std::max(fm.height(), static_cast<qreal>(buttonSize())), scale);

            // padding below
            // extra pixel is used for the active window outline (but not in the shaded state)
            const int baseSize = s->smallSpacing();
            top += KDecoration3::snapToPixelGrid(baseSize * Metrics::TitleBar_BottomMargin + (w->isShaded() ? 0 : 1), scale);

            // padding above
            top += KDecoration3::snapToPixelGrid(baseSize * Metrics::TitleBar_TopMargin, scale);
        }
        return QMarginsF(left, top, right, bottom);
    }

    //________________________________________________________________
    void Decoration::recalculateBorders()
    {
        const auto w = window();
        auto s = settings();

        setBorders(bordersFor(w->nextScale()));

        // extended sizes
        const qreal extSize = w->snapToPixelGrid(s->largeSpacing());
        qreal extSides = 0;
        qreal extBottom = 0;
        if (hasNoBorders())
        {
            if (!isMaximizedHorizontally())
                extSides = extSize;
            if (!isMaximizedVertically())
                extBottom = extSize;
        }
        else if (hasNoSideBorders() && !isMaximizedHorizontally())
        {
            extSides = extSize;
        }

        setResizeOnlyBorders(QMargins(extSides, 0, extSides, extBottom));
    }

    //________________________________________________________________
    void Decoration::resetBlurRegion()
    {
        // NOTE: "BlurEffect::decorationBlurRegion()" will consider the intersection of
        // the blur and decoration regions. Here we need to focus on corner rounding.

        if (titleBarAlpha() == 255 || !settings()->isAlphaChannelSupported())
        { // no blurring without translucency
            setBlurRegion(QRegion());
            return;
        }

        QRegion region;
        const auto w = window();
        QSizeF rSize(m_scaledCornerRadius, m_scaledCornerRadius);

        if (!w->isShaded() && !isMaximized() && !hasNoBorders())
        {
            // exclude the titlebar
            qreal topBorder = hideTitleBar() ? 0 : borderTop();
            QRectF rect(0, topBorder, size().width(), size().height() - topBorder);

            QRegion vert(QRectF(rect.topLeft() + QPointF(m_scaledCornerRadius, 0),
                                QSizeF(rect.width() - 2*m_scaledCornerRadius, rect.height())).toRect());
            QRegion topLeft, topRight, bottomLeft, bottomRight, horiz;
            if (hasBorders())
            {
                if (hideTitleBar())
                {
                    topLeft = QRegion(QRectF(rect.topLeft(), 2*rSize).toRect(),
                                      isLeftEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                    topRight = QRegion(QRectF(rect.topLeft() + QPointF(rect.width() - 2*m_scaledCornerRadius, 0),
                                              2*rSize).toRect(),
                                       isRightEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                    horiz = QRegion(QRectF(rect.topLeft() + QPointF(0, m_scaledCornerRadius),
                                           QSizeF(rect.width(), rect.height() - 2*m_scaledCornerRadius)).toRect());
                }
                else
                { // "horiz" is at the top because the titlebar is excluded
                    horiz = QRegion(QRectF(rect.topLeft(),
                                           QSizeF(rect.width(), rect.height() - m_scaledCornerRadius)).toRect());
                }
                bottomLeft = QRegion(QRectF(rect.topLeft() + QPointF(0, rect.height() - 2*m_scaledCornerRadius),
                                            2*rSize).toRect(),
                                     isLeftEdge() && isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                bottomRight = QRegion(QRectF(rect.topLeft() + QPointF(rect.width() - 2*m_scaledCornerRadius,
                                                                    rect.height() - 2*m_scaledCornerRadius),
                                            2*rSize).toRect(),
                                      isRightEdge() && isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            }
            else // no side border
            {
                horiz = QRegion(QRectF(rect.topLeft(),
                                       QSizeF(rect.width(), rect.height() - m_scaledCornerRadius)).toRect());
                bottomLeft = QRegion(QRectF(rect.topLeft() + QPointF(0, rect.height() - 2*m_scaledCornerRadius),
                                            2*rSize).toRect(),
                                     isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                bottomRight = QRegion(QRectF(rect.topLeft() + QPointF(rect.width() - 2*m_scaledCornerRadius,
                                                                      rect.height() - 2*m_scaledCornerRadius),
                                             2*rSize).toRect(),
                                      isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            }

            region = topLeft
                     .united(topRight)
                     .united(bottomLeft)
                     .united(bottomRight)
                     .united(horiz)
                     .united(vert);

            if (hideTitleBar())
            {
                setBlurRegion(region);
                return;
            }
        }

        const QRectF titleRect(QPointF(0, 0), QSizeF(size().width(), borderTop()));

        // add the titlebar
        if (m_scaledCornerRadius == 0
            || isMaximized()) // maximized + no border when maximized
        {
            region |= QRegion(titleRect.toRect());
        }
        else if (w->isShaded())
        {
            QRegion topLeft(QRectF(titleRect.topLeft(), 2*rSize).toRect(), QRegion::Ellipse);
            QRegion topRight(QRectF(titleRect.topLeft() + QPointF(titleRect.width() - 2*m_scaledCornerRadius, 0),
                                    2*rSize).toRect(),
                             QRegion::Ellipse);
            QRegion bottomLeft(QRectF(titleRect.topLeft() + QPointF(0, titleRect.height() - 2*m_scaledCornerRadius),
                                      2*rSize).toRect(),
                               QRegion::Ellipse);
            QRegion bottomRight(QRectF(titleRect.topLeft() + QPointF(titleRect.width() - 2*m_scaledCornerRadius,
                                                                     titleRect.height() - 2*m_scaledCornerRadius),
                                       2*rSize).toRect(),
                                QRegion::Ellipse);
            region = topLeft
                     .united(topRight)
                     .united(bottomLeft)
                     .united(bottomRight)
                     // vertical
                     .united(QRectF(titleRect.topLeft() + QPointF(m_scaledCornerRadius, 0),
                                    QSizeF(titleRect.width() - 2*m_scaledCornerRadius, titleRect.height())).toRect())
                     // horizontal
                     .united(QRectF(titleRect.topLeft() + QPointF(0, m_scaledCornerRadius),
                                    QSizeF(titleRect.width(), titleRect.height() - 2*m_scaledCornerRadius)).toRect());
        }
        else
        {
            QRegion topLeft(QRectF(titleRect.topLeft(), 2*rSize).toRect(),
                            isLeftEdge() || isTopEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            QRegion topRight(QRectF(titleRect.topLeft() + QPointF(titleRect.width() - 2*m_scaledCornerRadius, 0),
                                    2*rSize).toRect(),
                             isRightEdge() || isTopEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            region |= topLeft
                      .united(topRight)
                      // vertical
                      .united(QRectF(titleRect.topLeft() + QPointF(m_scaledCornerRadius, 0),
                                     QSizeF(titleRect.width() - 2*m_scaledCornerRadius, titleRect.height())).toRect())
                      // horizontal
                      .united(QRectF(titleRect.topLeft() + QPointF(0, m_scaledCornerRadius),
                                     QSizeF(titleRect.width(), titleRect.height() - m_scaledCornerRadius)).toRect());
        }

        setBlurRegion(region);
    }

    //________________________________________________________________
    void Decoration::createButtons()
    {
        m_leftButtons = new KDecoration3::DecorationButtonGroup(KDecoration3::DecorationButtonGroup::Position::Left, this, &Button::create);
        m_rightButtons = new KDecoration3::DecorationButtonGroup(KDecoration3::DecorationButtonGroup::Position::Right, this, &Button::create);
        updateButtonsGeometry();
    }

    //________________________________________________________________
    void Decoration::updateButtonsGeometryDelayed()
    {
        QTimer::singleShot(0, this, &Decoration::updateButtonsGeometry);
    }

    //________________________________________________________________
    void Decoration::updateButtonsGeometry()
    {
        const auto s = settings();

        // adjust button position
        const auto buttonList = m_leftButtons->buttons() + m_rightButtons->buttons();
        for (KDecoration3::DecorationButton *button : buttonList)
        {
            auto btn = static_cast<Button *>(button);

            const int verticalOffset = (isTopEdge() ? s->smallSpacing() * Metrics::TitleBar_TopMargin : 0);

            const QSizeF preferredSize = btn->preferredSize();
            const int bHeight = preferredSize.height() + verticalOffset;
            const int bWidth = preferredSize.width();

            btn->setGeometry(QRectF(QPoint(0, 0), QSizeF(bWidth, bHeight)));
            btn->setPadding(QMargins(0, verticalOffset, 0, 0));
        }

        // left buttons
        if (!m_leftButtons->buttons().isEmpty())
        {
            // spacing (use our own spacing instead of s->smallSpacing()*Metrics::TitleBar_ButtonSpacing)
            m_leftButtons->setSpacing(m_internalSettings->buttonSpacing());

            // padding
            const int vPadding = isTopEdge() ? 0 : s->smallSpacing() * Metrics::TitleBar_TopMargin;
            const int hPadding = s->smallSpacing() * Metrics::TitleBar_SideMargin;
            if (isLeftEdge())
            {
                // add offsets on the side buttons, to preserve padding, but satisfy Fitts law
                auto button = static_cast<Button *>(m_leftButtons->buttons().front());

                QRectF geometry = button->geometry();
                geometry.adjust(-hPadding, 0, 0, 0);
                button->setGeometry(geometry);
                button->setLeftPadding(hPadding);

                m_leftButtons->setPos(QPointF(0, vPadding));
            }
            else
                m_leftButtons->setPos(QPointF(hPadding + borderLeft(), vPadding));
        }

        // right buttons
        if (!m_rightButtons->buttons().isEmpty())
        {
            // spacing (use our own spacing instead of s->smallSpacing()*Metrics::TitleBar_ButtonSpacing)
            m_rightButtons->setSpacing(m_internalSettings->buttonSpacing());

            // padding
            const int vPadding = isTopEdge() ? 0 : s->smallSpacing() * Metrics::TitleBar_TopMargin;
            const int hPadding = s->smallSpacing() * Metrics::TitleBar_SideMargin;
            if (isRightEdge())
            {
                auto button = static_cast<Button *>(m_rightButtons->buttons().back());

                QRectF geometry = button->geometry();
                geometry.adjust(0, 0, hPadding, 0);
                button->setGeometry(geometry);
                button->setRightPadding(hPadding);

                m_rightButtons->setPos(QPointF(size().width() - m_rightButtons->geometry().width(), vPadding));
            }
            else
                m_rightButtons->setPos(QPointF(size().width() - m_rightButtons->geometry().width() - hPadding - borderRight(), vPadding));
        }

        update();

    }

    //________________________________________________________________
    void Decoration::paint(QPainter *painter, const QRectF &repaintRegion)
    {
        // TODO: optimize based on repaintRegion
        const auto w = window();
        auto s = settings();

        // paint background
        if (!w->isShaded())
        {
            painter->fillRect(rect(), Qt::transparent);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);

            QColor winCol = this->titleBarColor();
            winCol.setAlpha(titleBarAlpha());
            painter->setBrush(winCol);

            // clip away the top part
            if (!hideTitleBar())
                painter->setClipRect(QRectF(0, borderTop(), size().width(), size().height() - borderTop()), Qt::IntersectClip);

            if (s->isAlphaChannelSupported())
                painter->drawRoundedRect(rect(), m_scaledCornerRadius, m_scaledCornerRadius);
            else
                painter->drawRect(rect());

            painter->restore();
        }

        if (!hideTitleBar())
            paintTitleBar(painter, repaintRegion);

        if (hasBorders() && !s->isAlphaChannelSupported())
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(w->isActive() ? w->color(ColorGroup::Active, ColorRole::TitleBar)
                                          : w->color(ColorGroup::Inactive, ColorRole::Foreground));

            painter->drawRect(rect().adjusted(0, 0, -1, -1));
            painter->restore();
        }

    }

    //________________________________________________________________
    void Decoration::paintTitleBar(QPainter *painter, const QRectF &repaintRegion)
    {
        const auto w = window();
        const QRectF titleRect(QPointF(0, 0), QSizeF(size().width(), borderTop()));

        if (!titleRect.intersects(repaintRegion)) return;

        painter->save();
        painter->setPen(Qt::NoPen);

        // render a linear gradient on title area and draw a light border at the top
        if (m_internalSettings->drawBackgroundGradient() && !flatTitleBar())
        {
            QColor titleBarColor(this->titleBarColor());
            titleBarColor.setAlpha(titleBarAlpha());

            QLinearGradient gradient(0, 0, 0, titleRect.height());
            QColor lightCol(titleBarColor.lighter(130 + m_internalSettings->backgroundGradientIntensity()));
            gradient.setColorAt(0.0, lightCol);
            gradient.setColorAt(0.99 / titleRect.height(), lightCol);
            gradient.setColorAt(1.0 / titleRect.height(),
                                titleBarColor.lighter(100 + m_internalSettings->backgroundGradientIntensity()));
            gradient.setColorAt(1.0, titleBarColor);

            painter->setBrush(gradient);
        }
        else
        {
            QColor titleBarColor(this->titleBarColor());
            titleBarColor.setAlpha(titleBarAlpha());

            QLinearGradient gradient(0, 0, 0, titleRect.height());
            QColor lightCol(titleBarColor.lighter(130));
            gradient.setColorAt(0.0, lightCol);
            gradient.setColorAt(0.99 / titleRect.height(), lightCol);
            gradient.setColorAt(1.0 / titleRect.height(), titleBarColor);
            gradient.setColorAt(1.0, titleBarColor);

            painter->setBrush(gradient);
        }

        auto s = settings();
        if (isMaximized() || !s->isAlphaChannelSupported())
        {
            painter->drawRect(titleRect);
        }
        else if (w->isShaded())
        {
            painter->drawRoundedRect(titleRect, m_scaledCornerRadius, m_scaledCornerRadius);
        }
        else
        {
            painter->setClipRect(titleRect, Qt::IntersectClip);
            // the rect is made a little bit larger to be able to clip away the rounded corners at the bottom and sides
            painter->drawRoundedRect(titleRect.adjusted(isLeftEdge() ? -m_scaledCornerRadius :0,
                                                        isTopEdge() ? -m_scaledCornerRadius :0,
                                                        isRightEdge() ? m_scaledCornerRadius :0,
                                                        m_scaledCornerRadius),
                                     m_scaledCornerRadius, m_scaledCornerRadius);

        }

        painter->restore();

        // draw caption
        QFont f; f.fromString(m_internalSettings->titleBarFont());
        // KDE needs this FIXME: Why?
        QFontDatabase fd; f.setStyleName(fd.styleString(f));
        painter->setFont(f);
        painter->setPen(fontColor());
        const auto cR = captionRect();
        const QString caption = painter->fontMetrics().elidedText(w->caption(), Qt::ElideMiddle, cR.first.width());
        painter->drawText(cR.first, cR.second | Qt::TextSingleLine, caption);

        // draw all buttons
        m_leftButtons->paint(painter, repaintRegion);
        m_rightButtons->paint(painter, repaintRegion);
    }

    //________________________________________________________________
    int Decoration::buttonSize() const
    {
        const int baseSize = settings()->gridUnit();
        switch (m_internalSettings->buttonSize())
        {
            case InternalSettings::ButtonTiny: return baseSize;
            case InternalSettings::ButtonSmall: return baseSize*1.5;
            default:
            case InternalSettings::ButtonDefault: return baseSize*2;
            case InternalSettings::ButtonLarge: return baseSize*2.5;
            case InternalSettings::ButtonVeryLarge: return baseSize*3.5;
        }

    }

    //________________________________________________________________
    qreal Decoration::captionHeight() const
    {
        const auto w = window();
        return hideTitleBar() ? borderTop()
                              : borderTop()
                                - settings()->smallSpacing() * (Metrics::TitleBar_BottomMargin + Metrics::TitleBar_TopMargin)
                                - (w->isShaded() ? 0 : 1); // see recalculateBorders()
    }

    //________________________________________________________________
    QPair<QRectF, Qt::Alignment> Decoration::captionRect() const
    {
        if (hideTitleBar()) return qMakePair(QRectF(), Qt::AlignCenter);
        else {

            const qreal extraTitleMargin = m_internalSettings->extraTitleMargin();
            const auto w = window();
            const qreal leftOffset = m_leftButtons->buttons().isEmpty() ?
                Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin :
                m_leftButtons->geometry().x() + m_leftButtons->geometry().width() + Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin;

            const qreal rightOffset = m_rightButtons->buttons().isEmpty() ?
                Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin:
                size().width() - m_rightButtons->geometry().x() + Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin;

            const qreal yOffset = settings()->smallSpacing()*Metrics::TitleBar_TopMargin;
            const QRectF maxRect(leftOffset, yOffset, size().width() - leftOffset - rightOffset, captionHeight());

            switch (m_internalSettings->titleAlignment())
            {
                case InternalSettings::AlignLeft:
                return qMakePair(maxRect, Qt::AlignVCenter|Qt::AlignLeft);

                case InternalSettings::AlignRight:
                return qMakePair(maxRect, Qt::AlignVCenter|Qt::AlignRight);

                case InternalSettings::AlignCenter:
                return qMakePair(maxRect, Qt::AlignCenter);

                default:
                case InternalSettings::AlignCenterFullWidth:
                {

                    // full caption rect
                    const QRectF fullRect = QRectF(0, yOffset, size().width(), captionHeight());
                    QFont f; f.fromString(m_internalSettings->titleBarFont());
                    QFontMetricsF fm(f);
                    QRectF boundingRect(fm.boundingRect(w->caption()));

                    // text bounding rect
                    boundingRect.setTop(yOffset);
                    boundingRect.setHeight(captionHeight());
                    boundingRect.moveLeft((size().width() - boundingRect.width())/2);

                    if (boundingRect.left() < leftOffset)
                        return qMakePair(maxRect, Qt::AlignVCenter|Qt::AlignLeft);
                    else if (boundingRect.right() > size().width() - rightOffset)
                        return qMakePair(maxRect, Qt::AlignVCenter|Qt::AlignRight);
                    else
                        return qMakePair(fullRect, Qt::AlignCenter);

                }

            }

        }

    }

    //________________________________________________________________
    void Decoration::updateShadow()
    {
        const auto w = window();
        auto &shadow = w->isActive() ? g_sShadow : g_sShadowInactive;

        if (!shadow
            || g_shadowSizeEnum != m_internalSettings->shadowSize()
            || g_shadowStrength != m_internalSettings->shadowStrength()
            || g_shadowColor != m_internalSettings->shadowColor())
        {
            g_shadowSizeEnum = m_internalSettings->shadowSize();
            g_shadowStrength = m_internalSettings->shadowStrength();
            g_shadowColor = m_internalSettings->shadowColor();

            const CompositeShadowParams params = lookupShadowParams(g_shadowSizeEnum);
            if (params.isNone()) {
                g_sShadow.reset();
                g_sShadowInactive.reset();
                setShadow(shadow);
                return;
            }

            auto withOpacity = [](const QColor &color, qreal opacity) -> QColor {
                QColor c(color);
                c.setAlphaF(opacity);
                return c;
            };

            const QSize boxSize = BoxShadowRenderer::calculateMinimumBoxSize(params.shadow1.radius)
                .expandedTo(BoxShadowRenderer::calculateMinimumBoxSize(params.shadow2.radius));

            BoxShadowRenderer shadowRenderer;
            shadowRenderer.setBorderRadius(m_scaledCornerRadius + 0.5);
            shadowRenderer.setBoxSize(boxSize);

            const qreal strength = static_cast<qreal>(g_shadowStrength) / 255.0 * (w->isActive() ? 1.0 : 0.5);
            shadowRenderer.addShadow(params.shadow1.offset, params.shadow1.radius,
                withOpacity(g_shadowColor, params.shadow1.opacity * strength));
            shadowRenderer.addShadow(params.shadow2.offset, params.shadow2.radius,
                withOpacity(g_shadowColor, params.shadow2.opacity * strength));

            QImage shadowTexture = shadowRenderer.render();

            QPainter painter(&shadowTexture);
            painter.setRenderHint(QPainter::Antialiasing);

            const QRectF outerRect = shadowTexture.rect();

            QRectF boxRect(QPointF(0, 0), boxSize);
            boxRect.moveCenter(outerRect.center());

            // Mask out inner rect.
            const QMarginsF padding = QMarginsF(
                boxRect.left() - outerRect.left() - Metrics::Shadow_Overlap - params.offset.x(),
                boxRect.top() - outerRect.top() - Metrics::Shadow_Overlap - params.offset.y(),
                outerRect.right() - boxRect.right() - Metrics::Shadow_Overlap + params.offset.x(),
                outerRect.bottom() - boxRect.bottom() - Metrics::Shadow_Overlap + params.offset.y());
            const QRectF innerRect = outerRect - padding;
            // Push the shadow slightly under the window, which helps avoiding glitches with fractional scaling
            // TODO fix this more properly
            //innerRect.adjust(2, 2, -2, -2);

            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::black);
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            painter.drawRoundedRect(
                innerRect,
                m_scaledCornerRadius + 0.5,
                m_scaledCornerRadius + 0.5);

            // Draw outline.
            painter.setPen(withOpacity(g_shadowColor, 0.2 * strength));
            painter.setBrush(Qt::NoBrush);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawRoundedRect(
                innerRect,
                m_scaledCornerRadius - 0.5,
                m_scaledCornerRadius - 0.5);

            painter.end();

            shadow = std::make_shared<KDecoration3::DecorationShadow>();
            shadow->setPadding(padding);
            shadow->setInnerShadowRect(QRectF(outerRect.center(), QSizeF(1, 1)));
            shadow->setShadow(shadowTexture);
        }

        setShadow(shadow);
    }

    //________________________________________________________________
    void Decoration::setScaledCornerRadius()
    {
        // On X11, the smallSpacing value is used for scaling.
        // On Wayland, this value has constant factor of 2.
        // Removing it will break radius scaling on X11.
        m_scaledCornerRadius = window()->snapToPixelGrid(Metrics::Frame_FrameRadius * settings()->smallSpacing());
    }

    //________________________________________________________________
    void Decoration::updateScale()
    {
        setScaledCornerRadius();
        recalculateBorders();
    }

} // namespace


#include "breezedecoration.moc"
