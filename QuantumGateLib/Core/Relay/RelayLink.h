// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\Message.h"
#include "..\Peer\Peer.h"

#include <queue>

namespace QuantumGate::Implementation::Core::Relay
{
	enum class Status
	{
		Opened, Connect, Connecting, Connected, Disconnected, Exception, Closed
	};

	enum class Exception
	{
		Unknown, ConnectionReset, GeneralFailure, NoPeersAvailable,
		HostUnreachable, ConnectionRefused, TimedOut
	};

	enum class Position
	{
		Unknown, Beginning, Between, End
	};

	struct PeerDetails final
	{
		PeerLUID PeerLUID{ 0 };
		std::shared_ptr<Peer::Peer_ThS> Peer{ nullptr };
		bool GetStatusUpdates{ true };
	};

	class Link final
	{
	public:
		Link(const PeerLUID ipeer, const PeerLUID opeer,
			 const IPEndpoint& endpoint, const RelayPort port, const RelayHop hop,
			 const Position position) noexcept :
			m_Port(port), m_Hop(hop), m_Endpoint(endpoint),
			m_Position(position), m_IncomingPeer{ ipeer }, m_OutgoingPeer{ opeer }
		{}

		inline const Status GetStatus() const noexcept { return m_Status; }
		inline const Exception GetException() const noexcept { return m_Exception; }
		inline const RelayPort GetPort() const noexcept { return m_Port; }
		inline const RelayHop GetHop() const noexcept { return m_Hop; }
		inline const IPEndpoint& GetEndpoint() const noexcept { return m_Endpoint; }
		inline const SteadyTime GetLastStatusChangeSteadyTime() const noexcept { return m_LastStatusChangeSteadyTime; }
		inline const Position GetPosition() const noexcept { return m_Position; }
		inline PeerDetails& GetIncomingPeer() noexcept { assert(m_IncomingPeer.PeerLUID); return m_IncomingPeer; }
		inline const PeerDetails& GetIncomingPeer() const noexcept { assert(m_IncomingPeer.PeerLUID); return m_IncomingPeer; }
		inline PeerDetails& GetOutgoingPeer() noexcept { assert(m_OutgoingPeer.PeerLUID != 0); return m_OutgoingPeer; }
		inline const PeerDetails& GetOutgoingPeer() const noexcept { assert(m_OutgoingPeer.PeerLUID != 0); return m_OutgoingPeer; }

		const bool UpdateStatus(const PeerLUID from_pluid, const RelayStatusUpdate status) noexcept
		{
			CheckStatusUpdate(from_pluid, status);

			auto success = false;

			switch (status)
			{
				case RelayStatusUpdate::Connected:
					success = UpdateStatus(Status::Connected);
					break;
				case RelayStatusUpdate::Disconnected:
					success = UpdateStatus(Status::Disconnected);
					break;
				case RelayStatusUpdate::ConnectionReset:
					success = UpdateStatus(Status::Exception, Exception::ConnectionReset);
					break;
				case RelayStatusUpdate::GeneralFailure:
					success = UpdateStatus(Status::Exception, Exception::GeneralFailure);
					break;
				case RelayStatusUpdate::HostUnreachable:
					success = UpdateStatus(Status::Exception, Exception::HostUnreachable);
					break;
				case RelayStatusUpdate::ConnectionRefused:
					success = UpdateStatus(Status::Exception, Exception::ConnectionRefused);
					break;
				case RelayStatusUpdate::NoPeersAvailable:
					success = UpdateStatus(Status::Exception, Exception::NoPeersAvailable);
					break;
				case RelayStatusUpdate::TimedOut:
					success = UpdateStatus(Status::Exception, Exception::TimedOut);
					break;
				default:
					assert(false);
					break;
			}

			return success;
		}

		const bool UpdateStatus(const Status status, const Exception exception = Exception::Unknown) noexcept
		{
			auto success = false;
			const auto prev_status = m_Status;

			switch (status)
			{
				case Status::Connect:
				{
					if (m_Status == Status::Opened)
					{
						LogDbg(L"Relay link ready to connect on port %llu (local hop %u)", m_Port, m_Hop);
						m_Status = status;
						success = true;
					}
					else assert(false);
					break;
				}
				case Status::Connecting:
				{
					if (m_Status == Status::Connect)
					{
						LogDbg(L"Relay link connecting on port %llu (local hop %u)", m_Port, m_Hop);
						m_Status = status;
						success = true;
					}
					else assert(false);
					break;
				}
				case Status::Connected:
				{
					if (m_Status == Status::Connect || m_Status == Status::Connecting)
					{
						LogInfo(L"Relay link on port %llu connected (local hop %u)", m_Port, m_Hop);
						m_Status = status;
						success = true;
					}
					else assert(false);
					break;
				}
				case Status::Disconnected:
				{
					if (m_Status == Status::Connected ||
						m_Status == Status::Connecting ||
						m_Status == Status::Connect)
					{
						LogDbg(L"Relay link on port %llu disconnected (local hop %u)", m_Port, m_Hop);
						m_Status = status;
						success = true;
					}
					else assert(false);
					break;
				}
				case Status::Exception:
				{
					if (m_Status != Status::Closed)
					{
						LogDbg(L"Exception %u for relay link on port %llu (local hop %u)", exception, m_Port, m_Hop);
						m_Status = status;
						m_Exception = exception;
						success = true;
					}
					else assert(false);
					break;
				}
				case Status::Closed:
				{
					LogInfo(L"Relay link on port %llu closed (local hop %u)", m_Port, m_Hop);
					m_Status = status;
					success = true;
					break;
				}
				default:
				{
					assert(false);
					break;
				}
			}

			if (success)
			{
				m_LastStatusChangeSteadyTime = Util::GetCurrentSteadyTime();
			}
			else LogErr(L"Failed to change status for relay link on port %llu from %u to %u",
						m_Port, prev_status, status);

			return success;
		}

		const bool SendRelayStatus(Peer::Peer& to_peer, const std::optional<PeerLUID> from_pluid,
								   const RelayStatusUpdate status) noexcept
		{
			if (!MayGetStatusUpdate(to_peer.GetLUID())) return true;

			if (to_peer.GetMessageProcessor().SendRelayStatus(GetPort(), status))
			{
				CheckStatusUpdate(to_peer.GetLUID(), status);
				if (from_pluid) CheckStatusUpdate(*from_pluid, status);

				return true;
			}

			return false;
		}

	private:
		void CheckStatusUpdate(const PeerLUID from_pluid, const RelayStatusUpdate status) noexcept
		{
			switch (status)
			{
				case RelayStatusUpdate::Connected:
				{
					break;
				}
				default:
				{
					assert(m_IncomingPeer.PeerLUID != 0 && m_OutgoingPeer.PeerLUID != 0);

					// Since the peer will be gone we should not be
					// sending/forwarding status updates to it anymore
					if (m_IncomingPeer.PeerLUID == from_pluid)
					{
						m_IncomingPeer.GetStatusUpdates = false;
					}
					if (m_OutgoingPeer.PeerLUID == from_pluid)
					{
						m_OutgoingPeer.GetStatusUpdates = false;
					}

					break;
				}
			}
		}

		const bool MayGetStatusUpdate(const PeerLUID pluid) const noexcept
		{
			assert(m_IncomingPeer.PeerLUID != 0 && m_OutgoingPeer.PeerLUID != 0);

			if (m_IncomingPeer.PeerLUID == pluid &&
				m_IncomingPeer.GetStatusUpdates) return true;
			else if (m_OutgoingPeer.PeerLUID == pluid &&
					 m_OutgoingPeer.GetStatusUpdates) return true;

			return false;
		}

	private:
		Status m_Status{ Status::Opened };
		Exception m_Exception{ Exception::Unknown };
		RelayPort m_Port{ 0 };
		RelayHop m_Hop{ 0 };
		IPEndpoint m_Endpoint;
		SteadyTime m_LastStatusChangeSteadyTime;
		Position m_Position{ Position::Unknown };
		PeerDetails m_IncomingPeer;
		PeerDetails m_OutgoingPeer;
	};

	using Link_ThS = Concurrency::ThreadSafe<Link, std::shared_mutex>;
}