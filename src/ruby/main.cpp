// ============================================================================
// main.cpp — Entry Point for Ruby GG Studio (Qt6 Edition)
// ============================================================================

#include <QApplication>
#include <QSettings>
#include <string>
#include "ruby/editor/ruby_main_window.h"
#include "ruby/theme/ruby_theme.h"
#include "ruby/tools/zypher_look.h"   // app-level generator default
#include "tools/boulderx.h"           // set_ground_generator()

// Defined for libswcore asset paths
std::string g_instance_assets_dir = "assets";

int main(int argc, char* argv[]) {
    // Enable High-DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("Ruby Studio GG");
    app.setOrganizationName("OpenSwordigo");

    // Apply theme styling (restores user preference from QSettings)
    ruby::theme::init_theme();

    // App-level generator default. boulderx ships with Boulder as its library
    // default (a pinned invariant the tests assert), so the APP lays the real
    // default on top: Zypher with its styles GUI, unless the user saved a pick.
    // RUBY_GROUND_GENERATOR is left to boulderx's own initialiser — a debug env
    // var must not outrank a saved choice.
    if (qEnvironmentVariableIsEmpty("RUBY_GROUND_GENERATOR")) {
        const std::string id = ruby::tools::zypher_look::initial_generator_id();
        boulderx::set_ground_generator(boulderx::ground_generator_from_id(id));
    }

    ruby::RubyMainWindow window;
    window.show();

    // If launched with an asset/scene file path from CLI or file manager, open it
    if (argc > 1) {
        const QString cli_file = QString::fromLocal8Bit(argv[1]);
        window.open_external_file(cli_file);
    }

    return app.exec();
}
