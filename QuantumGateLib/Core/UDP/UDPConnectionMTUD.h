// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "UDPConnectionCommon.h"
#include "UDPMessage.h"
#include "..\..\Common\Random.h"

// Use to enable/disable MTU discovery debug console output
// #define UDPMTUD_DEBUG

namespace QuantumGate::Implementation::Core::UDP::Connection
{
	class Connection;

	class MTUDiscovery final
	{
		struct MTUDMessageData final
		{
			Size MaximumMessageSize{ 0 };
			bool Final{ false };
			Message::SequenceNumber SequenceNumber{ 0 };
			UInt NumTries{ 0 };
			SteadyTime TimeSent;
			Buffer Data;
			bool Acked{ false };
		};

		enum class TransmitResult { Success, MessageTooLarge, Failed };

	public:
		enum class Status { Start, Discovery, Finished, Failed };

		MTUDiscovery(Connection& connection, const std::chrono::milliseconds max_start_delay) noexcept;
		MTUDiscovery(const MTUDiscovery&) = delete;
		MTUDiscovery(MTUDiscovery&&) noexcept = delete;
		~MTUDiscovery() = default;
		MTUDiscovery& operator=(const MTUDiscovery&) = delete;
		MTUDiscovery& operator=(MTUDiscovery&&) noexcept = delete;

		[[nodiscard]] inline Size GetMaxMessageSize() const noexcept { return m_MaximumMessageSize; }

		[[nodiscard]] Status Process() noexcept;

		void ProcessReceivedAck(const Message::SequenceNumber seqnum) noexcept;

		static void AckReceivedMessage(Connection& connection, const Message::SequenceNumber seqnum) noexcept;

	private:
		[[nodiscard]] Status CreateAndTransmitMessage(const Size prev_msg_size, const Size msg_size,
													  const bool final_msg = false) noexcept;
		[[nodiscard]] bool CreateNewMessage(const Size msg_size, const Size next_msg_size,
											const bool final_msg) noexcept;
		[[nodiscard]] TransmitResult TransmitMessage() noexcept;
		[[nodiscard]] Status ProcessTransmitResult(const TransmitResult result) noexcept;

	private:
		static constexpr std::chrono::milliseconds MinRetransmissionTimeout{ 600 };
		static constexpr Size MaxNumRetries{ 6 };

	private:
		Connection& m_Connection;
		Status m_Status{ Status::Start };
		std::optional<MTUDMessageData> m_MTUDMessageData;
		Size m_MaximumMessageSize{ UDPMessageSizes::Min };
		Size m_CurrentMessageSizeIndex{ 0 };
		std::chrono::milliseconds m_RetransmissionTimeout{ MinRetransmissionTimeout };
		std::chrono::milliseconds m_StartDelay{ 0 };
		SteadyTime m_StartTime;
	};
}
