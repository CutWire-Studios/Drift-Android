#include <QtTest>

#include "playback/PlaybackClock.h"

class PlaybackTest : public QObject
{
    Q_OBJECT

private slots:
    void clockPausedPosition();
    void clockHoldsStartUntilSinkSyncs();
    void clockWallFallbackWhileRunning();
    void clockNeverRunsBackwardOnLateFirstSync();
    void produceAdvancesWithRenderedSamples();
    void playbackTracksSinkPosition();
    void seekWhileRunningKeepsClockAlive();
    void avSyncWithinTolerance();
    void rateScalesProduceAndPlayback();
    void renderedFramesIgnoreRate();
};

void PlaybackTest::clockPausedPosition()
{
    PlaybackClock clock;
    clock.reset(drift::secondsToUs(2.5), 48000);
    QCOMPARE(clock.currentTimeUs(), drift::secondsToUs(2.5));
    QCOMPARE(clock.pausedAt(), drift::secondsToUs(2.5));
}

// Opening the sink and pulling its first buffer takes a few hundred ms. Guessing
// forward on wall time during that window and then correcting to the sink's
// position snapped the playhead back to the start of playback.
void PlaybackTest::clockHoldsStartUntilSinkSyncs()
{
    PlaybackClock clock;
    clock.reset(drift::secondsToUs(2.5), 48000);
    clock.start();
    QTest::qWait(50);
    QCOMPARE(clock.currentTimeUs(), drift::secondsToUs(2.5));
}

// With no sink reporting progress at all (no audio device), the playhead still has
// to advance once the grace period is over, or video-only playback would freeze.
void PlaybackTest::clockWallFallbackWhileRunning()
{
    PlaybackClock clock;
    clock.reset(0, 48000);
    clock.start();
    QTest::qWait(650);
    QVERIFY(clock.currentTimeUs() > 0);
    clock.pause();
    QVERIFY(clock.pausedAt() > 0);
}

void PlaybackTest::clockNeverRunsBackwardOnLateFirstSync()
{
    PlaybackClock clock;
    clock.reset(0, 48000);
    clock.start();

    // Let the wall-clock fallback get going, then have the sink report that it has
    // barely played anything — exactly what the first sync looks like in practice.
    QTest::qWait(650);
    const drift::TimeUs before = clock.currentTimeUs();
    QVERIFY(before > 0);

    clock.syncPlaybackUs(0);
    QVERIFY(clock.currentTimeUs() >= before);
}

void PlaybackTest::produceAdvancesWithRenderedSamples()
{
    PlaybackClock clock;
    clock.reset(0, 48000);
    clock.start();
    clock.onAudioSamplesRendered(4800);
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(0.1));
    clock.onAudioSamplesRendered(4800);
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(0.2));
}

void PlaybackTest::playbackTracksSinkPosition()
{
    PlaybackClock clock;
    clock.reset(0, 48000);
    clock.start();

    clock.onAudioSamplesRendered(48000);
    clock.syncPlaybackUs(drift::secondsToUs(0.1));

    const drift::TimeUs played = clock.currentTimeUs();
    QVERIFY(played >= drift::secondsToUs(0.1));
    QVERIFY(played < drift::secondsToUs(0.2));
    QVERIFY(clock.produceTimeUs() >= drift::secondsToUs(1.0));
}

void PlaybackTest::seekWhileRunningKeepsClockAlive()
{
    PlaybackClock clock;
    clock.reset(drift::secondsToUs(1.0), 48000);
    clock.start();
    clock.onAudioSamplesRendered(4800);
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(1.1));

    clock.reset(drift::secondsToUs(2.0), 48000);
    QVERIFY(!clock.isRunning());
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(2.0));

    clock.start();
    QVERIFY(clock.isRunning());
    clock.onAudioSamplesRendered(4800);
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(2.1));
}

void PlaybackTest::avSyncWithinTolerance()
{
    PlaybackClock clock;
    clock.reset(0, 48000);
    clock.start();

    clock.onAudioSamplesRendered(1920);
    clock.syncPlaybackUs(drift::secondsToUs(0.04));

    const drift::TimeUs a = clock.currentTimeUs();
    const drift::TimeUs b = clock.currentTimeUs();
    QVERIFY(qAbs(a - b) <= 40'000);
}

void PlaybackTest::rateScalesProduceAndPlayback()
{
    PlaybackClock clock;
    clock.setRate(2.0);
    clock.reset(drift::secondsToUs(1.0), 48000);
    clock.start();

    // The sink still runs at 48 kHz; a rate of 2 only changes how much timeline each sample covers.
    clock.onAudioSamplesRendered(4800);
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(1.2));

    clock.syncPlaybackUs(drift::secondsToUs(0.1));
    const drift::TimeUs played = clock.currentTimeUs();
    QVERIFY(played >= drift::secondsToUs(1.2));
    QVERIFY(played < drift::secondsToUs(1.4));

    // Still monotonic under a rate: a repeated read can only stay put or move forward.
    QVERIFY(clock.currentTimeUs() >= played);
}

void PlaybackTest::renderedFramesIgnoreRate()
{
    PlaybackClock clock;
    clock.setRate(0.5);
    clock.reset(0, 48000);
    clock.start();

    clock.onAudioSamplesRendered(4800);
    // Sink domain, so no rate: this is what the post-mix retimer's output cursor advances by.
    QCOMPARE(clock.renderedFramesUs(), drift::secondsToUs(0.1));
    QCOMPARE(clock.produceTimeUs(), drift::secondsToUs(0.05));
}

QTEST_MAIN(PlaybackTest)
#include "tst_playback.moc"
