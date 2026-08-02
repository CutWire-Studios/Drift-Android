#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <vector>

namespace drift {

// Decode-only GPT-2 byte-level BPE tokenizer for Whisper. Loads vocab.json (piece->id),
// stores the raw bytes each text token maps to, and reassembles UTF-8 text from a token
// run. Special tokens (ids >= kTextTokenLimit) carry no text and are skipped.
class WhisperTokenizer
{
public:
    static constexpr int kTextTokenLimit = 50257; // ids >= this are special (eos, lang, timestamps)

    bool load(const QString &vocabJsonPath);
    bool isLoaded() const { return m_loaded; }

    // Reassembles UTF-8 text from the text tokens (id < kTextTokenLimit) in the run.
    QString decode(const std::vector<int> &tokens) const;

private:
    bool m_loaded = false;
    QHash<int, QByteArray> m_idToBytes;
};

} // namespace drift
