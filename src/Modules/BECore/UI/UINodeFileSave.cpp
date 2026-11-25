#include "UINodeFileSave.h"

#include <BECore/Config/XmlTextPatch.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIRectFilePatch.h>

namespace BECore {

    namespace {

        constexpr StringView kCanvasTypeName = "UICanvasComponent";

        /// The canvases of @p node in the order of the node's components: their position here is the ordinal of the `<component>` in the node file
        Vector<const UICanvasComponent*> CanvasesOf(const SceneNode& node) {
            Vector<const UICanvasComponent*> canvases;
            for (const auto& component : node.GetComponents()) {
                if (component->GetTypeMeta().typeName == kCanvasTypeName) {
                    canvases.push_back(static_cast<const UICanvasComponent*>(component.Get()));
                }
            }
            return canvases;
        }

        bool TreeHolds(const UIElement& parent, const UIElement& target) {
            for (const auto& child : parent.children) {
                if (child.Get() == &target || TreeHolds(*child, target)) {
                    return true;
                }
            }
            return false;
        }

        String ElementLabel(const SceneNode& node, const UIElement& element) {
            return Format("UI '{}/{}'", node.GetName().ToStringView(), element.name.ToStringView());
        }

        size_t CountEdited(const SceneNode& node, Span<const IntrusivePtr<UIElement>> edited) {
            size_t count = 0;
            for (const UICanvasComponent* canvas : CanvasesOf(node)) {
                for (const auto& element : edited) {
                    count += TreeHolds(*canvas->GetLayout().root, *element) ? 1 : 0;
                }
            }
            return count;
        }

        bool HoldsEditedElement(const SceneNode& node, Span<const IntrusivePtr<UIElement>> edited) {
            return CountEdited(node, edited) > 0;
        }

        void CollectNodes(SceneNode& node, Vector<SceneNode*>& out) {
            for (const auto& child : node.GetChildren()) {
                out.push_back(child.Get());
                CollectNodes(*child, out);
            }
        }

        size_t CountWithSourceRef(const Vector<SceneNode*>& nodes, PoolString ref) {
            size_t count = 0;
            for (const SceneNode* node : nodes) {
                count += node->GetSourceRef() == ref ? 1 : 0;
            }
            return count;
        }

    }  // namespace

    bool PatchNodeFileText(StringView fileText, const SceneNode& node, Span<const IntrusivePtr<UIElement>> edited, String& patched, size_t& elementsPatched, Vector<String>& notes) {
        patched = String(fileText);
        elementsPatched = 0;
        const String ref(node.GetSourceRef().ToStringView());

        pugi::xml_document doc;
        String error;
        if (!XmlTextPatch::Parse(fileText, doc, error)) {
            notes.push_back(Format("UI '{}': not written to {}: {}", node.GetName().ToStringView(), ref, error));
            return false;
        }
        const pugi::xml_node nodeEl = doc.document_element();

        Vector<XmlTextPatch::ElementEdit> edits;
        Vector<const UIElement*> seen;
        const Vector<const UICanvasComponent*> canvases = CanvasesOf(node);
        for (uint32_t ordinal = 0; ordinal < canvases.size(); ++ordinal) {
            const UICanvasComponent& canvas = *canvases[ordinal];
            for (const auto& element : edited) {
                if (!TreeHolds(*canvas.GetLayout().root, *element) || eastl::find(seen.begin(), seen.end(), element.Get()) != seen.end()) {
                    continue;
                }
                seen.push_back(element.Get());

                String why;
                const auto path = UIRectFilePatch::FindAuthoredPath(canvas, *element);
                if (!path) {
                    notes.push_back(Format("{}: not written to {}: the element was added in code, the file does not hold it", ElementLabel(node, *element), ref));
                    continue;
                }
                const pugi::xml_node fileElement = UIRectFilePatch::FindFileElement(nodeEl, ordinal, *path, *element, why);
                if (!fileElement) {
                    notes.push_back(Format("{}: not written to {}: {}", ElementLabel(node, *element), ref, why));
                    continue;
                }
                Vector<XmlTextPatch::AttributeEdit> attributes = UIRectFilePatch::RectAttributeEdits(fileElement, element->style.rect);
                if (!attributes.empty()) {
                    edits.push_back({fileElement, eastl::move(attributes)});
                }
            }
        }

        if (edits.empty()) {
            return false;
        }
        String result;
        if (!XmlTextPatch::Apply(fileText, edits, result, error)) {
            notes.push_back(Format("UI '{}': not written to {}: {}", node.GetName().ToStringView(), ref, error));
            return false;
        }
        elementsPatched = edits.size();
        patched = eastl::move(result);
        return patched != fileText;
    }

    size_t CountEditedElements(const SceneNode& node, Span<const IntrusivePtr<UIElement>> edited) {
        return CountEdited(node, edited);
    }

    size_t CountNodeInstances(Scene& scene, PoolString ref) {
        Vector<SceneNode*> nodes;
        CollectNodes(scene.GetRootNode(), nodes);
        return CountWithSourceRef(nodes, ref);
    }

    Vector<PoolString> NodeFilesToPatch(Scene& scene, Span<const IntrusivePtr<UIElement>> edited) {
        Vector<SceneNode*> nodes;
        CollectNodes(scene.GetRootNode(), nodes);
        Vector<PoolString> refs;
        for (const SceneNode* node : nodes) {
            const PoolString ref = node->GetSourceRef();
            if (ref.Empty() || !HoldsEditedElement(*node, edited) || eastl::find(refs.begin(), refs.end(), ref) != refs.end()) {
                continue;
            }
            if (CountWithSourceRef(nodes, ref) == 1 && IsValidNodeRef(ref.ToStringView())) {
                refs.push_back(ref);
            }
        }
        return refs;
    }

    UINodeFileReport SaveUIRectsToNodeFiles(Scene& scene, Span<const IntrusivePtr<UIElement>> edited, const UINodeFileIo& io) {
        UINodeFileReport report;
        Vector<SceneNode*> nodes;
        CollectNodes(scene.GetRootNode(), nodes);

        Vector<PoolString> handled;
        for (const SceneNode* node : nodes) {
            const PoolString ref = node->GetSourceRef();
            if (ref.Empty() || !HoldsEditedElement(*node, edited) || eastl::find(handled.begin(), handled.end(), ref) != handled.end()) {
                continue;
            }
            handled.push_back(ref);
            const String refText(ref.ToStringView());

            const size_t instances = CountWithSourceRef(nodes, ref);
            if (instances > 1) {
                report.notes.push_back(
                    Format("UI '{}': not written to {}: the file has {} instances in the scene, so the edit stays an override in the scene file", node->GetName().ToStringView(), refText, instances));
                continue;
            }
            if (!IsValidNodeRef(ref.ToStringView())) {
                report.notes.push_back(Format("UI '{}': not written to {}: not a node file reference", node->GetName().ToStringView(), refText));
                continue;
            }

            String text;
            String error;
            if (!io.read(ref.ToStringView(), text, error)) {
                report.notes.push_back(Format("UI '{}': not written to {}: {}", node->GetName().ToStringView(), refText, error));
                continue;
            }
            String patched;
            size_t patchedElements = 0;
            if (!PatchNodeFileText(text, *node, edited, patched, patchedElements, report.notes)) {
                continue;
            }
            if (!io.write(ref.ToStringView(), patched, error)) {
                report.notes.push_back(Format("UI '{}': not written to {}: {}", node->GetName().ToStringView(), refText, error));
                continue;
            }
            ++report.filesWritten;
            report.elementsPatched += patchedElements;
        }
        return report;
    }

}  // namespace BECore
