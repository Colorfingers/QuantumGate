// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "VideoSourceReader.h"

#include <Common\Util.h>

namespace QuantumGate::AVExtender
{
	VideoSourceReader::VideoSourceReader() noexcept
		: SourceReader(CaptureDevice::Type::Video, MFVideoFormat_RGB24)
	{}

	VideoSourceReader::~VideoSourceReader()
	{}

	STDMETHODIMP VideoSourceReader::QueryInterface(REFIID riid, void** ppvObject)
	{
		static const QITAB qit[] = { QITABENT(VideoSourceReader, IMFSourceReaderCallback), { 0 } };
		return QISearch(this, qit, riid, ppvObject);
	}

	STDMETHODIMP_(ULONG) VideoSourceReader::Release()
	{
		const ULONG count = InterlockedDecrement(&m_RefCount);
		if (count == 0)
		{
			delete this;
		}

		return count;
	}

	STDMETHODIMP_(ULONG) VideoSourceReader::AddRef()
	{
		return InterlockedIncrement(&m_RefCount);
	}

	Result<> VideoSourceReader::OnMediaTypeChanged(IMFMediaType* media_type) noexcept
	{
		auto video_settings = m_VideoSettings.WithUniqueLock();

		// Get width and height
		auto hr = MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE,
									 &video_settings->Width, &video_settings->Height);
		if (SUCCEEDED(hr))
		{
			// Get the stride for this format so we can calculate the number of bytes per pixel
			if (GetDefaultStride(media_type, &video_settings->Stride))
			{
				video_settings->BytesPerPixel = std::abs(video_settings->Stride) / video_settings->Width;

				return AVResultCode::Succeeded;
			}
		}

		return AVResultCode::Failed;
	}

	Result<Size> VideoSourceReader::GetBufferSize(IMFMediaType* media_type) noexcept
	{
		const auto video_settings = m_VideoSettings.WithSharedLock();

		return static_cast<Size>(video_settings->Width) *
			static_cast<Size>(video_settings->Height) *
			static_cast<Size>(video_settings->BytesPerPixel);
	}

	// Calculates the default stride based on the format and size of the frames
	bool VideoSourceReader::GetDefaultStride(IMFMediaType* type, LONG* stride) const noexcept
	{
		UINT32 tstride{ 0 };

		// Try to get the default stride from the media type
		auto hr = type->GetUINT32(MF_MT_DEFAULT_STRIDE, &tstride);
		if (SUCCEEDED(hr))
		{
			*stride = tstride;
			return true;
		}
		else
		{
			// Setting this atribute to NULL we can obtain the default stride
			GUID subtype{ GUID_NULL };
			UINT32 width{ 0 };
			UINT32 height{ 0 };

			// Obtain the subtype
			hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
			if (SUCCEEDED(hr))
			{
				// Obtain the width and height
				hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
				if (SUCCEEDED(hr))
				{
					// Calculate the stride based on the subtype and width
					hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, stride);
					if (SUCCEEDED(hr))
					{
						// Set the attribute so it can be read
						hr = type->SetUINT32(MF_MT_DEFAULT_STRIDE, *stride);
						if (SUCCEEDED(hr))
						{
							return true;
						}
					}
				}
			}
		}

		return false;
	}

	void VideoSourceReader::GetSample(BGRAPixel* buffer) noexcept
	{
		const auto video_settings = m_VideoSettings.WithSharedLock();
		const auto source_reader = GetSourceReader().WithSharedLock();

		if (source_reader->Format == MFVideoFormat_RGB24)
		{
			// For some reason MFVideoFormat_RGB24 has a BGR order
			BGR24ToBGRA32(buffer, reinterpret_cast<const BGRPixel*>(source_reader->RawData.GetBytes()),
						  video_settings->Width, video_settings->Height, video_settings->Stride);
		}
	}

	std::pair<UInt, UInt> VideoSourceReader::GetSampleDimensions() noexcept
	{
		const auto video_settings = m_VideoSettings.WithSharedLock();

		return std::make_pair(video_settings->Width, video_settings->Height);
	}
}