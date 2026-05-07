#include "Bindings.h"

namespace wasabiq::maki {

Bindings::Bindings()  = default;
Bindings::~Bindings() = default;

QString bindingsKey(const QUuid &g, const QString &funcName) {
    // Lowercase the function name so script-side capitalization
    // doesn't matter (Maki scripts often differ in case from the .h).
    return g.toString(QUuid::WithoutBraces) + QStringLiteral("::") + funcName.toLower();
}

void Bindings::registerMethod(const QUuid &g, const QString &funcName,
                              int paramCount, CallFn fn)
{
    Entry e;
    e.paramCount = paramCount;
    e.fn = std::move(fn);
    m_table.insert(bindingsKey(g, funcName), e);
    m_classCounts[g] += 1;
}

const Bindings::Entry *Bindings::find(const QUuid &g, const QString &funcName) const {
    auto it = m_table.constFind(bindingsKey(g, funcName));
    if (it == m_table.constEnd()) return nullptr;
    return &it.value();
}

bool Bindings::hasClass(const QUuid &g) const {
    return m_classCounts.value(g, 0) > 0;
}

} // namespace wasabiq::maki
