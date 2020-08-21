// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "UDPSocket.h"

namespace QuantumGate::Implementation::Core::UDP
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

	bool Socket::BeginAccept(const IPEndpoint& lendpoint, const IPEndpoint& pendpoint) noexcept
	{
		assert(m_IOStatus.IsOpen());
		assert(lendpoint.GetProtocol() == pendpoint.GetProtocol());

		m_LocalEndpoint = lendpoint;
		m_PeerEndpoint = pendpoint;

		m_AcceptCallback();

		return true;
	}

	bool Socket::CompleteAccept() noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_IOStatus.SetConnected(true);
		m_IOStatus.SetWrite(true);

		m_ConnectedSteadyTime = Util::GetCurrentSteadyTime();

		return m_ConnectCallback();
	}

	bool Socket::BeginConnect(const IPEndpoint& endpoint) noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_IOStatus.SetConnecting(true);

		// Local endpoint is set by the Relay manager once
		// a connection has been established
		m_PeerEndpoint = endpoint;

		m_ConnectingCallback();

		return true;
	}

	bool Socket::CompleteConnect() noexcept
	{
		assert(m_IOStatus.IsOpen() && m_IOStatus.IsConnecting());

		m_IOStatus.SetConnecting(false);
		m_IOStatus.SetConnected(true);

		m_ConnectedSteadyTime = Util::GetCurrentSteadyTime();

		return m_ConnectCallback();
	}

	void Socket::SetLocalEndpoint(const IPEndpoint& endpoint) noexcept
	{
		assert(endpoint.GetProtocol() == m_PeerEndpoint.GetProtocol());

		m_LocalEndpoint = IPEndpoint(endpoint.GetProtocol(), endpoint.GetIPAddress(),
									 endpoint.GetPort());
		m_PeerEndpoint = IPEndpoint(m_PeerEndpoint.GetProtocol(), m_PeerEndpoint.GetIPAddress(),
									m_PeerEndpoint.GetPort());
	}

	bool Socket::Send(Buffer& buffer, const Size /*max_snd_size*/) noexcept
	{/*
		assert(m_IOStatus.IsOpen() && m_IOStatus.IsConnected() && m_IOStatus.CanWrite());

		if (m_IOStatus.HasException()) return false;

		try
		{
			Size sent_size{ 0 };

			const auto available_size = std::invoke([&]()
			{
				Size size{ 0 };
				if (m_MaxSendBufferSize > m_SendBuffer.GetSize())
				{
					size = m_MaxSendBufferSize - m_SendBuffer.GetSize();
				}
				return size;
			});

			if (available_size >= buffer.GetSize())
			{
				m_SendBuffer += buffer;
				sent_size = buffer.GetSize();
			}
			else if (available_size > 0)
			{
				const auto pbuffer = BufferView(buffer).GetFirst(available_size);
				m_SendBuffer += pbuffer;
				sent_size = pbuffer.GetSize();
			}
			else
			{
				// Send buffer is full, we'll try again later
				LogDbg(L"UDP socket send buffer full/unavailable for endpoint %s", GetPeerName().c_str());
			}

			if (sent_size > 0)
			{
				buffer.RemoveFirst(sent_size);

				m_SendEvent.Set();

				m_BytesSent += sent_size;
			}

			return true;
		}
		catch (const std::exception& e)
		{
			LogErr(L"UDP socket send exception for endpoint %s - %s",
				   GetPeerName().c_str(), Util::ToStringW(e.what()).c_str());

			SetException(WSAENOBUFS);
		}*/

		return false;
	}

	bool Socket::Receive(Buffer& buffer, const Size /* max_rcv_size */) noexcept
	{
		assert(m_IOStatus.IsOpen() && m_IOStatus.IsConnected() && m_IOStatus.CanRead());

		if (m_IOStatus.HasException()) return false;

		auto success = false;
		/*
		try
		{
			const auto bytesrcv = m_ReceiveBuffer.GetSize();

			if (bytesrcv == 0 && m_ClosingRead)
			{
				LogDbg(L"UDP socket connection closed for endpoint %s", GetPeerName().c_str());

				m_ReceiveEvent.Reset();
			}
			else
			{
				buffer += m_ReceiveBuffer;

				m_ReceiveBuffer.Clear();
				m_ReceiveEvent.Reset();

				m_BytesReceived += bytesrcv;

				success = true;
			}
		}
		catch (const std::exception& e)
		{
			LogErr(L"UDP socket receive exception for endpoint %s - %s",
				   GetPeerName().c_str(), Util::ToStringW(e.what()).c_str());

			SetException(WSAENOBUFS);
		}*/

		return success;
	}

	void Socket::Close(const bool linger) noexcept
	{
		assert(m_IOStatus.IsOpen());

		m_CloseCallback();

		m_IOStatus.Reset();
	}

	bool Socket::UpdateIOStatus(const std::chrono::milliseconds& mseconds) noexcept
	{
		assert(m_IOStatus.IsOpen());
		/*
		m_ReceiveEvent.Reset();

		if (!m_IOStatus.IsOpen()) return false;

		if (m_IOStatus.IsConnected())
		{
			const bool read = (!m_ReceiveBuffer.IsEmpty() || m_ClosingRead);

			m_IOStatus.SetRead(read);

			if (read) m_ReceiveEvent.Set();
		}*/

		return true;
	}

	SystemTime Socket::GetConnectedTime() const noexcept
	{
		const auto dif = std::chrono::duration_cast<std::chrono::seconds>(Util::GetCurrentSteadyTime() -
																		  GetConnectedSteadyTime());
		return (Util::GetCurrentSystemTime() - dif);
	}
}