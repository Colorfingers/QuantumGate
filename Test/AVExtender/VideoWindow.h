// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "Common.h"
#include "CaptureDevice.h"
#include "VideoResampler.h"

#include <Windows.h>
#include <d2d1_1.h>

#pragma comment(lib, "d2d1.lib")

namespace QuantumGate::AVExtender
{
	class VideoWindow final
	{
	public:
		enum class RenderSize { Fit, Cover };

		VideoWindow() noexcept;
		VideoWindow(const VideoWindow&) = delete;
		VideoWindow(VideoWindow&&) = default;
		~VideoWindow();
		VideoWindow& operator=(const VideoWindow&) = delete;
		VideoWindow& operator=(VideoWindow&&) = default;

		[[nodiscard]] bool Create(const WChar* title, const DWORD dwExStyle, const DWORD dwStyle,
								  const int x, const int y, const int width, const int height,
								  const bool visible, const HWND parent) noexcept;
		void Close() noexcept;
		[[nodiscard]] inline bool IsOpen() const noexcept { return m_WndHandle != nullptr; }

		bool IsVisible() const noexcept;
		void SetWindowVisible(const bool visible) noexcept;

		inline void SetRenderSize(const RenderSize render_size) noexcept { m_RenderSize = render_size; }
		[[nodiscard]] inline RenderSize GetRenderSize() const noexcept { return m_RenderSize; }

		[[nodiscard]] bool SetInputFormat(const VideoFormat& fmt) noexcept;

		void Render(IMFSample* in_sample) noexcept;
		void Render(const UInt64 in_timestamp, const BufferView pixels) noexcept;

		void ProcessMessages() noexcept;

		void Redraw() noexcept;
		
	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;

		void Render(IMFSample* in_sample, const VideoFormat& format) noexcept;
		void Render(const BufferView pixels, const VideoFormat& format) noexcept;

		[[nodiscard]] bool InitializeD2DRenderTarget(const HWND hwnd, const UInt width, const UInt height) noexcept;
		void DeinitializeD2DRenderTarget() noexcept;
		void ResizeRenderTarget() noexcept;
		void ResizeDrawRect() noexcept;

	private:
		HWND m_WndHandle{ nullptr };
		RECT m_WndClientRect{ 0 };
		D2D_RECT_F m_DrawRect{ 0 };
		RenderSize m_RenderSize{ RenderSize::Fit };

		ID2D1Factory* m_D2D1Factory{ nullptr };
		ID2D1HwndRenderTarget* m_D2D1RenderTarget{ nullptr };
		ID2D1Bitmap* m_D2D1Bitmap{ nullptr };

		VideoResampler m_VideoResampler;
		
		IMFSample* m_OutputSample{ nullptr };

		Buffer m_ResampleBuffer;
	};
}