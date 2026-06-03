#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Sequencer / Level Sequence-related MCP commands.
 */
class FUnrealAISequencerCommands
{
public:
	FUnrealAISequencerCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleCreateLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadLevelSequenceContent(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddCameraCutTrackToLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddMasterTrackToLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddSectionToMasterTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSectionRangeInMasterTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveSectionFromMasterTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddActorPossessableToLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddTrackToBindingInLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddFloatKeyToBindingTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetLevelSequencePlaybackRange(const TSharedPtr<FJsonObject>& Params);
};