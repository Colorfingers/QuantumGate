// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "IPFilters.h"
#include "IPAccessControl.h"
#include "IPSubnetLimits.h"
#include "PeerAccessControl.h"
#include "..\..\Common\Dispatcher.h"

namespace QuantumGate::Implementation::Core::Peer
{
	class Manager;
}

namespace QuantumGate::Implementation::Core::Access
{
	class Manager
	{
	public:
		using AccessUpdateCallbacks = Dispatcher<void() noexcept>;
		using AccessUpdateCallbackHandle = AccessUpdateCallbacks::FunctionHandle;
		using AccessUpdateCallbacks_ThS = Concurrency::ThreadSafe<AccessUpdateCallbacks, std::shared_mutex>;

		Manager() = delete;
		Manager(const Settings_CThS& settings) noexcept;
		virtual ~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = default;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = default;

		Result<IPFilterID> AddIPFilter(const String& ip_cidr,
									   const IPFilterType type) noexcept;
		Result<IPFilterID> AddIPFilter(const String& ip, const String& mask,
									   const IPFilterType type) noexcept;

		Result<> RemoveIPFilter(const IPFilterID filterid, const IPFilterType type) noexcept;
		void RemoveAllIPFilters() noexcept;

		Result<std::vector<IPFilter>> GetAllIPFilters() const noexcept;

		Result<> SetIPReputation(const IPAddress& ip, const Int16 score,
								 const std::optional<Time>& time = std::nullopt) noexcept;
		Result<> SetIPReputation(const IPReputation& ip_rep) noexcept;
		Result<> ResetIPReputation(const String& ip) noexcept;
		Result<> ResetIPReputation(const IPAddress& ip) noexcept;
		void ResetAllIPReputations() noexcept;
		Result<std::pair<Int16, bool>> UpdateIPReputation(const IPAddress& ip,
														  const IPReputationUpdate rep_update) noexcept;
		Result<std::vector<IPReputation>> GetAllIPReputations() const noexcept;

		[[nodiscard]] const bool AddIPConnectionAttempt(const IPAddress& ip) noexcept;
		[[nodiscard]] const bool AddIPRelayConnectionAttempt(const IPAddress& ip) noexcept;

		Result<> AddIPSubnetLimit(const IPAddressFamily af, const String& cidr_lbits, const Size max_con) noexcept;
		Result<> RemoveIPSubnetLimit(const IPAddressFamily af, const String& cidr_lbits) noexcept;

		Result<std::vector<IPSubnetLimit>> GetAllIPSubnetLimits() const noexcept;

		[[nodiscard]] const bool AddIPConnection(const IPAddress& ip) noexcept;
		[[nodiscard]] const bool RemoveIPConnection(const IPAddress& ip) noexcept;

		Result<bool> IsIPAllowed(const String& ip, const AccessCheck check) noexcept;
		Result<bool> IsIPAllowed(const IPAddress& ip, const AccessCheck check) noexcept;

		Result<bool> IsIPConnectionAllowed(const IPAddress& ip, const AccessCheck check) noexcept;

		Result<> AddPeer(const PeerAccessSettings&& pas) noexcept;
		Result<> UpdatePeer(const PeerAccessSettings&& pas) noexcept;
		Result<> RemovePeer(const PeerUUID& puuid) noexcept;
		void RemoveAllPeers() noexcept;

		Result<bool> IsPeerAllowed(const PeerUUID& puuid) const noexcept;

		const ProtectedBuffer* GetPeerPublicKey(const PeerUUID& puuid) const noexcept;

		void SetPeerAccessDefault(const PeerAccessDefault pad) noexcept;
		[[nodiscard]] const PeerAccessDefault GetPeerAccessDefault() const noexcept;

		Result<std::vector<PeerAccessSettings>> GetAllPeers() const noexcept;

		inline AccessUpdateCallbacks_ThS& GetAccessUpdateCallbacks() noexcept { return m_AccessUpdateCallbacks; }

	private:
		const Settings_CThS& m_Settings;

		IPFilters_ThS m_IPFilters;
		IPAccessControl_ThS m_IPAccessControl{ m_Settings };
		IPSubnetLimits_ThS m_SubnetLimits;
		PeerAccessControl_ThS m_PeerAccessControl{ m_Settings };

		AccessUpdateCallbacks_ThS m_AccessUpdateCallbacks;
	};
}