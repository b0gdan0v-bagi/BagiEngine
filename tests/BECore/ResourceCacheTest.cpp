#include <BECore/Resource/IResource.h>
#include <BECore/Resource/ResourceCache.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        class FakeResource : public IResource {
        public:
            explicit FakeResource(PoolString path) : _path(path) {}

            ResourceState GetState() const override {
                return ResourceState::Loaded;
            }
            PoolString GetPath() const override {
                return _path;
            }
            uint64_t GetMemoryUsage() const override {
                return 1;
            }
            PoolString GetTypeName() const override {
                return "FakeResource"_intern;
            }

        private:
            PoolString _path;
        };

    }  // namespace

    TEST(ResourceCacheTest, RemoveDropsOnePathAndKeepsTheOthers) {
        ResourceCache cache;
        const auto a = "cache_test/a.png"_intern;
        const auto b = "cache_test/b.png"_intern;
        cache.Put(a, New<FakeResource>(a));
        cache.Put(b, New<FakeResource>(b));

        EXPECT_TRUE(cache.Remove(a));
        EXPECT_FALSE(cache.Contains(a));
        EXPECT_TRUE(cache.Contains(b));
        EXPECT_EQ(cache.GetCount(), 1u);
        EXPECT_FALSE(cache.Remove(a));
    }

    TEST(ResourceCacheTest, AHandleGivenOutOutlivesItsRemoval) {
        ResourceCache cache;
        const auto path = "cache_test/held.png"_intern;
        cache.Put(path, New<FakeResource>(path));
        IntrusivePtr<IResource> held = cache.Get(path);
        cache.Remove(path);
        ASSERT_TRUE(held);
        EXPECT_EQ(held->GetPath(), path);
        EXPECT_FALSE(cache.Get(path));
    }

}  // namespace BECore::Tests
