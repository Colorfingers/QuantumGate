// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\Concurrency\ThreadPool.h"
#include "Peer\PeerManager.h"
#include "Access\AccessManager.h"

namespace QuantumGate::Implementation::Core::Listener
{
	class Manager
	{
		struct ThreadData
		{
			ThreadData() = default;
			ThreadData(const ThreadData&) = delete;
			ThreadData(ThreadData&&) = default;
			
			~ThreadData()
			{
				if (Socket)
				{
					if (Socket->GetIOStatus().IsOpen()) Socket->Close();
					Socket.reset();
				}
			}

			ThreadData& operator=(const ThreadData&) = delete;
			ThreadData& operator=(ThreadData&&) = default;

			std::unique_ptr<Network::Socket> Socket{ nullptr };
			bool UseConditionalAcceptFunction{ true };
		};

		struct ThreadPoolData
		{
			ThreadPoolData(Manager& mgr) noexcept : ListenerManager(mgr) {}

			Manager& ListenerManager;
		};

		using ThreadPool = Concurrency::ThreadPool<ThreadPoolData, ThreadData>;

	public:
		Manager() = delete;
		Manager(const Settings_CThS& settings, Access::Manager& accessmgr, Peer::Manager& peers) noexcept;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = default;
		virtual ~Manager() = default;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = default;

		const bool Startup() noexcept;
		const bool Startup(const Vector<EthernetInterface>& interfaces) noexcept;
		void Shutdown() noexcept;
		inline const bool IsRunning() const noexcept { return m_Running; }

	private:
		void PreStartup() noexcept;
		void ResetState() noexcept;

		static const std::pair<bool, bool> WorkerThreadProcessor(ThreadPoolData& thpdata, ThreadData& thdata,
																 const Concurrency::EventCondition& shutdown_event);

		static void AcceptConnection(Manager& listeners, Network::Socket& listener_socket, const bool cond_accept) noexcept;

		const bool CanAcceptConnection(const IPAddress& ipaddr) const noexcept;

		static int CALLBACK AcceptConditionFunction(LPWSABUF lpCallerId, LPWSABUF lpCallerData, LPQOS lpSQOS,
													LPQOS lpGQOS, LPWSABUF lpCalleeId, LPWSABUF lpCalleeData,
													GROUP FAR* g, DWORD_PTR dwCallbackData) noexcept;

	private:
		std::atomic_bool m_Running{ false };
		const Settings_CThS& m_Settings;
		Access::Manager& m_AccessManager;
		Peer::Manager& m_PeerManager;
		
		ThreadPool m_ListenerThreadPool{ *this };
	};
}