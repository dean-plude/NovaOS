/* NovaOS: the Windows SDK's TraceLogging activities, as no-ops (see
 * TraceLoggingProvider.h).  _TlgActivityBase keeps the start/stop state and
 * the activity id its users read; nothing is written. */
#pragma once
#include <TraceLoggingProvider.h>

template <class TlgReflectorTag, UINT64 keyword, UINT8 level>
class _TlgActivityBase
{
public:
    static constexpr UINT64 Keyword = keyword;
    static constexpr UINT8 Level = level;
    static constexpr UINT64 PrivacyTag = 0;

    _TlgActivityBase() noexcept : state_(0), id_(), relatedId_(), hasRelated_(false) {}
    _TlgActivityBase(_TlgActivityBase &&o) noexcept : state_(o.state_), id_(o.id_), relatedId_(o.relatedId_), hasRelated_(o.hasRelated_)
    {
        o.state_ = 0;
    }
    _TlgActivityBase &operator=(_TlgActivityBase &&o) noexcept
    {
        state_ = o.state_;
        id_ = o.id_;
        relatedId_ = o.relatedId_;
        hasRelated_ = o.hasRelated_;
        o.state_ = 0;
        return *this;
    }

    const GUID *Id() const noexcept { return &id_; }
    const GUID *zInternalRelatedId() const noexcept { return hasRelated_ ? &relatedId_ : nullptr; }
    const GUID *GetRelatedId() const noexcept { return zInternalRelatedId(); }
    bool IsStarted() const noexcept { return state_ == 1; }
    void zInternalStart() noexcept
    {
        state_ = 1;
        static_cast<TlgReflectorTag *>(this)->OnStarted();
    }
    void zInternalStop() noexcept
    {
        state_ = 2;
        static_cast<TlgReflectorTag *>(this)->OnStopped();
    }

protected:
    void SetRelatedId(const GUID &related) noexcept
    {
        relatedId_ = related;
        hasRelated_ = true;
    }
    void PushThreadActivityId() noexcept {}
    void PopThreadActivityId() noexcept {}

private:
    int state_;
    GUID id_, relatedId_;
    bool hasRelated_;
};

template <TraceLoggingHProvider const &provider, UINT64 keyword = 0, UINT8 level = 5>
class TraceLoggingActivity : public _TlgActivityBase<TraceLoggingActivity<provider, keyword, level>, keyword, level>
{
    friend class _TlgActivityBase<TraceLoggingActivity<provider, keyword, level>, keyword, level>;
    void OnStarted() {}
    void OnStopped() {}
public:
    TraceLoggingHProvider Provider() const noexcept { return provider; }
};

#define TraceLoggingWriteStart(activity, name, ...) ((activity).zInternalStart())
#define TraceLoggingWriteStop(activity, name, ...) ((activity).zInternalStop())
#define TraceLoggingWriteTagged(...) do { } while (0)
