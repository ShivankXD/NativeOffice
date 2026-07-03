#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Avatars.h — circular account-avatar pixmap shared by the home screen and
// the settings window: the cached profile photo from AuthManager, or a
// coloured-initial fallback while no photo is cached.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/auth/AuthManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace NativeOffice {

inline QPixmap roundAvatarPixmap(int size, qreal dpr) {
    auto& auth = AuthManager::instance();
    const int px = int(size * dpr);
    QPixmap out(px, px);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addEllipse(0, 0, size, size);
    p.setClipPath(clip);

    const QPixmap photo(auth.avatarPath());
    if (!photo.isNull()) {
        QPixmap sc = photo.scaled(px, px, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
        const QRect src((sc.width() - px) / 2, (sc.height() - px) / 2, px, px);
        p.drawPixmap(QRect(0, 0, size, size), sc, src);
    } else {
        p.fillRect(0, 0, size, size, QColor("#2563EB"));
        QString letter = auth.displayName().left(1).toUpper();
        if (letter.isEmpty()) letter = QStringLiteral("N");
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", size * 2 / 5, QFont::Bold));
        p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, letter);
    }
    return out;
}

} // namespace NativeOffice
