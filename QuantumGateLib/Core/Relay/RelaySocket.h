// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\..\Network\SocketBase.h"

#include <queue>

namespace QuantumGate::Implementation::Core::Relay
{
	class Socket final : public Network::SocketBase
	{
		friend class Manager;

		using RelayDataQueue = std::queue<Buffer>;

	public:
		Socket() noexcept;
		Socket(const Socket&) = delete;
		Socket(Socket&&) = default;
		virtual ~Socket();
		Socket& operator=(const Socket&) = delete;
		Socket& operator=(Socket&&) = default;

		inline void SetRelays(Manager* relays) noexcept { m_RelayManager = relays; }

		[[nodiscard]] bool BeginAccept(const RelayPort rport, const RelayHop hop,
									   const IPEndpoint& lendpoint, const IPEndpoint& pendpoint) noexcept;
		[[nodiscard]] bool CompleteAccept() noexcept;

		[[nodiscard]] bool BeginConnect(const IPEndpoint& endpoint) noexcept override;
		[[nodiscard]] bool CompleteConnect() noexcept override;

		[[nodiscard]] bool Send(Buffer& buffer) noexcept override;
		[[nodiscard]] bool SendTo(const IPEndpoint& endpoint, Buffer& buffer) noexcept override { return false; }
		[[nodiscard]] bool Receive(Buffer& buffer) noexcept override;
		[[nodiscard]] bool ReceiveFrom(IPEndpoint& endpoint, Buffer& buffer) noexcept override { return false; }

		void Close(const bool linger = false) noexcept override;

		inline const IOStatus& GetIOStatus() const noexcept override { return m_IOStatus; }
		[[nodiscard]] bool UpdateIOStatus(const std::chrono::milliseconds& mseconds,
										  const IOStatus::Update ioupdate = IOStatus::Update::All) noexcept override;

		const SystemTime GetConnectedTime() const noexcept override;
		inline const SteadyTime& GetConnectedSteadyTime() const noexcept override { return m_ConnectedSteadyTime; }
		inline const Size GetBytesReceived() const noexcept override { return m_BytesReceived; }
		inline const Size GetBytesSent() const noexcept override { return m_BytesSent; }

		inline const IPEndpoint& GetLocalEndpoint() const noexcept override { return m_LocalEndpoint; }
		inline const IPAddress& GetLocalIPAddress() const noexcept override { return m_LocalEndpoint.GetIPAddress(); }
		inline const String GetLocalName() const noexcept override { return m_LocalEndpoint.GetString(); }
		inline const UInt32 GetLocalPort() const noexcept override { return m_LocalEndpoint.GetPort(); }

		inline const IPEndpoint& GetPeerEndpoint() const noexcept override { return m_PeerEndpoint; }
		inline const IPAddress& GetPeerIPAddress() const noexcept override { return m_PeerEndpoint.GetIPAddress(); }
		inline const UInt32 GetPeerPort() const noexcept override { return m_PeerEndpoint.GetPort(); }
		inline const String GetPeerName() const noexcept override { return m_PeerEndpoint.GetString(); }

		inline void SetConnectingCallback(ConnectingCallback&& callback) noexcept override
		{
			m_ConnectingCallback = std::move(callback);
		}

		inline void SetAcceptCallback(AcceptCallback&& callback) noexcept override
		{
			m_AcceptCallback = std::move(callback);
		}

		inline void SetConnectCallback(ConnectCallback&& callback) noexcept override
		{
			m_ConnectCallback = std::move(callback);
		}

		inline void SetCloseCallback(CloseCallback&& callback) noexcept override
		{
			m_CloseCallback = std::move(callback);
		}

	private:
		void SetLocalEndpoint(const IPEndpoint& endpoint, const RelayPort rport, const RelayHop hop) noexcept;

		bool AddToReceiveQueue(Buffer&& buffer) noexcept;

		inline void SetException(const Int errorcode) noexcept
		{
			m_IOStatus.SetException(true);
			m_IOStatus.SetErrorCode(errorcode);
		}

		inline void SetWrite() noexcept { m_IOStatus.SetWrite(true); }
		inline void SetRead() noexcept { m_ClosingRead = true; }

	private:
		IOStatus m_IOStatus;

		Manager* m_RelayManager{ nullptr };
		bool m_ClosingRead{ false };

		Size m_BytesReceived{ 0 };
		Size m_BytesSent{ 0 };

		IPEndpoint m_LocalEndpoint;
		IPEndpoint m_PeerEndpoint;

		SteadyTime m_ConnectedSteadyTime;

		RelayDataQueue m_ReceiveQueue;

		ConnectingCallback m_ConnectingCallback{ []() mutable noexcept {} };
		AcceptCallback m_AcceptCallback{ []() mutable noexcept {} };
		ConnectCallback m_ConnectCallback{ []() mutable noexcept -> bool { return true; } };
		CloseCallback m_CloseCallback{ []() mutable noexcept {} };
	};
}