// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "WebRTCIncludes.h"
#include "RHI.h"
#include "PlayerId.h"
#include "VideoCapturerContext.h"
#include "RHIGPUReadback.h"

// This is a video track source for WebRTC.
// Its main purpose is to copy frames from the Unreal Engine backbuffer.
class FVideoCapturer : public rtc::AdaptedVideoTrackSource
{
public:
	FVideoCapturer(FPlayerId InPlayerId);
	~FVideoCapturer();

	bool IsInitialized();
	void Initialize(const FTexture2DRHIRef& FrameBuffer, TSharedPtr<FVideoCapturerContext> CapturerContext);
	void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);

	void AddRef() const override
	{
		FPlatformAtomics::InterlockedIncrement(const_cast<volatile int32*>(&Count));
	}

	rtc::RefCountReleaseStatus Release() const override
	{
		if (FPlatformAtomics::InterlockedDecrement(const_cast<volatile int32*>(&Count)) == 0)
		{
			return rtc::RefCountReleaseStatus::kDroppedLastRef;
		}
		
		return rtc::RefCountReleaseStatus::kOtherRefsRemained;
	}

	virtual webrtc::MediaSourceInterface::SourceState state() const override
	{
		return this->CurrentState;
	}

	virtual bool remote() const override
	{
		return false;
	}

	virtual bool is_screencast() const override
	{
		return false;
	}

	virtual absl::optional<bool> needs_denoising() const override
	{
		return false;
	}

private:
	bool AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution);
	void SetCaptureResolution(int width, int height);
	void OnEncoderInitialized();

	bool bInitialized;
	FPlayerId PlayerId;
	TSharedPtr<FVideoCapturerContext> CapturerContext;
	webrtc::MediaSourceInterface::SourceState CurrentState;
	volatile int32 Count;





	static constexpr int32 TaskBReadbackSlotCount = 3;

	struct FTaskBReadbackSlot
	{
		TUniquePtr<FRHIGPUTextureReadback> Readback;

		bool bPending = false;

		uint64 Sequence = 0;
		int64 TimestampUs = 0;

		uint32 Width = 0;
		uint32 Height = 0;

		EPixelFormat Format = PF_Unknown;
	};

	struct FTaskBDRAMFrame
	{
		uint64 Sequence = 0;
		int64 TimestampUs = 0;

		uint32 Width = 0;
		uint32 Height = 0;

		EPixelFormat Format = PF_Unknown;

		TArray<uint8> Data;
	};

	void TaskBInitializeReadbacks();
	void TaskBProcessCompletedReadbacks();
	void TaskBSubmitReadback(
		const FTexture2DRHIRef& FrameBuffer,
		int64 TimestampUs);

	int32 TaskBFindFreeReadbackSlot() const;

	FTaskBReadbackSlot TaskBReadbackSlots[TaskBReadbackSlotCount];

	TArray<FTaskBDRAMFrame> TaskBDRAMQueue;

	uint64 TaskBNextSequence = 0;

	uint64 TaskBSubmitted = 0;
	uint64 TaskBCompleted = 0;
	uint64 TaskBSubmitDropped = 0;
	uint64 TaskBQueueDropped = 0;

	static constexpr int32 TaskBDRAMQueueCapacity = 3;
};
