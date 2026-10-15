// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativevideo.h"
#include "windowsplatform.h"
#include <QtTest>
#include <QQuickWindow>
#include <QDir>
#include <QJsonDocument>
#include <QFile>

// Opt-in GPU integration test. It exercises real local files without changing
// wallpaper settings or the user's desktop. Set HWS_TEST_VIDEO_FOLDER to run.
class NativeVideoTest : public QObject {
    Q_OBJECT
  private slots:
    void playbackAndLifetime() {
        const QString folder=qEnvironmentVariable("HWS_TEST_VIDEO_FOLDER");
        if (folder.isEmpty()) QSKIP("Set HWS_TEST_VIDEO_FOLDER for the GPU test");
        const auto files=QDir(folder).entryInfoList({"*.mp4"},QDir::Files,QDir::Name);
        QVERIFY(!files.isEmpty());
        QQuickWindow window;
        window.setTitle("Hyprwallselector video verification");
        window.resize(960,540);
        auto *video=new NativeVideo(window.contentItem());
        video->setSize(QSizeF(960,540));
        QSignalSpy errors(video,&NativeVideo::failed);
        window.show();
        ShowWindow(reinterpret_cast<HWND>(window.winId()),SW_SHOWNOACTIVATE);
        QJsonArray results;
        const int cycles=qEnvironmentVariableIntValue("HWS_TEST_CYCLES")>0 ? qEnvironmentVariableIntValue("HWS_TEST_CYCLES") : 3;
        const int dwell=qEnvironmentVariableIntValue("HWS_TEST_DWELL")>0 ? qEnvironmentVariableIntValue("HWS_TEST_DWELL") : 250;
        for (int cycle=0;cycle<cycles;++cycle) {
            for (const auto &file:files) {
                QElapsedTimer elapsed; elapsed.start();
                video->setSource(file.absoluteFilePath());
                QTRY_VERIFY_WITH_TIMEOUT(video->ready() || !errors.isEmpty(),5000);
                QVERIFY2(errors.isEmpty(),qPrintable(errors.isEmpty() ? QString() : errors.first().first().toString()));
                const auto ms=elapsed.elapsed();
                QCOMPARE(NativeVideo::livePlayers(),1);
                const auto before=NativeVideo::deliveredFrames();
                const auto first=window.grabWindow();
                QVERIFY(!first.isNull());
                QTest::qWait(dwell);
                QVERIFY(errors.isEmpty());
                QVERIFY(NativeVideo::deliveredFrames()>before+2);
                const auto second=window.grabWindow();
                QVERIFY2(first!=second,"Video is not moving");
                if (!cycle && results.isEmpty()) {
                    first.save(qEnvironmentVariable("HWS_TEST_CAPTURE","native-frame.png"));
                    video->setPaused(true);
                    QTest::qWait(80);
                    const auto paused=NativeVideo::deliveredFrames();
                    QTest::qWait(200);
                    QCOMPARE(NativeVideo::deliveredFrames(),paused);
                    video->setPaused(false);
                    QTRY_VERIFY_WITH_TIMEOUT(NativeVideo::deliveredFrames()>paused,500);
                }
                QJsonObject entry=QJsonObject::fromVariantMap(WindowsPlatform::memoryStats());
                entry["file"]=file.fileName(); entry["firstFrameMs"]=ms; entry["cycle"]=cycle;
                results.append(entry);
            }
        }
        // A carousel preview and wallpaper can coexist. Closing the preview
        // must release its independent decoder and keep the wallpaper playing.
        auto *preview=new NativeVideo(window.contentItem());
        preview->setPosition(QPointF(10,10)); preview->setSize(QSizeF(240,135));
        preview->setSource(files.first().absoluteFilePath());
        QTRY_VERIFY_WITH_TIMEOUT(preview->ready(),3000);
        QCOMPARE(NativeVideo::livePlayers(),2);
        delete preview;
        QTRY_COMPARE_WITH_TIMEOUT(NativeVideo::livePlayers(),1,2000);
        const auto continuing=NativeVideo::deliveredFrames();
        QTRY_VERIFY_WITH_TIMEOUT(NativeVideo::deliveredFrames()>continuing+2,1000);
        video->setSource({});
        QTRY_COMPARE_WITH_TIMEOUT(NativeVideo::livePlayers(),0,2000);
        video->setSource(folder+"/missing-video.mp4");
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),3000);
        video->setSource({});
        QTRY_COMPARE_WITH_TIMEOUT(NativeVideo::livePlayers(),0,2000);
        QFile report(qEnvironmentVariable("HWS_TEST_REPORT","native-video-results.json"));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(results).toJson());
    }
};
int main(int argc,char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc,argv);
    NativeVideoTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "test-nativevideo.moc"
