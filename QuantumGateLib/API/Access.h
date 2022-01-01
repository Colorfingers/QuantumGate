// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

namespace QuantumGate::API::Access
{
	using IPFilterID = UInt64;

	enum class IPFilterType : UInt16
	{
		Allowed, Blocked
	};

	struct IPFilter
	{
		IPFilterID ID{ 0 };
		IPFilterType Type{ IPFilterType::Blocked };
		IPAddress Address;
		IPAddress Mask;
	};

	struct IPSubnetLimit
	{
		IPAddress::Family AddressFamily{ IPAddress::Family::Unspecified };
		String CIDRLeadingBits;
		Size MaximumConnections{ 0 };
	};

	struct AddressReputation
	{
		struct ScoreLimits final
		{
			static constexpr const Int16 Minimum{ -3000 };
			static constexpr const Int16 Base{ 0 };
			static constexpr const Int16 Maximum{ 100 };
		};

		Address Address;
		Int16 Score{ ScoreLimits::Minimum };
		std::optional<Time> LastUpdateTime;
	};

	enum class CheckType : UInt16
	{
		AddressReputations, IPFilters, IPSubnetLimits, All
	};

	enum class PeerAccessDefault : UInt16
	{
		Allowed, NotAllowed
	};

	struct PeerSettings
	{
		PeerUUID UUID;
		ProtectedBuffer PublicKey;
		bool AccessAllowed{ false };
	};
}

namespace QuantumGate::Implementation::Core::Access
{
	class Manager;
}

namespace QuantumGate::API::Access
{
	class Export Manager final
	{
	public:
		Manager() = delete;
		Manager(QuantumGate::Implementation::Core::Access::Manager* accessmgr) noexcept;
		Manager(const Manager&) = delete;
		Manager(Manager&&) noexcept = default;
		virtual ~Manager() = default;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) noexcept = default;

		Result<IPFilterID> AddIPFilter(const WChar* ip_cidr,
									   const IPFilterType type) noexcept;
		Result<IPFilterID> AddIPFilter(const WChar* ip_str, const WChar* mask_str,
									   const IPFilterType type) noexcept;
		Result<IPFilterID> AddIPFilter(const String& ip_str, const String& mask_str,
									   const IPFilterType type) noexcept;
		Result<IPFilterID> AddIPFilter(const IPAddress& ip, const IPAddress& mask,
									   const IPFilterType type) noexcept;

		Result<> RemoveIPFilter(const IPFilterID filterid, const IPFilterType type) noexcept;
		void RemoveAllIPFilters() noexcept;

		Result<Vector<IPFilter>> GetAllIPFilters() const noexcept;

		Result<> AddIPSubnetLimit(const IPAddress::Family af, const String& cidr_lbits_str, const Size max_con) noexcept;
		Result<> AddIPSubnetLimit(const IPAddress::Family af, const UInt8 cidr_lbits, const Size max_con) noexcept;
		Result<> RemoveIPSubnetLimit(const IPAddress::Family af, const String& cidr_lbits_str) noexcept;
		Result<> RemoveIPSubnetLimit(const IPAddress::Family af, const UInt8 cidr_lbits) noexcept;

		Result<Vector<IPSubnetLimit>> GetAllIPSubnetLimits() const noexcept;

		Result<> SetAddressReputation(const AddressReputation& addr_rep) noexcept;
		Result<> ResetAddressReputation(const WChar* addr_str) noexcept;
		Result<> ResetAddressReputation(const String& addr_str) noexcept;
		Result<> ResetAddressReputation(const Address& addr) noexcept;
		void ResetAllAddressReputations() noexcept;
		Result<Vector<AddressReputation>> GetAllAddressReputations() const noexcept;

		Result<bool> GetAddressAllowed(const WChar* addr_str, const CheckType check) const noexcept;
		Result<bool> GetAddressAllowed(const String& addr_str, const CheckType check) const noexcept;
		Result<bool> GetAddressAllowed(const Address& addr, const CheckType check) const noexcept;

		Result<> AddPeer(PeerSettings&& pas) noexcept;
		Result<> UpdatePeer(PeerSettings&& pas) noexcept;
		Result<> RemovePeer(const PeerUUID& puuid) noexcept;
		void RemoveAllPeers() noexcept;

		Result<bool> GetPeerAllowed(const PeerUUID& puuid) const noexcept;

		void SetPeerAccessDefault(const PeerAccessDefault pad) noexcept;
		[[nodiscard]] PeerAccessDefault GetPeerAccessDefault() const noexcept;

		Result<Vector<PeerSettings>> GetAllPeers() const noexcept;

	private:
		QuantumGate::Implementation::Core::Access::Manager* m_AccessManager{ nullptr };
	};
}