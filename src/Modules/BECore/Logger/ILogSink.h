#pragma once

#include <BECore/Logger/LogLevel.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>

namespace BECore {

    class ILogSink : public RefCountedAtomic, public SubscriptionHolder {
        BE_CLASS(ILogSink, FACTORY_BASE)

    public:
        ~ILogSink() override = default;

        /// Connects the sink to LogEvent. Called when the entry is deserialized and again by LoggerManager::Initialize, which fixes the order of delivery by
        /// disconnecting every sink and connecting them again in order: an override must connect (after UnsubscribeAll()) on every call and keep any one-time setup
        /// behind its own flag.
        virtual void Initialize() = 0;

        /// Drops every connection of the sink; LoggerManager::Initialize does it for all sinks before it connects them in order.
        void Disconnect() {
            UnsubscribeAll();
        }

        virtual void Write(LogLevel level, StringView message) = 0;

        void SetMinLevel(LogLevel level) {
            _minLevel = level;
        }

        LogLevel GetMinLevel() const {
            return _minLevel;
        }

        bool ShouldLog(LogLevel level) const {
            return static_cast<int>(level) >= static_cast<int>(_minLevel);
        }

        /// Lower receives a line first; sinks of equal priority are served in config order.
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
