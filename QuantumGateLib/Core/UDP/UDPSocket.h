// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\..\Network\SocketBase.h"
#include "..\..\Concurrency\Event.h"

namespace QuantumGate::Implementation::Core::UDP::Connection
{
	class Manager;
}

namespace QuantumGate::Implementation::Core::UDP
{
	class Socket final : public Network::SocketBase
	{
		friend class Connection::Manager;

	public:
		class IOBuffer final
		{
		public:
			IOBuffer() noexcept = delete;

			IOBuffer(Buffer& buffer, Concurrency::Event& event) noexcept :
				m_Buffer(buffer), m_Event(event)
			{}

			IOBuffer(const IOBuffer&) = delete;
			IOBuffer(IOBuffer&&) noexcept = default;

			~IOBuffer()
			{
				if (!m_Buffer.IsEmpty()) m_Event.Set();
				else m_Event.Reset();
			}

			IOBuffer& operator=(const IOBuffer&) = delete;
			IOBuffer& operator=(IOBuffer&&) noexcept = default;

			inline Buffer* operator->() noexcept { return &m_Buffer; }

			inline Buffer& operator*() noexcept { return m_Buffer; }

		private:
			Buffer& m_Buffer;
			Concurrency::Event& m_Event;
		};

		struct Buffers final
		{
			Buffer m_SendBuffer;
			Concurrency::Event& m_SendEvent;
			Buffer m_ReceiveBuffer;
			Concurrency::Event m_ReceiveEvent;

			Buffers(Concurrency::Event& send_event) noexcept : m_SendEvent(send_event) {}
		};

		using Buffers_ThS = Concurrency::ThreadSafe<Buffers, std::shared_mutex>;

		Socket() noexcept;
		Socket(const Socket&) = delete;
		Socket(Socket&&) noexcept = default;
		virtual ~Socket();
		Socket& operator=(const Socket&) = delete;
		Socket& operator=(Socket&&) noexcept = default;

		[[nodiscard]] inline Concurrency::Event& GetReceiveEvent() noexcept { return m_Buffers->WithUniqueLock()->m_ReceiveEvent; }
		[[nodiscard]] inline const Concurrency::Event& GetReceiveEvent() const noexcept { return m_Buffers->WithSharedLock()->m_ReceiveEvent; }

		[[nodiscard]] bool BeginAccept(const IPEndpoint& lendpoint, const IPEndpoint& pendpoint) noexcept;
		[[nodiscard]] bool CompleteAccept() noexcept;

		[[nodiscard]] bool BeginConnect(const IPEndpoint& endpoint) noexcept override;
		[[nodiscard]] bool CompleteConnect() noexcept override;

		[[nodiscard]] bool Send(Buffer& buffer, const Size max_snd_size = 0) noexcept override;
		[[nodiscard]] bool SendTo(const IPEndpoint& endpoint, Buffer& buffer, const Size max_snd_size = 0) noexcept override { return false; }
		[[nodiscard]] bool Receive(Buffer& buffer, const Size max_rcv_size = 0) noexcept override;
		[[nodiscard]] bool ReceiveFrom(IPEndpoint& endpoint, Buffer& buffer, const Size max_rcv_size = 0) noexcept override { return false; }

		void Close(const bool linger = false) noexcept override;

		[[nodiscard]] inline const IOStatus& GetIOStatus() const noexcept override { return m_IOStatus; }
		[[nodiscard]] bool UpdateIOStatus(const std::chrono::milliseconds& mseconds) noexcept override;

		[[nodiscard]] SystemTime GetConnectedTime() const noexcept override;
		[[nodiscard]] inline const SteadyTime& GetConnectedSteadyTime() const noexcept override { return m_ConnectedSteadyTime; }
		[[nodiscard]] inline Size GetBytesReceived() const noexcept override { return m_BytesReceived; }
		[[nodiscard]] inline Size GetBytesSent() const noexcept override { return m_BytesSent; }

		[[nodiscard]] inline const IPEndpoint& GetLocalEndpoint() const noexcept override { return m_LocalEndpoint; }
		[[nodiscard]] inline const IPAddress& GetLocalIPAddress() const noexcept override { return m_LocalEndpoint.GetIPAddress(); }
		[[nodiscard]] inline String GetLocalName() const noexcept override { return m_LocalEndpoint.GetString(); }
		[[nodiscard]] inline UInt32 GetLocalPort() const noexcept override { return m_LocalEndpoint.GetPort(); }

		[[nodiscard]] inline const IPEndpoint& GetPeerEndpoint() const noexcept override { return m_PeerEndpoint; }
		[[nodiscard]] inline const IPAddress& GetPeerIPAddress() const noexcept override { return m_PeerEndpoint.GetIPAddress(); }
		[[nodiscard]] inline UInt32 GetPeerPort() const noexcept override { return m_PeerEndpoint.GetPort(); }
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
		inline void SetBuffers(const std::shared_ptr<Buffers_ThS>& buffers) noexcept { m_Buffers = buffers; }
		void SetLocalEndpoint(const IPEndpoint& endpoint) noexcept;

		//[[nodiscard]] inline IOBuffer GetSendBuffer() noexcept { return { m_SendBuffer, m_SendEvent }; }
		//[[nodiscard]] inline IOBuffer GetReceiveBuffer() noexcept { return { m_ReceiveBuffer, m_ReceiveEvent }; }

		inline void SetException(const Int errorcode) noexcept
		{
			m_IOStatus.SetException(true);
			m_IOStatus.SetErrorCode(errorcode);
		}

		inline void SetWrite() noexcept { m_IOStatus.SetWrite(true); }
		//inline void SetRead() noexcept { m_ClosingRead = true; m_ReceiveEvent.Set(); }

	private:
		static constexpr Size MinSendBufferSize{ 1u << 16 }; // 65KB

	private:
		IOStatus m_IOStatus;

		bool m_ClosingRead{ false };

		Size m_BytesReceived{ 0 };
		Size m_BytesSent{ 0 };

		IPEndpoint m_LocalEndpoint;
		IPEndpoint m_PeerEndpoint;

		SteadyTime m_ConnectedSteadyTime;

		Size m_MaxSendBufferSize{ MinSendBufferSize };
		std::shared_ptr<Buffers_ThS> m_Buffers;

		ConnectingCallback m_ConnectingCallback{ []() mutable noexcept {} };
		AcceptCallback m_AcceptCallback{ []() mutable noexcept {} };
		ConnectCallback m_ConnectCallback{ []() mutable noexcept -> bool { return true; } };
		CloseCallback m_CloseCallback{ []() mutable noexcept {} };
	};
}