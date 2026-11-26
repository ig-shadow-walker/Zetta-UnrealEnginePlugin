// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaUserSettingsDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Zetta3DEditorModule.h"
#include "ZettaSecureStore.h"
#include "ZettaSettings.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

void FZettaUserSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Shown first: it's what people come to these preferences for.
	IDetailCategoryBuilder& Agent = DetailBuilder.EditCategory(TEXT("Zetta Agent"), LOCTEXT("AgentCategory", "Zetta Agent — what it runs on"), ECategoryPriority::Important);

	Agent.AddCustomRow(LOCTEXT("AgentRunsOnSearch", "Zetta Agent own API key provider"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([]()
				{
					return UZettaUserSettings::Get()->bAgentUseOwnKeys
						? LOCTEXT("RunsOnOwn", "The agent runs on your own API keys. What it uses on a key is billed by that provider, not in Zetta credits; making 3D models and pictures still uses Zetta credits.")
						: LOCTEXT("RunsOnZetta", "The agent runs on your Zetta account. You can run it on your own Gemini, OpenAI, DeepSeek, Claude, GLM or Meta API key instead, on any plan.");
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::Format(LOCTEXT("KeysWhere", "Keys are kept in {0}, never in these settings or your project."), ZettaSecureStore::Describe()))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 6)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenOwnKeys", "Your own API keys…"))
				.ToolTipText(LOCTEXT("OpenOwnKeysTip", "Opens the Zetta 3D window on the agent's settings: choose your Zetta account or your own keys, and add a key per provider"))
				.OnClicked_Lambda([]()
				{
					FZetta3DEditorModule::Get().OpenAgentSettings();
					return FReply::Handled();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
