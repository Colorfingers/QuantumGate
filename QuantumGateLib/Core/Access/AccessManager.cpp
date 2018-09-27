// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "AccessManager.h"
#include "..\Peer\PeerManager.h"

namespace QuantumGate::Implementation::Core::Access
{
	Manager::Manager(const Settings_CThS& settings) noexcept :
		m_Settings(settings)
	{}

	Result<IPFilterID> Manager::AddIPFilter(const String& ip_cidr,
											const IPFilterType type) noexcept
	{
		auto result = m_IPFilters.WithUniqueLock()->AddFilter(ip_cidr, type);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<IPFilterID> Manager::AddIPFilter(const String& ip, const String& mask,
											const IPFilterType type) noexcept
	{
		auto result = m_IPFilters.WithUniqueLock()->AddFilter(ip, mask, type);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<> Manager::RemoveIPFilter(const IPFilterID filterid, const IPFilterType type) noexcept
	{
		auto result = m_IPFilters.WithUniqueLock()->RemoveFilter(filterid, type);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	void Manager::RemoveAllIPFilters() noexcept
	{
		m_IPFilters.WithUniqueLock()->Clear();
		m_AccessUpdateCallbacks.WithSharedLock()();
	}

	Result<Vector<IPFilter>> Manager::GetAllIPFilters() const noexcept
	{
		return m_IPFilters.WithSharedLock()->GetFilters();
	}

	Result<> Manager::SetIPReputation(const IPAddress& ip, const Int16 score,
									  const std::optional<Time>& time) noexcept
	{
		auto result = m_IPAccessControl.WithUniqueLock()->SetReputation(ip, score, time);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<> Manager::SetIPReputation(const IPReputation& ip_rep) noexcept
	{
		auto result = m_IPAccessControl.WithUniqueLock()->SetReputation(ip_rep.Address,
																		ip_rep.Score,
																		ip_rep.LastUpdateTime);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<> Manager::ResetIPReputation(const String& ip) noexcept
	{
		IPAddress ipaddr;
		if (IPAddress::TryParse(ip, ipaddr))
		{
			return ResetIPReputation(ipaddr);
		}

		return ResultCode::AddressInvalid;
	}

	Result<> Manager::ResetIPReputation(const IPAddress& ip) noexcept
	{
		auto result = m_IPAccessControl.WithUniqueLock()->ResetReputation(ip);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}
	
	void Manager::ResetAllIPReputations() noexcept
	{
		m_IPAccessControl.WithUniqueLock()->ResetAllReputations();
		m_AccessUpdateCallbacks.WithSharedLock()();
	}

	Result<std::pair<Int16, bool>> Manager::UpdateIPReputation(const IPAddress& ip,
															   const IPReputationUpdate rep_update) noexcept
	{
		auto result = m_IPAccessControl.WithUniqueLock()->UpdateReputation(ip, rep_update);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<Vector<IPReputation>> Manager::GetAllIPReputations() const noexcept
	{
		return m_IPAccessControl.WithUniqueLock()->GetReputations();
	}

	const bool Manager::AddIPConnection(const IPAddress& ip) noexcept
	{
		return m_SubnetLimits.WithUniqueLock()->AddConnection(ip);
	}

	const bool Manager::RemoveIPConnection(const IPAddress& ip) noexcept
	{
		return m_SubnetLimits.WithUniqueLock()->RemoveConnection(ip);
	}

	const bool Manager::AddIPConnectionAttempt(const IPAddress& ip) noexcept
	{
		if (!m_IPAccessControl.WithUniqueLock()->AddConnectionAttempt(ip))
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
			return false;
		}

		return true;
	}

	const bool Manager::AddIPRelayConnectionAttempt(const IPAddress& ip) noexcept
	{
		if (!m_IPAccessControl.WithUniqueLock()->AddRelayConnectionAttempt(ip))
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
			return false;
		}

		return true;
	}

	Result<> Manager::AddIPSubnetLimit(const IPAddressFamily af, const String& cidr_lbits,
									   const Size max_con) noexcept
	{
		auto result = m_SubnetLimits.WithUniqueLock()->AddLimit(af, cidr_lbits, max_con);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<> Manager::RemoveIPSubnetLimit(const IPAddressFamily af, const String& cidr_lbits) noexcept
	{
		auto result = m_SubnetLimits.WithUniqueLock()->RemoveLimit(af, cidr_lbits);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<Vector<IPSubnetLimit>> Manager::GetAllIPSubnetLimits() const noexcept
	{
		return m_SubnetLimits.WithSharedLock()->GetLimits();
	}

	Result<bool> Manager::IsIPAllowed(const String& ip, const AccessCheck check) noexcept
	{
		IPAddress ipaddr;
		if (IPAddress::TryParse(ip, ipaddr))
		{
			return IsIPAllowed(ipaddr, check);
		}

		return ResultCode::AddressInvalid;
	}

	Result<bool> Manager::IsIPAllowed(const IPAddress& ip, const AccessCheck check) noexcept
	{
		switch (check)
		{
			case AccessCheck::IPFilters:
			{
				return m_IPFilters.WithSharedLock()->IsAllowed(ip);
			}
			case AccessCheck::IPReputations:
			{
				return m_IPAccessControl.WithUniqueLock()->HasAcceptableReputation(ip);
			}
			case AccessCheck::IPSubnetLimits:
			{
				return !m_SubnetLimits.WithSharedLock()->HasConnectionOverflow(ip);
			}
			case AccessCheck::All:
			{
				if (const auto result = m_IPFilters.WithSharedLock()->IsAllowed(ip); result)
				{
					if (*result &&
						m_IPAccessControl.WithUniqueLock()->HasAcceptableReputation(ip) &&
						!m_SubnetLimits.WithSharedLock()->HasConnectionOverflow(ip))
					{
						return true;
					}
				}
				break;
			}
			default:
			{
				// Shouldn't ever get here
				assert(false);
				return ResultCode::InvalidArgument;
			}
		}

		// Blocked by default
		return false;
	}

	Result<bool> Manager::IsIPConnectionAllowed(const IPAddress& ip, const AccessCheck check) noexcept
	{
		switch (check)
		{
			case AccessCheck::IPFilters:
			{
				return m_IPFilters.WithSharedLock()->IsAllowed(ip);
			}
			case AccessCheck::IPReputations:
			{
				return m_IPAccessControl.WithUniqueLock()->HasAcceptableReputation(ip);
			}
			case AccessCheck::IPSubnetLimits:
			{
				return m_SubnetLimits.WithSharedLock()->CanAcceptConnection(ip);
			}
			case AccessCheck::All:
			{
				if (const auto result = m_IPFilters.WithSharedLock()->IsAllowed(ip); result)
				{
					if (*result &&
						m_IPAccessControl.WithUniqueLock()->HasAcceptableReputation(ip) &&
						m_SubnetLimits.WithSharedLock()->CanAcceptConnection(ip))
					{
						return true;
					}
				}
				break;
			}
			default:
			{
				// Shouldn't ever get here
				assert(false);
				break;
			}
		}

		// Blocked by default
		return false;
	}

	Result<> Manager::AddPeer(const PeerAccessSettings&& pas) noexcept
	{
		auto result = m_PeerAccessControl.WithUniqueLock()->AddPeer(std::move(pas));
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<> Manager::UpdatePeer(const PeerAccessSettings&& pas) noexcept
	{
		auto result = m_PeerAccessControl.WithUniqueLock()->UpdatePeer(std::move(pas));
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	Result<> Manager::RemovePeer(const PeerUUID& puuid) noexcept
	{
		auto result = m_PeerAccessControl.WithUniqueLock()->RemovePeer(puuid);
		if (result.Succeeded())
		{
			m_AccessUpdateCallbacks.WithSharedLock()();
		}

		return result;
	}

	void Manager::RemoveAllPeers() noexcept
	{
		m_PeerAccessControl.WithUniqueLock()->Clear();
		m_AccessUpdateCallbacks.WithSharedLock()();
	}

	Result<bool> Manager::IsPeerAllowed(const PeerUUID& puuid) const noexcept
	{
		return m_PeerAccessControl.WithSharedLock()->IsAllowed(puuid);
	}

	const ProtectedBuffer* Manager::GetPeerPublicKey(const PeerUUID& puuid) const noexcept
	{
		return m_PeerAccessControl.WithSharedLock()->GetPublicKey(puuid);
	}

	void Manager::SetPeerAccessDefault(const PeerAccessDefault pad) noexcept
	{
		m_PeerAccessControl.WithUniqueLock()->SetAccessDefault(pad);
		m_AccessUpdateCallbacks.WithSharedLock()();
	}

	const PeerAccessDefault Manager::GetPeerAccessDefault() const noexcept
	{
		return m_PeerAccessControl.WithSharedLock()->GetAccessDefault();
	}

	Result<Vector<PeerAccessSettings>> Manager::GetAllPeers() const noexcept
	{
		return m_PeerAccessControl.WithSharedLock()->GetPeers();
	}
}