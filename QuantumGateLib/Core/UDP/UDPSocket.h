// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\..\Network\Socket.h"
#include "UDPConnectionData.h"

namespace QuantumGate::Implementation::Core::UDP::Connection
{
	class Connection;
}

namespace QuantumGate::Implementation::Core::UDP
{
	class Socket final : public Network::SocketBase
	{
		friend class Connection::Connection;

	public:
		Socket() noexcept;
		Socket(const Socket&) = delete;
		Socket(Socket&&) noexcept = default;
		virtual ~Socket();
		Socket& operator=(const Socket&) = delete;
		Socket& operator=(Socket&&) noexcept = default;

		[[nodiscard]] inline Concurrency::Event& GetReceiveEvent() noexcept { return m_ConnectionData->WithUniqueLock()->GetReceiveEvent(); }
		[[nodiscard]] inline const Concurrency::Event& GetReceiveEvent() const noexcept { return m_ConnectionData->WithSharedLock()->GetReceiveEvent(); }

		[[nodiscard]] bool Accept(const std::shared_ptr<Listener::SendQueue_ThS>& send_queue,
								  const IPEndpoint& lendpoint, const IPEndpoint& pendpoint) noexcept;

		[[nodiscard]] bool BeginConnect(const Endpoint& endpoint) noexcept override;
		[[nodiscard]] bool CompleteConnect() noexcept override;

		[[nodiscard]] Result<Size> Send(const BufferView& buffer, const Size max_snd_size = 0) noexcept override;
		[[nodiscard]] Result<Size> SendTo(const Endpoint& endpoint, const BufferView& buffer, const Size max_snd_size = 0) noexcept override { return ResultCode::Failed; }
		[[nodiscard]] Result<Size> Receive(Buffer& buffer, const Size max_rcv_size = 0) noexcept override;
		[[nodiscard]] Result<Size> ReceiveFrom(Endpoint& endpoint, Buffer& buffer, const Size max_rcv_size = 0) noexcept override { return ResultCode::Failed; }

		void Close(const bool linger = false) noexcept override;

		[[nodiscard]] inline const IOStatus& GetIOStatus() const noexcept override { return m_IOStatus; }
		[[nodiscard]] bool UpdateIOStatus(const std::chrono::milliseconds& mseconds) noexcept override;

		[[nodiscard]] bool CanSuspend() const noexcept override { return true; }
		[[nodiscard]] std::optional<SteadyTime> GetLastSuspendedSteadyTime() const noexcept override { return m_LastSuspendedSteadyTime; }
		[[nodiscard]] std::optional<SteadyTime> GetLastResumedSteadyTime() const noexcept override { return m_LastResumedSteadyTime; }

		[[nodiscard]] SystemTime GetConnectedTime() const noexcept override;
		[[nodiscard]] inline const SteadyTime& GetConnectedSteadyTime() const noexcept override { return m_ConnectedSteadyTime; }
		[[nodiscard]] inline Size GetBytesReceived() const noexcept override { return m_BytesReceived; }
		[[nodiscard]] inline Size GetBytesSent() const noexcept override { return m_BytesSent; }

		[[nodiscard]] inline const Endpoint& GetLocalEndpoint() const noexcept override { return m_LocalEndpoint; }
		[[nodiscard]] inline String GetLocalName() const noexcept override { return m_LocalEndpoint.GetString(); }

		[[nodiscard]] inline const Endpoint& GetPeerEndpoint() const noexcept override { return m_PeerEndpoint; }
		[[nodiscard]] inline String GetPeerName() const noexcept override { return m_PeerEndpoint.GetString(); }

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
		inline void SetConnectionData(const std::shared_ptr<ConnectionData_ThS>& buffers) noexcept { m_ConnectionData = buffers; }
		void UpdateSocketInfo() noexcept;
		void SetException(const Int errorcode) noexcept;

	private:
		static constexpr Size MinSendBufferSize{ 1u << 16 }; // 65KB

	private:
		mutable Network::Socket::IOStatus m_IOStatus;

		Size m_BytesReceived{ 0 };
		Size m_BytesSent{ 0 };

		Endpoint m_LocalEndpoint;
		Endpoint m_PeerEndpoint;

		SteadyTime m_ConnectedSteadyTime;
		std::optional<SteadyTime> m_LastSuspendedSteadyTime;
		std::optional<SteadyTime> m_LastResumedSteadyTime;

		Size m_MaxSendBufferSize{ MinSendBufferSize };
		std::shared_ptr<ConnectionData_ThS> m_ConnectionData;

		ConnectingCallback m_ConnectingCallback{ []() mutable noexcept {} };
		AcceptCallback m_AcceptCallback{ []() mutable noexcept {} };
		ConnectCallback m_ConnectCallback{ []() mutable noexcept -> bool { return true; } };
		CloseCallback m_CloseCallback{ []() mutable noexcept {} };
	};
}