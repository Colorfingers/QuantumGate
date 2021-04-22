// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "PeerManager.h"
#include "..\..\Common\Random.h"
#include "..\..\Common\ScopeGuard.h"
#include "..\..\Memory\BufferReader.h"
#include "..\..\Memory\BufferWriter.h"
#include "..\..\API\Access.h"

using namespace std::literals;

namespace QuantumGate::Implementation::Core::Peer
{
	bool Manager::ThreadPoolData::AddWorkEvent(const Peer& peer) noexcept
	{
		switch (peer.GetGateType())
		{
			case GateType::TCPSocket:
				return WorkEvents.AddEvent(peer.GetSocket<TCP::Socket>().GetEvent().GetHandle());
			case GateType::UDPSocket:
				return WorkEvents.AddEvent(peer.GetSocket<UDP::Socket>().GetReceiveEvent().GetHandle());
			case GateType::RelaySocket:
				return WorkEvents.AddEvent(peer.GetSocket<Relay::Socket>().GetReceiveEvent().GetHandle());
			default:
				// Shouldn't get here
				assert(false);
				break;
		}

		return false;
	}

	void Manager::ThreadPoolData::RemoveWorkEvent(const Peer& peer) noexcept
	{
		switch (peer.GetGateType())
		{
			case GateType::TCPSocket:
				WorkEvents.RemoveEvent(peer.GetSocket<TCP::Socket>().GetEvent().GetHandle());
				break;
			case GateType::UDPSocket:
				WorkEvents.RemoveEvent(peer.GetSocket<UDP::Socket>().GetReceiveEvent().GetHandle());
				break;
			case GateType::RelaySocket:
				WorkEvents.RemoveEvent(peer.GetSocket<Relay::Socket>().GetReceiveEvent().GetHandle());
				break;
			default:
				// Shouldn't get here
				assert(false);
				break;
		}
	}

	Manager::Manager(const Settings_CThS& settings, LocalEnvironment_ThS& environment, UDP::Connection::Manager& udpmgr,
					 KeyGeneration::Manager& keymgr, Access::Manager& accessmgr,
					 Extender::Manager& extenders) noexcept :
		m_Settings(settings), m_LocalEnvironment(environment), m_UDPConnectionManager(udpmgr), m_KeyGenerationManager(keymgr),
		m_AccessManager(accessmgr), m_ExtenderManager(extenders)
	{}

	const Settings& Manager::GetSettings() const noexcept
	{
		return m_Settings.GetCache();
	}

	bool Manager::Startup() noexcept
	{
		if (m_Running) return true;

		LogSys(L"Peermanager starting...");

		if (!(StartupThreadPools() && AddCallbacks()))
		{
			RemoveCallbacks();
			ShutdownThreadPools();

			LogErr(L"Peermanager startup failed");

			return false;
		}

		LogSys(L"Peermanager startup successful");

		m_Running = true;

		return true;
	}

	void Manager::Shutdown() noexcept
	{
		if (!m_Running) return;

		m_Running = false;

		LogSys(L"Peermanager shutting down...");

		RemoveCallbacks();
		ShutdownThreadPools();

		LogSys(L"Peermanager shut down");
	}

	bool Manager::StartupRelays() noexcept
	{
		return m_RelayManager.Startup();
	}

	void Manager::ShutdownRelays() noexcept
	{
		m_RelayManager.Shutdown();
	}

	bool Manager::StartupThreadPools() noexcept
	{
		PreStartupThreadPools();

		const auto& settings = GetSettings();

		const auto numthreadpools = Util::GetNumThreadPools(settings.Local.Concurrency.PeerManager.MinThreadPools,
															settings.Local.Concurrency.PeerManager.MaxThreadPools, 1u);
		const auto numthreadsperpool = Util::GetNumThreadsPerPool(settings.Local.Concurrency.PeerManager.ThreadsPerPool,
																  settings.Local.Concurrency.PeerManager.ThreadsPerPool, 2u);

		// Must have at least one thread pool, and at least two threads
		// per pool one of which will be the primary thread
		assert(numthreadpools > 0 && numthreadsperpool > 1);

		LogSys(L"Creating %zu peer %s with %zu worker %s %s",
			   numthreadpools, numthreadpools > 1 ? L"threadpools" : L"threadpool",
			   numthreadsperpool, numthreadsperpool > 1 ? L"threads" : L"thread",
			   numthreadpools > 1 ? L"each" : L"");

		auto error = false;

		// Create the threadpools
		for (Size i = 0; i < numthreadpools; ++i)
		{
			try
			{
				auto thpool = std::make_unique<ThreadPool>();

				error = !thpool->GetData().InitializeWorkEvents();

				// Create the worker threads
				for (Size x = 0; x < numthreadsperpool && !error; ++x)
				{
					// First thread is primary worker thread
					if (x == 0)
					{
						if (!thpool->AddThread(L"QuantumGate Peers Thread (Main)",
											   MakeCallback(this, &Manager::PrimaryThreadProcessor),
											   MakeCallback(this, &Manager::PrimaryThreadWait)))
						{
							error = true;
						}
					}
					else
					{
						if (!thpool->AddThread(L"QuantumGate Peers Thread",
											   MakeCallback(this, &Manager::WorkerThreadProcessor),
											   MakeCallback(this, &Manager::WorkerThreadWait),
											   MakeCallback(this, &Manager::WorkerThreadWaitInterrupt)))
						{
							error = true;
						}
					}
				}

				if (!error && thpool->Startup())
				{
					m_ThreadPools[i] = std::move(thpool);
				}
				else
				{
					LogErr(L"Couldn't start a peers threadpool");
					error = true;
				}
			}
			catch (...) { error = true; }

			if (error) break;
		}

		return !error;
	}

	void Manager::ShutdownThreadPools() noexcept
	{
		for (const auto& thpool : m_ThreadPools)
		{
			thpool.second->Shutdown();
			thpool.second->Clear();
			thpool.second->GetData().TaskQueue.Clear();
		}

		// Disconnect and remove all peers
		DisconnectAndRemoveAll();

		for (const auto& thpool : m_ThreadPools)
		{
			thpool.second->GetData().DeinitializeWorkEvents();
		}

		DbgInvoke([&]()
		{
			// If all threads are shut down, and peers
			// are cleared the peercount should be zero
			for (const auto& thpool : m_ThreadPools)
			{
				assert(thpool.second->GetData().PeerMap.WithSharedLock()->empty());
			}
		});

		// If all peers were disconnected and our bookkeeping
		// was done right then the below should be true
		assert(m_LookupMaps.WithSharedLock()->IsEmpty());
		assert(m_AllPeers.WithSharedLock()->empty());

		ResetState();
	}

	void Manager::PreStartupThreadPools() noexcept
	{
		ResetState();
	}

	void Manager::ResetState() noexcept
	{
		m_LookupMaps.WithUniqueLock()->Clear();
		m_AllPeers.WithUniqueLock()->clear();
		m_ThreadPools.clear();
	}

	bool Manager::AddCallbacks() noexcept
	{
		auto success = true;

		m_AccessManager.GetAccessUpdateCallbacks().WithUniqueLock([&](auto& callbacks)
		{
			m_AccessUpdateCallbackHandle = callbacks.Add(MakeCallback(this, &Manager::OnAccessUpdate));
			if (!m_AccessUpdateCallbackHandle)
			{
				LogErr(L"Couldn't register 'AccessUpdateCallback' for peers");
				success = false;
			}
		});

		if (success)
		{
			m_ExtenderManager.GetExtenderUpdateCallbacks().WithUniqueLock([&](auto& callbacks)
			{

				m_ExtenderUpdateCallbackHandle = callbacks.Add(MakeCallback(this, &Manager::OnLocalExtenderUpdate));
				if (!m_ExtenderUpdateCallbackHandle)
				{

					LogErr(L"Couldn't register 'ExtenderUpdateCallback' for peers");
					success = false;
				}
			});
		}

		return success;
	}

	void Manager::RemoveCallbacks() noexcept
	{
		m_AccessManager.GetAccessUpdateCallbacks().WithUniqueLock([&](auto& callbacks)
		{
			callbacks.Remove(m_AccessUpdateCallbackHandle);
		});

		m_ExtenderManager.GetExtenderUpdateCallbacks().WithUniqueLock([&](auto& callbacks)
		{
			callbacks.Remove(m_ExtenderUpdateCallbackHandle);
		});
	}

	void Manager::PrimaryThreadWait(ThreadPoolData& thpdata, const Concurrency::Event& shutdown_event)
	{
		const auto result = thpdata.WaitForWorkEvent(1ms);
		if (!result.Waited)
		{
			shutdown_event.Wait(1ms);
		}
	}

	void Manager::PrimaryThreadProcessor(ThreadPoolData& thpdata, const Concurrency::Event& shutdown_event)
	{
		std::optional<Containers::List<PeerSharedPointer>> remove_list;

		thpdata.PeerMap.WithSharedLock([&](const PeerMap& peers)
		{
			if (peers.empty()) return;

			const auto& settings = GetSettings();
			const auto noise_enabled = settings.Noise.Enabled;
			const auto max_handshake_duration = settings.Local.MaxHandshakeDuration;
			const auto max_connect_duration = settings.Local.ConnectTimeout;

			for (auto it = peers.begin(); it != peers.end() && !shutdown_event.IsSet(); ++it)
			{
				auto& peerths = it->second;

				peerths->WithUniqueLock([&](Peer& peer)
				{
					if (peer.CheckStatus(noise_enabled, max_connect_duration, max_handshake_duration))
					{
						if (peer.HasPendingEvents())
						{
							DiscardReturnValue(peer.ProcessEvents());
						}
					}

					// If we should disconnect for some reason
					if (peer.ShouldDisconnect())
					{
						Disconnect(peer, false);

						// Collect the peer for removal
						if (!remove_list.has_value()) remove_list.emplace();

						remove_list->emplace_back(peerths);
					}
				});
			}
		});

		// Remove all peers that were collected for removal
		if (remove_list.has_value() && !remove_list->empty())
		{
			LogDbg(L"Removing peers");
			Remove(*remove_list);

			remove_list->clear();
		}
	}

	void Manager::WorkerThreadWait(ThreadPoolData& thpdata, const Concurrency::Event& shutdown_event)
	{
		thpdata.TaskQueue.Wait(shutdown_event);
	}

	void Manager::WorkerThreadWaitInterrupt(ThreadPoolData& thpdata)
	{
		thpdata.TaskQueue.InterruptWait();
	}

	void Manager::WorkerThreadProcessor(ThreadPoolData& thpdata, const Concurrency::Event& shutdown_event)
	{
		// Execute any scheduled tasks
		std::optional<ThreadPoolTask> task;
		
		thpdata.TaskQueue.PopFrontIf([&](auto& ftask) noexcept -> bool
		{
			task = std::move(ftask);
			return true;
		});

		if (task.has_value())
		{
			std::visit(Util::Overloaded{
				[&](Tasks::PeerAccessCheck& ptask)
				{
					thpdata.PeerMap.WithSharedLock([&](const PeerMap& peers)
					{
						for (auto it = peers.begin(); it != peers.end(); ++it)
						{
							it->second->WithUniqueLock([&](Peer& peer) noexcept
							{
								peer.SetNeedsAccessCheck();
							});
						}
					});
				},
				[](Tasks::PeerCallback& ptask)
				{
					try
					{
						ptask.Callback();
					}
					catch (const std::exception& e)
					{
						LogErr(L"Unhandled exception while executing peer callback - %s", Util::ToStringW(e.what()).c_str());
					}
					catch (...)
					{
						LogErr(L"Unhandled exception while executing peer callback");
					}
				}
			}, *task);
		}
	}

	PeerSharedPointer Manager::Get(const PeerLUID pluid) const noexcept
	{
		PeerSharedPointer rval{ nullptr };

		m_AllPeers.WithSharedLock([&](const PeerMap& peers)
		{
			const auto p = peers.find(pluid);
			if (p != peers.end()) rval = p->second;
		});

		return rval;
	}

	Result<API::Peer> Manager::GetPeer(const PeerLUID pluid) const noexcept
	{
		if (auto peerths = Get(pluid); peerths != nullptr)
		{
			return API::Peer(pluid, &peerths);
		}

		return ResultCode::PeerNotFound;
	}

	Result<PeerLUID> Manager::GetRelayPeer(const Vector<BinaryIPAddress>& excl_addr1,
										   const Vector<BinaryIPAddress>& excl_addr2) const noexcept
	{
		const auto& settings = GetSettings();
		return m_LookupMaps.WithSharedLock()->GetRandomPeer({}, excl_addr1, excl_addr2,
															settings.Relay.IPv4ExcludedNetworksCIDRLeadingBits,
															settings.Relay.IPv6ExcludedNetworksCIDRLeadingBits);
	}

	Result<bool> Manager::AreRelayIPsInSameNetwork(const BinaryIPAddress& ip1, const BinaryIPAddress& ip2) const noexcept
	{
		const auto& settings = GetSettings();
		return LookupMaps::AreIPsInSameNetwork(ip1, ip2,
											   settings.Relay.IPv4ExcludedNetworksCIDRLeadingBits,
											   settings.Relay.IPv6ExcludedNetworksCIDRLeadingBits);
	}

	Result<bool> Manager::AreRelayIPsInSameNetwork(const BinaryIPAddress& ip,
												   const Vector<BinaryIPAddress>& addresses) noexcept
	{
		const auto& settings = GetSettings();
		return LookupMaps::AreIPsInSameNetwork(ip, addresses,
											   settings.Relay.IPv4ExcludedNetworksCIDRLeadingBits,
											   settings.Relay.IPv6ExcludedNetworksCIDRLeadingBits);
	}

	PeerSharedPointer Manager::CreateUDP(const IP::AddressFamily af, const PeerConnectionType pctype,
										 const UDP::ConnectionID id, const UDP::Message::SequenceNumber seqnum,
										 std::optional<ProtectedBuffer>&& shared_secret) noexcept
	{
		try
		{
			auto shared_secret_copy = shared_secret;

			auto peer_ths = std::make_shared<Peer_ThS>(*this, af, IP::Protocol::UDP, pctype, std::move(shared_secret));
			auto peer = peer_ths->WithUniqueLock();

			if (peer->Initialize(peer_ths))
			{
				if (m_UDPConnectionManager.AddConnection(af, pctype, id, seqnum, peer->GetSocket<UDP::Socket>(),
														 std::move(shared_secret_copy)))
				{
					return peer_ths;
				}
			}
		}
		catch (...) {}

		return nullptr;
	}

	PeerSharedPointer Manager::CreateTCP(const IP::AddressFamily af, const PeerConnectionType pctype,
										 std::optional<ProtectedBuffer>&& shared_secret) noexcept
	{
		try
		{
			auto peer_ths = std::make_shared<Peer_ThS>(*this, af, IP::Protocol::TCP, pctype, std::move(shared_secret));
			if (peer_ths->WithUniqueLock()->Initialize(peer_ths))
			{
				return peer_ths;
			}
		}
		catch (...) {}

		return nullptr;
	}

	PeerSharedPointer Manager::Create(const IP::AddressFamily af, const IP::Protocol protocol,
									  const PeerConnectionType pctype, std::optional<ProtectedBuffer>&& shared_secret) noexcept
	{
		switch (protocol)
		{
			case IP::Protocol::TCP:
			{
				return CreateTCP(af, pctype, std::move(shared_secret));
			}
			case IP::Protocol::UDP:
			{
				const auto id = UDP::Connection::Connection::MakeConnectionID();
				if (id)
				{
					return CreateUDP(af, pctype, *id, 0, std::move(shared_secret));
				}
				else
				{
					LogErr(L"Failed to create UDP connection ID");
				}
				break;
			}
			default:
			{
				// Shouldn't get here
				assert(false);
				break;
			}
		}

		return nullptr;
	}

	PeerSharedPointer Manager::CreateRelay(const PeerConnectionType pctype,
										   std::optional<ProtectedBuffer>&& shared_secret) noexcept
	{
		try
		{
			auto peer_ths = std::make_shared<Peer_ThS>(*this, GateType::RelaySocket, pctype, std::move(shared_secret));
			if (peer_ths->WithUniqueLock()->Initialize(peer_ths)) return peer_ths;
		}
		catch (...) {}

		return nullptr;
	}

	void Manager::SchedulePeerCallback(const UInt64 threadpool_key, Callback<void()>&& callback) noexcept
	{
		const auto& thpool = m_ThreadPools[threadpool_key];

		thpool->GetData().TaskQueue.Push(Tasks::PeerCallback{ std::move(callback) });
	}

	bool Manager::Add(PeerSharedPointer& peerths) noexcept
	{
		auto success = false;

		peerths->WithUniqueLock([&](Peer& peer)
		{
			// Try to add connection to access manager; if this fails
			// the connection was not allowed
			if (m_AccessManager.AddIPConnection(peer.GetPeerIPAddress()))
			{
				PeerMap::iterator apit;

				try
				{
					// If this fails there was already a peer in the map (this should not happen)
					[[maybe_unused]] const auto [it, inserted] =
						m_AllPeers.WithUniqueLock()->insert({ peer.GetLUID(), peerths });

					assert(inserted);
					if (!inserted)
					{
						LogErr(L"Couldn't add new peer; a peer with LUID %llu already exists", peer.GetLUID());
						return;
					}

					apit = it;
				}
				catch (...) { return; }

				auto sg = MakeScopeGuard([&]
				{
					m_AllPeers.WithUniqueLock()->erase(apit);
				});

				// Get the threadpool with the least amount of peers so that the connections
				// eventually get distributed among all available pools
				const auto thpit = std::min_element(m_ThreadPools.begin(), m_ThreadPools.end(),
													[](const auto& a, const auto& b)
				{
					return (a.second->GetData().PeerMap.WithSharedLock()->size() <
							b.second->GetData().PeerMap.WithSharedLock()->size());
				});

				assert(thpit != m_ThreadPools.end());

				// Add peer to the threadpool
				peer.SetThreadPoolKey(thpit->first);

				PeerMap::iterator pit;

				try
				{
					// If this fails there was already a peer in the map (this should not happen)
					[[maybe_unused]] const auto [it, inserted] =
						thpit->second->GetData().PeerMap.WithUniqueLock()->insert({ peer.GetLUID(), peerths });

					assert(inserted);
					if (!inserted)
					{
						LogErr(L"Couldn't add new peer; a peer with LUID %llu already exists", peer.GetLUID());
						return;
					}

					pit = it;
				}
				catch (...) { return; }

				auto sg2 = MakeScopeGuard([&]
				{
					thpit->second->GetData().PeerMap.WithUniqueLock()->erase(pit);
				});

				if (!thpit->second->GetData().AddWorkEvent(peer)) return;

				sg.Deactivate();
				sg2.Deactivate();

				success = true;
			}
			else
			{
				LogErr(L"Couldn't add new peer with LUID %llu; IP address %s is not allowed",
					   peer.GetLUID(), peer.GetPeerIPAddress().GetString().c_str());
			}
		});

		return success;
	}

	void Manager::Remove(const PeerSharedPointer& peer_ths) noexcept
	{
		PeerLUID pluid{ 0 };
		ThreadPool* thpool{ nullptr };

		peer_ths->WithSharedLock([&](const Peer& peer)
		{
			pluid = peer.GetLUID();
			thpool = m_ThreadPools[peer.GetThreadPoolKey()].get();
			thpool->GetData().RemoveWorkEvent(peer);
		});

		m_AllPeers.WithUniqueLock()->erase(pluid);
		thpool->GetData().PeerMap.WithUniqueLock()->erase(pluid);
	}

	void Manager::Remove(const Containers::List<PeerSharedPointer>& peerlist) noexcept
	{
		for (const auto& peerths : peerlist)
		{
			Remove(peerths);
		}
	}

	void Manager::RemoveAll() noexcept
	{
		m_AllPeers.WithUniqueLock()->clear();

		for (const auto& thpool : m_ThreadPools)
		{
			thpool.second->GetData().ClearWorkEvents();
			thpool.second->GetData().PeerMap.WithUniqueLock()->clear();
		}
	}

	Result<> Manager::DisconnectFrom(const PeerLUID pluid, DisconnectCallback&& function) noexcept
	{
		if (auto peerths = Get(pluid); peerths != nullptr)
		{
			return DisconnectFrom(*peerths, std::move(function));
		}

		return ResultCode::PeerNotFound;
	}

	Result<> Manager::DisconnectFrom(API::Peer& peer, DisconnectCallback&& function) noexcept
	{
		return DisconnectFrom(*GetPeerFromPeerStorage(peer), std::move(function));
	}

	Result<> Manager::DisconnectFrom(Peer_ThS& peerths, DisconnectCallback&& function) noexcept
	{
		auto result_code = ResultCode::Failed;

		peerths.WithUniqueLock([&](Peer& peer) noexcept
		{
			// Peer should not already be disconnected
			if (peer.GetStatus() != Status::Disconnected)
			{
				if (function) peer.AddDisconnectCallback(std::move(function));

				// Set the disconnect condition so that the peer
				// gets disconnected as soon as possible
				peer.SetDisconnectCondition(DisconnectCondition::DisconnectRequest);

				result_code = ResultCode::Succeeded;
			}
		});

		return result_code;
	}

	void Manager::Disconnect(Peer& peer, const bool graceful) noexcept
	{
		// Remove connection from access manager
		if (!m_AccessManager.RemoveIPConnection(peer.GetPeerIPAddress()))
		{
			LogErr(L"Could not remove connection for endpoint %s from access manager", peer.GetPeerName().c_str());
		}

		if (peer.GetIOStatus().IsOpen())
		{
			LogInfo(L"Disconnecting from endpoint %s", peer.GetPeerName().c_str());
			peer.Close(graceful);
		}
	}

	void Manager::DisconnectAndRemoveAll() noexcept
	{
		m_AllPeers.WithSharedLock([&](const PeerMap& peers)
		{
			for (auto& it : peers)
			{
				it.second->WithUniqueLock([&](Peer& peer) noexcept
				{
					Disconnect(peer, false);
				});
			}
		});

		RemoveAll();
	}

	bool Manager::Accept(PeerSharedPointer& peerths) noexcept
	{
		return Add(peerths);
	}

	Result<std::pair<PeerLUID, bool>> Manager::ConnectTo(ConnectParameters&& params, ConnectCallback&& function) noexcept
	{
		auto result_code = ResultCode::Failed;

		if (params.PeerIPEndpoint.GetProtocol() == IPEndpoint::Protocol::UDP ||
			params.PeerIPEndpoint.GetProtocol() == IPEndpoint::Protocol::TCP)
		{
			if (const auto allowed = m_AccessManager.GetIPConnectionAllowed(params.PeerIPEndpoint.GetIPAddress(),
																			Access::CheckType::All); allowed && *allowed)
			{
				auto reused = false;
				PeerLUID pluid{ 0 };
				PeerSharedPointer peerths{ nullptr };

				if (params.ReuseExistingConnection)
				{
					const auto cendpoint = std::invoke([&]()
					{
						if (params.Relay.Hops == 0)
						{
							return IPEndpoint(params.PeerIPEndpoint.GetProtocol(), params.PeerIPEndpoint.GetIPAddress(),
											  params.PeerIPEndpoint.GetPort());
						}
						else
						{
							return IPEndpoint(params.PeerIPEndpoint.GetProtocol(), params.PeerIPEndpoint.GetIPAddress(),
											  params.PeerIPEndpoint.GetPort(), 0, params.Relay.Hops);
						}
					});

					// Do we have an existing connection to the endpoint?
					if (const auto result = m_LookupMaps.WithSharedLock()->GetPeer(cendpoint); result.Succeeded())
					{
						peerths = Get(*result);
					}
				}

				// If there's no existing connection make new one,
				// otherwise try to reuse existing connection
				if (peerths == nullptr)
				{
					if (params.Relay.Hops == 0)
					{
						LogInfo(L"Connecting to peer %s", params.PeerIPEndpoint.GetString().c_str());

						if (const auto result = DirectConnectTo(std::move(params), std::move(function)); result.Succeeded())
						{
							result_code = ResultCode::Succeeded;
							pluid = *result;
						}
					}
					else
					{
						LogInfo(L"Connecting to peer %s (Relayed)", params.PeerIPEndpoint.GetString().c_str());

						return RelayConnectTo(std::move(params), std::move(function));
					}
				}
				else
				{
					peerths->WithUniqueLock([&](Peer& peer) noexcept
					{
						if ((peer.GetIOStatus().IsConnecting() || peer.GetIOStatus().IsConnected()) &&
							!peer.GetIOStatus().HasException())
						{
							LogDbg(L"Reusing existing connection to peer %s", peer.GetPeerName().c_str());

							result_code = ResultCode::Succeeded;
							pluid = peer.GetLUID();
							reused = true;
						}
						else
						{
							LogErr(L"Error on existing connection to peer %s; retry connecting", peer.GetPeerName().c_str());

							// Set the disconnect condition so that the peer gets disconnected as soon as possible
							peer.SetDisconnectCondition(DisconnectCondition::ConnectError);
							result_code = ResultCode::FailedRetry;
						}
					});
				}

				if (result_code == ResultCode::Succeeded) return { std::make_pair(pluid, reused) };
			}
			else
			{
				LogErr(L"Could not connect to peer %s; IP address is not allowed", params.PeerIPEndpoint.GetString().c_str());
				result_code = ResultCode::NotAllowed;
			}
		}
		else
		{
			LogErr(L"Could not connect to peer %s; unsupported internetwork protocol", params.PeerIPEndpoint.GetString().c_str());
			result_code = ResultCode::InvalidArgument;
		}

		return result_code;
	}

	Result<PeerLUID> Manager::DirectConnectTo(ConnectParameters&& params, ConnectCallback&& function) noexcept
	{
		std::optional<PeerLUID> pluid;

		auto peerths = Create(params.PeerIPEndpoint.GetIPAddress().GetFamily(),
							  params.PeerIPEndpoint.GetProtocol(), PeerConnectionType::Outbound,
							  std::move(params.GlobalSharedSecret));
		if (peerths != nullptr)
		{
			peerths->WithUniqueLock([&](Peer& peer) noexcept
			{
				if (function) peer.AddConnectCallback(std::move(function));

				if (peer.BeginConnect(params.PeerIPEndpoint))
				{
					if (Add(peerths))
					{
						pluid = peer.GetLUID();
					}
					else peer.Close();
				}
			});
		}

		if (!pluid)
		{
			LogErr(L"Could not create connection to peer %s", params.PeerIPEndpoint.GetString().c_str());
			return ResultCode::Failed;
		}

		return *pluid;
	}

	Result<std::pair<PeerLUID, bool>> Manager::RelayConnectTo(ConnectParameters&& params, ConnectCallback&& function) noexcept
	{
		assert(params.Relay.Hops > 0);

		constexpr auto reused{ false };
		PeerLUID pluid{ 0 };
		auto result_code{ ResultCode::Failed };
		String error_details;

		const auto rport = m_RelayManager.MakeRelayPort();
		if (rport)
		{
			if (const auto result = GetRelayPeer(params, error_details); result.Succeeded())
			{
				const auto& [out_peer, out_reused] = *result;

				LogInfo(L"Using peer LUID %llu as gateway for relay connection to peer %s",
						out_peer, params.PeerIPEndpoint.GetString().c_str());

				if (auto in_peerths = CreateRelay(PeerConnectionType::Outbound,
												  std::move(params.GlobalSharedSecret)); in_peerths != nullptr)
				{
					in_peerths->WithUniqueLock([&](Peer& in_peer) noexcept
					{
						if (function) in_peer.AddConnectCallback(std::move(function));

						const auto out_endpoint = IPEndpoint(params.PeerIPEndpoint.GetProtocol(),
															 params.PeerIPEndpoint.GetIPAddress(),
															 params.PeerIPEndpoint.GetPort(), *rport, params.Relay.Hops);
						if (in_peer.BeginConnect(out_endpoint))
						{
							if (Add(in_peerths))
							{
								if (m_RelayManager.Connect(in_peer.GetLUID(), out_peer, out_endpoint, *rport, params.Relay.Hops))
								{
									pluid = in_peer.GetLUID();
									result_code = ResultCode::Succeeded;
								}
							}

							if (result_code != ResultCode::Succeeded) in_peer.Close();
						}
					});
				}

				// If creating relay failed and we made a new connection specifically
				// for this relay then we should close it since it's not needed
				if (result_code != ResultCode::Succeeded && params.Relay.Hops == 1 && !out_reused)
				{
					DiscardReturnValue(DisconnectFrom(out_peer, nullptr));
				}
			}
			else
			{
				if (IsResultCode(result)) result_code = GetResultCode(result);
			}
		}
		else error_details = L"couldn't get relay port (relays may not be enabled)";

		if (result_code == ResultCode::Succeeded)
		{
			return std::make_pair(pluid, reused);
		}
		else
		{
			LogErr(L"Couldn't create relay link to peer %s%s%s",
				   params.PeerIPEndpoint.GetString().c_str(), error_details.empty() ? L"" : L" - ", error_details.c_str());

			return result_code;
		}
	}

	Result<std::pair<PeerLUID, bool>> Manager::GetRelayPeer(const ConnectParameters& params, String& error_details) noexcept
	{
		PeerLUID out_peer{ 0 };
		auto out_reused{ false };
		auto result_code{ ResultCode::Failed };

		try
		{
			if (params.Relay.Hops == 1)
			{
				const auto excl_addr = GetLocalIPAddresses();
				if (excl_addr != nullptr)
				{
					// Don't include addresses/network of local instance
					const auto result = AreRelayIPsInSameNetwork(params.PeerIPEndpoint.GetIPAddress().GetBinary(), *excl_addr);
					if (result.Succeeded())
					{
						if (!result.GetValue())
						{
							if (params.Relay.GatewayPeer)
							{
								if (const auto gateway_peerths = Get(*params.Relay.GatewayPeer); gateway_peerths != nullptr)
								{
									const auto gateway_peer_ep = gateway_peerths->WithSharedLock()->GetPeerEndpoint();

									// For single hop relay we check that the final endpoint is the same as the
									// gateway peer endpoint
									if (gateway_peer_ep.GetIPAddress() == params.PeerIPEndpoint.GetIPAddress() &&
										gateway_peer_ep.GetPort() == params.PeerIPEndpoint.GetPort())
									{
										out_peer = *params.Relay.GatewayPeer;
										out_reused = true;
										result_code = ResultCode::Succeeded;
									}
									else error_details = Util::FormatString(L"the gateway peer LUID %llu does not have the same endpoint as the destination (they must be the same for single hop relays)",
																			params.Relay.GatewayPeer.value());
								}
								else error_details = Util::FormatString(L"a peer with LUID %llu (for use as relay gateway) wasn't found", params.Relay.GatewayPeer.value());
							}
							else
							{
								// Connect to specific endpoint for final hop 0; if we're
								// already connected we'll use the existing connection; note that we specify
								// the same global shared secret since the destination is the same
								const auto retval = ConnectTo({ params.PeerIPEndpoint, params.GlobalSharedSecret }, nullptr);
								if (retval.Succeeded())
								{
									out_peer = retval->first;
									out_reused = retval->second;
									result_code = ResultCode::Succeeded;
								}
								else
								{
									if (IsResultCode(retval)) result_code = GetResultCode(retval);
								}
							}
						}
						else error_details = L"the destination endpoint is on the same network as the local instance";
					}
					else error_details = L"couldn't check if the destination endpoint is on the same network as the local instance";
				}
				else error_details = L"couldn't get IP addresses of local instance";
			}
			else
			{
				const auto excl_addr1 = GetLocalIPAddresses();
				if (excl_addr1 != nullptr)
				{
					Vector<BinaryIPAddress> excl_addr2{ params.PeerIPEndpoint.GetIPAddress().GetBinary() };

					if (params.Relay.GatewayPeer)
					{
						if (const auto gateway_peerths = Get(*params.Relay.GatewayPeer); gateway_peerths != nullptr)
						{
							const auto gateway_peer_ip =
								gateway_peerths->WithSharedLock()->GetPeerEndpoint().GetIPAddress().GetBinary();

							// Don't include addresses/network of local instance
							const auto result1 = AreRelayIPsInSameNetwork(gateway_peer_ip, *excl_addr1);

							// Don't include the final endpoint/network
							const auto result2 = AreRelayIPsInSameNetwork(gateway_peer_ip, excl_addr2);

							if (result1.Succeeded() && result2.Succeeded())
							{
								if (!result1.GetValue() && !result2.GetValue())
								{
									out_peer = *params.Relay.GatewayPeer;
									result_code = ResultCode::Succeeded;
								}
								else error_details = Util::FormatString(L"cannot go through peer LUID %llu because it's on the same network as the local or destination endpoint",
																		params.Relay.GatewayPeer.value());
							}
							else error_details = Util::FormatString(L"couldn't check if peer LUID %llu is on the same network as the local or destination endpoint",
																	params.Relay.GatewayPeer.value());
						}
						else error_details = Util::FormatString(L"a peer with LUID %llu (for use as relay gateway) wasn't found", params.Relay.GatewayPeer.value());
					}
					else
					{
						// Try to get a (random) peer for the hop in between
						// and don't include endpoints on excluded networks
						const auto result = GetRelayPeer(*excl_addr1, excl_addr2);
						if (result.Succeeded())
						{
							out_peer = result.GetValue();
							result_code = ResultCode::Succeeded;
						}
						else
						{
							if (result == ResultCode::PeerNotFound)
							{
								result_code = ResultCode::NoPeersForRelay;
								error_details = L"no peers available to create relay link";
							}
							else
							{
								error_details = L"failed to get a peer to create relay link";
							}
						}
					}
				}
				else error_details = L"couldn't get IP addresses of local instance";
			}
		}
		catch (...)
		{
			error_details = L"an exception was thrown";
		}

		if (result_code == ResultCode::Succeeded) return std::make_pair(out_peer, out_reused);

		return result_code;
	}

	Result<> Manager::QueryPeers(const PeerQueryParameters& params, Vector<PeerLUID>& pluids) const noexcept
	{
		return m_LookupMaps.WithSharedLock()->QueryPeers(params, pluids);
	}

	Result<> Manager::Broadcast(const MessageType msgtype, const Buffer& buffer, BroadcastCallback&& callback)
	{
		m_AllPeers.WithSharedLock([&](const PeerMap& peers)
		{
			for (const auto& it : peers)
			{
				it.second->WithUniqueLock([&](Peer& peer)
				{
					auto broadcast_result = BroadcastResult::Succeeded;

					if (peer.IsReady())
					{
						// Note the copy
						auto bbuffer = buffer;
						if (peer.Send(msgtype, std::move(bbuffer)).Failed())
						{
							broadcast_result = BroadcastResult::SendFailure;
						}
					}
					else broadcast_result = BroadcastResult::PeerNotReady;

					if (callback) callback(peer, broadcast_result);
				});
			}
		});

		return ResultCode::Succeeded;
	}

	const Vector<BinaryIPAddress>* Manager::GetLocalIPAddresses() const noexcept
	{
		return m_LocalEnvironment.WithSharedLock()->GetTrustedAndVerifiedIPAddresses();
	}

	Result<> Manager::SendTo(const ExtenderUUID& extuuid, const std::atomic_bool& running, const std::atomic_bool& ready,
							 const PeerLUID pluid, Buffer&& buffer, const SendParameters& params, SendCallback&& callback) noexcept
	{
		if (auto peerths = Get(pluid); peerths != nullptr)
		{
			auto peer = peerths->WithUniqueLock();
			return SendTo(extuuid, running, ready, *peer, std::move(buffer), params, std::move(callback));
		}

		return ResultCode::PeerNotFound;
	}

	Result<Size> Manager::Send(const ExtenderUUID& extuuid, const std::atomic_bool& running, const std::atomic_bool& ready,
							   const PeerLUID pluid, const BufferView& buffer, const SendParameters& params,
							   SendCallback&& callback) noexcept
	{
		if (auto peerths = Get(pluid); peerths != nullptr)
		{
			auto peer = peerths->WithUniqueLock();
			return Send(extuuid, running, ready, *peer, buffer, params, std::move(callback));
		}

		return ResultCode::PeerNotFound;
	}

	Result<> Manager::SendTo(const ExtenderUUID& extuuid, const std::atomic_bool& running, const std::atomic_bool& ready,
							 API::Peer& api_peer, Buffer&& buffer, const SendParameters& params,
							 SendCallback&& callback) noexcept
	{
		auto& peerths = GetPeerFromPeerStorage(api_peer);
		auto peer = peerths->WithUniqueLock();
		return SendTo(extuuid, running, ready, *peer, std::move(buffer), params, std::move(callback));
	}

	Result<Size> Manager::Send(const ExtenderUUID& extuuid, const std::atomic_bool& running, const std::atomic_bool& ready,
							   API::Peer& api_peer, const BufferView& buffer, const SendParameters& params,
							   SendCallback&& callback) noexcept
	{
		auto& peerths = GetPeerFromPeerStorage(api_peer);
		auto peer = peerths->WithUniqueLock();
		return Send(extuuid, running, ready, *peer, buffer, params, std::move(callback));
	}

	Result<Size> Manager::Send(const ExtenderUUID& extuuid, const std::atomic_bool& running, const std::atomic_bool& ready,
							   Peer& peer, const BufferView& buffer, const SendParameters& params,
							   SendCallback&& callback) noexcept
	{
		try
		{
			const auto max_size = peer.GetAvailableExtenderCommunicationSendBufferSize();
			if (max_size > 0)
			{
				const auto snd_size = std::min(buffer.GetSize(), max_size);

				// Note the copy
				Buffer snd_buf = buffer.GetFirst(snd_size);

				if (const auto result = SendTo(extuuid, running, ready, peer, std::move(snd_buf), params, std::move(callback)); result.Succeeded())
				{
					return snd_size;
				}
				else
				{
					if (IsResultCode(result)) return GetResultCode(result);
				}
			}
			else return ResultCode::PeerSendBufferFull;
		}
		catch (...)
		{
			return ResultCode::OutOfMemory;
		}

		return ResultCode::Failed;
	}

	Result<> Manager::SendTo(const ExtenderUUID& extuuid, const std::atomic_bool& running, const std::atomic_bool& ready,
							 Peer& peer, Buffer&& buffer, const SendParameters& params, SendCallback&& callback) noexcept
	{
		// Only if peer status is ready (handshake succeeded, etc.)
		if (peer.IsReady())
		{
			// If peer has extender installed and active
			if (peer.GetPeerExtenderUUIDs().HasExtender(extuuid))
			{
				// If local extender is still running
				if (running)
				{
					// If extender is ready
					if (ready)
					{
						return peer.Send(Message(MessageOptions(MessageType::ExtenderCommunication,
																extuuid, std::move(buffer), params.Compress)),
										 params.Priority, params.Delay, std::move(callback));
					}
					else return ResultCode::FailedRetry;
				}
				else return ResultCode::NotRunning;
			}
			else return ResultCode::PeerNoExtender;
		}
		else if (peer.IsSuspended())
		{
			return ResultCode::PeerSuspended;
		}

		return ResultCode::PeerNotReady;
	}

	Result<Buffer> Manager::GetExtenderUpdateData() const noexcept
	{
		const auto& lsextlist = m_ExtenderManager.GetActiveExtenderUUIDs().SerializedUUIDs;

		Memory::BufferWriter wrt(true);
		if (wrt.WriteWithPreallocation(Memory::WithSize(lsextlist, Memory::MaxSize::_65KB)))
		{
			return Buffer(wrt.MoveWrittenBytes());
		}

		return ResultCode::Failed;
	}

	bool Manager::BroadcastExtenderUpdate()
	{
		// If there are no connections, don't bother
		if (m_AllPeers.WithSharedLock()->size() == 0) return true;

		if (const auto result = GetExtenderUpdateData(); result.Succeeded())
		{
			const auto result2 = Broadcast(MessageType::ExtenderUpdate, *result,
										   [](Peer& peer, const BroadcastResult broadcast_result) mutable
			{
				switch (broadcast_result)
				{
					case BroadcastResult::PeerNotReady:
					{
						if (peer.IsInSessionInit() || peer.IsSuspended())
						{
							// We'll need to send an extender update to the peer
							// when it gets in the ready state
							peer.SetNeedsExtenderUpdate();

							LogDbg(L"Couldn't broadcast ExtenderUpdate message to peer LUID %llu; will send update when it gets in ready state",
								   peer.GetLUID());
						}
						break;
					}
					default:
					{
						break;
					}
				}
			});

			if (result2.Succeeded())
			{
				LogInfo(L"Broadcasted ExtenderUpdate to peers");
				return true;
			}
			else LogErr(L"Couldn't broadcast ExtenderUpdate message to peers");
		}
		else LogErr(L"Couldn't prepare ExtenderUpdate message data for peers");

		return false;
	}

	void Manager::OnAccessUpdate() noexcept
	{
		assert(m_Running);

		// This function should not update peers directly since
		// it can get called by all kinds of outside threads and
		// could cause deadlocks. A task is scheduled for the threadpools
		// to handle updating the peers.

		for (const auto& thpool : m_ThreadPools)
		{
			thpool.second->GetData().TaskQueue.Push(Tasks::PeerAccessCheck{});
		}
	}

	void Manager::OnLocalExtenderUpdate(const Vector<ExtenderUUID>& extuuids, const bool added)
	{
		assert(m_Running);

		{
			const auto peers = m_AllPeers.WithSharedLock();

			// If there are no connections, don't bother
			if (peers->size() == 0) return;

			if (added)
			{
				// If an extender was added, update it with all existing connections
				// in case the peers also support this extender
				for (auto& it : *peers)
				{
					it.second->WithUniqueLock([&](Peer& peer)
					{
						peer.ProcessLocalExtenderUpdate(extuuids);
					});
				}
			}
		}

		// Let connected peers know we added or removed an extender
		BroadcastExtenderUpdate();
	}

	void Manager::OnPeerEvent(const Peer& peer, const Event&& event) noexcept
	{
		switch (event.GetType())
		{
			case Event::Type::Connected:
			{
				// Add new peer to lookup maps
				if (!m_LookupMaps.WithUniqueLock()->AddPeerData(peer.GetPeerData()))
				{
					LogErr(L"Couldn't add peer with UUID %s, LUID %llu to peer lookup maps",
						   event.GetPeerUUID().GetString().c_str(), event.GetPeerLUID());
				}

				break;
			}
			case Event::Type::Suspended:
			case Event::Type::Resumed:
			{
				break;
			}
			case Event::Type::Disconnected:
			{
				// Remove peer from lookup maps
				if (!m_LookupMaps.WithUniqueLock()->RemovePeerData(peer.GetPeerData()))
				{
					LogErr(L"Couldn't remove peer with UUID %s, LUID %llu from peer lookup maps",
						   event.GetPeerUUID().GetString().c_str(), event.GetPeerLUID());
				}

				break;
			}
			default:
			{
				break;
			}
		}
	}

	void Manager::AddReportedPublicIPEndpoint(const IPEndpoint& pub_endpoint, const IPEndpoint& rep_peer,
											  const PeerConnectionType rep_con_type, const bool trusted) noexcept
	{
		DiscardReturnValue(m_LocalEnvironment.WithUniqueLock()->AddPublicIPEndpoint(pub_endpoint, rep_peer,
																					rep_con_type, trusted));
	}
}