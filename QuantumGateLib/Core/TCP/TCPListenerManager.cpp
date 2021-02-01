// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "TCPListenerManager.h"

using namespace std::literals;
using namespace QuantumGate::Implementation::Network;

namespace QuantumGate::Implementation::Core::TCP::Listener
{
	Manager::Manager(const Settings_CThS& settings, Access::Manager& accessmgr, Peer::Manager& peers) noexcept :
		m_Settings(settings), m_AccessManager(accessmgr), m_PeerManager(peers)
	{}

	// Starts listening on the default interfaces
	bool Manager::Startup() noexcept
	{
		if (m_Running) return true;

		LogSys(L"TCP listenermanager starting up...");

		PreStartup();

		const auto& settings = m_Settings.GetCache();
		const auto& listener_ports = settings.Local.Listeners.TCP.Ports;
		const auto nat_traversal = settings.Local.Listeners.NATTraversal;
		const auto cond_accept = settings.Local.Listeners.TCP.UseConditionalAcceptFunction;

		// Should have at least one port
		if (listener_ports.empty())
		{
			LogErr(L"TCP listenermanager startup failed; no ports given");
			return false;
		}

		const std::array<IPAddress::Family, 2> afs{ IPAddress::Family::IPv4, IPAddress::Family::IPv6 };

		for (const auto& af : afs)
		{
			const std::optional<IPAddress> address = std::invoke([&]() -> std::optional<IPAddress>
			{
				switch (af)
				{
					case IPAddress::Family::IPv4:
						return IPAddress::AnyIPv4();
						break;
					case IPAddress::Family::IPv6:
						return IPAddress::AnyIPv6();
						break;
					default:
						assert(false);
						break;
				}

				return std::nullopt;
			});

			if (address.has_value())
			{
				DiscardReturnValue(AddListenerThreads(*address, listener_ports, cond_accept, nat_traversal));
			}
		}

		if (m_ThreadPool.Startup())
		{
			m_Running = true;
			m_ListeningOnAnyAddresses = true;

			LogSys(L"TCP listenermanager startup successful");
		}
		else LogErr(L"TCP listenermanager startup failed");

		return m_Running;
	}

	// Starts listening on all active interfaces
	bool Manager::Startup(const Vector<API::Local::Environment::EthernetInterface>& interfaces) noexcept
	{
		if (m_Running) return true;

		LogSys(L"TCP listenermanager starting...");

		PreStartup();

		const auto& settings = m_Settings.GetCache();
		const auto& listener_ports = settings.Local.Listeners.TCP.Ports;
		const auto nat_traversal = settings.Local.Listeners.NATTraversal;
		const auto cond_accept = settings.Local.Listeners.TCP.UseConditionalAcceptFunction;

		// Should have at least one port
		if (listener_ports.empty())
		{
			LogErr(L"TCP listenermanager startup failed; no ports given");
			return false;
		}

		// Create a listening socket for each interface that's online
		for (const auto& ifs : interfaces)
		{
			if (ifs.Operational)
			{
				for (const auto& address : ifs.IPAddresses)
				{
					// Only for IPv4 and IPv6 addresses
					if (address.GetFamily() == IPAddress::Family::IPv4 ||
						address.GetFamily() == IPAddress::Family::IPv6)
					{
						DiscardReturnValue(AddListenerThreads(address, listener_ports, cond_accept, nat_traversal));
					}
					else assert(false);
				}
			}
		}

		if (m_ThreadPool.Startup())
		{
			m_Running = true;
			m_ListeningOnAnyAddresses = false;

			LogSys(L"TCP listenermanager startup successful");
		}
		else LogErr(L"TCP listenermanager startup failed");

		return m_Running;
	}

	bool Manager::AddListenerThreads(const IPAddress& address, const Vector<UInt16> ports,
									 const bool cond_accept, const bool nat_traversal) noexcept
	{
		// Separate listener for every port
		for (const auto port : ports)
		{
			try
			{
				const auto endpoint = IPEndpoint(IPEndpoint::Protocol::TCP, address, port);

				// Create and start the listenersocket
				ThreadData ltd;
				ltd.Socket = Network::Socket(endpoint.GetIPAddress().GetFamily(),
											 Network::Socket::Type::Stream, Network::IP::Protocol::TCP);
				ltd.UseConditionalAcceptFunction = cond_accept;
				
				if (ltd.Socket.Listen(endpoint, true, nat_traversal))
				{
					if (m_ThreadPool.AddThread(L"QuantumGate Listener Thread " + endpoint.GetString(),
											   std::move(ltd), MakeCallback(this, &Manager::WorkerThreadProcessor)))
					{
						LogSys(L"Listening on endpoint %s", endpoint.GetString().c_str());
					}
					else
					{
						LogErr(L"Could not add listener thread for endpoint %s", endpoint.GetString().c_str());
					}
				}
			}
			catch (const std::exception& e)
			{
				LogErr(L"Could not add listener thread for IP %s due to exception: %s",
					   address.GetString().c_str(), Util::ToStringW(e.what()).c_str());
			}
			catch (...) {}
		}

		return true;
	}

	std::optional<Manager::ThreadPool::ThreadType> Manager::RemoveListenerThread(Manager::ThreadPool::ThreadType&& thread) noexcept
	{
		const IPEndpoint endpoint = thread.GetData().Socket.GetLocalEndpoint();

		const auto [success, next_thread] = m_ThreadPool.RemoveThread(std::move(thread));
		if (success)
		{
			LogSys(L"Stopped listening on endpoint %s", endpoint.GetString().c_str());
		}
		else
		{
			LogErr(L"Could not remove listener thread for endpoint %s", endpoint.GetString().c_str());
		}

		return next_thread;
	}

	bool Manager::Update(const Vector<API::Local::Environment::EthernetInterface>& interfaces) noexcept
	{
		if (!m_Running) return false;
		
		// No need to update in this case
		if (m_ListeningOnAnyAddresses) return true;

		LogSys(L"Updating TCP listenermanager...");

		const auto& settings = m_Settings.GetCache();
		const auto& listener_ports = settings.Local.Listeners.TCP.Ports;
		const auto nat_traversal = settings.Local.Listeners.NATTraversal;
		const auto cond_accept = settings.Local.Listeners.TCP.UseConditionalAcceptFunction;

		// Check for interfaces/IP addresses that were added for which
		// there are no listeners; we add listeners for those
		for (const auto& ifs : interfaces)
		{
			if (ifs.Operational)
			{
				for (const auto& address : ifs.IPAddresses)
				{
					// Only for IPv4 and IPv6 addresses
					if (address.GetFamily() == IP::AddressFamily::IPv4 || address.GetFamily() == IP::AddressFamily::IPv6)
					{
						auto found = false;

						auto thread = m_ThreadPool.GetFirstThread();

						while (thread.has_value())
						{
							if (thread->GetData().Socket.GetLocalIPAddress() == address)
							{
								found = true;
								break;
							}
							else thread = m_ThreadPool.GetNextThread(*thread);
						}

						if (!found)
						{
							DiscardReturnValue(AddListenerThreads(address, listener_ports, cond_accept, nat_traversal));
						}
					}
				}
			}
		}

		// Check for interfaces/IP addresses that were removed for which
		// there are still listeners; we remove listeners for those
		auto thread = m_ThreadPool.GetFirstThread();

		while (thread.has_value())
		{
			auto found = false;

			for (const auto& ifs : interfaces)
			{
				if (ifs.Operational)
				{
					for (const auto& address : ifs.IPAddresses)
					{
						if (thread->GetData().Socket.GetLocalIPAddress() == address)
						{
							found = true;
							break;
						}
					}
				}

				if (found) break;
			}

			if (!found)
			{
				thread = RemoveListenerThread(std::move(*thread));
			}
			else thread = m_ThreadPool.GetNextThread(*thread);
		}

		return true;
	}

	void Manager::Shutdown() noexcept
	{
		if (!m_Running) return;

		m_Running = false;

		LogSys(L"TCP listenermanager shutting down...");

		m_ThreadPool.Shutdown();

		ResetState();

		LogSys(L"TCP listenermanager shut down");
	}

	void Manager::PreStartup() noexcept
	{
		ResetState();
	}

	void Manager::ResetState() noexcept
	{
		m_ListeningOnAnyAddresses = false;
		m_ThreadPool.Clear();
	}

	void Manager::WorkerThreadProcessor(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event)
	{
		while (!shutdown_event.IsSet())
		{
			// Check if we have a read event waiting for us
			if (thdata.Socket.UpdateIOStatus(1ms))
			{
				if (thdata.Socket.GetIOStatus().CanRead())
				{
					// Probably have a connection waiting to accept
					LogInfo(L"Accepting new connection on endpoint %s",
							thdata.Socket.GetLocalEndpoint().GetString().c_str());

					AcceptConnection(thdata.Socket, thdata.UseConditionalAcceptFunction);
				}
				else if (thdata.Socket.GetIOStatus().HasException())
				{
					LogErr(L"Exception on listener socket for endpoint %s (%s); will exit thread",
						   thdata.Socket.GetLocalEndpoint().GetString().c_str(),
						   GetSysErrorString(thdata.Socket.GetIOStatus().GetErrorCode()).c_str());
					break;
				}
			}
			else
			{
				LogErr(L"Could not get status of listener socket for endpoint %s; will exit thread",
					   thdata.Socket.GetLocalEndpoint().GetString().c_str());
				break;
			}
		}
	}

	void Manager::AcceptConnection(Network::Socket& listener_socket, const bool cond_accept) noexcept
	{
		auto peerths = m_PeerManager.CreateTCP(listener_socket.GetAddressFamily(), PeerConnectionType::Inbound, std::nullopt);
		if (peerths != nullptr)
		{
			peerths->WithUniqueLock([&](Peer::Peer& peer)
			{
				if (cond_accept)
				{
					if (!listener_socket.Accept(peer.GetSocket<TCP::Socket>(), true,
												&Manager::AcceptConditionFunction, this))
					{
						// Couldn't accept for some reason
						return;
					}
				}
				else
				{
					if (listener_socket.Accept(peer.GetSocket<TCP::Socket>(), false, nullptr, nullptr))
					{
						// Check if the IP address is allowed
						if (!CanAcceptConnection(peer.GetPeerIPAddress()))
						{
							peer.Close();
							LogWarn(L"Incoming connection from peer %s was rejected; IP address is not allowed by access configuration",
									peer.GetPeerName().c_str());

							return;
						}
					}
				}

				if (m_PeerManager.Accept(peerths))
				{
					LogInfo(L"Connection accepted from peer %s", peer.GetPeerName().c_str());
				}
				else
				{
					peer.Close();
					LogErr(L"Could not accept connection from peer %s", peer.GetPeerName().c_str());
				}
			});
		}
	}

	bool Manager::CanAcceptConnection(const IPAddress& ipaddr) const noexcept
	{
		// Increase connection attempts for this IP; if attempts get too high
		// for a given interval the IP will get a bad reputation and this will fail
		if (m_AccessManager.AddIPConnectionAttempt(ipaddr))
		{
			// Check if IP is allowed through filters/limits and if it has acceptible reputation
			if (const auto result = m_AccessManager.GetIPConnectionAllowed(ipaddr, Access::CheckType::All); result.Succeeded())
			{
				return *result;
			}
		}

		// If anything goes wrong we always deny access
		return false;
	}

	int CALLBACK Manager::AcceptConditionFunction(LPWSABUF lpCallerId, LPWSABUF lpCallerData, LPQOS lpSQOS, LPQOS lpGQOS,
												  LPWSABUF lpCalleeId, LPWSABUF lpCalleeData, GROUP FAR* g,
												  DWORD_PTR dwCallbackData) noexcept
	{
		const IPEndpoint endpoint(IPEndpoint::Protocol::TCP, reinterpret_cast<sockaddr_storage*>(lpCallerId->buf));

		if (reinterpret_cast<Manager*>(dwCallbackData)->CanAcceptConnection(endpoint.GetIPAddress()))
		{
			return CF_ACCEPT;
		}

		LogWarn(L"Incoming connection attempt from peer %s was rejected; IP address is not allowed by access configuration",
				endpoint.GetString().c_str());

		return CF_REJECT;
	}
}