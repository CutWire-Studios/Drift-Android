#include "Effect.h"

namespace drift {

QString Effect::filterGraphString() const
{
    if (name.isEmpty())
        return {};

    QStringList parts;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        const QString value = it.value().toString();
        if (!value.isEmpty())
            parts.append(QStringLiteral("%1=%2").arg(it.key(), value));
    }

    if (parts.isEmpty())
        return name;

    return QStringLiteral("%1=%2").arg(name, parts.join(QLatin1Char(':')));
}

QVariant Effect::valueAt(const QString &key, TimeUs clipTimeUs) const
{
    const auto it = paramKeyframes.constFind(key);
    if (it == paramKeyframes.constEnd() || it->isEmpty())
        return parameters.value(key);
    return it->evaluateAt(clipTimeUs);
}

Effect Effect::resolvedAt(TimeUs clipTimeUs) const
{
    Effect out = *this;
    for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            out.parameters.insert(it.key(), it->evaluateAt(clipTimeUs));
    }
    return out;
}

} // namespace drift
