#include "WhisperTokenizer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <vector>

namespace drift {

namespace {

// GPT-2 byte<->unicode remap: inverse table mapping each remapped code point back to its
// original byte. Mirrors transformers' bytes_to_unicode().
QHash<uint, unsigned char> buildUnicodeToByte()
{
    std::vector<int> bs;
    for (int b = '!'; b <= '~'; ++b)
        bs.push_back(b);
    for (int b = 0xA1; b <= 0xAC; ++b)
        bs.push_back(b);
    for (int b = 0xAE; b <= 0xFF; ++b)
        bs.push_back(b);

    std::vector<int> cs = bs;
    int n = 0;
    for (int b = 0; b < 256; ++b) {
        if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
            bs.push_back(b);
            cs.push_back(256 + n);
            ++n;
        }
    }

    QHash<uint, unsigned char> map;
    map.reserve(static_cast<int>(bs.size()));
    for (size_t i = 0; i < bs.size(); ++i)
        map.insert(static_cast<uint>(cs[i]), static_cast<unsigned char>(bs[i]));
    return map;
}

} // namespace

bool WhisperTokenizer::load(const QString &vocabJsonPath)
{
    m_loaded = false;
    m_idToBytes.clear();

    QFile file(vocabJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;

    const QHash<uint, unsigned char> uniToByte = buildUnicodeToByte();
    const QJsonObject obj = doc.object();

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const int id = it.value().toInt(-1);
        if (id < 0)
            continue;

        const QString piece = it.key();
        QByteArray bytes;
        bytes.reserve(piece.size());
        for (const QChar ch : piece) {
            const auto found = uniToByte.constFind(ch.unicode());
            if (found != uniToByte.constEnd())
                bytes.append(static_cast<char>(found.value()));
        }
        m_idToBytes.insert(id, bytes);
    }

    m_loaded = !m_idToBytes.isEmpty();
    return m_loaded;
}

QString WhisperTokenizer::decode(const std::vector<int> &tokens) const
{
    QByteArray bytes;
    for (const int id : tokens) {
        if (id < 0 || id >= kTextTokenLimit)
            continue;
        const auto found = m_idToBytes.constFind(id);
        if (found != m_idToBytes.constEnd())
            bytes.append(found.value());
    }
    return QString::fromUtf8(bytes);
}

} // namespace drift
