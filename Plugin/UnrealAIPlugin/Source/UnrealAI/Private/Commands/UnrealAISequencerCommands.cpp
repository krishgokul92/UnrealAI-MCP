#include "Commands/UnrealAISequencerCommands.h"

#include "AssetToolsModule.h"
#include "Commands/UnrealAICommonUtils.h"
#include "EditorAssetLibrary.h"
#include "Factories/Factory.h"
#include "LevelSequence.h"
#include "Misc/FrameRate.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"

namespace
{
	bool TryGetTrimmedStringParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, FString& OutValue)
	{
		if (!Params->TryGetStringField(FieldName, OutValue))
		{
			return false;
		}

		OutValue = OutValue.TrimStartAndEnd();
		return !OutValue.IsEmpty();
	}

	bool TryGetInt32Param(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, int32& OutValue)
	{
		double NumericValue = 0.0;
		if (!Params->TryGetNumberField(FieldName, NumericValue))
		{
			return false;
		}

		if (NumericValue < static_cast<double>(MIN_int32) || NumericValue > static_cast<double>(MAX_int32))
		{
			return false;
		}

		const double RoundedValue = FMath::RoundToDouble(NumericValue);
		if (RoundedValue != NumericValue)
		{
			return false;
		}

		OutValue = static_cast<int32>(RoundedValue);
		return true;
	}

	FString SanitizePackageFolder(const FString& RequestedPath)
	{
		FString SanitizedPath = RequestedPath.TrimStartAndEnd();
		if (SanitizedPath.IsEmpty())
		{
			SanitizedPath = TEXT("/Game/Cinematics");
		}

		SanitizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (SanitizedPath.EndsWith(TEXT("/")))
		{
			SanitizedPath.LeftChopInline(1);
		}

		if (!SanitizedPath.StartsWith(TEXT("/")))
		{
			SanitizedPath = TEXT("/") + SanitizedPath;
		}

		return SanitizedPath;
	}

	TSharedPtr<FJsonObject> MakeFrameRateJson(const FFrameRate& FrameRate)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("numerator"), FrameRate.Numerator);
		Result->SetNumberField(TEXT("denominator"), FrameRate.Denominator);
		Result->SetNumberField(
			TEXT("fps"),
			FrameRate.Denominator != 0 ? static_cast<double>(FrameRate.Numerator) / static_cast<double>(FrameRate.Denominator) : 0.0);
		return Result;
	}

	TSharedPtr<FJsonObject> MakeFrameRangeJson(const TRange<FFrameNumber>& Range)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		const TRangeBound<FFrameNumber> LowerBound = Range.GetLowerBound();
		const bool bHasLowerBound = !LowerBound.IsOpen();
		Result->SetBoolField(TEXT("has_lower_bound"), bHasLowerBound);
		Result->SetBoolField(TEXT("lower_bound_inclusive"), bHasLowerBound ? LowerBound.IsInclusive() : false);
		if (bHasLowerBound)
		{
			Result->SetNumberField(TEXT("lower_bound_value"), LowerBound.GetValue().Value);
		}

		const TRangeBound<FFrameNumber> UpperBound = Range.GetUpperBound();
		const bool bHasUpperBound = !UpperBound.IsOpen();
		Result->SetBoolField(TEXT("has_upper_bound"), bHasUpperBound);
		Result->SetBoolField(TEXT("upper_bound_inclusive"), bHasUpperBound ? UpperBound.IsInclusive() : false);
		if (bHasUpperBound)
		{
			Result->SetNumberField(TEXT("upper_bound_value"), UpperBound.GetValue().Value);
		}

		return Result;
	}

	TSharedPtr<FJsonObject> SerializeSection(const UMovieSceneSection* Section)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Section->GetName());
		Result->SetStringField(TEXT("path"), Section->GetPathName());
		Result->SetStringField(TEXT("class"), Section->GetClass()->GetName());
		Result->SetNumberField(TEXT("row_index"), Section->GetRowIndex());
		Result->SetBoolField(TEXT("has_start_frame"), Section->HasStartFrame());
		Result->SetBoolField(TEXT("has_end_frame"), Section->HasEndFrame());
		if (Section->HasStartFrame())
		{
			Result->SetNumberField(TEXT("inclusive_start_frame"), Section->GetInclusiveStartFrame().Value);
		}
		if (Section->HasEndFrame())
		{
			Result->SetNumberField(TEXT("exclusive_end_frame"), Section->GetExclusiveEndFrame().Value);
		}
		Result->SetObjectField(TEXT("range"), MakeFrameRangeJson(Section->GetRange()));

		if (const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
		{
			const FMovieSceneFloatChannel& Channel = FloatSection->GetChannel();
			const TArrayView<const FFrameNumber> Times = Channel.GetTimes();
			const TArrayView<const FMovieSceneFloatValue> Values = Channel.GetValues();

			TArray<TSharedPtr<FJsonValue>> FloatKeyArray;
			const int32 KeyCount = FMath::Min(Times.Num(), Values.Num());
			FloatKeyArray.Reserve(KeyCount);
			for (int32 Index = 0; Index < KeyCount; ++Index)
			{
				TSharedPtr<FJsonObject> KeyJson = MakeShared<FJsonObject>();
				KeyJson->SetNumberField(TEXT("frame"), Times[Index].Value);
				KeyJson->SetNumberField(TEXT("value"), Values[Index].Value);
				FloatKeyArray.Add(MakeShared<FJsonValueObject>(KeyJson));
			}

			Result->SetArrayField(TEXT("float_keys"), FloatKeyArray);
			Result->SetNumberField(TEXT("float_key_count"), FloatKeyArray.Num());
		}
		return Result;
	}

	TSharedPtr<FJsonObject> SerializeTrack(const UMovieSceneTrack* Track)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Track->GetName());
		Result->SetStringField(TEXT("path"), Track->GetPathName());
		Result->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		Result->SetStringField(TEXT("display_name"), Track->GetDisplayName().ToString());

		if (const UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
		{
			Result->SetStringField(TEXT("property_name"), PropertyTrack->GetPropertyName().ToString());
			Result->SetStringField(TEXT("property_path"), PropertyTrack->GetPropertyPath().ToString());
		}

		const FGuid ObjectBindingGuid = Track->FindObjectBindingGuid();
		if (ObjectBindingGuid.IsValid())
		{
			Result->SetStringField(TEXT("object_binding_guid"), ObjectBindingGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}

		TArray<TSharedPtr<FJsonValue>> SectionArray;
		const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
		SectionArray.Reserve(Sections.Num());
		for (const UMovieSceneSection* Section : Sections)
		{
			if (Section)
			{
				SectionArray.Add(MakeShared<FJsonValueObject>(SerializeSection(Section)));
			}
		}

		Result->SetArrayField(TEXT("sections"), SectionArray);
		Result->SetNumberField(TEXT("section_count"), SectionArray.Num());
		return Result;
	}

	TSharedPtr<FJsonObject> SerializeBinding(const FMovieSceneBinding& Binding, UMovieScene* MovieScene)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		const FGuid BindingGuid = Binding.GetObjectGuid();
		Result->SetStringField(TEXT("guid"), BindingGuid.ToString(EGuidFormats::DigitsWithHyphens));

		FString BindingKind = TEXT("unknown");
		FString BindingName;
		FString ObjectClassName;
		FString ParentGuid;

		if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingGuid))
		{
			BindingKind = TEXT("spawnable");
			BindingName = Spawnable->GetName();
			if (const UObject* ObjectTemplate = Spawnable->GetObjectTemplate())
			{
				ObjectClassName = ObjectTemplate->GetClass()->GetName();
			}
		}
		else if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
		{
			BindingKind = TEXT("possessable");
			BindingName = Possessable->GetName();
			if (const UClass* PossessedObjectClass = Possessable->GetPossessedObjectClass())
			{
				ObjectClassName = PossessedObjectClass->GetName();
			}
			if (Possessable->GetParent().IsValid())
			{
				ParentGuid = Possessable->GetParent().ToString(EGuidFormats::DigitsWithHyphens);
			}
		}

		Result->SetStringField(TEXT("binding_kind"), BindingKind);
		Result->SetStringField(TEXT("name"), BindingName);
		Result->SetStringField(TEXT("object_class"), ObjectClassName);
		if (!ParentGuid.IsEmpty())
		{
			Result->SetStringField(TEXT("parent_guid"), ParentGuid);
		}

		TArray<TSharedPtr<FJsonValue>> TrackArray;
		const TArray<UMovieSceneTrack*>& Tracks = Binding.GetTracks();
		TrackArray.Reserve(Tracks.Num());
		for (const UMovieSceneTrack* Track : Tracks)
		{
			if (Track)
			{
				TrackArray.Add(MakeShared<FJsonValueObject>(SerializeTrack(Track)));
			}
		}

		Result->SetArrayField(TEXT("tracks"), TrackArray);
		Result->SetNumberField(TEXT("track_count"), TrackArray.Num());
		return Result;
	}

	ULevelSequence* LoadLevelSequenceAsset(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}

		return Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	}

	UClass* LoadMovieSceneTrackClass(const FString& RequestedClassPath, const TCHAR* DefaultClassPath)
	{
		FString ClassPath = RequestedClassPath.TrimStartAndEnd();
		if (ClassPath.IsEmpty())
		{
			ClassPath = DefaultClassPath;
		}

		return LoadClass<UMovieSceneTrack>(nullptr, *ClassPath);
	}

	AActor* FindActorByName(const FString& ActorName)
	{
		if (!GWorld)
		{
			return nullptr;
		}

		TArray<AActor*> AllActors;
		UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
		for (AActor* Actor : AllActors)
		{
			if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
			{
				return Actor;
			}
		}

		return nullptr;
	}
}

FUnrealAISequencerCommands::FUnrealAISequencerCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("create_level_sequence"))
	{
		return HandleCreateLevelSequence(Params);
	}
	else if (CommandType == TEXT("read_level_sequence_content"))
	{
		return HandleReadLevelSequenceContent(Params);
	}
	else if (CommandType == TEXT("add_camera_cut_track_to_level_sequence"))
	{
		return HandleAddCameraCutTrackToLevelSequence(Params);
	}
	else if (CommandType == TEXT("add_master_track_to_level_sequence"))
	{
		return HandleAddMasterTrackToLevelSequence(Params);
	}
	else if (CommandType == TEXT("add_section_to_master_track_in_level_sequence"))
	{
		return HandleAddSectionToMasterTrackInLevelSequence(Params);
	}
	else if (CommandType == TEXT("set_section_range_in_master_track_in_level_sequence"))
	{
		return HandleSetSectionRangeInMasterTrackInLevelSequence(Params);
	}
	else if (CommandType == TEXT("remove_section_from_master_track_in_level_sequence"))
	{
		return HandleRemoveSectionFromMasterTrackInLevelSequence(Params);
	}
	else if (CommandType == TEXT("add_actor_possessable_to_level_sequence"))
	{
		return HandleAddActorPossessableToLevelSequence(Params);
	}
	else if (CommandType == TEXT("add_track_to_binding_in_level_sequence"))
	{
		return HandleAddTrackToBindingInLevelSequence(Params);
	}
	else if (CommandType == TEXT("add_float_key_to_binding_track_in_level_sequence"))
	{
		return HandleAddFloatKeyToBindingTrackInLevelSequence(Params);
	}
	else if (CommandType == TEXT("set_level_sequence_playback_range"))
	{
		return HandleSetLevelSequencePlaybackRange(Params);
	}

	return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown sequencer command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleCreateLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString SequenceName;
	if (!TryGetTrimmedStringParam(Params, TEXT("sequence_name"), SequenceName))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'sequence_name' parameter"));
	}

	FString DestinationPath = TEXT("/Game/Cinematics");
	Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
	DestinationPath = SanitizePackageFolder(DestinationPath);
	if (!DestinationPath.StartsWith(TEXT("/Game")))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'destination_path' must be under /Game"));
	}

	if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
	{
		UEditorAssetLibrary::MakeDirectory(DestinationPath);
	}

	const FString LevelSequencePath = FString::Printf(TEXT("%s/%s"), *DestinationPath, *SequenceName);
	if (UEditorAssetLibrary::DoesAssetExist(LevelSequencePath))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Level Sequence already exists: %s"), *LevelSequencePath));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UClass* FactoryClass = LoadClass<UFactory>(nullptr, TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew"));
	if (!FactoryClass)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load LevelSequenceFactoryNew class"));
	}

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	if (!Factory)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create LevelSequenceFactoryNew instance"));
	}

	ULevelSequence* LevelSequence = Cast<ULevelSequence>(
		AssetTools.CreateAsset(SequenceName, DestinationPath, ULevelSequence::StaticClass(), Factory));
	if (!LevelSequence)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create Level Sequence asset"));
	}

	if (!LevelSequence->GetMovieScene())
	{
		LevelSequence->Initialize();
	}

	if (!LevelSequence->GetMovieScene())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Created Level Sequence is missing its MovieScene"));
	}

	LevelSequence->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);

	TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
	ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

	TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
	Result->SetBoolField(TEXT("created"), true);
	Result->SetStringField(TEXT("destination_path"), DestinationPath);
	return Result;
}

TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleAddCameraCutTrackToLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString LevelSequencePath;
	if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
	}

	ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
	if (!LevelSequence)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
	}

	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (!MovieScene)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
	}

	UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
	const bool bCreated = CameraCutTrack == nullptr;
	if (bCreated)
	{
		FString RequestedTrackClassPath;
		Params->TryGetStringField(TEXT("track_class"), RequestedTrackClassPath);

		UClass* TrackClass = LoadMovieSceneTrackClass(
			RequestedTrackClassPath,
			TEXT("/Script/MovieSceneTracks.MovieSceneCameraCutTrack"));
		if (!TrackClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load 'track_class' as a MovieScene track class"));
		}

		LevelSequence->Modify();
		MovieScene->Modify();
		CameraCutTrack = MovieScene->AddCameraCutTrack(TrackClass);
		if (!CameraCutTrack)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create camera cut track"));
		}

		LevelSequence->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);
	}

	TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
	ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

	TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetBoolField(TEXT("created"), bCreated);
		Result->SetStringField(TEXT("camera_cut_track_path"), CameraCutTrack->GetPathName());
		Result->SetStringField(TEXT("camera_cut_track_class"), CameraCutTrack->GetClass()->GetPathName());
	return Result;
}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleAddMasterTrackToLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		FString RequestedTrackClassPath;
		Params->TryGetStringField(TEXT("track_class"), RequestedTrackClassPath);

		UClass* TrackClass = LoadMovieSceneTrackClass(
			RequestedTrackClassPath,
			TEXT("/Script/MovieSceneTracks.MovieSceneCinematicShotTrack"));
		if (!TrackClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load 'track_class' as a MovieScene track class"));
		}

		UMovieSceneTrack* MasterTrack = MovieScene->FindTrack(TrackClass);
		const bool bCreated = MasterTrack == nullptr;
		if (bCreated)
		{
			LevelSequence->Modify();
			MovieScene->Modify();
			MasterTrack = MovieScene->AddTrack(TrackClass);
			if (!MasterTrack)
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create master track"));
			}

			LevelSequence->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);
		}

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetBoolField(TEXT("created"), bCreated);
		Result->SetStringField(TEXT("master_track_path"), MasterTrack->GetPathName());
		Result->SetStringField(TEXT("master_track_class"), MasterTrack->GetClass()->GetPathName());
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleAddSectionToMasterTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		int32 StartFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("start_frame"), StartFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'start_frame' parameter"));
		}

		int32 EndFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("end_frame"), EndFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'end_frame' parameter"));
		}

		if (EndFrameValue <= StartFrameValue)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'end_frame' must be greater than 'start_frame'"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		FString RequestedTrackClassPath;
		Params->TryGetStringField(TEXT("track_class"), RequestedTrackClassPath);

		UClass* TrackClass = LoadMovieSceneTrackClass(
			RequestedTrackClassPath,
			TEXT("/Script/MovieSceneTracks.MovieSceneCinematicShotTrack"));
		if (!TrackClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load 'track_class' as a MovieScene track class"));
		}

		UMovieSceneTrack* MasterTrack = MovieScene->FindTrack(TrackClass);
		if (!MasterTrack)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Master track not found for 'track_class'; call add_master_track_to_level_sequence first"));
		}

		LevelSequence->Modify();
		MovieScene->Modify();
		MasterTrack->Modify();

		UMovieSceneSection* Section = MasterTrack->CreateNewSection();
		if (!Section)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create section for master track"));
		}

		Section->SetRange(TRange<FFrameNumber>(
			TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(StartFrameValue)),
			TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(EndFrameValue))));
		MasterTrack->AddSection(UMovieSceneTrack::FSectionParameter(*Section));

		LevelSequence->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetStringField(TEXT("master_track_path"), MasterTrack->GetPathName());
		Result->SetStringField(TEXT("master_track_class"), MasterTrack->GetClass()->GetPathName());
		Result->SetStringField(TEXT("section_path"), Section->GetPathName());
		Result->SetStringField(TEXT("section_class"), Section->GetClass()->GetPathName());
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleSetSectionRangeInMasterTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		int32 SectionIndex = 0;
		if (!TryGetInt32Param(Params, TEXT("section_index"), SectionIndex) || SectionIndex < 0)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'section_index' parameter"));
		}

		int32 StartFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("start_frame"), StartFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'start_frame' parameter"));
		}

		int32 EndFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("end_frame"), EndFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'end_frame' parameter"));
		}

		if (EndFrameValue <= StartFrameValue)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'end_frame' must be greater than 'start_frame'"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		FString RequestedTrackClassPath;
		Params->TryGetStringField(TEXT("track_class"), RequestedTrackClassPath);

		UClass* TrackClass = LoadMovieSceneTrackClass(
			RequestedTrackClassPath,
			TEXT("/Script/MovieSceneTracks.MovieSceneCinematicShotTrack"));
		if (!TrackClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load 'track_class' as a MovieScene track class"));
		}

		UMovieSceneTrack* MasterTrack = MovieScene->FindTrack(TrackClass);
		if (!MasterTrack)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Master track not found for 'track_class'; call add_master_track_to_level_sequence first"));
		}

		const TArray<UMovieSceneSection*>& Sections = MasterTrack->GetAllSections();
		if (!Sections.IsValidIndex(SectionIndex) || Sections[SectionIndex] == nullptr)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Section index is out of range for the requested master track"));
		}

		UMovieSceneSection* Section = Sections[SectionIndex];
		LevelSequence->Modify();
		MovieScene->Modify();
		MasterTrack->Modify();
		Section->Modify();
		Section->SetRange(TRange<FFrameNumber>(
			TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(StartFrameValue)),
			TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(EndFrameValue))));

		LevelSequence->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetStringField(TEXT("master_track_path"), MasterTrack->GetPathName());
		Result->SetStringField(TEXT("master_track_class"), MasterTrack->GetClass()->GetPathName());
		Result->SetStringField(TEXT("section_path"), Section->GetPathName());
		Result->SetStringField(TEXT("section_class"), Section->GetClass()->GetPathName());
		Result->SetNumberField(TEXT("section_index"), SectionIndex);
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleRemoveSectionFromMasterTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		int32 SectionIndex = 0;
		if (!TryGetInt32Param(Params, TEXT("section_index"), SectionIndex) || SectionIndex < 0)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'section_index' parameter"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		FString RequestedTrackClassPath;
		Params->TryGetStringField(TEXT("track_class"), RequestedTrackClassPath);

		UClass* TrackClass = LoadMovieSceneTrackClass(
			RequestedTrackClassPath,
			TEXT("/Script/MovieSceneTracks.MovieSceneCinematicShotTrack"));
		if (!TrackClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load 'track_class' as a MovieScene track class"));
		}

		UMovieSceneTrack* MasterTrack = MovieScene->FindTrack(TrackClass);
		if (!MasterTrack)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Master track not found for 'track_class'; call add_master_track_to_level_sequence first"));
		}

		const TArray<UMovieSceneSection*>& Sections = MasterTrack->GetAllSections();
		if (!Sections.IsValidIndex(SectionIndex) || Sections[SectionIndex] == nullptr)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Section index is out of range for the requested master track"));
		}

		UMovieSceneSection* RemovedSection = Sections[SectionIndex];
		const FString RemovedSectionPath = RemovedSection->GetPathName();
		const FString RemovedSectionClass = RemovedSection->GetClass()->GetPathName();

		LevelSequence->Modify();
		MovieScene->Modify();
		MasterTrack->Modify();
		RemovedSection->Modify();
		MasterTrack->RemoveSectionAt(UMovieSceneTrack::FSectionIndexParameter(SectionIndex));

		LevelSequence->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetStringField(TEXT("master_track_path"), MasterTrack->GetPathName());
		Result->SetStringField(TEXT("master_track_class"), MasterTrack->GetClass()->GetPathName());
		Result->SetStringField(TEXT("removed_section_path"), RemovedSectionPath);
		Result->SetStringField(TEXT("removed_section_class"), RemovedSectionClass);
		Result->SetNumberField(TEXT("removed_section_index"), SectionIndex);
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleAddActorPossessableToLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		FString ActorName;
		if (!TryGetTrimmedStringParam(Params, TEXT("actor_name"), ActorName))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_name' parameter"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		}

		if (!LevelSequence->CanPossessObject(*Actor, GWorld))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Actor cannot be possessed by this Level Sequence"));
		}

		FGuid BindingGuid = LevelSequence->FindPossessableObjectId(*Actor, GWorld);
		const bool bCreated = !BindingGuid.IsValid();
		if (bCreated)
		{
			LevelSequence->Modify();
			MovieScene->Modify();

			BindingGuid = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
			if (!BindingGuid.IsValid())
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create possessable binding for actor"));
			}

			LevelSequence->BindPossessableObject(BindingGuid, *Actor, GWorld);

			LevelSequence->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);
		}

		FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid);
		if (!Possessable)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Possessable binding was not found after actor binding update"));
		}

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetBoolField(TEXT("created"), bCreated);
		Result->SetStringField(TEXT("actor_name"), ActorName);
		Result->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actor_path"), Actor->GetPathName());
		Result->SetStringField(TEXT("binding_guid"), BindingGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Result->SetStringField(TEXT("binding_name"), Possessable->GetName());
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleAddTrackToBindingInLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		FString BindingGuidString;
		if (!TryGetTrimmedStringParam(Params, TEXT("binding_guid"), BindingGuidString))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'binding_guid' parameter"));
		}

		FGuid BindingGuid;
		if (!FGuid::Parse(BindingGuidString, BindingGuid) || !BindingGuid.IsValid())
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'binding_guid' parameter"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		if (!MovieScene->FindBinding(BindingGuid))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Binding guid was not found in the requested Level Sequence"));
		}

		FString RequestedTrackClassPath;
		Params->TryGetStringField(TEXT("track_class"), RequestedTrackClassPath);

		UClass* TrackClass = LoadMovieSceneTrackClass(
			RequestedTrackClassPath,
			TEXT("/Script/MovieSceneTracks.MovieSceneFloatTrack"));
		if (!TrackClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to load 'track_class' as a MovieScene track class"));
		}

		UMovieSceneTrack* BindingTrack = MovieScene->FindTrack(TrackClass, BindingGuid);
		const bool bCreated = BindingTrack == nullptr;
		if (bCreated)
		{
			LevelSequence->Modify();
			MovieScene->Modify();

			BindingTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
			if (!BindingTrack)
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create track for binding"));
			}

			LevelSequence->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);
		}

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetBoolField(TEXT("created"), bCreated);
		Result->SetStringField(TEXT("binding_guid"), BindingGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Result->SetStringField(TEXT("binding_track_path"), BindingTrack->GetPathName());
		Result->SetStringField(TEXT("binding_track_class"), BindingTrack->GetClass()->GetPathName());
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleAddFloatKeyToBindingTrackInLevelSequence(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		FString BindingGuidString;
		if (!TryGetTrimmedStringParam(Params, TEXT("binding_guid"), BindingGuidString))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'binding_guid' parameter"));
		}

		FGuid BindingGuid;
		if (!FGuid::Parse(BindingGuidString, BindingGuid) || !BindingGuid.IsValid())
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'binding_guid' parameter"));
		}

		FString PropertyName;
		if (!TryGetTrimmedStringParam(Params, TEXT("property_name"), PropertyName))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'property_name' parameter"));
		}

		FString PropertyPath;
		if (!TryGetTrimmedStringParam(Params, TEXT("property_path"), PropertyPath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'property_path' parameter"));
		}

		int32 KeyFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("frame"), KeyFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'frame' parameter"));
		}

		double KeyNumericValue = 0.0;
		if (!Params->TryGetNumberField(TEXT("value"), KeyNumericValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'value' parameter"));
		}

		int32 SectionStartFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("section_start_frame"), SectionStartFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'section_start_frame' parameter"));
		}

		int32 SectionEndFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("section_end_frame"), SectionEndFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'section_end_frame' parameter"));
		}

		if (SectionEndFrameValue <= SectionStartFrameValue)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'section_end_frame' must be greater than 'section_start_frame'"));
		}

		FString Interpolation = TEXT("auto");
		Params->TryGetStringField(TEXT("interpolation"), Interpolation);
		Interpolation = Interpolation.TrimStartAndEnd().ToLower();

		EMovieSceneKeyInterpolation KeyInterpolation = EMovieSceneKeyInterpolation::Auto;
		if (Interpolation == TEXT("auto"))
		{
			KeyInterpolation = EMovieSceneKeyInterpolation::Auto;
		}
		else if (Interpolation == TEXT("smart_auto"))
		{
			KeyInterpolation = EMovieSceneKeyInterpolation::SmartAuto;
		}
		else if (Interpolation == TEXT("user"))
		{
			KeyInterpolation = EMovieSceneKeyInterpolation::User;
		}
		else if (Interpolation == TEXT("break"))
		{
			KeyInterpolation = EMovieSceneKeyInterpolation::Break;
		}
		else if (Interpolation == TEXT("linear"))
		{
			KeyInterpolation = EMovieSceneKeyInterpolation::Linear;
		}
		else if (Interpolation == TEXT("constant"))
		{
			KeyInterpolation = EMovieSceneKeyInterpolation::Constant;
		}
		else
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Invalid 'interpolation' parameter; expected auto, smart_auto, user, break, linear, or constant"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		if (!MovieScene->FindBinding(BindingGuid))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Binding guid was not found in the requested Level Sequence"));
		}

		UMovieSceneTrack* RawTrack = MovieScene->FindTrack(UMovieSceneFloatTrack::StaticClass(), BindingGuid);
		if (!RawTrack)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Float binding track not found for binding guid; call add_track_to_binding_in_level_sequence first"));
		}

		UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(RawTrack);
		UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(RawTrack);
		if (!FloatTrack || !PropertyTrack)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Binding track is not a float property track"));
		}

		LevelSequence->Modify();
		MovieScene->Modify();
		RawTrack->Modify();
		PropertyTrack->SetPropertyNameAndPath(FName(*PropertyName), PropertyPath);

		bool bSectionAdded = false;
		UMovieSceneSection* BaseSection = PropertyTrack->FindOrAddSection(FFrameNumber(KeyFrameValue), bSectionAdded);
		if (!BaseSection)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to find or create a float section for the binding track"));
		}

		UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(BaseSection);
		if (!FloatSection)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Binding track section is not a float section"));
		}

		FloatSection->Modify();
		if (bSectionAdded)
		{
			FloatSection->SetRange(TRange<FFrameNumber>(
				TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(SectionStartFrameValue)),
				TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(SectionEndFrameValue))));
		}

		FMovieSceneFloatChannel& Channel = FloatSection->GetChannel();
		const bool bKeyCreated = Channel.GetData().FindKey(FFrameNumber(KeyFrameValue)) == INDEX_NONE;
		AddKeyToChannel(&Channel, FFrameNumber(KeyFrameValue), static_cast<float>(KeyNumericValue), KeyInterpolation);

		LevelSequence->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetBoolField(TEXT("section_created"), bSectionAdded);
		Result->SetBoolField(TEXT("key_created"), bKeyCreated);
		Result->SetStringField(TEXT("binding_guid"), BindingGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Result->SetStringField(TEXT("binding_track_path"), RawTrack->GetPathName());
		Result->SetStringField(TEXT("binding_track_class"), RawTrack->GetClass()->GetPathName());
		Result->SetStringField(TEXT("section_path"), FloatSection->GetPathName());
		Result->SetStringField(TEXT("section_class"), FloatSection->GetClass()->GetPathName());
		Result->SetStringField(TEXT("property_name"), PropertyName);
		Result->SetStringField(TEXT("property_path"), PropertyPath);
		Result->SetNumberField(TEXT("key_frame"), KeyFrameValue);
		Result->SetNumberField(TEXT("key_value"), static_cast<float>(KeyNumericValue));
		Result->SetStringField(TEXT("interpolation"), Interpolation);
		return Result;
	}

	TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleSetLevelSequencePlaybackRange(const TSharedPtr<FJsonObject>& Params)
	{
		FString LevelSequencePath;
		if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
		}

		int32 StartFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("start_frame"), StartFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'start_frame' parameter"));
		}

		int32 EndFrameValue = 0;
		if (!TryGetInt32Param(Params, TEXT("end_frame"), EndFrameValue))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'end_frame' parameter"));
		}

		if (EndFrameValue <= StartFrameValue)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'end_frame' must be greater than 'start_frame'"));
		}

		ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
		if (!LevelSequence)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
		}

		LevelSequence->Modify();
		MovieScene->Modify();
		MovieScene->SetPlaybackRange(
			TRange<FFrameNumber>(
				TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(StartFrameValue)),
				TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(EndFrameValue))));

		LevelSequence->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(LevelSequence->GetOutermost()->GetName(), false);

		TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
		ReadParams->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());

		TSharedPtr<FJsonObject> Result = HandleReadLevelSequenceContent(ReadParams);
		Result->SetNumberField(TEXT("start_frame"), StartFrameValue);
		Result->SetNumberField(TEXT("end_frame"), EndFrameValue);
		return Result;
	}

TSharedPtr<FJsonObject> FUnrealAISequencerCommands::HandleReadLevelSequenceContent(const TSharedPtr<FJsonObject>& Params)
{
	FString LevelSequencePath;
	if (!TryGetTrimmedStringParam(Params, TEXT("level_sequence_path"), LevelSequencePath))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'level_sequence_path' parameter"));
	}

	ULevelSequence* LevelSequence = LoadLevelSequenceAsset(LevelSequencePath);
	if (!LevelSequence)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Level Sequence not found: %s"), *LevelSequencePath));
	}

	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (!MovieScene)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Level Sequence is missing its MovieScene"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("level_sequence_path"), LevelSequence->GetOutermost()->GetName());
	Result->SetStringField(TEXT("level_sequence_object_path"), LevelSequence->GetPathName());
	Result->SetStringField(TEXT("level_sequence_name"), LevelSequence->GetName());
	Result->SetStringField(TEXT("movie_scene_name"), MovieScene->GetName());
	Result->SetObjectField(TEXT("display_rate"), MakeFrameRateJson(MovieScene->GetDisplayRate()));
	Result->SetObjectField(TEXT("tick_resolution"), MakeFrameRateJson(MovieScene->GetTickResolution()));
	Result->SetObjectField(TEXT("playback_range"), MakeFrameRangeJson(MovieScene->GetPlaybackRange()));
	Result->SetBoolField(TEXT("has_camera_cut_track"), MovieScene->GetCameraCutTrack() != nullptr);

	TArray<TSharedPtr<FJsonValue>> SpawnableArray;
	const int32 SpawnableCount = MovieScene->GetSpawnableCount();
	SpawnableArray.Reserve(SpawnableCount);
	for (int32 Index = 0; Index < SpawnableCount; ++Index)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Index);
		TSharedPtr<FJsonObject> SpawnableJson = MakeShared<FJsonObject>();
		SpawnableJson->SetStringField(TEXT("guid"), Spawnable.GetGuid().ToString(EGuidFormats::DigitsWithHyphens));
		SpawnableJson->SetStringField(TEXT("name"), Spawnable.GetName());
		if (const UObject* ObjectTemplate = Spawnable.GetObjectTemplate())
		{
			SpawnableJson->SetStringField(TEXT("object_class"), ObjectTemplate->GetClass()->GetName());
		}
		SpawnableArray.Add(MakeShared<FJsonValueObject>(SpawnableJson));
	}

	TArray<TSharedPtr<FJsonValue>> PossessableArray;
	const int32 PossessableCount = MovieScene->GetPossessableCount();
	PossessableArray.Reserve(PossessableCount);
	for (int32 Index = 0; Index < PossessableCount; ++Index)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
		TSharedPtr<FJsonObject> PossessableJson = MakeShared<FJsonObject>();
		PossessableJson->SetStringField(TEXT("guid"), Possessable.GetGuid().ToString(EGuidFormats::DigitsWithHyphens));
		PossessableJson->SetStringField(TEXT("name"), Possessable.GetName());
		if (const UClass* PossessedObjectClass = Possessable.GetPossessedObjectClass())
		{
			PossessableJson->SetStringField(TEXT("object_class"), PossessedObjectClass->GetName());
		}
		if (Possessable.GetParent().IsValid())
		{
			PossessableJson->SetStringField(TEXT("parent_guid"), Possessable.GetParent().ToString(EGuidFormats::DigitsWithHyphens));
		}
		PossessableArray.Add(MakeShared<FJsonValueObject>(PossessableJson));
	}

	TArray<TSharedPtr<FJsonValue>> ObjectBindingArray;
	const UMovieScene* MovieSceneView = MovieScene;
	const TArray<FMovieSceneBinding>& ObjectBindings = MovieSceneView->GetBindings();
	ObjectBindingArray.Reserve(ObjectBindings.Num());
	for (const FMovieSceneBinding& Binding : ObjectBindings)
	{
		ObjectBindingArray.Add(MakeShared<FJsonValueObject>(SerializeBinding(Binding, MovieScene)));
	}

	TArray<TSharedPtr<FJsonValue>> MasterTrackArray;
	const UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetTracks();
	MasterTrackArray.Reserve(MasterTracks.Num() + (CameraCutTrack ? 1 : 0));
	if (CameraCutTrack)
	{
		MasterTrackArray.Add(MakeShared<FJsonValueObject>(SerializeTrack(CameraCutTrack)));
	}
	for (const UMovieSceneTrack* Track : MasterTracks)
	{
		if (Track && Track != CameraCutTrack)
		{
			MasterTrackArray.Add(MakeShared<FJsonValueObject>(SerializeTrack(Track)));
		}
	}

	Result->SetArrayField(TEXT("spawnables"), SpawnableArray);
	Result->SetArrayField(TEXT("possessables"), PossessableArray);
	Result->SetArrayField(TEXT("object_bindings"), ObjectBindingArray);
	Result->SetArrayField(TEXT("master_tracks"), MasterTrackArray);
	Result->SetNumberField(TEXT("spawnable_count"), SpawnableArray.Num());
	Result->SetNumberField(TEXT("possessable_count"), PossessableArray.Num());
	Result->SetNumberField(TEXT("binding_count"), ObjectBindingArray.Num());
	Result->SetNumberField(TEXT("master_track_count"), MasterTrackArray.Num());
	Result->SetNumberField(TEXT("marked_frame_count"), MovieScene->GetMarkedFrames().Num());
	return Result;
}