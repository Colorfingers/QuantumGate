// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "UDPConnection.h"
#include "..\..\Concurrency\ThreadPool.h"
#include "..\..\Concurrency\EventGroup.h"

namespace QuantumGate::Implementation::Core::UDP::Listener
{
	class Manager;
}

namespace QuantumGate::Implementation::Core::UDP::Connection
{
	class Manager final
	{
		friend class Listener::Manager;

		using ConnectionMap = Containers::UnorderedMap<ConnectionID, Connection>;
		using ConnectionMap_ThS = Concurrency::ThreadSafe<ConnectionMap, std::shared_mutex>;

		using ThreadKey = UInt64;

		using ThreadKeyToConnectionTotalMap = Containers::UnorderedMap<ThreadKey, Size>;
		using ThreadKeyToConnectionTotalMap_ThS = Concurrency::ThreadSafe<ThreadKeyToConnectionTotalMap, Concurrency::SharedSpinMutex>;

		struct ThreadData final
		{
			explicit ThreadData(const ThreadKey thread_key) noexcept :
				ThreadKey(thread_key),
				WorkEvents(std::make_unique<Concurrency::EventGroup>()),
				Connections(std::make_unique<ConnectionMap_ThS>())
			{}

			ThreadKey ThreadKey{ 0 };
			std::unique_ptr<Concurrency::EventGroup> WorkEvents;
			std::unique_ptr<ConnectionMap_ThS> Connections;
		};

		struct ThreadPoolData final
		{
			ThreadKeyToConnectionTotalMap_ThS ThreadKeyToConnectionTotals;
		};

		using ThreadPool = Concurrency::ThreadPool<ThreadPoolData, ThreadData>;

	public:
		Manager() = delete;
		Manager(const Settings_CThS& settings) noexcept : m_Settings(settings) {}
		Manager(const Manager&) = delete;
		Manager(Manager&&) noexcept = default;
		~Manager() { if (IsRunning()) Shutdown(); }
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) noexcept = default;

		[[nodiscard]] bool Startup() noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] inline bool IsRunning() const noexcept { return m_Running; }

		[[nodiscard]] bool AddConnection(const Network::IP::AddressFamily af, const PeerConnectionType type,
										 const ConnectionID id, const Message::SequenceNumber seqnum,
										 Socket& socket) noexcept;

		void OnLocalIPInterfaceChanged() noexcept;

	private:
		[[nodiscard]] inline const Settings& GetSettings() const noexcept { return m_Settings.GetCache(); }

		void PreStartup() noexcept;
		void ResetState() noexcept;

		[[nodiscard]] bool StartupThreadPool() noexcept;
		void ShutdownThreadPool() noexcept;

		std::optional<ThreadKey> GetThreadKeyWithLeastConnections() const noexcept;
		[[nodiscard]] std::optional<ThreadPool::ThreadType> GetThreadWithLeastConnections() noexcept;

		void RemoveConnection(const ConnectionID id, ConnectionMap& connections, const ThreadData& thdata) noexcept;
		void RemoveConnections(const Containers::List<ConnectionID>& list, ConnectionMap& connections,
							   const ThreadData& thdata) noexcept;

		[[nodiscard]] bool IncrementThreadConnectionTotal(const ThreadKey key) noexcept;
		[[nodiscard]] bool DecrementThreadConnectionTotal(const ThreadKey key) noexcept;

		[[nodiscard]] bool HasConnection(const ConnectionID id, const PeerConnectionType type) const noexcept;

		void WorkerThreadWait(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event);
		void WorkerThreadProcessor(ThreadPoolData& thpdata, ThreadData& thdata, const Concurrency::Event& shutdown_event);

	private:
		std::atomic_bool m_Running{ false };
		const Settings_CThS& m_Settings;

		ThreadPool m_ThreadPool;
	};
}