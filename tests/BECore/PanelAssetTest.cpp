#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Widgets/PanelAssets/PanelAsset.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Every kind of element once, a table with each column type, and a nested container of each kind
        constexpr const char* kSampleXml = R"(
<PanelAsset id="Sample" title="Sample" titleBind="files.title" command="view.sample" open="true" window="Floating" width="640" height="320">
  <sources>
    <source name="files" model="FileChangesPanelModel"/>
  </sources>
  <elements>
    <element type="PanelEmptyState" id="idle" icon="file-diff" text="Nothing to show" visible="!files.running"/>
    <element type="PanelSection" id="section" title="Details" defaultOpen="false" tooltip="A section">
      <elements>
        <element type="PanelRow" id="toolbar">
          <elements>
            <element type="PanelButton" id="rescan" text="Rescan" icon="refresh-cw" command="fileChanges.rescan" argument="all files" width="9" enableWhen="files.hasPending"/>
            <element type="PanelInput" id="filter" bind="files.filter" hint="Filter" icon="search" width="12.5" focus="true" submitCommand="files.apply" commitCommand="files.save"/>
            <element type="PanelCheckbox" id="errors" bind="files.showErrors" text="Errors" labelBind="files.errorsText" look="Toggle" style="Error"/>
            <element type="PanelCombo" id="category" bind="files.category" items="files.categories" itemField="name" width="11"/>
            <element type="PanelNumber" id="budget" bind="files.budget" text="ms" min="-1.5" max="100" step="0.25" slider="true" width="8" commitCommand="files.save"/>
          </elements>
        </element>
        <element type="PanelText" id="status" bind="files.status" style="Warning" wrap="true" mono="true" maxLines="12"/>
        <element type="PanelText" id="caption" text="Pending changes" style="Disabled"/>
        <element type="PanelSeparator" id="line" title="Fields"/>
        <element type="PanelField" id="one" bind="files.count" label="Count"/>
        <element type="PanelFields" id="all" bind="files"/>
        <element type="PanelSlot" id="custom" name="DiffView"/>
        <element type="PanelChild" id="pane" width="14.5" height="8" border="true">
          <elements>
            <element type="PanelText" id="inPane" text="In a pane"/>
          </elements>
        </element>
      </elements>
    </element>
    <element type="PanelTabs" id="tabs">
      <elements>
        <element type="PanelTab" id="first" title="First">
          <elements>
            <element type="PanelText" id="inTab" text="In a tab"/>
          </elements>
        </element>
        <element type="PanelTab" id="second" title="Second"/>
      </elements>
    </element>
    <element type="PanelPopup" id="confirm" title="Confirm" open="!files.running" closeCommand="files.closeConfirm">
      <elements>
        <element type="PanelText" id="inPopup" text="Sure?"/>
      </elements>
    </element>
    <element type="PanelTable" id="pending" source="files.pending" header="false" borders="true" maxRows="20" scrollRows="10" visible="files.hasPending">
      <columns>
        <column id="path" type="Text" title="Path" field="path"/>
        <column id="pick" type="Selectable" field="name" selectedField="chosen"/>
        <column id="apply" type="Button" icon="check" tooltip="Apply this change" command="fileChanges.apply" argument="path"/>
        <column id="call" type="Button" field="label" command="fileChanges.apply" argument="index" enableField="callable"/>
        <column id="args" type="Input" field="arguments" hint="name=value" enableField="hasParams"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        IntrusivePtr<PanelAsset> Read(const String& xml, XmlDeserializer& deserializer) {
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            auto asset = New<PanelAsset>();
            asset->Deserialize(deserializer);
            return asset;
        }

        /// The serializer's own root element holds the asset's attributes, as a file's root element does for the reader
        String Write(const PanelAsset& asset) {
            XmlSerializer serializer;
            asset.Serialize(serializer);
            return serializer.SaveToString();
        }

        template <typename T>
        const T* ElementAs(const IntrusivePtr<PanelElement>& element) {
            return element ? element->Cast<T>() : nullptr;
        }

    }  // namespace

    TEST(PanelAssetTest, RoundTrip) {
        XmlDeserializer first;
        const auto asset = Read(kSampleXml, first);
        ASSERT_FALSE(first.HasErrors());

        const String written = Write(*asset);
        XmlDeserializer second;
        const auto again = Read(written, second);

        EXPECT_FALSE(second.HasErrors());
        EXPECT_EQ(Write(*again), written);
        EXPECT_EQ(again->id, "Sample");
        EXPECT_TRUE(again->open);
        EXPECT_EQ(again->window, PanelWindowKind::Floating);
        EXPECT_EQ(again->titleBind, "files.title");
        EXPECT_FLOAT_EQ(again->width, 640.0f);
        ASSERT_EQ(again->sources.size(), 1u);
        EXPECT_EQ(again->sources[0].model, "FileChangesPanelModel");

        ASSERT_EQ(again->elements.size(), 5u);
        const auto* section = ElementAs<PanelSection>(again->elements[1]);
        ASSERT_NE(section, nullptr);
        EXPECT_FALSE(section->defaultOpen);
        ASSERT_EQ(section->elements.size(), 8u);
        const auto* row = ElementAs<PanelRow>(section->elements[0]);
        ASSERT_NE(row, nullptr);
        ASSERT_EQ(row->elements.size(), 5u);
        const auto* rescan = ElementAs<PanelButton>(row->elements[0]);
        ASSERT_NE(rescan, nullptr);
        EXPECT_EQ(rescan->argument, "all files");
        EXPECT_FLOAT_EQ(rescan->width, 9.0f);
        const auto* input = ElementAs<PanelInput>(row->elements[1]);
        ASSERT_NE(input, nullptr);
        EXPECT_FLOAT_EQ(input->width, 12.5f);
        EXPECT_TRUE(input->focus);
        EXPECT_EQ(input->submitCommand, "files.apply");
        EXPECT_EQ(input->commitCommand, "files.save");
        const auto* checkbox = ElementAs<PanelCheckbox>(row->elements[2]);
        ASSERT_NE(checkbox, nullptr);
        EXPECT_EQ(checkbox->bind, "files.showErrors");
        EXPECT_EQ(checkbox->text, "Errors");
        EXPECT_EQ(checkbox->labelBind, "files.errorsText");
        EXPECT_EQ(checkbox->look, PanelCheckLook::Toggle);
        EXPECT_EQ(checkbox->style, PanelTextStyle::Error);
        const auto* combo = ElementAs<PanelCombo>(row->elements[3]);
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->items, "files.categories");
        EXPECT_EQ(combo->itemField, "name");
        EXPECT_FLOAT_EQ(combo->width, 11.0f);
        const auto* number = ElementAs<PanelNumber>(row->elements[4]);
        ASSERT_NE(number, nullptr);
        EXPECT_EQ(number->text, "ms");
        EXPECT_FLOAT_EQ(number->min, -1.5f);
        EXPECT_FLOAT_EQ(number->max, 100.0f);
        EXPECT_FLOAT_EQ(number->step, 0.25f);
        EXPECT_TRUE(number->slider);
        EXPECT_FLOAT_EQ(number->width, 8.0f);
        EXPECT_EQ(number->commitCommand, "files.save");
        const auto* status = ElementAs<PanelText>(section->elements[1]);
        ASSERT_NE(status, nullptr);
        EXPECT_EQ(status->style, PanelTextStyle::Warning);
        EXPECT_TRUE(status->mono);
        EXPECT_EQ(status->maxLines, 12u);
        const auto* all = ElementAs<PanelFields>(section->elements[5]);
        ASSERT_NE(all, nullptr);
        EXPECT_EQ(all->bind, "files");

        const auto* pane = ElementAs<PanelChild>(section->elements[7]);
        ASSERT_NE(pane, nullptr);
        EXPECT_FLOAT_EQ(pane->width, 14.5f);
        EXPECT_FLOAT_EQ(pane->height, 8.0f);
        EXPECT_TRUE(pane->border);
        ASSERT_EQ(pane->elements.size(), 1u);
        const auto* tabs = ElementAs<PanelTabs>(again->elements[2]);
        ASSERT_NE(tabs, nullptr);
        ASSERT_EQ(tabs->elements.size(), 2u);
        const auto* firstTab = ElementAs<PanelTab>(tabs->elements[0]);
        ASSERT_NE(firstTab, nullptr);
        EXPECT_EQ(firstTab->title, "First");
        EXPECT_EQ(firstTab->elements.size(), 1u);
        const auto* popup = ElementAs<PanelPopup>(again->elements[3]);
        ASSERT_NE(popup, nullptr);
        EXPECT_EQ(popup->title, "Confirm");
        EXPECT_EQ(popup->open, "!files.running");
        EXPECT_EQ(popup->closeCommand, "files.closeConfirm");
        ASSERT_EQ(popup->elements.size(), 1u);

        const auto* table = ElementAs<PanelTable>(again->elements[4]);
        ASSERT_NE(table, nullptr);
        EXPECT_FALSE(table->header);
        EXPECT_EQ(table->maxRows, 20u);
        EXPECT_EQ(table->scrollRows, 10u);
        ASSERT_EQ(table->columns.size(), 5u);
        EXPECT_EQ(table->columns[1].type, PanelColumnType::Selectable);
        EXPECT_EQ(table->columns[1].selectedField, "chosen");
        EXPECT_EQ(table->columns[2].argument, "path");
        EXPECT_EQ(table->columns[3].field, "label");
        EXPECT_EQ(table->columns[3].enableField, "callable");
        EXPECT_EQ(table->columns[4].type, PanelColumnType::Input);
        EXPECT_EQ(table->columns[4].hint, "name=value");
        EXPECT_EQ(table->columns[4].enableField, "hasParams");
    }

    TEST(PanelAssetTest, EveryKindIsMadeByItsClassName) {
        for (const char* name : {"PanelSection", "PanelRow", "PanelText", "PanelField", "PanelFields", "PanelInput", "PanelCheckbox", "PanelCombo", "PanelNumber", "PanelButton", "PanelTable",
                                 "PanelSeparator", "PanelEmptyState", "PanelSlot", "PanelChild", "PanelPopup", "PanelTabs", "PanelTab"}) {
            const IntrusivePtr<PanelElement> element = AbstractFactory<PanelElement>::GetInstance().Create(StringView{name});
            ASSERT_NE(element, nullptr) << name;
            EXPECT_EQ(element->GetTypeMeta().typeName, StringView{name});
        }
    }

    TEST(PanelAssetTest, TheWindowKindDefaultsToDockedAndNamesItsValues) {
        XmlDeserializer deserializer;
        const auto asset = Read(R"(<PanelAsset id="A"/>)", deserializer);
        EXPECT_EQ(asset->window, PanelWindowKind::Docked);

        for (const char* name : {"Docked", "Floating", "Dialogs"}) {
            XmlDeserializer reader;
            const auto named = Read(Format(R"(<PanelAsset id="A" window="{}"/>)", name), reader);
            EXPECT_FALSE(reader.HasErrors()) << name;
            EXPECT_EQ(EnumUtils<PanelWindowKind>::ToString(named->window), StringView(name));
        }
    }

    TEST(PanelAssetTest, UnknownTypeIsReportedAndLeavesAPlainElement) {
        XmlDeserializer deserializer;
        const auto asset = Read(R"(<PanelAsset id="A"><elements>
                                     <element type="PanelHologram" id="x"/>
                                     <element id="y"/>
                                   </elements></PanelAsset>)",
                                deserializer);

        EXPECT_TRUE(deserializer.HasErrors());
        ASSERT_EQ(asset->elements.size(), 2u);
        for (const auto& element : asset->elements) {
            ASSERT_NE(element, nullptr);
            EXPECT_TRUE(element->Is<PanelElement>());
        }
    }

}  // namespace BECore::Tests
