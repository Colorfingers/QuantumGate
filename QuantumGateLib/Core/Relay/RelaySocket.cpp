// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "RelaySocket.h"
#include "RelayManager.h"

namespace QuantumGate::Implementation::Core::Relay
{
	Socket::Socket() noexcept
	{
		m_ConnectedSteadyTime = Util::GetCurrentSteadyTime();
		m_IOStatus.SetOpen(true);
	}

	Socket::~Socket()
	{
		if (m_IOStatus.IsOpen()) Close();
	}

	const bool Socket::BeginAccept(const RelayPort rport, const RelayHop hop,
								   const IPEndpoint& lendpoint, const IPEndpoint& pendpoint) noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_LocalEndpoint = IPEndpoint(lendpoint.GetIPAddress(),
									 lendpoint.GetPort(), rport, hop);
		m_PeerEndpoint = IPEndpoint(pendpoint.GetIPAddress(),
									pendpoint.GetPort(), rport, hop);

		m_OnAcceptCallback();

		return true;
	}

	const bool Socket::CompleteAccept() noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_IOStatus.SetConnected(true);
		m_IOStatus.SetWrite(true);

		m_ConnectedSteadyTime = Util::GetCurrentSteadyTime();

		return m_OnConnectCallback();
	}

	const bool Socket::BeginConnect(const IPEndpoint& endpoint) noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_IOStatus.SetConnecting(true);

		// Local endpoint is set by the Relay manager once
		// a connection has been established
		m_PeerEndpoint = endpoint;

		m_OnConnectingCallback();

		return true;
	}

	const bool Socket::CompleteConnect() noexcept
	{
		assert(m_IOStatus.IsOpen() && m_IOStatus.IsConnecting());

		m_IOStatus.SetConnecting(false);
		m_IOStatus.SetConnected(true);

		m_ConnectedSteadyTime = Util::GetCurrentSteadyTime();

		return m_OnConnectCallback();
	}

	void Socket::SetLocalEndpoint(const IPEndpoint& endpoint, const RelayPort rport, const RelayHop hop) noexcept
	{
		m_LocalEndpoint = IPEndpoint(endpoint.GetIPAddress(),
									 endpoint.GetPort(),
									 rport, hop);
		m_PeerEndpoint = IPEndpoint(m_PeerEndpoint.GetIPAddress(),
									m_PeerEndpoint.GetPort(),
									rport, hop);
	}

	const bool Socket::Send(Buffer& buffer) noexcept
	{
		assert(m_IOStatus.IsOpen() && m_IOStatus.IsConnected() && m_IOStatus.CanWrite());
		assert(m_RelayManager != nullptr);

		if (m_IOStatus.HasException()) return false;

		auto success = false;

		try
		{
			// Update the total amount of bytes sent
			m_BytesSent += buffer.GetSize();

			Events::RelayData red;
			red.Port = m_LocalEndpoint.GetRelayPort();
			red.Data = std::move(buffer);
			red.Origin.PeerLUID = 0;

			success = m_RelayManager->AddRelayEvent(red.Port, std::move(red));
		}
		catch (const std::exception& e)
		{
			LogErr(L"Send exception for endpoint %s - %s",
				   GetPeerName().c_str(), Util::ToStringW(e.what()).c_str());

			SetException(WSAENOBUFS);
		}

		return success;
	}

	const bool Socket::Receive(Buffer& buffer) noexcept
	{
		assert(m_IOStatus.IsOpen() && m_IOStatus.IsConnected() && m_IOStatus.CanRead());

		if (m_IOStatus.HasException()) return false;

		auto success = false;

		try
		{
			Size rcvsize{ 0 };

			while (!m_ReceiveQueue.empty())
			{
				rcvsize += m_ReceiveQueue.front().GetSize();

				if (buffer.IsEmpty()) buffer = std::move(m_ReceiveQueue.front());
				else buffer += m_ReceiveQueue.front();

				m_ReceiveQueue.pop();
			}

			if (!buffer.IsEmpty())
			{
				// Update the total amount of bytes received
				m_BytesReceived += rcvsize;
				success = true;
			}
			else LogDbg(L"Connection closed for endpoint %s", GetPeerName().c_str());
		}
		catch (const std::exception& e)
		{
			LogErr(L"Receive exception for endpoint %s - %s",
				   GetPeerName().c_str(), Util::ToStringW(e.what()).c_str());

			SetException(WSAENOBUFS);
		}

		return success;
	}

	void Socket::Close(const bool linger) noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_OnCloseCallback();

		m_IOStatus.Reset();
	}

	const bool Socket::UpdateIOStatus(const std::chrono::milliseconds& mseconds) noexcept
	{
		assert(m_IOStatus.IsOpen());

		if (!m_IOStatus.IsOpen()) return false;

		if (m_IOStatus.IsConnected())
		{
			m_IOStatus.SetRead(!m_ReceiveQueue.empty() || m_ClosingRead);
		}

		return true;
	}

	const SystemTime Socket::GetConnectedTime() const noexcept
	{
		const auto dif = std::chrono::duration_cast<std::chrono::seconds>(Util::GetCurrentSteadyTime() -
																		  GetConnectedSteadyTime());
		return (Util::GetCurrentSystemTime() - dif);
	}

	const bool Socket::AddToReceiveQueue(Buffer&& buffer) noexcept
	{
		try
		{
			m_ReceiveQueue.push(std::move(buffer));
			return true;
		}
		catch (...) {}

		return false;
	}
}

