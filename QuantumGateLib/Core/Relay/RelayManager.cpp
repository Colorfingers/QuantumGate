// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "RelayManager.h"
#include "..\Peer\PeerManager.h"

using namespace std::literals;

namespace QuantumGate::Implementation::Core::Relay
{
	Peer::Manager& Manager::GetPeerManager() const noexcept
	{
		return m_PeerManager;
	}

	Access::Manager& Manager::GetAccessManager() const noexcept
	{
		return GetPeerManager().GetAccessManager();
	}

	const Settings& Manager::GetSettings() const noexcept
	{
		return GetPeerManager().GetSettings();
	}

	bool Manager::Startup() noexcept
	{
		if (m_Running) return true;

		LogSys(L"Relaymanager starting...");

		PreStartup();

		if (!StartupThreadPool())
		{
			BeginShutdownThreadPool();
			EndShutdownThreadPool();

			LogErr(L"Relaymanager startup failed");

			return false;
		}

		m_Running = true;

		LogSys(L"Relaymanager startup successful");

		return true;
	}

	void Manager::Shutdown() noexcept
	{
		if (!m_Running) return;

		m_Running = false;

		LogSys(L"Relaymanager shutting down...");

		BeginShutdownThreadPool();

		// Disconnect and remove all relays
		DisconnectAndRemoveAll();

		EndShutdownThreadPool();

		// If all relays were disconnected and our bookkeeping
		// was done right then the below should be true
		assert(m_RelayLinks.WithUniqueLock()->empty());

		ResetState();

		LogSys(L"Relaymanager shut down");
	}

	void Manager::PreStartup() noexcept
	{
		ResetState();
	}

	void Manager::ResetState() noexcept
	{
		m_ThreadPool.GetData().RelayEventQueues.clear();
		m_ThreadPool.GetData().RelayPortToThreadKeys.WithUniqueLock()->clear();
		m_ThreadPool.GetData().ThreadKeyToLinkTotals.WithUniqueLock()->clear();

		m_RelayLinks.WithUniqueLock()->clear();
	}

	bool Manager::StartupThreadPool() noexcept
	{
		const auto& settings = GetSettings();

		const auto numthreadsperpool = Util::GetNumThreadsPerPool(settings.Local.Concurrency.RelayManager.MinThreads,
																  settings.Local.Concurrency.RelayManager.MaxThreads, 2u);

		// Must have at least two threads in pool 
		// one of which will be the primary thread
		assert(numthreadsperpool > 1);

		LogSys(L"Creating relay threadpool with %zu worker %s",
			   numthreadsperpool, numthreadsperpool > 1 ? L"threads" : L"thread");

		auto error = !m_ThreadPool.GetData().WorkEvents.Initialize();

		// Create the worker threads
		for (Size x = 0; x < numthreadsperpool && !error; ++x)
		{
			// First thread is primary worker thread
			if (x == 0)
			{
				if (!m_ThreadPool.AddThread(L"QuantumGate Relay Thread (Main)", ThreadData(x, nullptr),
											MakeCallback(this, &Manager::PrimaryThreadProcessor),
											MakeCallback(this, &Manager::PrimaryThreadWait)))
				{
					error = true;
				}
			}
			else
			{
				try
				{
					m_ThreadPool.GetData().RelayEventQueues[x] = std::make_unique<EventQueue_ThS>();

					if (m_ThreadPool.AddThread(L"QuantumGate Relay Thread (Event Processor)",
											   ThreadData(x, m_ThreadPool.GetData().RelayEventQueues[x].get()),
											   MakeCallback(this, &Manager::WorkerThreadProcessor),
											   MakeCallback(this, &Manager::WorkerThreadWait),
											   MakeCallback(this, &Manager::WorkerThreadWaitInterrupt)))
					{
						// Add entry for the total number of relay links this thread is handling
						m_ThreadPool.GetData().ThreadKeyToLinkTotals.WithUniqueLock([&](ThreadKeyToLinkTotalMap& link_totals)
						{
							[[maybe_unused]] const auto [it, inserted] = link_totals.insert({ x, 0 });
							if (!inserted)
							{
								error = true;
							}
						});
					}
					else error = true;
				}
				catch (...) { error = true; }
			}
		}

		if (!error && m_ThreadPool.Startup())
		{
			return true;
		}

		return false;
	}

	void Manager::BeginShutdownThreadPool() noexcept
	{
		m_ThreadPool.Shutdown();
		m_ThreadPool.Clear();
	}

	void Manager::EndShutdownThreadPool() noexcept
	{
		m_ThreadPool.GetData().WorkEvents.Deinitialize();
	}

	std::optional<RelayPort> Manager::MakeRelayPort() const noexcept
	{
		if (IsRunning())
		{
			if (const auto rport = Crypto::GetCryptoRandomNumber(); rport.has_value())
			{
				return { *rport };
			}
		}

		return std::nullopt;
	}

	bool Manager::Connect(const PeerLUID in_peer, const PeerLUID out_peer,
						  const IPEndpoint& endpoint, const RelayPort rport, const RelayHop hops) noexcept
	{
		assert(IsRunning());

		try
		{
			auto success = false;

			auto rcths = std::make_unique<Link_ThS>(in_peer, out_peer, endpoint,
													rport, hops, Position::Beginning);

			success = rcths->WithUniqueLock()->UpdateStatus(Status::Connect);
			if (success && Add(rport, std::move(rcths)))
			{
				return true;
			}
		}
		catch (...) {}

		return false;
	}

	bool Manager::Accept(const Events::Connect& rcevent, const PeerLUID out_peer) noexcept
	{
		assert(IsRunning());

		try
		{
			auto success = false;

			const auto position = (rcevent.Hop == 0) ? Position::End : Position::Between;

			auto rcths = std::make_unique<Link_ThS>(rcevent.Origin.PeerLUID, out_peer,
													rcevent.Endpoint, rcevent.Port, rcevent.Hop, position);

			success = rcths->WithUniqueLock()->UpdateStatus(Status::Connect);
			if (success && Add(rcevent.Port, std::move(rcths)))
			{
				return true;
			}
		}
		catch (...) {}

		return false;
	}

	std::optional<Manager::ThreadKey> Manager::GetThreadKey(const RelayPort rport) const noexcept
	{
		std::optional<Manager::ThreadKey> thkey;
		m_ThreadPool.GetData().RelayPortToThreadKeys.WithSharedLock([&](const RelayPortToThreadKeyMap& ports)
		{
			if (const auto it = ports.find(rport); it != ports.end())
			{
				thkey = it->second;
			}
		});

		return thkey;
	}

	bool Manager::MapRelayPortToThreadKey(const RelayPort rport) noexcept
	{
		auto success = false;

		const auto thkey = GetThreadKeyWithLeastLinks();
		if (thkey)
		{
			try
			{
				m_ThreadPool.GetData().RelayPortToThreadKeys.WithUniqueLock([&](RelayPortToThreadKeyMap& ports)
				{
					// Add a relationship between RelayPort and ThreadKey so we can
					// lookup which thread handles events for a certain port
					if (const auto ret_pair = ports.insert({ rport, *thkey }); ret_pair.second)
					{
						// Update the total amount of relay links the thread is handling
						m_ThreadPool.GetData().ThreadKeyToLinkTotals.WithUniqueLock(
							[&](ThreadKeyToLinkTotalMap& link_totals)
						{
							if (const auto ltit = link_totals.find(*thkey); ltit != link_totals.end())
							{
								++ltit->second;
								success = true;
							}
							else
							{
								// Shouldn't get here
								assert(false);

								ports.erase(ret_pair.first);
							}
						});
					}
					else
					{
						// Shouldn't get here
						assert(false);
					}
				});
			}
			catch (...) {}
		}

		return success;
	}

	void Manager::UnMapRelayPortFromThreadKey(const RelayPort rport) noexcept
	{
		m_ThreadPool.GetData().RelayPortToThreadKeys.WithUniqueLock([&](RelayPortToThreadKeyMap& ports)
		{
			if (const auto it = ports.find(rport); it != ports.end())
			{
				m_ThreadPool.GetData().ThreadKeyToLinkTotals.WithUniqueLock([&](ThreadKeyToLinkTotalMap& link_totals)
				{
					if (const auto ltit = link_totals.find(it->second); ltit != link_totals.end())
					{
						if (ltit->second > 0) --ltit->second;
						else
						{
							// Shouldn't get here
							assert(false);
						}
					}
					else
					{
						// Shouldn't get here
						assert(false);
					}
				});

				ports.erase(it);
			}
			else
			{
				// Shouldn't get here
				assert(false);
			}
		});
	}

	std::optional<Manager::ThreadKey> Manager::GetThreadKeyWithLeastLinks() const noexcept
	{
		std::optional<ThreadKey> thkey;

		// Get the threadpool with the least amount of relay links
		m_ThreadPool.GetData().ThreadKeyToLinkTotals.WithSharedLock([&](const ThreadKeyToLinkTotalMap& link_totals)
		{
			// Should have at least one item (at least
			// one event worker thread running)
			assert(link_totals.size() > 0);

			const auto it = std::min_element(link_totals.begin(), link_totals.end(),
											 [](const auto& a, const auto& b)
			{
				return (a.second < b.second);
			});

			assert(it != link_totals.end());

			thkey = it->first;
		});

		return thkey;
	}

	bool Manager::AddRelayEvent(const RelayPort rport, Event&& event) noexcept
	{
		if (!IsRunning()) return false;

		auto success = false;

		try
		{
			// Check if the relay port is already mapped to a specific thread
			std::optional<ThreadKey> thkey = GetThreadKey(rport);
			if (!thkey)
			{
				// Get the thread with the least amount of relay links
				thkey = GetThreadKeyWithLeastLinks();
			}

			if (thkey)
			{
				m_ThreadPool.GetData().RelayEventQueues[*thkey]->Push(std::move(event));

				success = true;
			}
		}
		catch (...) {}

		return success;
	}

	bool Manager::Add(const RelayPort rport, std::unique_ptr<Link_ThS>&& rl) noexcept
	{
		auto success = false;

		try
		{
			m_RelayLinks.WithUniqueLock([&](LinkMap& relays)
			{
				const auto result = relays.insert({ rport, std::move(rl) });
				if (result.second)
				{
					auto sg = MakeScopeGuard([&] { relays.erase(result.first); });

					if (MapRelayPortToThreadKey(rport))
					{
						success = true;

						sg.Deactivate();
					}
					else LogErr(L"Failed to map relay port %llu to worker thread!", rport);
				}
				else LogErr(L"Attempt to add relay port %llu which already exists; this could mean relay loop!", rport);
			});
		}
		catch (...) {}

		return success;
	}

	void Manager::Remove(const Containers::List<RelayPort>& rlist) noexcept
	{
		try
		{
			m_RelayLinks.WithUniqueLock([&](LinkMap& relays)
			{
				for (auto rport : rlist)
				{
					if (relays.erase(rport) == 0)
					{
						LogErr(L"Attempt to remove relay port %llu which doesn't exists!", rport);
					}

					UnMapRelayPortFromThreadKey(rport);
				}
			});
		}
		catch (...) {}
	}

	void Manager::DisconnectAndRemoveAll() noexcept
	{
		try
		{
			std::optional<Containers::List<RelayPort>> remove_list;

			m_RelayLinks.WithUniqueLock([&](LinkMap& relays)
			{
				for (auto& it : relays)
				{
					it.second->WithUniqueLock([&](Link& rl)
					{
						{
							Peer::Peer_ThS::UniqueLockedType in_peer;
							Peer::Peer_ThS::UniqueLockedType out_peer;

							// Get the peers and lock them
							GetUniqueLocks(rl.GetIncomingPeer(), in_peer, rl.GetOutgoingPeer(), out_peer);

							if (rl.GetStatus() != Status::Closed)
							{
								rl.UpdateStatus(Status::Disconnected);

								ProcessRelayDisconnect(rl, in_peer, out_peer);
							}
						}

						// Collect the relay for removal
						if (!remove_list.has_value()) remove_list.emplace();

						remove_list->emplace_back(rl.GetPort());
					});
				}
			});

			// Remove all relays that were collected for removal
			if (remove_list.has_value() && !remove_list->empty())
			{
				Remove(*remove_list);
				remove_list->clear();
			}
		}
		catch (...) {}
	}

	void Manager::GetUniqueLocks(PeerDetails& ipeer, Peer::Peer_ThS::UniqueLockedType& in_peer,
								 PeerDetails& opeer, Peer::Peer_ThS::UniqueLockedType& out_peer) const noexcept
	{
		// Important to keep a copy of the shared_ptr
		// to the peers while we do work, in case they go
		// away in the mean time and are removed in the Peers
		// class, otherwise we're going to get memory faults

		if (ipeer.Peer == nullptr) ipeer.Peer = GetPeerManager().Get(ipeer.PeerLUID);
		if (opeer.Peer == nullptr) opeer.Peer = GetPeerManager().Get(opeer.PeerLUID);

		// Ensure deterministic lock order/direction to prevent possible deadlock
		// situations; smaller PeerLUID always gets locked first
		if (ipeer.PeerLUID < opeer.PeerLUID)
		{
			if (ipeer.Peer != nullptr) in_peer = ipeer.Peer->WithUniqueLock();
			if (opeer.Peer != nullptr) out_peer = opeer.Peer->WithUniqueLock();
		}
		else
		{
			if (opeer.Peer != nullptr) out_peer = opeer.Peer->WithUniqueLock();
			if (ipeer.Peer != nullptr) in_peer = ipeer.Peer->WithUniqueLock();
		}

		// If the peers are disconnected remove them
		{
			if (in_peer && in_peer->GetStatus() == Peer::Status::Disconnected)
			{
				in_peer.Reset();
			}

			if (out_peer && out_peer->GetStatus() == Peer::Status::Disconnected)
			{
				out_peer.Reset();
			}
		}
	}

	void Manager::GetUniqueLock(PeerDetails& rpeer, Peer::Peer_ThS::UniqueLockedType& peer) const noexcept
	{
		if (rpeer.Peer == nullptr) rpeer.Peer = GetPeerManager().Get(rpeer.PeerLUID);

		if (rpeer.Peer != nullptr) peer = rpeer.Peer->WithUniqueLock();

		// If the peer is disconnected remove it
		{
			if (peer && peer->GetStatus() == Peer::Status::Disconnected)
			{
				peer.Reset();
			}
		}
	}

	void Manager::DeterioratePeerReputation(const PeerLUID pluid,
											const Access::IPReputationUpdate rep_update) const noexcept
	{
		if (auto orig_peer = GetPeerManager().Get(pluid); orig_peer != nullptr)
		{
			orig_peer->WithUniqueLock([&](Peer::Peer& peer) noexcept
			{
				peer.UpdateReputation(rep_update);
			});
		}
	}

	const Link_ThS* Manager::Get(const RelayPort rport) const noexcept
	{
		Link_ThS* rcths{ nullptr };

		m_RelayLinks.WithSharedLock([&](const LinkMap& relays)
		{
			const auto it = relays.find(rport);
			if (it != relays.end())
			{
				rcths = it->second.get();
			}
		});

		return rcths;
	}

	Link_ThS* Manager::Get(const RelayPort rport) noexcept
	{
		return const_cast<Link_ThS*>(const_cast<const Manager*>(this)->Get(rport));
	}

	void Manager::PrimaryThreadWait(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event)
	{
		const auto result = thpdata.WorkEvents.Wait(1ms);
		if (!result.Waited)
		{
			shutdown_event.Wait(1ms);
		}
	}

	void Manager::PrimaryThreadProcessor(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event)
	{
		std::optional<Containers::List<RelayPort>> remove_list;

		m_RelayLinks.WithSharedLock([&](const LinkMap& relays)
		{
			if (relays.empty()) return;

			const auto& settings = GetSettings();
			const auto max_connect_duration = settings.Relay.ConnectTimeout;
			const auto closed_grace_period = settings.Relay.GracePeriod;

			for (auto it = relays.begin(); it != relays.end() && !shutdown_event.IsSet(); ++it)
			{
				it->second->IfUniqueLock([&](Link& rc)
				{
					if (rc.GetStatus() != Status::Closed)
					{
						Peer::Peer_ThS::UniqueLockedType in_peer;
						Peer::Peer_ThS::UniqueLockedType out_peer;

						// Get the peers and lock them
						GetUniqueLocks(rc.GetIncomingPeer(), in_peer, rc.GetOutgoingPeer(), out_peer);

						if (!in_peer)
						{
							LogDbg(L"No incoming peer for relay link on port %llu", rc.GetPort());

							auto exception = Exception::Unknown;

							if (rc.GetPosition() != Position::Beginning)
							{
								if (rc.GetStatus() == Status::Connected)
								{
									// If we were connected and the peer went away
									exception = Exception::ConnectionReset;
								}
							}

							rc.UpdateStatus(Status::Exception, exception);
						}
						else if (!out_peer)
						{
							LogDbg(L"No outgoing peer for relay link on port %llu", rc.GetPort());

							auto exception = Exception::Unknown;

							if (rc.GetPosition() != Position::End)
							{
								if (rc.GetStatus() == Status::Connect)
								{
									// Peer went away or connection failed
									exception = Exception::HostUnreachable;
								}
								else if (rc.GetStatus() == Status::Connecting || rc.GetStatus() == Status::Connected)
								{
									// If we were connecting/ed and the peer went away
									exception = Exception::ConnectionReset;
								}
							}

							rc.UpdateStatus(Status::Exception, exception);
						}
						else // Both peers are present
						{
							// Check for timeout
							if (rc.GetStatus() < Status::Connected &&
								((Util::GetCurrentSteadyTime() - rc.GetLastStatusChangeSteadyTime()) > max_connect_duration))
							{
								LogErr(L"Relay link on port %llu timed out; will remove", rc.GetPort());

								rc.UpdateStatus(Status::Exception, Exception::TimedOut);
							}
							else if (rc.GetStatus() == Status::Connect)
							{
								if ((rc.GetPosition() == Position::Beginning || rc.GetPosition() == Position::Between) &&
									out_peer->GetStatus() != Peer::Status::Ready)
								{
									// Outgoing peer may still be connecting;
									// we'll try again later
								}
								else
								{
									DiscardReturnValue(ProcessRelayConnect(rc, in_peer, out_peer));
								}
							}
							else if (rc.GetStatus() == Status::Connected)
							{
								DiscardReturnValue(ProcessRelayConnected(rc, in_peer, out_peer));
							}
						}

						if (rc.GetStatus() == Status::Disconnected || rc.GetStatus() == Status::Exception)
						{
							ProcessRelayDisconnect(rc, in_peer, out_peer);
						}
					}
					else if (rc.GetStatus() == Status::Closed &&
						((Util::GetCurrentSteadyTime() - rc.GetLastStatusChangeSteadyTime()) > closed_grace_period))
					{
						// Collect the relay for removal
						if (!remove_list.has_value()) remove_list.emplace();

						remove_list->emplace_back(rc.GetPort());
					}
				});
			}
		});

		// Remove all relays that were collected for removal
		if (remove_list.has_value() && !remove_list->empty())
		{
			LogDbg(L"Removing relays");
			Remove(*remove_list);

			remove_list->clear();
		}
	}

	void Manager::WorkerThreadWait(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event)
	{
		thdata.EventQueue->Wait(shutdown_event);
	}

	void Manager::WorkerThreadWaitInterrupt(ThreadPoolData& thpdata, ThreadData& thdata)
	{
		thdata.EventQueue->InterruptWait();
	}

	void Manager::WorkerThreadProcessor(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event)
	{
		std::optional<Event> event;

		thdata.EventQueue->PopFrontIf([&](auto& fevent) noexcept -> bool
		{
			event = std::move(fevent);
			return true;
		});

		if (event.has_value())
		{
			std::visit(Util::Overloaded{
				[&](auto& revent)
				{
					ProcessRelayEvent(revent);
				},
				[&](Events::RelayData& revent)
				{
					while (ProcessRelayEvent(revent) == RelayDataProcessResult::Retry && !shutdown_event.IsSet())
					{
						std::this_thread::sleep_for(1ms);
					}
				}
			}, *event);
		}
	}

	bool Manager::ProcessRelayConnect(Link& rl,
									  Peer::Peer_ThS::UniqueLockedType& in_peer,
									  Peer::Peer_ThS::UniqueLockedType& out_peer)
	{
		assert(rl.GetStatus() == Status::Connect);

		auto success = false;
		Peer::Peer_ThS::UniqueLockedType* peer{ nullptr };

		switch (rl.GetPosition())
		{
			case Position::Beginning:
			{
				peer = &in_peer;

				LogDbg(L"Connecting relay to peer %s on port %llu for hop %u (beginning); outgoing peer %s",
					   rl.GetEndpoint().GetString().c_str(), rl.GetPort(), rl.GetHop(), out_peer->GetPeerName().c_str());

				if (out_peer->GetMessageProcessor().SendBeginRelay(rl.GetPort(), rl.GetEndpoint(), rl.GetHop() - 1))
				{
					in_peer->GetSocket<Socket>().SetLocalEndpoint(out_peer->GetLocalEndpoint(), rl.GetPort(), rl.GetHop());
					success = rl.UpdateStatus(Status::Connecting);
				}

				break;
			}
			case Position::End:
			{
				peer = &out_peer;

				LogDbg(L"Connecting relay to peer %s on port %llu for hop %u (end); incoming peer %s",
					   rl.GetEndpoint().GetString().c_str(), rl.GetPort(), rl.GetHop(), in_peer->GetPeerName().c_str());

				if (rl.SendRelayStatus(*in_peer, std::nullopt, RelayStatusUpdate::Connected))
				{
					if (rl.UpdateStatus(Status::Connected))
					{
						success = out_peer->GetSocket<Socket>().CompleteAccept();
					}
				}

				break;
			}
			case Position::Between:
			{
				LogDbg(L"Connecting relay to peer %s on port %llu for hop %u (between); incoming peer %s, outgoing peer %s",
					   rl.GetEndpoint().GetString().c_str(), rl.GetPort(), rl.GetHop(),
					   in_peer->GetPeerName().c_str(), out_peer->GetPeerName().c_str());

				if (out_peer->GetMessageProcessor().SendBeginRelay(rl.GetPort(), rl.GetEndpoint(), rl.GetHop() - 1))
				{
					success = rl.UpdateStatus(Status::Connecting);
				}

				break;
			}
			default:
			{
				assert(false);
				break;
			}
		}

		if (peer != nullptr)
		{
			success = m_ThreadPool.GetData().WorkEvents.AddEvent((*peer)->GetSocket<Socket>().GetSendEvent().GetHandle());
		}

		if (!success) rl.UpdateStatus(Status::Exception, Exception::GeneralFailure);

		return success;
	}

	bool Manager::ProcessRelayConnected(Link& rl,
										Peer::Peer_ThS::UniqueLockedType& in_peer,
										Peer::Peer_ThS::UniqueLockedType& out_peer)
	{
		assert(rl.GetStatus() == Status::Connected);

		bool success = true;

		PeerLUID orig_luid{ 0 };
		Peer::Peer_ThS::UniqueLockedType* orig_peer{ nullptr };

		switch (rl.GetPosition())
		{
			case Position::Beginning:
				orig_luid = rl.GetIncomingPeer().PeerLUID;
				orig_peer = &in_peer;
				break;
			case Position::End:
				orig_luid = rl.GetOutgoingPeer().PeerLUID;
				orig_peer = &out_peer;
				break;
			default:
				break;
		}

		if (orig_peer != nullptr)
		{
			auto send_buffer = (*orig_peer)->GetSocket<Socket>().GetSendBuffer();
			auto& rdrl = rl.GetDataRateLimiter();

			while (success && rdrl.CanAddMTU())
			{
				const auto send_size = std::invoke([&]()
				{
					// Shouldn't send more than available MTU size
					auto size = std::min(send_buffer->GetSize(), rdrl.GetMTUSize());
					// Shouldn't send more than maximum data a relay data message can handle
					size = std::min(size, RelayDataMessage::MaxMessageDataSize);
					return size;
				});

				if (send_size > 0)
				{
					const auto msg_id = rdrl.GetNewMessageID();

					Events::RelayData red;
					red.Port = rl.GetPort();
					red.MessageID = msg_id;
					red.Data = BufferView(*send_buffer).GetFirst(send_size);
					red.Origin.PeerLUID = orig_luid;

					if (AddRelayEvent(rl.GetPort(), std::move(red)))
					{
						send_buffer->RemoveFirst(send_size);

						success = rdrl.AddMTU(msg_id, send_size, Util::GetCurrentSteadyTime());
					}
					else success = false;

					if (!success)
					{
						rl.UpdateStatus(Status::Exception, Exception::GeneralFailure);
					}
				}
				else break;
			}

			// Update socket send event
			(*orig_peer)->GetSocket<Socket>().SetRelayWrite(rdrl.CanAddMTU());
		}

		return success;
	}

	void Manager::ProcessRelayDisconnect(Link& rl,
										 Peer::Peer_ThS::UniqueLockedType& in_peer,
										 Peer::Peer_ThS::UniqueLockedType& out_peer) noexcept
	{
		assert(rl.GetStatus() == Status::Disconnected || rl.GetStatus() == Status::Exception);

		auto status_update{ RelayStatusUpdate::Disconnected };
		auto wsaerror{ -1 };

		switch (rl.GetException())
		{
			case Exception::Unknown:
				break;
			case Exception::GeneralFailure:
				status_update = RelayStatusUpdate::GeneralFailure;
				wsaerror = WSAECONNABORTED;
				break;
			case Exception::ConnectionReset:
				status_update = RelayStatusUpdate::ConnectionReset;
				wsaerror = WSAECONNRESET;
				break;
			case Exception::NoPeersAvailable:
				status_update = RelayStatusUpdate::NoPeersAvailable;
				wsaerror = WSAENETUNREACH;
				break;
			case Exception::HostUnreachable:
				status_update = RelayStatusUpdate::HostUnreachable;
				wsaerror = WSAEHOSTUNREACH;
				break;
			case Exception::ConnectionRefused:
				status_update = RelayStatusUpdate::ConnectionRefused;
				wsaerror = WSAECONNREFUSED;
				break;
			case Exception::TimedOut:
				status_update = RelayStatusUpdate::TimedOut;
				wsaerror = WSAETIMEDOUT;
				break;
			default:
				assert(false);
				break;
		}

		Peer::Peer_ThS::UniqueLockedType temp_peer;
		Peer::Peer_ThS::UniqueLockedType* peer{ nullptr };

		switch (rl.GetPosition())
		{
			case Position::Beginning:
			{
				if (in_peer)
				{
					peer = &in_peer;

					// In case the connection was closed properly we just enable read
					// on the socket so that it will receive 0 bytes indicating the connection closed
					if (wsaerror != -1) in_peer->GetSocket<Socket>().SetException(wsaerror);
					else in_peer->GetSocket<Socket>().SetSocketRead();
				}
				else
				{
					temp_peer = rl.GetIncomingPeer().Peer->WithUniqueLock();
					peer = &temp_peer;
				}

				if (out_peer) DiscardReturnValue(rl.SendRelayStatus(*out_peer, std::nullopt, status_update));

				break;
			}
			case Position::End:
			{
				if (out_peer)
				{
					peer = &out_peer;

					// In case the connection was closed properly we just enable read
					// on the socket so that it will receive 0 bytes indicating the connection closed
					if (wsaerror != -1) out_peer->GetSocket<Socket>().SetException(wsaerror);
					else out_peer->GetSocket<Socket>().SetSocketRead();
				}
				else
				{
					temp_peer = rl.GetOutgoingPeer().Peer->WithUniqueLock();
					peer = &temp_peer;
				}

				if (in_peer) DiscardReturnValue(rl.SendRelayStatus(*in_peer, std::nullopt, status_update));

				break;
			}
			case Position::Between:
			{
				if (in_peer) DiscardReturnValue(rl.SendRelayStatus(*in_peer, std::nullopt, status_update));

				if (out_peer) DiscardReturnValue(rl.SendRelayStatus(*out_peer, std::nullopt, status_update));

				break;
			}
			default:
			{
				assert(false);
				break;
			}
		}

		if (peer != nullptr)
		{
			m_ThreadPool.GetData().WorkEvents.RemoveEvent((*peer)->GetSocket<Socket>().GetSendEvent().GetHandle());
		}

		rl.UpdateStatus(Status::Closed);
	}

	bool Manager::ProcessRelayEvent(const Events::Connect& connect_event) noexcept
	{
		// Increase relay connection attempts for this IP; if attempts get too high
		// for a given interval the IP will get a bad reputation and this will fail
		if (!GetAccessManager().AddIPRelayConnectionAttempt(connect_event.Origin.PeerEndpoint.GetIPAddress()))
		{
			LogWarn(L"Relay link from peer %s (LUID %llu) was rejected; maximum number of allowed attempts exceeded",
					connect_event.Origin.PeerEndpoint.GetString().c_str(), connect_event.Origin.PeerLUID);
			return false;
		}

		auto rstatus = RelayStatusUpdate::GeneralFailure;
		String error_details;

		LogInfo(L"Accepting new relay link on endpoint %s for port %llu (hop %u)",
				connect_event.Origin.LocalEndpoint.GetString().c_str(),
				connect_event.Port, connect_event.Hop);

		std::optional<PeerLUID> out_peer;
		auto reused = false;

		if (connect_event.Hop == 0) // Final hop
		{
			auto peerths = m_PeerManager.CreateRelay(PeerConnectionType::Inbound, std::nullopt);
			if (peerths != nullptr)
			{
				peerths->WithUniqueLock([&](Peer::Peer& peer) noexcept
				{
					if (peer.GetSocket<Socket>().BeginAccept(connect_event.Port, connect_event.Hop,
															 connect_event.Origin.LocalEndpoint,
															 connect_event.Origin.PeerEndpoint))
					{
						if (m_PeerManager.Add(peerths))
						{
							out_peer = peer.GetLUID();
						}
						else peer.Close();
					}
				});
			}
		}
		else if (connect_event.Hop == 1)
		{
			try
			{
				const auto excl_addr1 = m_PeerManager.GetLocalIPAddresses();
				if (excl_addr1 != nullptr)
				{
					Vector<BinaryIPAddress> excl_addr2{ connect_event.Origin.PeerEndpoint.GetIPAddress().GetBinary() };

					// Don't include addresses/network of local instance
					const auto result1 = m_PeerManager.AreRelayIPsInSameNetwork(connect_event.Endpoint.GetIPAddress().GetBinary(),
																		  *excl_addr1);
					// Don't include origin address/network
					const auto result2 = m_PeerManager.AreRelayIPsInSameNetwork(connect_event.Endpoint.GetIPAddress().GetBinary(),
																		  excl_addr2);

					if (result1.Succeeded() && result2.Succeeded())
					{
						if (!result1.GetValue() && !result2.GetValue())
						{
							// Connect to a specific endpoint for final hop 0
							const auto result2 = m_PeerManager.ConnectTo({ connect_event.Endpoint }, nullptr);
							if (result2.Succeeded())
							{
								out_peer = result2->first;
								reused = result2->second;
							}
							else
							{
								LogErr(L"Couldn't connect to final endpoint %s for relay port %llu",
									   connect_event.Endpoint.GetString().c_str(), connect_event.Port);

								if (result2 == ResultCode::NotAllowed)
								{
									rstatus = RelayStatusUpdate::ConnectionRefused;
									error_details = L"connection to final endpoint is not allowed by access configuration";
								}
							}
						}
						else
						{
							rstatus = RelayStatusUpdate::ConnectionRefused;
							error_details = L"connection to final endpoint is not allowed because it's on the same network as the origin or local instance";
						}
					}
					else error_details = L"couldn't check if endpoint is on excluded networks";
				}
				else error_details = L"couldn't get IP addresses of local instance";
			}
			catch (...)
			{
				error_details = L"an exception was thrown";
			}
		}
		else // Hop in between
		{
			try
			{
				// Don't include addresses/network of local instance
				const auto excl_addr1 = m_PeerManager.GetLocalIPAddresses();
				if (excl_addr1 != nullptr)
				{
					Vector<BinaryIPAddress> excl_addr2
					{
						// Don't include origin address/network
						connect_event.Origin.PeerEndpoint.GetIPAddress().GetBinary(),
						// Don't include the final endpoint/network
						connect_event.Endpoint.GetIPAddress().GetBinary()
					};

					const auto result = m_PeerManager.GetRelayPeer(*excl_addr1, excl_addr2);
					if (result.Succeeded())
					{
						out_peer = result.GetValue();
					}
					else
					{
						if (result == ResultCode::PeerNotFound)
						{
							rstatus = RelayStatusUpdate::NoPeersAvailable;
							error_details = L"no peers available to create relay connection";
						}
						else
						{
							error_details = L"failed to get a peer to create relay connection";
						}
					}
				}
				else error_details = L"couldn't get IP addresses of local instance";
			}
			catch (...)
			{
				error_details = L"an exception was thrown";
			}
		}

		if (out_peer)
		{
			if (!Accept(connect_event, *out_peer))
			{
				// Failed to accept, so cancel connection
				// we made for this relay link
				if (connect_event.Hop == 0 ||
					(connect_event.Hop == 1 && !reused))
				{
					DiscardReturnValue(m_PeerManager.DisconnectFrom(*out_peer, nullptr));
				}

				out_peer.reset();
			}
		}

		if (!out_peer)
		{
			if (!error_details.empty()) error_details = L" - " + error_details;

			LogErr(L"Failed to accept relay link on endpoint %s for relay port %llu (hop %u)%s",
				   connect_event.Origin.LocalEndpoint.GetString().c_str(),
				   connect_event.Port, connect_event.Hop, error_details.c_str());

			// Couldn't accept; let the incoming peer know
			auto peerths = m_PeerManager.Get(connect_event.Origin.PeerLUID);
			if (peerths != nullptr)
			{
				peerths->WithUniqueLock()->GetMessageProcessor().SendRelayStatus(connect_event.Port, rstatus);
			}
		}

		return true;
	}

	bool Manager::ProcessRelayEvent(const Events::StatusUpdate& event) noexcept
	{
		auto success = false;

		if (auto relayths = Get(event.Port); relayths != nullptr)
		{
			relayths->WithUniqueLock([&](Link& rl) noexcept
			{
				// Event should come from expected origin
				if (!ValidateEventOrigin(event, rl)) return;

				// If relay is already closed don't bother
				if (rl.GetStatus() == Status::Closed) return;

				{
					Peer::Peer_ThS::UniqueLockedType in_peer;
					Peer::Peer_ThS::UniqueLockedType out_peer;

					// Get the peers and lock them
					GetUniqueLocks(rl.GetIncomingPeer(), in_peer, rl.GetOutgoingPeer(), out_peer);

					if (in_peer && out_peer) // Both peers are present
					{
						const auto prev_status = rl.GetStatus();

						if (rl.UpdateStatus(event.Origin.PeerLUID, event.Status))
						{
							if (rl.GetPosition() == Position::Beginning &&
								rl.GetStatus() == Status::Connected &&
								prev_status != rl.GetStatus())
							{
								// We went to the connected state while we were connecting;
								// the socket is now writable
								in_peer->GetSocket<Socket>().SetSocketWrite();
								success = true;
							}
							else if (rl.GetPosition() == Position::Between)
							{
								Peer::Peer_ThS::UniqueLockedType* peer1 = &in_peer;
								Peer::Peer_ThS::UniqueLockedType* peer2 = &out_peer;
								if (event.Origin.PeerLUID == rl.GetOutgoingPeer().PeerLUID)
								{
									peer1 = &out_peer;
									peer2 = &in_peer;
								}

								// Forward status update to the other peer
								if (rl.SendRelayStatus(*(*peer2), (*peer1)->GetLUID(), event.Status))
								{
									success = true;
								}
							}
							else success = true;
						}
					}
				}

				if (!success) rl.UpdateStatus(Status::Exception, Exception::GeneralFailure);
			});
		}
		else
		{
			// Received event for invalid relay link; this could be an attack
			LogWarn(L"Peer LUID %llu sent relay status update for an unknown port %llu",
					event.Origin.PeerLUID, event.Port);

			DeterioratePeerReputation(event.Origin.PeerLUID);
		}

		return success;
	}

	Manager::RelayDataProcessResult Manager::ProcessRelayEvent(Events::RelayData& event) noexcept
	{
		auto retval = RelayDataProcessResult::Failed;

		if (auto relayths = Get(event.Port); relayths != nullptr)
		{
			relayths->WithUniqueLock([&](Link& rl) noexcept
			{
				// Event should come from expected origin
				if (!ValidateEventOrigin(event, rl)) return;

				// If relay is not (yet) connected (anymore) don't bother
				if (rl.GetStatus() != Status::Connected)
				{
					DbgInvoke([&]()
					{
						LogErr(L"Received relay data event from peer LUID %llu on port %llu that's not connected",
							   event.Origin.PeerLUID, event.Port);
					});

					return;
				}

				bool data_ack_needed{ false };

				auto orig_rpeer = &rl.GetOutgoingPeer();
				auto dest_rpeer = &rl.GetIncomingPeer();
				if (event.Origin.PeerLUID == rl.GetIncomingPeer().PeerLUID)
				{
					orig_rpeer = &rl.GetIncomingPeer();
					dest_rpeer = &rl.GetOutgoingPeer();
				}

				{
					Peer::Peer_ThS::UniqueLockedType dest_peer;

					// Get the peer and lock it
					GetUniqueLock(*dest_rpeer, dest_peer);

					if (dest_peer) // If peer is present
					{
						switch (rl.GetPosition())
						{
							case Position::Beginning:
							{
								if (event.Origin.PeerLUID == rl.GetIncomingPeer().PeerLUID)
								{
									if (const auto result = dest_peer->GetMessageProcessor().SendRelayData(
										RelayDataMessage{ rl.GetPort(), event.MessageID, event.Data }); result.Succeeded())
									{
										retval = RelayDataProcessResult::Succeeded;
									}
									else if (result == ResultCode::PeerSendBufferFull)
									{
										retval = RelayDataProcessResult::Retry;
									}
								}
								else
								{
									try
									{
										data_ack_needed = true;

										auto rcv_buffer = dest_peer->GetSocket<Socket>().GetReceiveBuffer();
										*rcv_buffer += event.Data;

										retval = RelayDataProcessResult::Succeeded;
									}
									catch (...) {}
								}
								break;
							}
							case Position::End:
							{
								if (event.Origin.PeerLUID == rl.GetIncomingPeer().PeerLUID)
								{
									try
									{
										data_ack_needed = true;

										auto rcv_buffer = dest_peer->GetSocket<Socket>().GetReceiveBuffer();
										*rcv_buffer += event.Data;

										retval = RelayDataProcessResult::Succeeded;
									}
									catch (...) {}
								}
								else
								{
									if (const auto result = dest_peer->GetMessageProcessor().SendRelayData(
										RelayDataMessage{ rl.GetPort(), event.MessageID, event.Data }); result.Succeeded())
									{
										retval = RelayDataProcessResult::Succeeded;
									}
									else if (result == ResultCode::PeerSendBufferFull)
									{
										retval = RelayDataProcessResult::Retry;
									}
								}
								break;
							}
							case Position::Between:
							{
								if (const auto result = dest_peer->GetMessageProcessor().SendRelayData(
									RelayDataMessage{ rl.GetPort(), event.MessageID, event.Data }); result.Succeeded())
								{
									retval = RelayDataProcessResult::Succeeded;
								}
								else if (result == ResultCode::PeerSendBufferFull)
								{
									retval = RelayDataProcessResult::Retry;
								}
								break;
							}
							default:
							{
								assert(false);
								break;
							}
						}
					}
				}

				if (data_ack_needed && retval == RelayDataProcessResult::Succeeded)
				{
					Peer::Peer_ThS::UniqueLockedType orig_peer;

					// Get the peer and lock it
					GetUniqueLock(*orig_rpeer, orig_peer);

					if (orig_peer) // If peer is present
					{
						// Send RelayDataAck to the origin
						if (!orig_peer->GetMessageProcessor().SendRelayDataAck({ rl.GetPort(), event.MessageID }))
						{
							retval = RelayDataProcessResult::Failed;
						}
					}
				}

				if (retval == RelayDataProcessResult::Failed)
				{
					rl.UpdateStatus(Status::Exception, Exception::GeneralFailure);
				}
			});
		}
		else
		{
			// Received event for invalid relay link; this could be an attack
			LogWarn(L"Peer LUID %llu sent relay data for an unknown port %llu",
					event.Origin.PeerLUID, event.Port);

			DeterioratePeerReputation(event.Origin.PeerLUID);
		}

		return retval;
	}

	bool Manager::ProcessRelayEvent(const Events::RelayDataAck& event) noexcept
	{
		auto success = false;

		if (auto relayths = Get(event.Port); relayths != nullptr)
		{
			relayths->WithUniqueLock([&](Link& rl) noexcept
			{
				// Event should come from expected origin
				if (!ValidateEventOrigin(event, rl)) return;

				// If relay is not (yet) connected (anymore) don't bother
				if (rl.GetStatus() != Status::Connected)
				{
					DbgInvoke([&]()
					{
						LogErr(L"Received relay data ack from peer LUID %llu on port %llu that's not connected",
							   event.Origin.PeerLUID, event.Port);
					});

					return;
				}

				auto dest_rpeer = &rl.GetIncomingPeer();
				if (event.Origin.PeerLUID == rl.GetIncomingPeer().PeerLUID)
				{
					dest_rpeer = &rl.GetOutgoingPeer();
				}

				Peer::Peer_ThS::UniqueLockedType dest_peer;

				switch (rl.GetPosition())
				{
					case Position::Beginning:
					case Position::End:
					{
						auto& rdrl = rl.GetDataRateLimiter();

						if (rdrl.AckMTU(event.MessageID, Util::GetCurrentSteadyTime()))
						{
							success = true;

							// Get the peer and lock it
							GetUniqueLock(*dest_rpeer, dest_peer);

							if (dest_peer) // If peer is present
							{
								// Update socket send event
								dest_peer->GetSocket<Socket>().SetRelayWrite(rdrl.CanAddMTU());
							}
						}

						break;
					}
					case Position::Between:
					{
						// Get the peer and lock it
						GetUniqueLock(*dest_rpeer, dest_peer);

						if (dest_peer) // If peer is present
						{
							// Forward RelayDataAck to the destination
							success = dest_peer->GetMessageProcessor().SendRelayDataAck({ event.Port, event.MessageID });
						}

						break;
					}
					default:
					{
						assert(false);
						break;
					}
				}

				if (!success) rl.UpdateStatus(Status::Exception, Exception::GeneralFailure);
			});
		}
		else
		{
			// Received event for invalid relay link; this could be an attack
			LogWarn(L"Peer LUID %llu sent relay data ack for an unknown port %llu",
					event.Origin.PeerLUID, event.Port);

			DeterioratePeerReputation(event.Origin.PeerLUID);
		}

		return success;
	}

	template<typename T>
	bool Manager::ValidateEventOrigin(const T& event, const Link& rl) const noexcept
	{
		if (event.Origin.PeerLUID != rl.GetIncomingPeer().PeerLUID &&
			event.Origin.PeerLUID != rl.GetOutgoingPeer().PeerLUID)
		{
			// Received event from a peer not related to this relay
			// link locally; this could be an attack
			LogErr(L"Peer LUID %llu sent relay data for unrelated port %llu",
				   event.Origin.PeerLUID, event.Port);

			DeterioratePeerReputation(event.Origin.PeerLUID, Access::IPReputationUpdate::DeteriorateSevere);
			return false;
		}

		return true;
	}
}