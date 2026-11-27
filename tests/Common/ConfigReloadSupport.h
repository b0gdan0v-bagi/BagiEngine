#pragma once

#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FakeFileWatchBackend.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Types/Types.h>
#include <Common/EngineEnvironment.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    /// A time that is surely past the settle window of a post made at @p postedAt
    constexpr double After(double postedAt) {
        return postedAt + 2.0 * kFileWatchSettleSeconds;
    }

    /// Records every `ConfigChangedEvent`; a test reads the fields it needs
    class ChangeRecorder : public SubscriptionHolder {
    public:
        ChangeRecorder() {
            Subscribe<ConfigEvents::ConfigChangedEvent, &ChangeRecorder::OnChanged>(this);
        }

        Vector<PoolString> names;
        Vector<ConfigEvents::ConfigChangeCause> causes;
        std::thread::id lastThread;

    private:
        void OnChanged(const ConfigEvents::ConfigChangedEvent& event) {
            names.push_back(event.name);
            causes.push_back(event.cause);
            lastThread = std::this_thread::get_id();
        }
    };

    /// Records every `ConfigReloadFailedEvent`
    class FailureRecorder : public SubscriptionHolder {
    public:
        FailureRecorder() {
            Subscribe<ConfigEvents::ConfigReloadFailedEvent, &FailureRecorder::OnFailed>(this);
        }

        Vector<PoolString> names;
        Vector<String> reasons;
        std::thread::id lastThread;

    private:
        void OnFailed(const ConfigEvents::ConfigReloadFailedEvent& event) {
            names.push_back(event.name);
            reasons.push_back(event.reason);
            lastThread = std::this_thread::get_id();
        }
    };

    /**
     * A manager and a watcher of their own over a `config` folder in a temporary directory: the engine's own configs are never written. The
     * watcher runs on a fake backend (a post is an `Emit`, the clock is a parameter) unless a case says otherwise. A test derives, names its
     * temp folder and writes the files the manager loads.
     */
    class ConfigReloadFixture : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        /// The prefix of the temp folder (`TestTempDirectory`): one per test file, so that two files never share a name
        virtual const char* TempPrefix() const = 0;

        /// Writes the files of the folder `_dir` before the manager loads it
        virtual void WriteConfigFiles() = 0;

        void SetUp() override {
            _dir = TestTempDirectory(TempPrefix());
            MakeEmptyDirectory(_dir);
            WriteConfigFiles();
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
        }

        void TearDown() override {
            _manager.StopWatching();
            _watcher.Stop();
            RemoveDirectory(_dir);
        }

        void StartOnFake() {
            _fake = New<FakeFileWatchBackend>();
            _watcher.Start(IntrusivePtr<IFileWatchBackend>(_fake));
            _manager.StartWatching(_watcher);
        }

        /// Posts @p path and lets the settle window pass
        void PostAndSettle(const Path& path) {
            Require(_fake).Emit(path);
            _watcher.Update(_now);
            _now = After(_now);
            _watcher.Update(_now);
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        FileWatcher _watcher{_fileSystem};
        IntrusivePtr<FakeFileWatchBackend> _fake;
        double _now = 0.0;
    };

}  // namespace BECore::Tests
