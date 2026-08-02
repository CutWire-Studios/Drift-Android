#pragma once

#include <QString>
#include <QStringList>

namespace drift {

// Compare dotted numeric versions; trailing pre-release text is ignored, which is enough for the
// "is there something newer" question both the Addon Manager and the update check actually ask.
// It does mean 0.2.0-rc1 compares equal to 0.2.0 — fine here, because GitHub's releases/latest
// never returns a pre-release and no build is ever tagged as one.
inline int compareVersions(const QString &a, const QString &b)
{
    const QStringList left = a.split(QLatin1Char('.'));
    const QStringList right = b.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(left.size(), right.size()); ++i) {
        const int l = left.value(i).section(QLatin1Char('-'), 0, 0).toInt();
        const int r = right.value(i).section(QLatin1Char('-'), 0, 0).toInt();
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

} // namespace drift
