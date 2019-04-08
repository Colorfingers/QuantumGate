// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "QuantumGate.h"

namespace StressExtender
{
	using namespace QuantumGate;

	enum class MessageType : UInt16
	{
		Unknown = 0,
		MessageString,
		BenchmarkStart,
		BenchmarkEnd
	};

	struct ExceptionTest final
	{
		bool Startup{ false };
		bool PostStartup{ false };
		bool PreShutdown{ false };
		bool Shutdown{ false };
		bool PeerEvent{ false };
		bool PeerMessage{ false };
	};

	class Extender final : public QuantumGate::Extender
	{
	public:
		Extender();
		virtual ~Extender();

		inline void SetUseCompression(const bool compression) noexcept { m_UseCompression = compression; }
		inline bool IsUsingCompression() const noexcept { return m_UseCompression; }

		bool SendMessage(const PeerLUID pluid, const std::wstring& msg) const;

		bool SendBenchmarkStart(const PeerLUID pluid);
		bool SendBenchmarkEnd(const PeerLUID pluid);

		bool BenchmarkSendMessage(const PeerLUID pluid);

		inline ExceptionTest& GetExceptionTest() noexcept { return m_ExceptionTest; }

	protected:
		bool OnStartup();
		void OnPostStartup();
		void OnPreShutdown();
		void OnShutdown();
		void OnPeerEvent(PeerEvent&& event);
		const std::pair<bool, bool> OnPeerMessage(PeerEvent&& event);

	private:
		std::atomic_bool m_UseCompression{ true };
		
		bool m_IsLocalBenchmarking{ false };
		bool m_IsPeerBenchmarking{ false };
		std::chrono::time_point<std::chrono::high_resolution_clock> m_LocalBenchmarkStart;
		std::chrono::time_point<std::chrono::high_resolution_clock> m_PeerBenchmarkStart;

		ExceptionTest m_ExceptionTest;
	};
}