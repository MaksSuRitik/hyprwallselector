// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatemanager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
class UpdatesTest : public QObject {
    Q_OBJECT
    QJsonObject release(QString version) {
        const QString name = "Hyprwallselector-" + version + "-Setup.exe";
        return {{"tag_name", "windows-v" + version}, {"draft", false}, {"prerelease", false},
            {"assets", QJsonArray{QJsonObject{{"name", name}, {"state", "uploaded"}, {"size", 100},
                {"digest", "sha256:" + QString(64, 'a')},
                {"browser_download_url", "https://github.com/MaksSuRitik/hyprwallselector/releases/download/windows-v" + version + '/' + name}}}}};
    }
    WindowsRelease select(QJsonArray array, QString current = "0.2.0") { return UpdateManager::selectRelease(QJsonDocument(array).toJson(), current); }
  private slots:
    void numericVersionsAndNoDowngrades() {
        QCOMPARE(select({release("0.9.0"), release("0.10.0"), release("0.3.0")}).version, QString("0.10.0"));
        QVERIFY(!select({release("0.2.0"), release("0.1.0")}).valid());
    }
    void filterOtherPlatformsAndUnpublished() {
        auto linux = release("1.0.0"); linux["tag_name"] = "v1.0.0";
        auto draft = release("2.0.0"); draft["draft"] = true;
        auto preview = release("3.0.0"); preview["prerelease"] = true;
        QVERIFY(!select({linux, draft, preview}).valid());
        QVERIFY(!UpdateManager::selectRelease("not JSON", "0.2.0").valid());
    }
    void rejectUnverifiedOrForeignInstaller() {
        for (const QString key : {"digest", "browser_download_url", "name", "size", "state"}) {
            auto data = release("0.3.0"); auto asset = data["assets"].toArray()[0].toObject();
            asset[key] = key == "size" ? QJsonValue(-1) : QJsonValue("invalid");
            data["assets"] = QJsonArray{asset};
            QVERIFY2(!select({data}).valid(), qPrintable(key));
        }
    }
    void redirectPolicy() {
        QVERIFY(UpdateManager::trustedRedirect(QUrl("https://release-assets.githubusercontent.com/signed/path?token=test")));
        QVERIFY(!UpdateManager::trustedRedirect(QUrl("http://github.com/path")));
        QVERIFY(!UpdateManager::trustedRedirect(QUrl("https://github.com.evil.test/path")));
        QVERIFY(!UpdateManager::trustedRedirect(QUrl("https://user@github.com/path")));
        QVERIFY(!UpdateManager::trustedRedirect(QUrl("https://github.com:444/path")));
        QVERIFY(!UpdateManager::trustedRedirect(QUrl("file:///C:/tmp/setup.exe")));
    }
};
QTEST_GUILESS_MAIN(UpdatesTest)
#include "test-updates.moc"
