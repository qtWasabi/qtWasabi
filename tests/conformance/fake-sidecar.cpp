// fake-sidecar — FakeHost behind SidecarService: the framework serving
// its own player protocol with no player at all.  The conformance
// probe (probe.mjs) drives it over grpc-js; run via run.sh.
#include <QCoreApplication>

#include <cstdio>

#include <qtWasabi/FakeHost.h>
#include <qtWasabi/serve/SidecarService.h>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        fprintf(stderr, "usage: fake-sidecar <socketPath> [musicRoot]\n");
        return 2;
    }

    qtWasabi::FakeHost host;
    qtWasabi::serve::ServeHooks hooks;
    if (argc > 2) hooks.musicRoot = QString::fromLocal8Bit(argv[2]);
    hooks.playerName = QStringLiteral("qtwasabi-fake-sidecar");

    qtWasabi::serve::SidecarService sidecar(&host, std::move(hooks));
    if (!sidecar.listen(QString::fromLocal8Bit(argv[1]))) {
        fprintf(stderr, "fake-sidecar: cannot bind %s\n", argv[1]);
        return 1;
    }
    return app.exec();
}
