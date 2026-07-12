// HeadWiring — the engine↔head runtime wiring every head shares.
//
// The Maki VM reaches interactive behavior through a set of global
// callback registries (widget-click delegation, named windows, EQ /
// slider / volume stores, playback status, scripted resize).
// wireRuntime() binds all of them to a HeadWindow + PlayerHost pair —
// one call in the embedder's startup, identical for every head.
#pragma once

namespace qtWasabi {
class PlayerHost;
}

namespace qtWasabi::head {

class HeadWindow;

void wireRuntime(HeadWindow *view, PlayerHost *host);

}  // namespace qtWasabi::head
