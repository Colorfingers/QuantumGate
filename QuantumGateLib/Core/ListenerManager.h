// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\Concurrency\ThreadPool.h"
#include "Peer\PeerManager.h"
#include "Access\AccessManager.h"

namespace QuantumGate::Implementation::Core::Listener
{
	class Manager final
	{
		struct ThreadData
		{
			ThreadData() = default;
			ThreadData(const ThreadData&) = delete;
			ThreadData(ThreadData&&) = default;

			~ThreadData()
			{
				if (Socket.GetIOStatus().IsOpen()) Socket.Close();
			}

			ThreadData& operator=(const ThreadData&) = delete;
			ThreadData& operator=(ThreadData&&) = default;

			Network::Socket Socket;
			bool UseConditionalAcceptFunction{ true };
		};

		struct ThreadPoolData
		{};

		using ThreadPool = Concurrency::ThreadPool<ThreadPoolData, ThreadData>;

	public:
		Manager() = delete;
		Manager(const Settings_CThS& settings, Access::Manager& accessmgr, Peer::Manager& peers) noexcept;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = default;
		~Manager() { if (IsRunning()) Shutdown(); }
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = default;

		[[nodiscard]] const bool Startup() noexcept;
		[[nodiscard]] const bool Startup(const Vector<EthernetInterface>& interfaces) noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] inline const bool IsRunning() const noexcept { return m_Running; }

		[[nodiscard]] const bool AddListenerThreads(const IPAddress& address, const Vector<UInt16> ports,
													const bool cond_accept, const bool nat_traversal) noexcept;
		std::optional<ThreadPool::Thread> RemoveListenerThread(ThreadPool::Thread&& thread) noexcept;
		[[nodiscard]] const bool Update(const Vector<EthernetInterface>& interfaces) noexcept;

	private:
		void PreStartup() noexcept;
		void ResetState() noexcept;

		const std::pair<bool, bool> WorkerThreadProcessor(ThreadPoolData& thpdata, ThreadData& thdata,
														  const Concurrency::EventCondition& shutdown_event);

		void AcceptConnection(Network::Socket& listener_socket, const bool cond_accept) noexcept;

		[[nodiscard]] const bool CanAcceptConnection(const IPAddress& ipaddr) const noexcept;

		static int CALLBACK AcceptConditionFunction(LPWSABUF lpCallerId, LPWSABUF lpCallerData, LPQOS lpSQOS,
													LPQOS lpGQOS, LPWSABUF lpCalleeId, LPWSABUF lpCalleeData,
													GROUP FAR* g, DWORD_PTR dwCallbackData) noexcept;

	private:
		std::atomic_bool m_Running{ false };
		std::atomic_bool m_ListeningOnAnyAddresses{ false };
		const Settings_CThS& m_Settings;
		Access::Manager& m_AccessManager;
		Peer::Manager& m_PeerManager;

		ThreadPool m_ListenerThreadPool;
	};
}