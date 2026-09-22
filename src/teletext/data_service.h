#pragma once

#include <memory>
#include <string>

#include "page.h"

namespace teletext {

// What App needs from a teletext section's backing service, regardless of
// where its data comes from. NewsService (RSS/Atom, NEWS and TAGESSCHAU) and
// MvwService (the MediathekViewWeb JSON API, ARD and ZDF) both implement
// this, so App::renderTeletext()/handleTeletextKey() work identically
// across every section without knowing which kind of service they're
// driving.
class TeletextDataService {
public:
    virtual ~TeletextDataService() = default;

    // The current page set. Never null; page 100 always exists.
    virtual std::shared_ptr<const PageStore> snapshot() const = 0;

    // Blue key ("Refresh"): re-fetch as soon as possible, bypassing
    // whatever timer/cache-TTL would otherwise delay it. Safe to call from
    // the render thread; the actual fetch happens on the service's own
    // worker thread (or is a no-op if the service has none running).
    virtual void requestRefresh() = 0;

    // Header/not-found-page provider name for this section as a whole
    // (individual pages may show their own more specific service name).
    virtual std::string serviceName() const = 0;
};

}  // namespace teletext
