#include "SceneControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlProperty.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControlAnswers.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Events/SceneEvents.h>

namespace BECore {

    namespace {

        /// The serialized name of IComponent::_enabled
        constexpr StringView kEnabledPath = "enabled";

        /// What the handlers of one registration share
        struct SceneSource : RefCounted {
            explicit SceneSource(SceneProvider provider) : find(std::move(provider)) {}

            SceneProvider find;
        };

        std::unexpected<ControlError> Refused(StringView code, String message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
        }

        std::expected<Scene*, ControlError> RequireScene(SceneSource& source) {
            Scene* scene = source.find ? source.find() : nullptr;
            if (scene == nullptr) {
                return Refused(SceneControlErrors::kNoScene, "there is no active scene");
            }
            return scene;
        }

        std::expected<SceneNode*, ControlError> RequireNode(Scene& scene, StringView path) {
            const auto found = FindSceneNodeByPath(scene.GetRootNode(), path);
            if (!found) {
                return Refused(ControlErrors::kInvalidParams, Format("'node': {}", found.error()));
            }
            if (*found == nullptr) {
                return Refused(SceneControlErrors::kNodeNotFound, Format("there is no node '{}' in scene '{}'", ShownClientText(path), scene.GetName().ToStringView()));
            }
            return *found;
        }

        /// The integer member @p key from @p smallest to @p largest, @p fallback when the member is absent
        std::expected<size_t, ControlError> ReadCount(JsonView params, StringView key, size_t fallback, size_t smallest, size_t largest) {
            if (!FindParam(params, key)) {
                return fallback;
            }
            const Optional<int64_t> number = GetParamInt(params, key);
            if (!number || *number < static_cast<int64_t>(smallest) || *number > static_cast<int64_t>(largest)) {
                return Refused(ControlErrors::kInvalidParams, Format("'{}' has to be an integer from {} to {}", key, smallest, largest));
            }
            return static_cast<size_t>(*number);
        }

        /// The component a request names: its node's `component`-th one of that type (`index`, 0 by default)
        struct ComponentRequest {
            String node;
            String component;
            size_t index = 0;
        };

        std::expected<ComponentRequest, ControlError> ReadComponentRequest(JsonView params) {
            auto node = RequireParamString(params, "node");
            if (!node) {
                return std::unexpected(std::move(node.error()));
            }
            auto component = RequireParamString(params, "component");
            if (!component) {
                return std::unexpected(std::move(component.error()));
            }
            const auto index = ReadCount(params, "index", 0, 0, Scene::kMaxComponentsPerNode - 1);
            if (!index) {
                return std::unexpected(index.error());
            }
            return ComponentRequest{std::move(*node), std::move(*component), *index};
        }

        struct ComponentTarget {
            Scene* scene = nullptr;
            SceneNode* node = nullptr;
            IComponent* component = nullptr;
        };

        std::expected<ComponentTarget, ControlError> RequireComponent(SceneSource& source, const ComponentRequest& request) {
            const auto scene = RequireScene(source);
            if (!scene) {
                return std::unexpected(scene.error());
            }
            const auto node = RequireNode(**scene, request.node);
            if (!node) {
                return std::unexpected(node.error());
            }
            size_t left = request.index;
            for (const auto& component : (*node)->GetComponents()) {
                if (component->GetTypeMeta().typeName == StringView(request.component) && left-- == 0) {
                    return ComponentTarget{*scene, *node, component.Get()};
                }
            }
            return Refused(SceneControlErrors::kComponentNotFound,
                           Format("node '{}' has no component '{}' at index {}", ShownClientText(request.node), ShownClientText(request.component), request.index));
        }

        /// The nodes of a subtree as `scene_tree` lists them
        class TreeListing {
        public:
            TreeListing(SceneTreeAnswer& answer, size_t limit) : _answer(answer), _limit(limit) {}

            /// Adds @p node, whose segment ends _path and which is @p depth segments long, and then its descendants
            void Add(const SceneNode& node, size_t depth) {
                if (depth > SceneNodePathLimits::kMaxSegments || _path.size() > SceneNodePathLimits::kMaxPathBytes) {
                    _answer.truncated = true;  // a path that could not be read back is not listed
                    return;
                }
                SceneTreeNodeAnswer entry;
                entry.components.reserve(node.GetComponents().size());
                for (const auto& component : node.GetComponents()) {
                    entry.components.push_back(TextView(component->GetTypeMeta().typeName));
                }
                entry.layer = node.GetLayer();
                entry.name = TextView(node.GetName().ToStringView());
                entry.path = ArenaString(_path.data(), _path.size());
                _answer.nodes.push_back(std::move(entry));
                AddChildren(node, depth, true);
            }

            void AddChildren(const SceneNode& node, size_t depth, bool separate) {
                for (const auto& child : node.GetChildren()) {
                    if (_answer.nodes.size() >= _limit) {
                        _answer.truncated = true;
                        return;
                    }
                    const size_t length = _path.size();
                    if (separate) {
                        _path.push_back('/');
                    }
                    AppendSceneNodeSegment(_path, *child);
                    Add(*child, depth + 1);
                    _path.resize(length);
                }
            }

            /// Lists @p start and its descendants
            void AddFrom(const SceneNode& start) {
                AppendSceneNodePath(_path, start);
                size_t depth = 0;
                for (const SceneNode* node = &start; node->GetParent() != nullptr; node = node->GetParent()) {
                    ++depth;
                }
                Add(start, depth);
            }

        private:
            SceneTreeAnswer& _answer;
            const size_t _limit;
            String _path;
        };

        /// The write of `scene_set`. The hidden field `enabled`, which the generic write refuses, goes through SetEnabled() and so through the component's hook
        std::expected<void, PropertyError> WriteComponentField(IComponent& component, StringView path, const PropertyValue& value) {
            if (path == kEnabledPath) {
                const bool* const enabled = std::get_if<bool>(&value);
                if (enabled == nullptr) {
                    return std::unexpected(PropertyError{PropertyErrorCode::type_mismatch, Format("'{}' takes true or false", kEnabledPath)});
                }
                component.SetEnabled(*enabled);
                return {};
            }
            // A written PoolString is interned and the pool never frees: only a text it holds already costs it nothing
            const String* const text = std::get_if<String>(&value);
            const SetOptions options{.allowInterning = text != nullptr && PoolString::Find(StringView(*text)).has_value()};
            return SetProperty(TypedRef::Of(component), path, value, options);
        }

        /// `{component, node, path, value}`. By hand: `value` is a `PropertyValue`, written as the bare scalar
        void WriteFieldAnswer(JsonWriter& writer, const ComponentRequest& request, StringView path, const PropertyValue& value) {
            writer.BeginObject();
            writer.Member("component", StringView(request.component));
            writer.Member("node", StringView(request.node));
            writer.Member("path", path);
            writer.Key("value");
            WritePropertyValue(writer, value);
            writer.EndObject();
        }

        void HandleSceneTree(ControlCall& call, SceneSource& source) {
            const auto limit = ReadCount(call.Params(), "limit", SceneControlLimits::kDefaultTreeNodes, 1, SceneControlLimits::kMaxTreeNodes);
            if (!limit) {
                call.Fail(limit.error());
                return;
            }
            Optional<String> startPath;
            if (FindParam(call.Params(), "node")) {
                auto text = RequireParamString(call.Params(), "node");
                if (!text) {
                    call.Fail(text.error());
                    return;
                }
                startPath = std::move(*text);
            }
            const auto scene = RequireScene(source);
            if (!scene) {
                call.Fail(scene.error());
                return;
            }
            SceneTreeAnswer answer;
            answer.scene = TextView((*scene)->GetName().ToStringView());
            TreeListing listing(answer, *limit);
            if (startPath) {
                const auto start = RequireNode(**scene, *startPath);
                if (!start) {
                    call.Fail(start.error());
                    return;
                }
                listing.AddFrom(**start);
            } else {
                listing.AddChildren((*scene)->GetRootNode(), 0, false);
            }
            call.ReplyObject(answer);
        }

        void HandleSceneGet(ControlCall& call, SceneSource& source) {
            const auto request = ReadComponentRequest(call.Params());
            if (!request) {
                call.Fail(request.error());
                return;
            }
            Optional<String> path;
            if (FindParam(call.Params(), "path")) {
                auto text = RequireParamString(call.Params(), "path");
                if (!text) {
                    call.Fail(text.error());
                    return;
                }
                path = std::move(*text);
            }
            const auto target = RequireComponent(source, *request);
            if (!target) {
                call.Fail(target.error());
                return;
            }
            if (!path) {
                call.Reply([&](JsonWriter& writer) {
                    writer.BeginObject();
                    writer.Member("component", StringView(request->component));
                    writer.Key("fields");
                    target->component->WriteJson(writer);
                    writer.Member("node", StringView(request->node));
                    writer.EndObject();
                });
                return;
            }
            const auto value = GetProperty(TypedRef::Of(*target->component), *path);
            if (!value) {
                call.Fail(MakePropertyControlError(value.error()));
                return;
            }
            call.Reply([&](JsonWriter& writer) { WriteFieldAnswer(writer, *request, *path, *value); });
        }

        void HandleSceneSet(ControlCall& call, ControlService& service, SceneSource& source) {
            const auto request = ReadComponentRequest(call.Params());
            if (!request) {
                call.Fail(request.error());
                return;
            }
            const auto path = RequireParamString(call.Params(), "path");
            if (!path) {
                call.Fail(path.error());
                return;
            }
            const auto value = ReadPropertyValue(call.Params());
            if (!value) {
                call.Fail(value.error());
                return;
            }
            const auto target = RequireComponent(source, *request);
            if (!target) {
                call.Fail(target.error());
                return;
            }
            if (const auto written = WriteComponentField(*target->component, *path, *value); !written) {
                if (written.error().code == PropertyErrorCode::interning) {
                    call.Fail(MakePropertyControlError({PropertyErrorCode::interning, "the field takes only a text the program already knows, so that a client cannot grow the string pool"}));
                } else {
                    call.Fail(MakePropertyControlError(written.error()));
                }
                return;
            }
            // A path starts with the name of a field of the component: what a write succeeded through
            const StringView field = StringView(*path).substr(0, StringView(*path).find_first_of(".["));
            target->component->OnPropertyWritten(field);
            // The answer is what the field holds now, which the component may have changed
            const auto readBack = GetProperty(TypedRef::Of(*target->component), *path);
            if (!readBack) {
                call.Fail(ControlErrors::kInternalError, Format("the value was written but cannot be read back: {}", readBack.error().message));
                return;
            }
            ReportNodeTouched(service, *target->node, target->component->GetTypeMeta().typeName, field);
            SceneEvents::SceneNodeEditedEvent::Emit(target->scene, target->node);
            call.Reply([&](JsonWriter& writer) { WriteFieldAnswer(writer, *request, *path, *readBack); });
        }

    }  // namespace

    void RegisterSceneControl(ControlService& service, SceneProvider activeScene) {
        const IntrusivePtr<SceneSource> source = New<SceneSource>(std::move(activeScene));
        service.RegisterHandler("scene_tree", [source](ControlCall& call) { HandleSceneTree(call, *source); });
        service.RegisterHandler("scene_get", [source](ControlCall& call) { HandleSceneGet(call, *source); });
        service.RegisterHandler("scene_set", [&service, source](ControlCall& call) { HandleSceneSet(call, service, *source); });

        const ControlParamInfo node{"node", "string", true, "The path of the node: names below the scene root joined by '/', '[n]' for a later namesake"};
        const ControlParamInfo component{"component", "string", true, "The type name of a component of the node"};
        const ControlParamInfo index{"index", "integer", false, "Which component of that type, from 0"};
        service.DescribeMethod("scene_tree", {"The nodes of the active scene with their paths, layers and component types",
                                              {{"node", "string", false, "The path of a node: only it and what is below it"},
                                               {"limit", "integer", false, Format("The most nodes to list, {} by default", SceneControlLimits::kDefaultTreeNodes)}},
                                              /*quiet=*/true});
        service.DescribeMethod("scene_get", {"The fields of a component of a node, or one of them by its path",
                                             {node, component, index, {"path", "string", false, "The field, as in get_property; all fields without it"}},
                                             /*quiet=*/true});
        service.DescribeMethod("scene_set", {"Writes a field of a component of a node, as an edit in the inspector does",
                                             {node,
                                              component,
                                              index,
                                              {"path", "string", true, "The field, as in set_property"},
                                              {"value", "any", true, "A boolean, a number or a string; a name field takes only a text the program knows"}}});
    }

    void ReportNodeTouched(ControlService& service, const SceneNode& node, StringView component, StringView field) {
        service.ReportTouch(node.GetRuntimeId(), component, field);
    }

    void ReportNodeTouched(const SceneNode& node, StringView component, StringView field) {
        ReportNodeTouched(CoreManager::GetControlService(), node, component, field);
    }

}  // namespace BECore
