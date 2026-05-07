#pragma once
//
// Bindings — the (classGuid, functionName) → C++ callable dispatch
// table that the Maki VM uses to satisfy CALLM/CALLM2/UMC opcodes.
//
// A native runtime class registers itself by GUID; for each method
// it exposes a CallFn that takes the Slot[] argument array and the
// `this`-IObject and returns a Slot.  The VM never knows about
// concrete Wasabi types — everything goes through this table.

#include <QHash>
#include <QString>
#include <QUuid>
#include <QVector>

#include <functional>

namespace wasabiq::maki {

struct Slot;

// All script-callable runtime objects derive from IObject.  The base
// has no virtual methods of its own beyond the destructor — concrete
// classes are dispatched via the Bindings table by GUID, not vtable.
class IObject {
public:
    virtual ~IObject() = default;

    // Most native classes need to know their own GUID for dispatch;
    // they can override this.  Default returns the null GUID.
    virtual QUuid scriptObjectGuid() const { return {}; }
};

using CallFn = std::function<Slot(IObject *self, const QVector<Slot> &args)>;

class Bindings {
public:
    Bindings();
    ~Bindings();

    // Register a callable on a native class, identified by its GUID.
    // funcName is the script-side method name (matches the DLF table
    // entries on disk).  paramCount is the number of arguments the
    // method declared in the .h — the VM uses it to pop the right
    // number of slots before dispatch.
    void registerMethod(const QUuid &classGuid,
                        const QString &funcName,
                        int paramCount,
                        CallFn fn);

    // Look up an entry — returns nullptr if no method is registered.
    struct Entry {
        int     paramCount = 0;
        CallFn  fn;
    };
    const Entry *find(const QUuid &classGuid, const QString &funcName) const;

    // Returns true if a class has any registered methods.
    bool hasClass(const QUuid &classGuid) const;

private:
    struct Key { QUuid g; QString n; };
    struct KeyHash {
        size_t operator()(const Key &k) const noexcept {
            return qHash(k.g) ^ qHash(k.n);
        }
    };
    struct KeyEq {
        bool operator()(const Key &a, const Key &b) const noexcept {
            return a.g == b.g && a.n == b.n;
        }
    };

    QHash<QString, Entry> m_table;          // key = "{guid}::funcname"
    QHash<QUuid, int>     m_classCounts;
};

// Helper: build the canonical lookup key.
QString bindingsKey(const QUuid &g, const QString &funcName);

} // namespace wasabiq::maki
