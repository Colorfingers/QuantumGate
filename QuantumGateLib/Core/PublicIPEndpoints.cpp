// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "PublicIPEndpoints.h"
#include "..\Common\ScopeGuard.h"
#include "..\Crypto\Crypto.h"

using namespace std::literals;

namespace QuantumGate::Implementation::Core
{
	const bool PublicIPEndpoints::Initialize() noexcept
	{
		if (m_Initialized) return true;

		PreInitialize();

		if (!InitializeSockets()) return false;

		// Upon failure deinitialize sockets
		auto sg = MakeScopeGuard([&] { DeinitializeSockets(); });

		if (!m_ThreadPool.AddThread(L"QuantumGate PublicIPEndpoints Thread",
									MakeCallback(this, &PublicIPEndpoints::WorkerThreadProcessor)))
		{
			LogErr(L"Could add PublicIPEndpoints thread");
			return false;
		}

		m_ThreadPool.SetWorkerThreadsMaxBurst(64);
		m_ThreadPool.SetWorkerThreadsMaxSleep(1ms);

		if (!m_ThreadPool.Startup())
		{
			LogErr(L"PublicIPEndpoints threadpool initialization failed");
			return false;
		}

		sg.Deactivate();

		m_Initialized = true;

		return true;
	}

	void PublicIPEndpoints::Deinitialize() noexcept
	{
		if (!m_Initialized) return;

		m_ThreadPool.Shutdown();

		DeinitializeSockets();

		ResetState();

		m_Initialized = false;
	}

	void PublicIPEndpoints::PreInitialize() noexcept
	{
		ResetState();
	}

	void PublicIPEndpoints::ResetState() noexcept
	{
		m_ThreadPool.Clear();
		m_IPEndpoints.WithUniqueLock()->clear();
		m_ReportingNetworks.clear();
	}

	[[nodiscard]] const bool PublicIPEndpoints::InitializeSockets() noexcept
	{
		auto success = true;
		auto nat_traversal = true;

		// Upon failure deinitialize sockets
		auto sg = MakeScopeGuard([&] { DeinitializeSockets(); });

		m_ThreadPool.Data().IPv4UDPSocket.WithUniqueLock([&](Network::Socket& socket)
		{
			constexpr auto endpoint = IPEndpoint(IPAddress::AnyIPv4(), 9001);
			socket = Network::Socket(endpoint.GetIPAddress().GetFamily(), SOCK_DGRAM, IPPROTO_UDP);

			if (!socket.Bind(endpoint, nat_traversal))
			{
				success = false;
				LogErr(L"Could not bind socket to endpoint %s", endpoint.GetString().c_str());
			}
		});

		if (!success) return false;

		m_ThreadPool.Data().IPv6UDPSocket.WithUniqueLock([&](Network::Socket& socket)
		{
			constexpr auto endpoint = IPEndpoint(IPAddress::AnyIPv6(), 9001);
			socket = Network::Socket(endpoint.GetIPAddress().GetFamily(), SOCK_DGRAM, IPPROTO_UDP);

			if (!socket.Bind(endpoint, nat_traversal))
			{
				success = false;
				LogErr(L"Could not bind socket to endpoint %s", endpoint.GetString().c_str());
			}
		});

		if (!success) return false;

		sg.Deactivate();

		return true;
	}

	void PublicIPEndpoints::DeinitializeSockets() noexcept
	{
		m_ThreadPool.Data().IPv4UDPSocket.WithUniqueLock([](Network::Socket& socket)
		{
			if (socket.GetIOStatus().IsOpen()) socket.Close();
		});

		m_ThreadPool.Data().IPv6UDPSocket.WithUniqueLock([](Network::Socket& socket)
		{
			if (socket.GetIOStatus().IsOpen()) socket.Close();
		});
	}

	const std::pair<bool, bool> PublicIPEndpoints::WorkerThreadProcessor(ThreadPoolData& thpdata,
																		 const Concurrency::EventCondition& shutdown_event)
	{
		auto success = true;
		auto didwork = false;

		const std::array<ThreadPoolData::Socket_ThS*, 2> sockets{ &thpdata.IPv4UDPSocket, &thpdata.IPv6UDPSocket };

		for (auto socket_ths : sockets)
		{
			// Check if we have a read event waiting for us
			if (socket_ths->WithUniqueLock()->UpdateIOStatus(0ms))
			{
				if (socket_ths->WithSharedLock()->GetIOStatus().CanRead())
				{
					IPEndpoint sender_endpoint;
					std::optional<UInt64> num;

					socket_ths->WithUniqueLock([&](Network::Socket& socket)
					{
						LogInfo(L"Receiving on endpoint %s", socket.GetLocalEndpoint().GetString().c_str());

						Buffer buffer;

						if (socket.ReceiveFrom(sender_endpoint, buffer))
						{
							num = *reinterpret_cast<UInt64*>(buffer.GetBytes());

							LogInfo(L"Received %llu from endpoint %s", num.value(), sender_endpoint.GetString().c_str());
						}
					});

					if (num.has_value())
					{
						m_IPAddressVerification.WithUniqueLock([&](auto& verification_map)
						{
							if (const auto it = verification_map.find(*num); it != verification_map.end())
							{
								m_IPEndpoints.WithUniqueLock([&](auto& ipendpoints)
								{
									if (const auto it2 = ipendpoints.find(it->second.IPAddress); it2 != ipendpoints.end())
									{
										it2->second.Verified = true;

										LogInfo(L"Verified public IP address %s",
												IPAddress(it->second.IPAddress).GetString().c_str());
									}
									else
									{
										LogErr(L"Couldn't verify IP address %s",
											   IPAddress(it->second.IPAddress).GetString().c_str());
									}
								});

								verification_map.erase(it);
							}
							else
							{
								LogErr(L"Received invalid %llu from endpoint %s", num, sender_endpoint.GetString().c_str());
							}
						});
					}

					didwork = true;
				}
				else if (socket_ths->WithSharedLock()->GetIOStatus().HasException())
				{
					LogErr(L"Exception on listener socket for endpoint %s (%s)",
						   socket_ths->WithSharedLock()->GetLocalEndpoint().GetString().c_str(),
						   GetSysErrorString(socket_ths->WithSharedLock()->GetIOStatus().GetErrorCode()).c_str());

					success = false;
				}
			}
			else
			{
				LogErr(L"Could not get status of listener socket for endpoint %s",
					   socket_ths->WithSharedLock()->GetLocalEndpoint().GetString().c_str());

				success = false;
			}
		}

		return std::make_pair(success, didwork);
	}

	const bool PublicIPEndpoints::AddIPAddressVerification(const BinaryIPAddress& ip) noexcept
	{
		try
		{
			const auto num = Crypto::GetCryptoRandomNumber();
			if (num)
			{
				auto verification_map = m_IPAddressVerification.WithUniqueLock();

				[[maybe_unused]] const auto[it, inserted] = verification_map->emplace(
					std::make_pair(*num, IPAddressVerification{ IPAddressVerification::Status::Registered,
								   ip, Util::GetCurrentSteadyTime() }));
				if (inserted)
				{
					if (SendIPAddressVerification(ip, *num))
					{
						it->second.Status = IPAddressVerification::Status::VerificationSent;
						it->second.LastUpdate = Util::GetCurrentSteadyTime();
						return true;
					}
				}
			}
		}
		catch (const std::exception& e)
		{
			LogErr(L"Could not add public IP address verification due to exception: %s",
				   Util::ToStringW(e.what()).c_str());
		}
		catch (...) {}

		return false;
	}

	const bool PublicIPEndpoints::SendIPAddressVerification(const BinaryIPAddress& ip, const UInt64 num) noexcept
	{
		ThreadPoolData::Socket_ThS* socket_ths = (ip.AddressFamily == IPAddressFamily::IPv4) ?
			&m_ThreadPool.Data().IPv4UDPSocket : &m_ThreadPool.Data().IPv6UDPSocket;

		LogInfo(L"Send %llu on endpoint", num);

		Buffer snd_buf(reinterpret_cast<const Byte*>(&num), sizeof(num));
		if (socket_ths->WithUniqueLock()->SendTo(IPEndpoint(ip, 9001), snd_buf))
		{
			if (snd_buf.IsEmpty()) return true;
		}

		return false;
	}

	Result<std::pair<bool, bool>> PublicIPEndpoints::AddIPEndpoint(const IPEndpoint& pub_endpoint,
																   const IPEndpoint& rep_peer,
																   const PeerConnectionType rep_con_type,
																   const bool trusted) noexcept
	{
		if (rep_con_type != PeerConnectionType::Unknown &&
			pub_endpoint.GetIPAddress().GetFamily() == rep_peer.GetIPAddress().GetFamily())
		{
			AddIPAddressVerification(pub_endpoint.GetIPAddress().GetBinary());

			// Should be in public network address range
			if (!pub_endpoint.GetIPAddress().IsLocal() &&
				!pub_endpoint.GetIPAddress().IsMulticast() &&
				!pub_endpoint.GetIPAddress().IsReserved())
			{
				BinaryIPAddress network;
				const auto cidr = (rep_peer.GetIPAddress().GetFamily() == IPAddressFamily::IPv4) ?
					ReportingPeerNetworkIPv4CIDR : ReportingPeerNetworkIPv6CIDR;

				if (BinaryIPAddress::GetNetwork(rep_peer.GetIPAddress().GetBinary(), cidr, network))
				{
					if (AddReportingNetwork(network, trusted))
					{
						// Upon failure to add the public IP address details remove the network
						auto sg = MakeScopeGuard([&] { RemoveReportingNetwork(network); });

						auto ipendpoints = m_IPEndpoints.WithUniqueLock();

						const auto[pub_ipd, new_insert] =
							GetIPEndpointDetails(pub_endpoint.GetIPAddress().GetBinary(), *ipendpoints);
						if (pub_ipd != nullptr)
						{
							sg.Deactivate();

							pub_ipd->LastUpdateSteadyTime = Util::GetCurrentSteadyTime();

							if (trusted) pub_ipd->Trusted = true;

							try
							{
								// Only interested in the port for inbound peers
								// so we know what public port they actually used
								// to connect to us
								if (rep_con_type == PeerConnectionType::Inbound &&
									pub_ipd->Ports.size() < MaxPortsPerIPAddress)
								{
									pub_ipd->Ports.emplace(pub_endpoint.GetPort());
								}

								if (pub_ipd->ReportingPeerNetworkHashes.size() < MaxReportingPeerNetworks)
								{
									pub_ipd->ReportingPeerNetworkHashes.emplace(network.GetHash());
								}

								return std::make_pair(true, new_insert);
							}
							catch (...) {}
						}
					}
					else return std::make_pair(false, false);
				}
			}
		}

		return ResultCode::Failed;
	}

	std::pair<PublicIPEndpointDetails*, bool>
		PublicIPEndpoints::GetIPEndpointDetails(const BinaryIPAddress& pub_ip, IPEndpointsMap& ipendpoints) noexcept
	{
		auto new_insert = false;
		PublicIPEndpointDetails* pub_ipd{ nullptr };

		if (const auto it = ipendpoints.find(pub_ip); it != ipendpoints.end())
		{
			pub_ipd = &it->second;
		}
		else
		{
			if (ipendpoints.size() >= MaxIPEndpoints)
			{
				RemoveLeastRecentIPEndpoints(ipendpoints.size() - MaxIPEndpoints, ipendpoints);
			}

			if (ipendpoints.size() < MaxIPEndpoints)
			{
				try
				{
					const auto[iti, inserted] = ipendpoints.emplace(
						std::make_pair(pub_ip, PublicIPEndpointDetails{}));

					pub_ipd = &iti->second;
					new_insert = inserted;
				}
				catch (...) {}
			}
		}

		return std::make_pair(pub_ipd, new_insert);
	}

	const bool PublicIPEndpoints::RemoveLeastRecentIPEndpoints(Size num, IPEndpointsMap& ipendpoints) noexcept
	{
		if (!ipendpoints.empty())
		{
			try
			{
				struct MinimalIPEndpointDetails
				{
					BinaryIPAddress IPAddress;
					bool Trusted{ false };
					SteadyTime LastUpdateSteadyTime;
				};

				Vector<MinimalIPEndpointDetails> temp_endp;

				std::for_each(ipendpoints.begin(), ipendpoints.end(), [&](const auto& it)
				{
					temp_endp.emplace_back(MinimalIPEndpointDetails{ it.first, it.second.Trusted,
										   it.second.LastUpdateSteadyTime });
				});

				// Sort by least trusted and least recent
				std::sort(temp_endp.begin(), temp_endp.end(), [](const auto& a, const auto& b) noexcept
				{
					if (!a.Trusted && b.Trusted) return true;
					if (a.Trusted && !b.Trusted) return false;

					return (a.LastUpdateSteadyTime < b.LastUpdateSteadyTime);
				});

				if (num > temp_endp.size()) num = temp_endp.size();

				// Remove first few items which should be
				// least trusted and least recent;
				for (Size x = 0; x < num; x++)
				{
					ipendpoints.erase((temp_endp.begin() + x)->IPAddress);
				}
			}
			catch (...) { return false; }
		}

		return true;
	}

	Result<> PublicIPEndpoints::AddIPAddresses(Vector<BinaryIPAddress>& ips) const noexcept
	{
		try
		{
			auto ipendpoints = m_IPEndpoints.WithSharedLock();

			for (const auto it : *ipendpoints)
			{
				if (std::find(ips.begin(), ips.end(), it.first) == ips.end())
				{
					ips.emplace_back(it.first);
				}
			}

			return ResultCode::Succeeded;
		}
		catch (const std::exception& e)
		{
			LogErr(L"Could not add public IP addresses due to exception: %s",
				   Util::ToStringW(e.what()).c_str());
		}
		catch (...) {}

		return ResultCode::Failed;
	}

	Result<> PublicIPEndpoints::AddIPAddresses(Vector<IPAddressDetails>& ips) const noexcept
	{
		try
		{
			auto ipendpoints = m_IPEndpoints.WithSharedLock();

			for (const auto it : *ipendpoints)
			{
				const auto it2 = std::find_if(ips.begin(), ips.end(), [&](const auto& ipd)
				{
					return (ipd.IPAddress.GetBinary() == it.first);
				});

				if (it2 == ips.end())
				{
					auto& ipdetails = ips.emplace_back();
					ipdetails.IPAddress = it.first;
					ipdetails.BoundToLocalEthernetInterface = false;

					ipdetails.PublicDetails.emplace();
					ipdetails.PublicDetails->ReportedByPeers = true;
					ipdetails.PublicDetails->ReportedByTrustedPeers = it.second.Trusted;
					ipdetails.PublicDetails->NumReportingNetworks = it.second.ReportingPeerNetworkHashes.size();
					ipdetails.PublicDetails->Verified = it.second.Verified;
				}
				else
				{
					// May be a locally configured IP that's also
					// publicly visible; add the public details 
					if (!it2->PublicDetails.has_value())
					{
						it2->PublicDetails.emplace();
						it2->PublicDetails->ReportedByPeers = true;
						it2->PublicDetails->ReportedByTrustedPeers = it.second.Trusted;
						it2->PublicDetails->NumReportingNetworks = it.second.ReportingPeerNetworkHashes.size();
						it2->PublicDetails->Verified = it.second.Verified;
					}
				}
			}

			return ResultCode::Succeeded;
		}
		catch (const std::exception& e)
		{
			LogErr(L"Could not add public IP addresses due to exception: %s",
				   Util::ToStringW(e.what()).c_str());
		}
		catch (...) {}

		return ResultCode::Failed;
	}

	const bool PublicIPEndpoints::IsNewReportingNetwork(const BinaryIPAddress& network) const noexcept
	{
		return (m_ReportingNetworks.find(network) == m_ReportingNetworks.end());
	}

	const bool PublicIPEndpoints::AddReportingNetwork(const BinaryIPAddress& network, const bool trusted) noexcept
	{
		if (!IsNewReportingNetwork(network))
		{
			if (!trusted) return false;
			else
			{
				// If the peer is trusted we are very much interested
				// in the public IP and port that it reports back to us
				// even if we already heard from the network it's on
				return true;
			}
		}

		while (m_ReportingNetworks.size() >= MaxReportingPeerNetworks)
		{
			// Remove the network that's least recent
			const auto it = std::min_element(m_ReportingNetworks.begin(), m_ReportingNetworks.end(),
											 [](const auto& a, const auto& b)
			{
				return (a.second < b.second);
			});

			m_ReportingNetworks.erase(it);
		}

		try
		{
			m_ReportingNetworks.emplace(std::make_pair(network, Util::GetCurrentSteadyTime()));
			return true;
		}
		catch (...) {}

		return false;
	}

	void PublicIPEndpoints::RemoveReportingNetwork(const BinaryIPAddress& network) noexcept
	{
		m_ReportingNetworks.erase(network);
	}
}