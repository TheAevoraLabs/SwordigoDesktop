// ============================================================================
// main_mobile.cpp — Entry Point for Ruby GG Mobile (Android Edition, Qt Quick)
// ============================================================================

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <string>

#include "ruby_file_model.h"
#include "ruby_texture_provider.h"
#include "ruby_highlighter_bridge.h"
#include "ruby_quick_viewport.h"
#include "ruby_audio_bridge.h"
#include "ruby_tools_bridge.h"
#include "ruby_settings_bridge.h"
#include "scene_orientation.h"
#include "ruby/core/project/ruby_project.h"

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

// Defined for libswcore asset paths
std::string g_instance_assets_dir = "assets";

int main(int argc, char* argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);
    app.setApplicationName("Ruby GG");
    app.setOrganizationName("Aevora");

    // Enable Material 3 Style
    QQuickStyle::setStyle("Material");

    // Register C++ Quick types
    qmlRegisterType<ruby::android::RubyQuickViewport>("Ruby", 1, 0, "RubyQuickViewport");
    qmlRegisterType<ruby::android::RubyHighlighterBridge>("Ruby", 1, 0, "RubyHighlighterBridge");
    qmlRegisterType<ruby::android::RubyAudioBridge>("Ruby", 1, 0, "RubyAudioBridge");

    QQmlApplicationEngine engine;

    // Instantiate bridges
    ruby::android::RubyFileModel fileModel;
    ruby::android::RubyTextureBridge textureBridge;
    ruby::android::RubyCodeBridge codeBridge;
    ruby::android::RubyToolsBridge toolsBridge;
    ruby::android::RubySettingsBridge settingsBridge;
    ruby::android::RubyAudioBridge audioBridge;
    // Runtime screen-orientation policy: the landscape-only scene editor asks
    // this to hold the activity in landscape and to hand portrait back on exit.
    ruby::android::SceneOrientation screenOrientation;

    // Register custom image provider for PVR / TEX textures
    auto* textureProvider = new ruby::android::RubyTextureProvider();
    textureProvider->setBridge(&textureBridge);
    engine.addImageProvider(QStringLiteral("ruby_pvr"), textureProvider);


    engine.rootContext()->setContextProperty("rubyFileModel", &fileModel);
    engine.rootContext()->setContextProperty("textureBridge", &textureBridge);
    engine.rootContext()->setContextProperty("codeBridge", &codeBridge);
    engine.rootContext()->setContextProperty("toolsBridge", &toolsBridge);
    engine.rootContext()->setContextProperty("rubyToolsBridge", &toolsBridge);
    engine.rootContext()->setContextProperty("settingsBridge", &settingsBridge);
    engine.rootContext()->setContextProperty("rubySettings", &settingsBridge);
    engine.rootContext()->setContextProperty("audioBridge", &audioBridge);
    engine.rootContext()->setContextProperty("screenOrientation", &screenOrientation);
    engine.rootContext()->setContextProperty("androidContext", &screenOrientation);
    engine.rootContext()->setContextProperty("rubyProject", &ruby::core::RubyProject::instance());

    engine.addImportPath(QStringLiteral("assets:/qml"));
    engine.addImportPath(QStringLiteral("assets:/--bundled-resources--/qml"));
    engine.addImportPath(QStringLiteral("qrc:/"));

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            QCoreApplication::exit(-1);
        } else if (obj && url == objUrl) {
#if defined(Q_OS_ANDROID)
            // Signal to Java that Qt/QML is loaded so the splash screen smoothly fades out
            QJniObject::callStaticMethod<void>(
                "in/aevora/ruby/RubyActivity",
                "onNativeInitialized",
                "()V"
            );
#endif
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
