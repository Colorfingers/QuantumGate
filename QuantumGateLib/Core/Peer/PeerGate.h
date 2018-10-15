// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\..\Network\Socket.h"
#include "..\Relay\RelaySocket.h"

#include <variant>

namespace QuantumGate::Implementation::Core::Peer
{
	using namespace QuantumGate::Implementation::Network;

	enum class GateType
	{
		Unknown, Socket, RelaySocket
	};

	class Gate
	{
	public:
		Gate(const GateType type);
		Gate(const IP::AddressFamily af, const Socket::Type type, const IP::Protocol protocol);
		Gate(const Gate&) = delete;
		Gate(Gate&&) = default;
		virtual ~Gate();
		Gate& operator=(const Gate&) = delete;
		Gate& operator=(Gate&&) = default;

		template<typename T>
		T& GetSocket() noexcept { return *dynamic_cast<T*>(m_Socket); }

		const GateType GetGateType() const noexcept { return m_Type; }

		const bool BeginConnect(const IPEndpoint& endpoint) noexcept
		{
			assert(m_Socket); return m_Socket->BeginConnect(endpoint);
		}

		const bool CompleteConnect() noexcept { assert(m_Socket); return m_Socket->CompleteConnect(); }

		const bool Send(Buffer& buffer) noexcept { assert(m_Socket); return m_Socket->Send(buffer); }
		const bool Receive(Buffer& buffer) noexcept { assert(m_Socket); return m_Socket->Receive(buffer); }

		void Close(const bool linger = false) noexcept { assert(m_Socket); return m_Socket->Close(linger); }

		const Socket::IOStatus& GetIOStatus() const noexcept { assert(m_Socket); return m_Socket->GetIOStatus(); }

		const bool UpdateIOStatus(const std::chrono::milliseconds& mseconds) noexcept
		{
			assert(m_Socket); return m_Socket->UpdateIOStatus(mseconds);
		}

		const SystemTime GetConnectedTime() const noexcept { assert(m_Socket); return m_Socket->GetConnectedTime(); }

		const SteadyTime& GetConnectedSteadyTime() const noexcept
		{
			assert(m_Socket); return m_Socket->GetConnectedSteadyTime();
		}

		const Size GetBytesReceived() const noexcept { assert(m_Socket); return m_Socket->GetBytesReceived(); }
		const Size GetBytesSent() const noexcept { assert(m_Socket); return m_Socket->GetBytesSent(); }

		const IPEndpoint& GetLocalEndpoint() const noexcept { assert(m_Socket); return m_Socket->GetLocalEndpoint(); }
		const IPAddress& GetLocalIPAddress() const noexcept { assert(m_Socket); return m_Socket->GetLocalIPAddress(); }
		virtual const String GetLocalName() const noexcept { assert(m_Socket); return m_Socket->GetLocalName(); }
		const UInt32 GetLocalPort() const noexcept { assert(m_Socket); return m_Socket->GetLocalPort(); }

		const IPEndpoint& GetPeerEndpoint() const noexcept { assert(m_Socket); return m_Socket->GetPeerEndpoint(); }
		const IPAddress& GetPeerIPAddress() const noexcept { assert(m_Socket); return m_Socket->GetPeerIPAddress(); }
		const UInt32 GetPeerPort() const noexcept { assert(m_Socket); return m_Socket->GetPeerPort(); }
		virtual const String GetPeerName() const noexcept { assert(m_Socket); return m_Socket->GetPeerName(); }

	protected:
		virtual void OnConnecting() noexcept {}
		virtual void OnAccept() noexcept {}
		virtual const bool OnConnect() noexcept { return true; }
		virtual void OnClose() noexcept {}

	private:
		void SetCallbacks() noexcept;

	private:
		SocketBase* m_Socket{ nullptr };
		typename std::aligned_storage<std::max(sizeof(Socket), sizeof(Relay::Socket))>::type m_SocketStorage{ 0 };
		GateType m_Type{ GateType::Unknown };
	};
}