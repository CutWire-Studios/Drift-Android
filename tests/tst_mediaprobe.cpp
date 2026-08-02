#include <QtTest>

#include "engine/MediaProbe.h"

class TestMediaProbe : public QObject
{
    Q_OBJECT

private slots:
    void missingFileFails();
};

void TestMediaProbe::missingFileFails()
{
    const MediaInfo info = MediaProbe::probe(QStringLiteral("/nonexistent/path/does-not-exist.mp4"));
    QVERIFY(!info.ok);
    QVERIFY(!info.errorString.isEmpty());
    QCOMPARE(info.streams.size(), 0);
}

QTEST_MAIN(TestMediaProbe)
#include "tst_mediaprobe.moc"
