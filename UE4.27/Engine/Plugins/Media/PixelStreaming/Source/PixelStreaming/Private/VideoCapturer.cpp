// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "Utils.h"
#include "Engine/Engine.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingFrameBuffer.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingStats.h"
#include "LatencyTester.h"
#include "VideoCapturerContext.h"

FVideoCapturer::FVideoCapturer(FPlayerId InPlayerId)
	: bInitialized(false)
	, PlayerId(InPlayerId)

{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;
}

FVideoCapturer::~FVideoCapturer()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
}

bool FVideoCapturer::IsInitialized()
{
	return this->bInitialized;
}

void FVideoCapturer::Initialize(const FTexture2DRHIRef& FrameBuffer, TSharedPtr<FVideoCapturerContext> InCapturerContext)
{
	// Check if already initialized
	if(this->bInitialized)
	{
		return;
	}

	this->CapturerContext = InCapturerContext;

	const int64 TimestampUs = rtc::TimeMicros();
	if(!AdaptCaptureFrame(TimestampUs, FrameBuffer->GetSizeXY()))
	{
		return;
	}

	// Here we pass the frame to Webrtc which will pass it to the correct encoder.
	// We pass a special frame called the FPixelStreamingInitFrameBuffer that contains the player id, so the encoder is associated with a specific player id.
	rtc::scoped_refptr<FPixelStreamingInitFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingInitFrameBuffer>(
		PlayerId, 
		this->CapturerContext->GetCaptureWidth(), 
		this->CapturerContext->GetCaptureHeight());

	// Bind to when encoder is actually intialized as we will use this as a signal to start sending proper frames for encoding
	Buffer->OnEncoderInitialized.BindRaw(this, &FVideoCapturer::OnEncoderInitialized);

	// Build a WebRTC frame (note frame id hardcoded to zero, we assume this is okay)
	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
		.set_video_frame_buffer(Buffer)
		.set_timestamp_us(TimestampUs)
		.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
		.set_id(0).build();

	// Pass the frame to WebRTC, where is will eventually end up being processed by an encoder
	OnFrame(Frame);
}

void FVideoCapturer::OnEncoderInitialized()
{
	this->bInitialized = true;
}

void FVideoCapturer::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	
	if(!this->bInitialized)
	{
		// Not initialized, can't do anything with this framebuffer
		return;
	}

	const int64 TimestampUs = rtc::TimeMicros();
	if(!AdaptCaptureFrame(TimestampUs, FrameBuffer->GetSizeXY()))
	{
		return;
	}

	// ---------------------------------------------------------
	// Task B experimental GPU -> DRAM path.
	//
	// First harvest previous GPU readbacks without waiting.
	// Then submit the current framebuffer into a free slot.
	//
	// The original Pixel Streaming path below remains unchanged.
	// ---------------------------------------------------------
	TaskBProcessCompletedReadbacks();
	TaskBSubmitReadback(FrameBuffer, TimestampUs);

	if(CurrentState != webrtc::MediaSourceInterface::SourceState::kLive)
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;
	}
	
	FVideoCapturerContext::FCapturerInput CapturerInput = this->CapturerContext->ObtainCapturerInput();

	if(CapturerInput.InputFrame == nullptr || !CapturerInput.Texture.IsSet())
	{
		return;
	}

	AVEncoder::FVideoEncoderInputFrame* InputFrame = CapturerInput.InputFrame;
	FTexture2DRHIRef Texture = CapturerInput.Texture.GetValue();

	const int32 FrameId = InputFrame->GetFrameID();
	InputFrame->SetTimestampUs(TimestampUs);

	// Latency test pre capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_CAPTURE)
	{
		FLatencyTester::RecordPreCaptureTime(FrameId);
	}

	// Actual texture copy (i.e the actual "capture")
	CopyTexture(FrameBuffer, Texture);

	// Latency test post capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_CAPTURE)
	{
		FLatencyTester::RecordPostCaptureTime(FrameId);
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);

	// Here we pass the frame to Webrtc which will pass it to the correct encoder.
	rtc::scoped_refptr<FPixelStreamingFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingFrameBuffer>(Texture, InputFrame, this->CapturerContext->GetVideoEncoderInput());
	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
		.set_video_frame_buffer(Buffer)
		.set_timestamp_us(TimestampUs)
		.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
		.set_id(FrameId)
		.build();
	
	// Send the frame from the video source back into WebRTC where it will eventually makes its way into an encoder
	OnFrame(Frame);

	InputFrame->Release();

	// If stats are enabled, records the stats during capture now.
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	if(Stats.GetStatsEnabled())
	{
		int64 TimestampNowUs = rtc::TimeMicros();
		int64 CaptureLatencyUs = TimestampNowUs - TimestampUs;
		double CaptureLatencyMs = (double)CaptureLatencyUs / 1000.0;
		Stats.SetCaptureLatency(CaptureLatencyMs);
		Stats.OnCaptureFinished();
	}

}



bool FVideoCapturer::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
{
	int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
	if(!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
	{
		return false;
	}

	// Set resolution of encoder using user-defined params (i.e. not the back buffer).
	if(!PixelStreamingSettings::CVarPixelStreamingUseBackBufferCaptureSize.GetValueOnRenderThread())
	{
		// set resolution based on cvars
		FString CaptureSize = PixelStreamingSettings::CVarPixelStreamingCaptureSize.GetValueOnRenderThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = CaptureSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if(bValidSize)
		{
			Resolution.X = FCString::Atoi(*TargetWidth);
			Resolution.Y = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("CVarPixelStreamingCaptureSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *CaptureSize);
			PixelStreamingSettings::CVarPixelStreamingCaptureSize->Set(*FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		}
	}
	else
	{
		Resolution.X = outWidth;
		Resolution.Y = outHeight;
	}

	this->CapturerContext->SetCaptureResolution(Resolution.X, Resolution.Y);

	return true;

}

void FVideoCapturer::TaskBInitializeReadbacks()
{
	for (int32 Index = 0; Index < TaskBReadbackSlotCount; ++Index)
	{
		if (!TaskBReadbackSlots[Index].Readback)
		{
			TaskBReadbackSlots[Index].Readback =
				MakeUnique<FRHIGPUTextureReadback>(
					FName(*FString::Printf(
						TEXT("PixelStreamingTaskBReadback_%d"),
						Index)));
		}
	}
}
int32 FVideoCapturer::TaskBFindFreeReadbackSlot() const
{
	for (int32 Index = 0; Index < TaskBReadbackSlotCount; ++Index)
	{
		if (!TaskBReadbackSlots[Index].bPending)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void FVideoCapturer::TaskBSubmitReadback(
	const FTexture2DRHIRef& FrameBuffer,
	int64 TimestampUs)
{
	check(IsInRenderingThread());

	TaskBInitializeReadbacks();

	const int32 SlotIndex = TaskBFindFreeReadbackSlot();

	if (SlotIndex == INDEX_NONE)
	{
		++TaskBSubmitDropped;

		if ((TaskBSubmitDropped % 60) == 1)
		{
			UE_LOG(
				PixelStreamer,
				Warning,
				TEXT("[TaskB] No free GPU readback slot. dropped=%llu"),
				TaskBSubmitDropped);
		}

		return;
	}

	FTaskBReadbackSlot& Slot =
		TaskBReadbackSlots[SlotIndex];

	Slot.Sequence = TaskBNextSequence++;
	Slot.TimestampUs = TimestampUs;

	Slot.Width = FrameBuffer->GetSizeX();
	Slot.Height = FrameBuffer->GetSizeY();
	Slot.Format = FrameBuffer->GetFormat();

	FRHICommandListImmediate& RHICmdList =
		FRHICommandListExecutor::GetImmediateCommandList();

	Slot.Readback->EnqueueCopy(
		RHICmdList,
		FrameBuffer.GetReference());

	Slot.bPending = true;

	++TaskBSubmitted;

	if ((TaskBSubmitted % 120) == 1)
	{
		UE_LOG(
			PixelStreamer,
			Log,
			TEXT(
				"[TaskB] submitted seq=%llu slot=%d size=%ux%u format=%d submitted=%llu"),
			Slot.Sequence,
			SlotIndex,
			Slot.Width,
			Slot.Height,
			static_cast<int32>(Slot.Format),
			TaskBSubmitted);
	}
}

void FVideoCapturer::TaskBProcessCompletedReadbacks()
{
	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList =
		FRHICommandListExecutor::GetImmediateCommandList();

	for (int32 SlotIndex = 0;
		 SlotIndex < TaskBReadbackSlotCount;
		 ++SlotIndex)
	{
		FTaskBReadbackSlot& Slot =
			TaskBReadbackSlots[SlotIndex];

		if (!Slot.bPending || !Slot.Readback)
		{
			continue;
		}

		// Critical:
		// Never wait for the GPU here.
		if (!Slot.Readback->IsReady())
		{
			continue;
		}

		void* BufferPtr = nullptr;
		int32 RowPitchInPixels = 0;

		Slot.Readback->LockTexture(
			RHICmdList,
			BufferPtr,
			RowPitchInPixels);

		if (BufferPtr == nullptr)
		{
			UE_LOG(
				PixelStreamer,
				Error,
				TEXT("[TaskB] LockTexture returned nullptr"));

			Slot.Readback->Unlock();
			Slot.bPending = false;
			continue;
		}

		/*
		 * First experimental version:
		 *
		 * Only accept the expected common 32-bit formats.
		 * Do NOT silently assume every RHI format is 4 Bpp.
		 */
		const bool bSupportedFormat =
			Slot.Format == PF_B8G8R8A8 ||
			Slot.Format == PF_R8G8B8A8;

		if (!bSupportedFormat)
		{
			UE_LOG(
				PixelStreamer,
				Error,
				TEXT(
					"[TaskB] Unsupported framebuffer format=%d. "
					"seq=%llu"),
				static_cast<int32>(Slot.Format),
				Slot.Sequence);

			Slot.Readback->Unlock();
			Slot.bPending = false;
			continue;
		}

		const uint32 BytesPerPixel = 4;

		const uint64 PackedRowBytes =
			static_cast<uint64>(Slot.Width) * BytesPerPixel;

		const uint64 SourceRowBytes =
			static_cast<uint64>(RowPitchInPixels) *
			BytesPerPixel;

		const uint64 TotalBytes =
			PackedRowBytes * Slot.Height;

		FTaskBDRAMFrame CPUFrame;

		CPUFrame.Sequence = Slot.Sequence;
		CPUFrame.TimestampUs = Slot.TimestampUs;
		CPUFrame.Width = Slot.Width;
		CPUFrame.Height = Slot.Height;
		CPUFrame.Format = Slot.Format;

		CPUFrame.Data.SetNumUninitialized(TotalBytes);

		const uint8* Src =
			static_cast<const uint8*>(BufferPtr);

		uint8* Dst =
			CPUFrame.Data.GetData();

		for (uint32 Y = 0; Y < Slot.Height; ++Y)
		{
			FMemory::Memcpy(
				Dst + static_cast<uint64>(Y) * PackedRowBytes,
				Src + static_cast<uint64>(Y) * SourceRowBytes,
				PackedRowBytes);
		}

		Slot.Readback->Unlock();

		Slot.bPending = false;

		++TaskBCompleted;

		// Fixed-size DRAM queue: drop oldest.
		if (TaskBDRAMQueue.Num() >= TaskBDRAMQueueCapacity)
		{
			TaskBDRAMQueue.RemoveAt(0, 1, false);
			++TaskBQueueDropped;
		}

		TaskBDRAMQueue.Add(MoveTemp(CPUFrame));

		if ((TaskBCompleted % 120) == 1)
		{
			UE_LOG(
				PixelStreamer,
				Log,
				TEXT(
					"[TaskB] completed seq=%llu slot=%d "
					"size=%ux%u pitchPixels=%d "
					"queue=%d submitted=%llu completed=%llu "
					"submitDropped=%llu queueDropped=%llu"),
				Slot.Sequence,
				SlotIndex,
				Slot.Width,
				Slot.Height,
				RowPitchInPixels,
				TaskBDRAMQueue.Num(),
				TaskBSubmitted,
				TaskBCompleted,
				TaskBSubmitDropped,
				TaskBQueueDropped);
		}
	}
}
