// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "PeerMessageProcessor.h"
#include "PeerManager.h"
#include "Peer.h"
#include "..\..\Memory\BufferReader.h"
#include "..\..\Memory\BufferWriter.h"

using namespace QuantumGate::Implementation::Memory;
using namespace std::literals;

namespace QuantumGate::Implementation::Core::Peer
{
	const bool MessageProcessor::SendBeginRelay(const RelayPort rport, const IPEndpoint& endpoint,
												const RelayHop hops) const noexcept
	{
		Dbg(L"*********** SendBeginRelay ***********");

		try
		{
			BufferWriter wrt(true);
			if (wrt.WriteWithPreallocation(rport, Network::SerializedBinaryIPAddress{ endpoint.GetIPAddress().GetBinary() },
										   endpoint.GetPort(), hops))
			{
				if (m_Peer.Send(MessageType::RelayCreate, wrt.MoveWrittenBytes()))
				{
					return true;
				}
				else LogDbg(L"Couldn't send RelayCreate message to peer %s", m_Peer.GetPeerName().c_str());
			}
			else LogDbg(L"Couldn't prepare RelayCreate message for peer %s", m_Peer.GetPeerName().c_str());
		}
		catch (...) {}

		return false;
	}

	const bool MessageProcessor::SendEndRelay(const RelayPort rport) const noexcept
	{
		Dbg(L"*********** SendEndRelay ***********");

		return SendRelayStatus(rport, RelayStatusUpdate::Disconnected);
	}

	const bool MessageProcessor::SendRelayStatus(const RelayPort rport,
												 const RelayStatusUpdate status) const noexcept
	{
		Dbg(L"*********** SendRelayStatus ***********");

		try
		{
			LogDbg(L"Sending relay status %u to peer %s", status, m_Peer.GetPeerName().c_str());

			BufferWriter wrt(true);
			if (wrt.WriteWithPreallocation(rport, status))
			{
				if (m_Peer.Send(MessageType::RelayStatus, wrt.MoveWrittenBytes()))
				{
					return true;
				}
				else LogDbg(L"Couldn't send RelayStatus message to peer %s", m_Peer.GetPeerName().c_str());
			}
			else LogDbg(L"Couldn't prepare RelayStatus message for peer %s", m_Peer.GetPeerName().c_str());
		}
		catch (...) {}

		return false;
	}

	const bool MessageProcessor::SendRelayData(const RelayPort rport, const Buffer& buffer) const noexcept
	{
		BufferWriter wrt(true);
		if (wrt.WriteWithPreallocation(rport, WithSize(buffer, MaxSize::_2MB)))
		{
			// Note that relayed data doesn't get compressed (again) because
			// it is mostly encrypted and random looking so it wouldn't compress well
			if (m_Peer.Send(MessageType::RelayData, wrt.MoveWrittenBytes(), 0ms, false))
			{
				return true;
			}
			else
			{
				LogDbg(L"Couldn't send RelayData message to peer %s for relay port %llu",
						m_Peer.GetPeerName().c_str(), rport);
			}
		}
		else
		{
			LogDbg(L"Couldn't prepare RelayData message to peer %s for relay port %llu",
					m_Peer.GetPeerName().c_str(), rport);
		}

		return false;
	}

	const std::pair<bool, bool> MessageProcessor::ProcessMessageReadyState(const MessageDetails& msg) const
	{
		auto handled = false;
		auto success = false;

		switch (msg.GetMessageType())
		{
			case MessageType::ExtenderUpdate:
			{
				Dbg(L"*********** ExtenderUpdate ***********");

				handled = true;

				if (auto& buffer = msg.GetMessageData(); !buffer.IsEmpty())
				{
					Vector<SerializedUUID> psextlist;

					BufferReader rdr(buffer, true);
					if (rdr.Read(WithSize(psextlist, MaxSize::_65KB)))
					{
						Dbg(L"ExtenderUpdate: %u extenders", psextlist.size());

						if (auto pextlist = ValidateExtenderUUIDs(psextlist); pextlist.has_value())
						{
							success = m_Peer.ProcessPeerExtenderUpdate(std::move(*pextlist));
						}
						else LogDbg(L"Invalid ExtenderUpdate message from peer %s; invalid UUID(s)",
									m_Peer.GetPeerName().c_str());
					}
					else LogDbg(L"Invalid ExtenderUpdate message from peer %s; couldn't read message data",
								m_Peer.GetPeerName().c_str());
				}
				else LogDbg(L"Invalid ExtenderUpdate message from peer %s; data expected", m_Peer.GetPeerName().c_str());

				break;
			}
			case MessageType::RelayCreate:
			{
				Dbg(L"*********** RelayCreate ***********");

				handled = true;

				if (m_Peer.GetRelayManager().IsRunning())
				{
					if (auto& buffer = msg.GetMessageData(); !buffer.IsEmpty())
					{
						RelayPort rport{ 0 };
						Network::SerializedBinaryIPAddress ip;
						UInt16 port{ 0 };
						RelayHop hop{ 0 };

						BufferReader rdr(buffer, true);
						if (rdr.Read(rport, ip, port, hop))
						{
							Relay::Events::Connect rce;
							rce.Port = rport;
							rce.Endpoint = IPEndpoint(IPAddress{ ip }, port);
							rce.Hop = hop;
							rce.Origin.PeerLUID = m_Peer.GetLUID();
							rce.Origin.LocalEndpoint = m_Peer.GetLocalEndpoint();
							rce.Origin.PeerEndpoint = m_Peer.GetPeerEndpoint();

							if (!m_Peer.GetRelayManager().AddRelayEvent(rport, std::move(rce)))
							{
								// Let the peer know we couldn't accept
								SendRelayStatus(rport, RelayStatusUpdate::GeneralFailure);
							}

							success = true;
						}
						else LogDbg(L"Invalid RelayCreate message from peer %s; couldn't read message data",
									m_Peer.GetPeerName().c_str());
					}
					else LogDbg(L"Invalid RelayCreate message from peer %s; data expected", m_Peer.GetPeerName().c_str());
				}
				else LogDbg(L"Received RelayCreate message from peer %s, but relays are not enabled",
							m_Peer.GetPeerName().c_str());

				break;
			}
			case MessageType::RelayStatus:
			{
				Dbg(L"*********** RelayStatus ***********");

				handled = true;

				if (m_Peer.GetRelayManager().IsRunning())
				{
					if (auto& buffer = msg.GetMessageData(); !buffer.IsEmpty())
					{
						RelayPort rport{ 0 };
						RelayStatusUpdate status{ RelayStatusUpdate::GeneralFailure };

						BufferReader rdr(buffer, true);
						if (rdr.Read(rport, status))
						{
							LogDbg(L"Received relay peer status %u for port %llu", status, rport);

							Relay::Events::StatusUpdate resu;
							resu.Port = rport;
							resu.Status = status;
							resu.Origin.PeerLUID = m_Peer.GetLUID();

							if (!m_Peer.GetRelayManager().AddRelayEvent(rport, std::move(resu)))
							{
								LogErr(L"Could not add relay event for port %llu", rport);
							}

							success = true;
						}
						else LogDbg(L"Invalid RelayStatus message from peer %s; couldn't read message data",
									m_Peer.GetPeerName().c_str());
					}
					else LogDbg(L"Invalid RelayStatus message from peer %s; data expected", m_Peer.GetPeerName().c_str());
				}
				else LogDbg(L"Received RelayStatus message from peer %s, but relays are not enabled",
							m_Peer.GetPeerName().c_str());

				break;
			}
			case MessageType::RelayData:
			{
				Dbg(L"*********** RelayData ***********");

				handled = true;

				if (m_Peer.GetRelayManager().IsRunning())
				{
					if (auto& buffer = msg.GetMessageData(); !buffer.IsEmpty())
					{
						RelayPort rport{ 0 };
						Buffer data;

						BufferReader rdr(buffer, true);
						if (rdr.Read(rport, WithSize(data, MaxSize::_2MB)))
						{
							Relay::Events::RelayData red;
							red.Port = rport;
							red.Data = std::move(data);
							red.Origin.PeerLUID = m_Peer.GetLUID();

							if (!m_Peer.GetRelayManager().AddRelayEvent(rport, std::move(red)))
							{
								LogErr(L"Could not add relay event for port %llu", rport);
							}

							success = true;
						}
						else LogDbg(L"Invalid RelayData message from peer %s; couldn't read message data",
									m_Peer.GetPeerName().c_str());
					}
					else LogDbg(L"Invalid RelayData message from peer %s; data expected", m_Peer.GetPeerName().c_str());
				}
				else LogDbg(L"Received RelayData message from peer %s, but relays are not enabled",
							m_Peer.GetPeerName().c_str());

				break;
			}
			case MessageType::BeginPrimaryKeyUpdateExchange:
			{
				if (m_Peer.GetConnectionType() == PeerConnectionType::Outbound)
				{
					if (m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::PrimaryExchange))
					{
						if (m_Peer.InitializeKeyExchange())
						{
							const auto retval = ProcessKeyExchange(msg);
							if (retval.first && retval.second)
							{
								handled = true;
								success = m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::SecondaryExchange);
							}
						}
					}
				}

				break;
			}
			case MessageType::EndPrimaryKeyUpdateExchange:
			{
				if (m_Peer.GetKeyUpdate().GetStatus() == KeyUpdate::Status::PrimaryExchange &&
					m_Peer.GetConnectionType() == PeerConnectionType::Inbound)
				{
					const auto retval = ProcessKeyExchange(msg);
					if (retval.first && retval.second)
					{
						handled = true;
						success = m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::SecondaryExchange);
					}
				}

				break;
			}
			case MessageType::BeginSecondaryKeyUpdateExchange:
			{
				if (m_Peer.GetKeyUpdate().GetStatus() == KeyUpdate::Status::SecondaryExchange &&
					m_Peer.GetConnectionType() == PeerConnectionType::Outbound)
				{
					const auto retval = ProcessKeyExchange(msg);
					if (retval.first && retval.second)
					{
						handled = true;
						success = m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::ReadyWait);
					}
				}

				break;
			}
			case MessageType::EndSecondaryKeyUpdateExchange:
			{
				if (m_Peer.GetKeyUpdate().GetStatus() == KeyUpdate::Status::SecondaryExchange &&
					m_Peer.GetConnectionType() == PeerConnectionType::Inbound)
				{
					const auto retval = ProcessKeyExchange(msg);
					if (retval.first && retval.second)
					{
						handled = true;

						if (m_Peer.Send(MessageType::KeyUpdateReady, Buffer()))
						{
							success = (m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::ReadyWait) &&
									   m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::UpdateWait));
						}
						else LogDbg(L"Couldn't send KeyUpdateReady message to peer %s",
									m_Peer.GetPeerName().c_str());
					}
				}

				break;
			}
			case MessageType::KeyUpdateReady:
			{
				if (m_Peer.GetKeyUpdate().GetStatus() == KeyUpdate::Status::ReadyWait &&
					m_Peer.GetConnectionType() == PeerConnectionType::Outbound)
				{
					handled = true;

					if (auto& buffer = msg.GetMessageData(); buffer.IsEmpty())
					{
						// From now on we encrypt messages using the
						// secondary symmetric key-pair
						m_Peer.GetKeyExchange().StartUsingSecondarySymmetricKeyPairForEncryption();

						success = m_Peer.GetKeyUpdate().SetStatus(KeyUpdate::Status::UpdateWait);
					}
					else LogDbg(L"Invalid KeyUpdateReady message from peer %s; no data expected",
								m_Peer.GetPeerName().c_str());
				}

				break;
			}
			default:
			{
				assert(false);
				break;
			}
		}

		return std::make_pair(handled, success);
	}
}