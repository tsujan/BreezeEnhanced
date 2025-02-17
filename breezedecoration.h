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

#pragma once

#include "breeze.h"
#include "breezesettings.h"

#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationSettings>

#include <QPalette>
#include <QVariant>
#include <QVariantAnimation>

namespace KDecoration3
{
    class DecorationButton;
    class DecorationButtonGroup;
}

namespace Breeze
{
    class Decoration : public KDecoration3::Decoration
    {
        Q_OBJECT

        public:

        //* constructor
        explicit Decoration(QObject *parent = nullptr, const QVariantList &args = QVariantList());

        //* destructor
        ~Decoration() override;

        //* paint
        void paint(QPainter *painter, const QRectF &repaintRegion) override;

        //* internal settings
        InternalSettingsPtr internalSettings() const
        { return m_internalSettings; }

        //* caption height
        qreal captionHeight() const;

        //* button size
        int buttonSize() const;

        //*@name colors
        //@{
        QColor titleBarColor() const;
        QColor fontColor() const;
        //@}

        //*@name maximization modes
        //@{
        inline bool isMaximized() const;
        inline bool isMaximizedHorizontally() const;
        inline bool isMaximizedVertically() const;

        inline bool isLeftEdge() const;
        inline bool isRightEdge() const;
        inline bool isTopEdge() const;
        inline bool isBottomEdge() const;

        inline bool hideTitleBar() const;
        //@}

        public Q_SLOTS:
        bool init() override;

        private Q_SLOTS:
        void reconfigure();
        void recalculateBorders();
        void resetBlurRegion();
        void updateButtonsGeometry();
        void updateButtonsGeometryDelayed();
        void updateTitleBar();
        void updateActiveState();
        void updateScale();

        private:

        //* return the rect in which caption will be drawn
        QPair<QRectF,Qt::Alignment> captionRect() const;

        void createButtons();
        void paintTitleBar(QPainter *painter, const QRectF &repaintRegion);
        void updateShadow();

        void setScaledCornerRadius();

        //*@name border size
        //@{
        qreal borderSize(bool bottom, qreal scale) const;
        inline bool hasBorders() const;
        inline bool hasNoBorders() const;
        inline bool hasNoSideBorders() const;
        QMarginsF bordersFor(qreal scale) const;
        //@}

        //*@name color customization
        //@{
        inline bool opaqueTitleBar() const;
        inline bool flatTitleBar() const;
        inline int titleBarAlpha() const;
        //@}

        InternalSettingsPtr m_internalSettings;
        KDecoration3::DecorationButtonGroup *m_leftButtons = nullptr;
        KDecoration3::DecorationButtonGroup *m_rightButtons = nullptr;

        //*frame corner radius, scaled according to DPI
        qreal m_scaledCornerRadius = 3;
    };

    bool Decoration::hasBorders() const
    {
        if (m_internalSettings && m_internalSettings->mask() & BorderSize)
            return m_internalSettings->borderSize() > InternalSettings::BorderNoSides;
        else
            return settings()->borderSize() > KDecoration3::BorderSize::NoSides;
    }

    bool Decoration::hasNoBorders() const
    {
        if (m_internalSettings && m_internalSettings->mask() & BorderSize)
            return m_internalSettings->borderSize() == InternalSettings::BorderNone;
        else
            return settings()->borderSize() == KDecoration3::BorderSize::None;
    }

    bool Decoration::hasNoSideBorders() const
    {
        if (m_internalSettings && m_internalSettings->mask() & BorderSize)
            return m_internalSettings->borderSize() == InternalSettings::BorderNoSides;
        else
            return settings()->borderSize() == KDecoration3::BorderSize::NoSides;
    }

    bool Decoration::isMaximized() const
    {
        return window()->isMaximized();
    }

    bool Decoration::isMaximizedHorizontally() const
    {
        return window()->isMaximizedHorizontally();
    }

    bool Decoration::isMaximizedVertically() const
    {
        return window()->isMaximizedVertically();
    }

    bool Decoration::isLeftEdge() const
    {
        const auto w = window();
        return (w->isMaximizedHorizontally() || w->adjacentScreenEdges().testFlag(Qt::LeftEdge));
    }

    bool Decoration::isRightEdge() const
    {
        const auto w = window();
        return (w->isMaximizedHorizontally() || w->adjacentScreenEdges().testFlag(Qt::RightEdge));
    }

    bool Decoration::isTopEdge() const
    {
        const auto w = window();
        return (w->isMaximizedVertically() || w->adjacentScreenEdges().testFlag(Qt::TopEdge));
    }

    bool Decoration::isBottomEdge() const
    {
        const auto w = window();
        return (w->isMaximizedVertically() || w->adjacentScreenEdges().testFlag(Qt::BottomEdge));
    }

    bool Decoration::hideTitleBar() const
    { return m_internalSettings->hideTitleBar() && !window()->isShaded(); }

    bool Decoration::opaqueTitleBar() const
    { return m_internalSettings->opaqueTitleBar(); }

    bool Decoration::flatTitleBar() const
    { return m_internalSettings->flatTitleBar(); }

    int Decoration::titleBarAlpha() const
    {
        if (m_internalSettings->opaqueTitleBar())
            return 255;
        int a = m_internalSettings->opacityOverride() > -1 ? m_internalSettings->opacityOverride()
                                                           : m_internalSettings->backgroundOpacity();
        a =  qBound(0, a, 100);
        return qRound(static_cast<qreal>(a) * static_cast<qreal>(2.55));
    }

}

