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

#include <KDecoration2/DecorationButtonGroup>
#include <KDecoration2/DecorationShadow>

#include <KColorUtils>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QPainter>
#include <QTextStream>
#include <QTimer>

#include <cmath>

K_PLUGIN_FACTORY_WITH_JSON(
    BreezeDecoFactory,
    "breeze.json",
    registerPlugin<Breeze::Decoration>();
    registerPlugin<Breeze::Button>();
    registerPlugin<Breeze::ConfigWidget>();
)

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

    using KDecoration2::ColorRole;
    using KDecoration2::ColorGroup;

    //________________________________________________________________
    static int g_sDecoCount = 0;
    static int g_shadowSizeEnum = InternalSettings::ShadowLarge;
    static int g_shadowStrength = 255;
    static QColor g_shadowColor = Qt::black;
    static QSharedPointer<KDecoration2::DecorationShadow> g_sShadow;
    static QSharedPointer<KDecoration2::DecorationShadow> g_sShadowInactive;

    //________________________________________________________________
    Decoration::Decoration(QObject *parent, const QVariantList &args)
        : KDecoration2::Decoration(parent, args)
    {
        g_sDecoCount++;
    }

    //________________________________________________________________
    Decoration::~Decoration()
    {
        g_sDecoCount--;
        if (g_sDecoCount == 0) {
            // last deco destroyed, clean up shadow
            g_sShadow.clear();
            g_sShadowInactive.clear();
        }
    }

    //________________________________________________________________
    QColor Decoration::titleBarColor() const
    {

        const auto c = client().toStrongRef();
        if( hideTitleBar() ) return c->color( ColorGroup::Inactive, ColorRole::TitleBar );
        return c->color( c->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::TitleBar );

    }

    //________________________________________________________________
    QColor Decoration::fontColor() const
    {

        const auto c = client().toStrongRef();
        return  c->color( c->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::Foreground );

    }

    //________________________________________________________________
    void Decoration::init()
    {
        const auto c = client().toStrongRef();

        reconfigure();
        updateTitleBar();
        auto s = settings();
        connect(s.data(), &KDecoration2::DecorationSettings::borderSizeChanged, this, &Decoration::recalculateBorders);

        // a change in font might cause the borders to change
        recalculateBorders();
        resetBlurRegion();
        connect(s.data(), &KDecoration2::DecorationSettings::spacingChanged, this, &Decoration::recalculateBorders);

        // buttons
        connect(s.data(), &KDecoration2::DecorationSettings::spacingChanged, this, &Decoration::updateButtonsGeometryDelayed);
        connect(s.data(), &KDecoration2::DecorationSettings::decorationButtonsLeftChanged, this, &Decoration::updateButtonsGeometryDelayed);
        connect(s.data(), &KDecoration2::DecorationSettings::decorationButtonsRightChanged, this, &Decoration::updateButtonsGeometryDelayed);

        // full reconfiguration
        connect(s.data(), &KDecoration2::DecorationSettings::reconfigured, this, &Decoration::reconfigure);
        connect(s.data(), &KDecoration2::DecorationSettings::reconfigured, SettingsProvider::self(), &SettingsProvider::reconfigure, Qt::UniqueConnection );
        connect(s.data(), &KDecoration2::DecorationSettings::reconfigured, this, &Decoration::updateButtonsGeometryDelayed);

        connect(c.data(), &KDecoration2::DecoratedClient::adjacentScreenEdgesChanged, this, &Decoration::recalculateBorders);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedHorizontallyChanged, this, &Decoration::recalculateBorders);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedVerticallyChanged, this, &Decoration::recalculateBorders);
        connect(c.data(), &KDecoration2::DecoratedClient::shadedChanged, this, &Decoration::recalculateBorders);

        connect(c.data(), &KDecoration2::DecoratedClient::captionChanged, this,
            [this]()
            {
                // update the caption area
                update(titleBar());
            }
        );

        connect(c.data(), &KDecoration2::DecoratedClient::activeChanged, this, &Decoration::updateActiveState);
        connect(c.data(), &KDecoration2::DecoratedClient::widthChanged, this, &Decoration::updateTitleBar);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::updateTitleBar);
        //connect(c, &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::setOpaque);

        connect(c.data(), &KDecoration2::DecoratedClient::widthChanged, this, &Decoration::updateButtonsGeometry);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::updateButtonsGeometry);
        connect(c.data(), &KDecoration2::DecoratedClient::adjacentScreenEdgesChanged, this, &Decoration::updateButtonsGeometry);
        connect(c.data(), &KDecoration2::DecoratedClient::shadedChanged, this, &Decoration::updateButtonsGeometry);

        connect(s.data(), &KDecoration2::DecorationSettings::borderSizeChanged, this, &Decoration::resetBlurRegion);
        connect(s.data(), &KDecoration2::DecorationSettings::spacingChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::adjacentScreenEdgesChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedHorizontallyChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedVerticallyChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::shadedChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::widthChanged, this, &Decoration::resetBlurRegion);
        connect(c.data(), &KDecoration2::DecoratedClient::heightChanged, this, [this]() {
            if (!hasNoSideBorders()) resetBlurRegion();
        });

        createButtons();
        updateShadow();
    }

    //________________________________________________________________
    void Decoration::updateTitleBar()
    {
        auto s = settings();
        const auto c = client().toStrongRef();
        const bool maximized = isMaximized();
        const int width =  maximized ? c->width() : c->width() - 2*s->largeSpacing()*Metrics::TitleBar_SideMargin;
        const int height = maximized ? borderTop() : borderTop() - s->smallSpacing()*Metrics::TitleBar_TopMargin;
        const int x = maximized ? 0 : s->largeSpacing()*Metrics::TitleBar_SideMargin;
        const int y = maximized ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
        setTitleBar(QRect(x, y, width, height));
    }

    //________________________________________________________________
    void Decoration::updateActiveState()
    {
        updateShadow(); // active and inactive shadows are different
        update();
    }

    //________________________________________________________________
    int Decoration::borderSize(bool bottom) const
    {
        const int baseSize = settings()->smallSpacing();
        if( m_internalSettings && (m_internalSettings->mask() & BorderSize ) )
        {
            switch (m_internalSettings->borderSize()) {
                case InternalSettings::BorderNone: return 0;
                case InternalSettings::BorderNoSides: return bottom ? qMax(4, baseSize) : 0;
                default:
                case InternalSettings::BorderTiny: return bottom ? qMax(4, baseSize) : baseSize;
                case InternalSettings::BorderNormal: return baseSize*2;
                case InternalSettings::BorderLarge: return baseSize*3;
                case InternalSettings::BorderVeryLarge: return baseSize*4;
                case InternalSettings::BorderHuge: return baseSize*5;
                case InternalSettings::BorderVeryHuge: return baseSize*6;
                case InternalSettings::BorderOversized: return baseSize*10;
            }

        } else {

            switch (settings()->borderSize()) {
                case KDecoration2::BorderSize::None: return 0;
                case KDecoration2::BorderSize::NoSides: return bottom ? qMax(4, baseSize) : 0;
                default:
                case KDecoration2::BorderSize::Tiny: return bottom ? qMax(4, baseSize) : baseSize;
                case KDecoration2::BorderSize::Normal: return baseSize*2;
                case KDecoration2::BorderSize::Large: return baseSize*3;
                case KDecoration2::BorderSize::VeryLarge: return baseSize*4;
                case KDecoration2::BorderSize::Huge: return baseSize*5;
                case KDecoration2::BorderSize::VeryHuge: return baseSize*6;
                case KDecoration2::BorderSize::Oversized: return baseSize*10;

            }

        }
    }

    //________________________________________________________________
    void Decoration::reconfigure()
    {

        m_internalSettings = SettingsProvider::self()->internalSettings( this );

        setScaledCornerRadius();

        // borders
        recalculateBorders();

        // blur region
        resetBlurRegion();

        // shadow
        updateShadow();

    }

    //________________________________________________________________
    void Decoration::recalculateBorders()
    {
        const auto c = client().toStrongRef();
        auto s = settings();

        // left, right and bottom borders
        const int left   = isLeftEdge() ? 0 : borderSize();
        const int right  = isRightEdge() ? 0 : borderSize();
        const int bottom = (c->isShaded() || isBottomEdge()) ? 0 : borderSize(true);

        int top = 0;
        if (hideTitleBar())
            top = bottom;
        else
        {
            QFont f; f.fromString(m_internalSettings->titleBarFont());
            QFontMetrics fm(f);
            top += qMax(fm.height(), buttonHeight());

            // padding below
            // extra pixel is used for the active window outline (but not in the shaded state)
            const int baseSize = s->smallSpacing();
            top += baseSize*Metrics::TitleBar_BottomMargin + (c->isShaded() ? 0 : 1);

            // padding above
            top += baseSize*Metrics::TitleBar_TopMargin;
        }

        setBorders(QMargins(left, top, right, bottom));

        // extended sizes
        const int extSize = s->largeSpacing();
        int extSides = 0;
        int extBottom = 0;
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
        const auto c = client().toStrongRef();
        QSize rSize(m_scaledCornerRadius, m_scaledCornerRadius);

        if (!c->isShaded() && !isMaximized() && !hasNoBorders())
        {
            // exclude the titlebar
            int topBorder = hideTitleBar() ? 0 : borderTop();
            QRect rect(0, topBorder, size().width(), size().height() - topBorder);

            QRegion vert(QRect(rect.topLeft() + QPoint(m_scaledCornerRadius, 0),
                               QSize(rect.width() - 2*m_scaledCornerRadius, rect.height())));
            QRegion topLeft, topRight, bottomLeft, bottomRight, horiz;
            if (hasBorders())
            {
                if (hideTitleBar())
                {
                    topLeft = QRegion(QRect(rect.topLeft(), 2*rSize),
                                      isLeftEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                    topRight = QRegion(QRect(rect.topLeft() + QPoint(rect.width() - 2*m_scaledCornerRadius, 0),
                                             2*rSize),
                                       isRightEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                    horiz = QRegion(QRect(rect.topLeft() + QPoint(0, m_scaledCornerRadius),
                                          QSize(rect.width(), rect.height() - 2*m_scaledCornerRadius)));
                }
                else
                { // "horiz" is at the top because the titlebar is excluded
                    horiz = QRegion(QRect(rect.topLeft(),
                                    QSize(rect.width(), rect.height() - m_scaledCornerRadius)));
                }
                bottomLeft = QRegion(QRect(rect.topLeft() + QPoint(0, rect.height() - 2*m_scaledCornerRadius),
                                           2*rSize),
                                     isLeftEdge() && isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                bottomRight = QRegion(QRect(rect.topLeft() + QPoint(rect.width() - 2*m_scaledCornerRadius,
                                                                    rect.height() - 2*m_scaledCornerRadius),
                                            2*rSize),
                                      isRightEdge() && isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            }
            else // no side border
            {
                horiz = QRegion(QRect(rect.topLeft(),
                                      QSize(rect.width(), rect.height() - m_scaledCornerRadius)));
                bottomLeft = QRegion(QRect(rect.topLeft() + QPoint(0, rect.height() - 2*m_scaledCornerRadius),
                                           2*rSize),
                                     isBottomEdge() ? QRegion::Rectangle : QRegion::Ellipse);
                bottomRight = QRegion(QRect(rect.topLeft() + QPoint(rect.width() - 2*m_scaledCornerRadius,
                                                                    rect.height() - 2*m_scaledCornerRadius),
                                            2*rSize),
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

        const QRect titleRect(QPoint(0, 0), QSize(size().width(), borderTop()));

        // add the titlebar
        if (m_scaledCornerRadius == 0
            || isMaximized()) // maximized + no border when maximized
        {
            region |= QRegion(titleRect);
        }
        else if (c->isShaded())
        {
            QRegion topLeft(QRect(titleRect.topLeft(), 2*rSize), QRegion::Ellipse);
            QRegion topRight(QRect(titleRect.topLeft() + QPoint(titleRect.width() - 2*m_scaledCornerRadius, 0),
                                   2*rSize),
                             QRegion::Ellipse);
            QRegion bottomLeft(QRect(titleRect.topLeft() + QPoint(0, titleRect.height() - 2*m_scaledCornerRadius),
                                     2*rSize),
                               QRegion::Ellipse);
            QRegion bottomRight(QRect(titleRect.topLeft() + QPoint(titleRect.width() - 2*m_scaledCornerRadius,
                                                                   titleRect.height() - 2*m_scaledCornerRadius),
                                      2*rSize),
                                QRegion::Ellipse);
            region = topLeft
                     .united(topRight)
                     .united(bottomLeft)
                     .united(bottomRight)
                     // vertical
                     .united(QRect(titleRect.topLeft() + QPoint(m_scaledCornerRadius, 0),
                                   QSize(titleRect.width() - 2*m_scaledCornerRadius, titleRect.height())))
                     // horizontal
                     .united(QRect(titleRect.topLeft() + QPoint(0, m_scaledCornerRadius),
                                   QSize(titleRect.width(), titleRect.height() - 2*m_scaledCornerRadius)));
        }
        else
        {
            QRegion topLeft(QRect(titleRect.topLeft(), 2*rSize),
                            isLeftEdge() || isTopEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            QRegion topRight(QRect(titleRect.topLeft() + QPoint(titleRect.width() - 2*m_scaledCornerRadius, 0),
                                   2*rSize),
                             isRightEdge() || isTopEdge() ? QRegion::Rectangle : QRegion::Ellipse);
            region |= topLeft
                      .united(topRight)
                      // vertical
                      .united(QRect(titleRect.topLeft() + QPoint(m_scaledCornerRadius, 0),
                                    QSize(titleRect.width() - 2*m_scaledCornerRadius, titleRect.height())))
                      // horizontal
                      .united(QRect(titleRect.topLeft() + QPoint(0, m_scaledCornerRadius),
                                    QSize(titleRect.width(), titleRect.height() - m_scaledCornerRadius)));
        }

        setBlurRegion(region);
    }

    //________________________________________________________________
    void Decoration::createButtons()
    {
        m_leftButtons = new KDecoration2::DecorationButtonGroup(KDecoration2::DecorationButtonGroup::Position::Left, this, &Button::create);
        m_rightButtons = new KDecoration2::DecorationButtonGroup(KDecoration2::DecorationButtonGroup::Position::Right, this, &Button::create);
        updateButtonsGeometry();
    }

    //________________________________________________________________
    void Decoration::updateButtonsGeometryDelayed()
    { QTimer::singleShot( 0, this, &Decoration::updateButtonsGeometry ); }

    //________________________________________________________________
    void Decoration::updateButtonsGeometry()
    {
        const auto s = settings();

        // adjust button position
        const int bHeight = captionHeight() + (isTopEdge() ? s->smallSpacing()*Metrics::TitleBar_TopMargin:0);
        const int bWidth = buttonHeight();
        const int verticalOffset = (isTopEdge() ? s->smallSpacing()*Metrics::TitleBar_TopMargin:0) + (captionHeight()-buttonHeight())/2;
        foreach( const QPointer<KDecoration2::DecorationButton>& button, m_leftButtons->buttons() + m_rightButtons->buttons() )
        {
            button.data()->setGeometry( QRectF( QPoint( 0, 0 ), QSizeF( bWidth, bHeight ) ) );
            static_cast<Button*>( button.data() )->setOffset( QPointF( 0, verticalOffset ) );
            static_cast<Button*>( button.data() )->setIconSize( QSize( bWidth, bWidth ) );
        }

        // left buttons
        if( !m_leftButtons->buttons().isEmpty() )
        {

            // spacing (use our own spacing instead of s->smallSpacing()*Metrics::TitleBar_ButtonSpacing)
            m_leftButtons->setSpacing(m_internalSettings->buttonSpacing());

            // padding
            const int vPadding = isTopEdge() ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
            const int hPadding = s->smallSpacing()*Metrics::TitleBar_SideMargin;
            if( isLeftEdge() )
            {
                // add offsets on the side buttons, to preserve padding, but satisfy Fitts law
                auto button = static_cast<Button*>( m_leftButtons->buttons().front().data() );
                button->setGeometry( QRectF( QPoint( 0, 0 ), QSizeF( bWidth + hPadding, bHeight ) ) );
                button->setFlag( Button::FlagFirstInList );
                button->setHorizontalOffset( hPadding );

                m_leftButtons->setPos(QPointF(0, vPadding));

            } else m_leftButtons->setPos(QPointF(hPadding + borderLeft(), vPadding));

        }

        // right buttons
        if( !m_rightButtons->buttons().isEmpty() )
        {

            // spacing (use our own spacing instead of s->smallSpacing()*Metrics::TitleBar_ButtonSpacing)
            m_rightButtons->setSpacing(m_internalSettings->buttonSpacing());

            // padding
            const int vPadding = isTopEdge() ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
            const int hPadding = s->smallSpacing()*Metrics::TitleBar_SideMargin;
            if( isRightEdge() )
            {

                auto button = static_cast<Button*>( m_rightButtons->buttons().back().data() );
                button->setGeometry( QRectF( QPoint( 0, 0 ), QSizeF( bWidth + hPadding, bHeight ) ) );
                button->setFlag( Button::FlagLastInList );

                m_rightButtons->setPos(QPointF(size().width() - m_rightButtons->geometry().width(), vPadding));

            } else m_rightButtons->setPos(QPointF(size().width() - m_rightButtons->geometry().width() - hPadding - borderRight(), vPadding));

        }

        update();

    }

    //________________________________________________________________
    void Decoration::paint(QPainter *painter, const QRect &repaintRegion)
    {
        // TODO: optimize based on repaintRegion
        auto c = client().toStrongRef();
        auto s = settings();

        // paint background
        if (!c->isShaded())
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
                painter->setClipRect(0, borderTop(), size().width(), size().height() - borderTop(), Qt::IntersectClip);

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
            painter->setPen(c->isActive() ? c->color(ColorGroup::Active, ColorRole::TitleBar)
                                          : c->color(ColorGroup::Inactive, ColorRole::Foreground));

            painter->drawRect(rect().adjusted(0, 0, -1, -1 ));
            painter->restore();
        }

    }

    //________________________________________________________________
    void Decoration::paintTitleBar(QPainter *painter, const QRect &repaintRegion)
    {
        const auto c = client().toStrongRef();
        const QRect titleRect(QPoint(0, 0), QSize(size().width(), borderTop()));

        if (!titleRect.intersects(repaintRegion)) return;

        painter->save();
        painter->setPen(Qt::NoPen);

        // render a linear gradient on title area and draw a light border at the top
        if(m_internalSettings->drawBackgroundGradient() && !flatTitleBar())
        {
            QColor titleBarColor(this->titleBarColor());
            titleBarColor.setAlpha(titleBarAlpha());

            QLinearGradient gradient(0, 0, 0, titleRect.height());
            QColor lightCol(titleBarColor.lighter(130 + m_internalSettings->backgroundGradientIntensity()));
            gradient.setColorAt(0.0, lightCol);
            gradient.setColorAt(0.99 / static_cast<qreal>(titleRect.height()), lightCol );
            gradient.setColorAt(1.0 / static_cast<qreal>(titleRect.height()),
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
            gradient.setColorAt(0.99 / static_cast<qreal>(titleRect.height()), lightCol);
            gradient.setColorAt(1.0 / static_cast<qreal>(titleRect.height()), titleBarColor);
            gradient.setColorAt(1.0, titleBarColor);

            painter->setBrush(gradient);
        }

        auto s = settings();
        if(isMaximized() || !s->isAlphaChannelSupported())
        {
            painter->drawRect(titleRect);
        }
        else if(c->isShaded())
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
        painter->setPen( fontColor() );
        const auto cR = captionRect();
        const QString caption = painter->fontMetrics().elidedText(c->caption(), Qt::ElideMiddle, cR.first.width());
        painter->drawText(cR.first, cR.second | Qt::TextSingleLine, caption);

        // draw all buttons
        m_leftButtons->paint(painter, repaintRegion);
        m_rightButtons->paint(painter, repaintRegion);
    }

    //________________________________________________________________
    int Decoration::buttonHeight() const
    {
        const int baseSize = settings()->gridUnit();
        switch( m_internalSettings->buttonSize() )
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
    int Decoration::captionHeight() const
    {
        const auto c = client().toStrongRef();
        return hideTitleBar() ? borderTop()
                              : borderTop()
                                - settings()->smallSpacing()*(Metrics::TitleBar_BottomMargin + Metrics::TitleBar_TopMargin )
                                - (c->isShaded() ? 0 : 1); // see recalculateBorders()
    }

    //________________________________________________________________
    QPair<QRect,Qt::Alignment> Decoration::captionRect() const
    {
        if( hideTitleBar() ) return qMakePair( QRect(), Qt::AlignCenter );
        else {

            const int extraTitleMargin = m_internalSettings->extraTitleMargin();
            auto c = client().toStrongRef();
            const int leftOffset = m_leftButtons->buttons().isEmpty() ?
                Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin :
                m_leftButtons->geometry().x() + m_leftButtons->geometry().width() + Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin;

            const int rightOffset = m_rightButtons->buttons().isEmpty() ?
                Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin:
                size().width() - m_rightButtons->geometry().x() + Metrics::TitleBar_SideMargin*settings()->smallSpacing() + extraTitleMargin;

            const int yOffset = settings()->smallSpacing()*Metrics::TitleBar_TopMargin;
            const QRect maxRect( leftOffset, yOffset, size().width() - leftOffset - rightOffset, captionHeight() );

            switch( m_internalSettings->titleAlignment() )
            {
                case InternalSettings::AlignLeft:
                return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignLeft );

                case InternalSettings::AlignRight:
                return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignRight );

                case InternalSettings::AlignCenter:
                return qMakePair( maxRect, Qt::AlignCenter );

                default:
                case InternalSettings::AlignCenterFullWidth:
                {

                    // full caption rect
                    const QRect fullRect = QRect( 0, yOffset, size().width(), captionHeight() );
                    QFont f; f.fromString(m_internalSettings->titleBarFont());
                    QFontMetrics fm(f);
                    QRect boundingRect( fm.boundingRect( c->caption()) );

                    // text bounding rect
                    boundingRect.setTop( yOffset );
                    boundingRect.setHeight( captionHeight() );
                    boundingRect.moveLeft( ( size().width() - boundingRect.width() )/2 );

                    if( boundingRect.left() < leftOffset ) return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignLeft );
                    else if( boundingRect.right() > size().width() - rightOffset ) return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignRight );
                    else return qMakePair(fullRect, Qt::AlignCenter);

                }

            }

        }

    }

    //________________________________________________________________
    void Decoration::updateShadow()
    {
        const auto c = client().toStrongRef();
        auto &shadow = c->isActive() ? g_sShadow : g_sShadowInactive;

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
                g_sShadow.clear();
                g_sShadowInactive.clear();
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

            const qreal strength = static_cast<qreal>(g_shadowStrength) / 255.0 * (c->isActive() ? 1.0 : 0.5);
            shadowRenderer.addShadow(params.shadow1.offset, params.shadow1.radius,
                withOpacity(g_shadowColor, params.shadow1.opacity * strength));
            shadowRenderer.addShadow(params.shadow2.offset, params.shadow2.radius,
                withOpacity(g_shadowColor, params.shadow2.opacity * strength));

            QImage shadowTexture = shadowRenderer.render();

            QPainter painter(&shadowTexture);
            painter.setRenderHint(QPainter::Antialiasing);

            const QRect outerRect = shadowTexture.rect();

            QRect boxRect(QPoint(0, 0), boxSize);
            boxRect.moveCenter(outerRect.center());

            // Mask out inner rect.
            const QMargins padding = QMargins(
                boxRect.left() - outerRect.left() - Metrics::Shadow_Overlap - params.offset.x(),
                boxRect.top() - outerRect.top() - Metrics::Shadow_Overlap - params.offset.y(),
                outerRect.right() - boxRect.right() - Metrics::Shadow_Overlap + params.offset.x(),
                outerRect.bottom() - boxRect.bottom() - Metrics::Shadow_Overlap + params.offset.y());
            const QRect innerRect = outerRect - padding;

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

            shadow = QSharedPointer<KDecoration2::DecorationShadow>::create();
            shadow->setPadding(padding);
            shadow->setInnerShadowRect(QRect(outerRect.center(), QSize(1, 1)));
            shadow->setShadow(shadowTexture);
        }

        setShadow(shadow);
    }

    void Decoration::setScaledCornerRadius()
    {
        m_scaledCornerRadius = Metrics::Frame_FrameRadius*settings()->smallSpacing();

    }

} // namespace


#include "breezedecoration.moc"
