#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AiStreamTarget.h — how the sidebar writes a presentation without knowing what
// one is.
//
// The sidebar lives in core, and core is what the modules are built on top of,
// so it cannot include ImpressModule: that dependency runs the wrong way and
// would not link. The panel therefore talks to this interface, and the app
// layer, which already links both, supplies the implementation that knows about
// slides.
//
// The shape deliberately mirrors the document agent: begin, feed the stream,
// end, and be able to take it all back again. The operations differ per
// surface; the streaming and the rollback do not, so only this much is shared.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

namespace NativeOffice {

class AiStreamTarget {
public:
    virtual ~AiStreamTarget() = default;

    virtual void aiBegin() = 0;
    virtual void aiFeed(const QString& chunk) = 0;
    virtual void aiEnd() = 0;

    // Characters of real content written, for the usage tally.
    virtual int  aiCharactersWritten() const = 0;

    virtual bool aiCanRollback() const = 0;
    virtual bool aiCanRollforward() const = 0;
    virtual void aiRollback() = 0;
    virtual void aiRollforward() = 0;
};

} // namespace NativeOffice
