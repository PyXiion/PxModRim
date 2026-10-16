#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJSEngine>
#include <QJSValue>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWidget>

namespace {

const QString kWorkshopDir = QStringLiteral("qrc:/qt/qml/PxModRim");

constexpr qint64 kWaitMs = 5000;


// Loads thumbs.js the way `import "thumbs.js" as Thumbs` does, minus the `.pragma library`
// header, which only QML's loader understands.
class Thumbs {
public:
    Thumbs() {
        const QString path = QStringLiteral(":/qt/qml/PxModRim/thumbs.js");
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QString script = QString::fromUtf8(file.readAll());
        const QString pragma = QStringLiteral(".pragma library");
        if (script.startsWith(pragma))
            script.remove(0, pragma.size());
        const QJSValue result = m_engine.evaluate(script, path);
        REQUIRE(!result.isError());
        m_sized = m_engine.globalObject().property(QStringLiteral("sized"));
        REQUIRE(m_sized.isCallable());
    }

    QString sized(const QString& url, int width, int height) {
        return m_sized.call(QJSValueList{QJSValue(url), QJSValue(width), QJSValue(height)}).toString();
    }

private:
    QJSEngine m_engine;
    QJSValue m_sized;
};

// Spins the event loop until `predicate` holds or kWaitMs elapses.
bool waitUntil(const std::function<bool()>& predicate) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate()) {
        if (clock.hasExpired(kWaitMs))
            return false;
        QEventLoop loop;
        QTimer::singleShot(10, &loop, [&loop] { loop.quit(); });
        loop.exec();
    }
    return true;
}

// Runs the teardown when a REQUIRE/CHECK failure unwinds the test body.
struct Cleanup {
    std::function<void()> run;
    ~Cleanup() { run(); }
};

}  // namespace

TEST_CASE("thumbnail leaves other URLs unchanged") {
    Thumbs thumbs;
    const QStringList urls{
        QStringLiteral("https://example.com/image.png"),
        QStringLiteral("https://notsteamstatic.com/image.png"),
        QStringLiteral("https://steamusercontent.com.example.com/image.png"),
        QStringLiteral("https://user@steamstatic.com/image.png"),
        QStringLiteral(""),
    };
    for (const QString& url : urls) {
        INFO(url.toStdString());
        CHECK(thumbs.sized(url, 256, 128) == url);
    }
}

TEST_CASE("thumbnail replaces resize query before fragment") {
    Thumbs thumbs;
    for (const char* host : {"images.steamusercontent.com", "steamstatic.com", "cdn.steamstatic.com"}) {
        const QString hostName = QString::fromLatin1(host);
        INFO(host);
        const QString url = QStringLiteral("https://%1/image.png?token=abc&imw=720&imh=360&ima=fit&impolicy=Letterbox#preview")
                                .arg(hostName);
        const QString expected = QStringLiteral("https://%1/image.png?token=abc&imw=256&imh=128&ima=fit&impolicy=Letterbox#preview")
                                     .arg(hostName);
        const QString resized = thumbs.sized(url, 256, 128);
        CHECK(resized == expected);
        CHECK(thumbs.sized(resized, 256, 128) == expected);
    }
}

TEST_CASE("hidden thumbnail unloads and can load again") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QImage image(64, 32, QImage::Format_RGB32);
    image.fill(0xFF00FF00);
    const QString imagePath = dir.filePath(QStringLiteral("preview.png"));
    REQUIRE(image.save(imagePath));

    const QString sourcePath = dir.filePath(QStringLiteral("Image.qml"));
    {
        QFile source(sourcePath);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Text));
        const QString text = QStringLiteral(
                                 "import QtQuick\n"
                                 "import \"%1\" as Workshop\n"
                                 "Workshop.CatalogImage { previewUrl: \"%2\" }\n")
                                 .arg(kWorkshopDir,
                                      QUrl::fromLocalFile(imagePath).toString());
        REQUIRE(source.write(text.toUtf8()) >= 0);
    }

    QStringList warnings;
    auto owner = std::make_unique<QWidget>();
    auto* engine = new QQmlEngine(owner.get());
    QObject::connect(engine, &QQmlEngine::warnings, owner.get(), [&warnings](const QList<QQmlError>& errors) {
        for (const QQmlError& error : errors)
            warnings.append(error.toString());
    });

    auto* widget = new QQuickWidget(engine, owner.get());
    widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget->resize(200, 110);
    widget->setSource(QUrl::fromLocalFile(sourcePath));
    owner->show();
    widget->show();
    const Cleanup cleanup{[&] {
        delete widget;
        delete engine;
    }};

    QQuickItem* root = widget->rootObject();
    REQUIRE(root != nullptr);

    // QQuickImageBase::Status: Null = 0, Ready = 1.
    constexpr int kNull = 0;
    constexpr int kReady = 1;
    REQUIRE(waitUntil([&] { return root->property("status").toInt() == kReady; }));
    root->setVisible(false);
    REQUIRE(waitUntil([&] { return root->property("status").toInt() == kNull; }));
    root->setVisible(true);
    widget->resize(300, 150);
    REQUIRE(waitUntil([&] { return root->property("status").toInt() == kReady; }));
    CHECK(widget->grabFramebuffer().pixelColor(150, 75).name() == QStringLiteral("#00ff00"));
    INFO(warnings.join(QLatin1Char('\n')).toStdString());
    CHECK(warnings.isEmpty());
}
