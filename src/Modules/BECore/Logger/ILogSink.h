#pragma once

namespace BECore {

    class ILogSink : public RefCountedAtomic, public SubscriptionHolder {
        BE_CLASS(ILogSink, FACTORY_BASE)

    public:
        ~ILogSink() override = default;

        virtual void Initialize() = 0;

        virtual void Write(LogLevel level, eastl::string_view message) = 0;

        void SetMinLevel(LogLevel level) {
            _minLevel = level;
        }

        LogLevel GetMinLevel() const {
            return _minLevel;
        }

        bool ShouldLog(LogLevel level) const {
            return static_cast<int>(level) >= static_cast<int>(_minLevel);
        }

        virtual int GetPriority() const {
            return _priority;
        }
        void SetPriority(int priority) {
            _priority = priority;
        }

    protected:
        ILogSink() = default;

        BE_REFLECT_FIELD LogLevel _minLevel = LogLevel::Debug;
        BE_REFLECT_FIELD int _priority = 0;
    };

}  // namespace BECore
