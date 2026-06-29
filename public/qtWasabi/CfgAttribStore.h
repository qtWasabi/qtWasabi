#pragma once
//
// CfgAttribStore — shared integer config values keyed by Wasabi
// `cfgattrib` strings ("{GUID};Name").  Multiple widgets that
// declare the same cfgattrib stay in sync: any widget that writes
// the value broadcasts to every subscriber, which re-derives its
// visual state from the new value.
//
// Real Wasabi routes this through the AttribsApi service; widgets
// `getAttrib(guid, name)` at construction and listen for attrib-
// changed callbacks.  qtWasabi has no service registry yet, so the
// store is a process-global singleton keyed by the verbatim
// cfgattrib string.  This is sufficient because Wasabi's GUID-and-
// name pair is what real skins write into the XML attribute too.
//
// Cross-widget example (WinampModernPP Repeat):
//   • `Repeat` (NStates 3-state, main button) — cfgattrib=...;Repeat
//   • `RepeatDisplay` (NStates 3-state, songinfo LED) — same key
//   • `RepeatLED` (NStates 3-state, LED next to button) — same key
// Clicking any one writes its state value to the store; the other
// two receive a notification and re-derive their `m_state` from
// the new value.
//
// Values are ints because Wasabi attribs are int-typed (toggles
// store 0/1; NStates with `cfgvals="0;1;-1"` stores one of those
// three).  Widgets translate state ↔ value through cfgvals; the
// store stays type-agnostic.
//

#include <QHash>
#include <QString>
#include <functional>

namespace qtWasabi {

class CfgAttribStore {
public:
    using Subscriber = std::function<void(int newValue)>;

    static CfgAttribStore &instance();

    // Read current value.  Returns 0 when the key has never been
    // written — matches Wasabi's "missing attrib defaults to 0"
    // behaviour for boolean toggles.
    int get(const QString &key) const;

    // Returns true when the key has been written at least once.
    // Subscribers use this to decide whether to seed their initial
    // state from the store or from their own `activated`/state attr.
    bool has(const QString &key) const;

    // Write a new value.  All subscribers (including the one that
    // triggered the write) receive the notification — subscribers
    // should compare to their current state and no-op if unchanged
    // to avoid feedback loops.
    void set(const QString &key, int value);

    // Subscribe; returns an opaque handle.  Pass back to
    // `unsubscribe` to remove.  The callback fires synchronously
    // from `set` so widgets can update + requestRepaint inline.
    int subscribe(const QString &key, Subscriber cb);
    void unsubscribe(int handle);

private:
    CfgAttribStore() = default;
    QHash<QString, int> m_values;
    struct Sub { QString key; Subscriber cb; };
    QHash<int, Sub> m_subs;
    int m_nextHandle = 1;
};

}  // namespace qtWasabi
