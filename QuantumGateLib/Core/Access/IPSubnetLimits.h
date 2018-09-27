// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\..\Network\IPAddress.h"

#include <map>
#include <unordered_map>

namespace QuantumGate::Implementation::Core::Access
{
	struct IPSubnetLimitImpl
	{
		IPAddressFamily AddressFamily{ IPAddressFamily::Unknown };
		UInt8 CIDRLeadingBits{ 0 };
		Network::BinaryIPAddress SubnetMask;  // Network byte order (big endian)
		Size MaximumConnections{ 0 };

		constexpr static const bool Compare(const UInt8 key1, const UInt8 key2) noexcept
		{
			return (key1 > key2);
		}
	};

	using IPSubnetLimitMap = std::map<UInt8, IPSubnetLimitImpl, decltype(&IPSubnetLimitImpl::Compare)>;

	struct IPSubnetLimitDetail
	{
		IPSubnetLimitDetail(const IPAddressFamily af, const UInt8 cidr_lbits) noexcept :
			AddressFamily(af), CIDRLeadingBits(cidr_lbits) {}

		IPAddressFamily AddressFamily{ IPAddressFamily::Unknown };
		UInt8 CIDRLeadingBits{ 0 };
		Size CurrentConnections{ 0 };
	};

	using IPSubnetLimitDetailMap = std::unordered_map<Network::BinaryIPAddress, IPSubnetLimitDetail>;

	struct IPSubnetConnection
	{
		Network::BinaryIPAddress Address;
		Size CurrentConnections{ 0 };
	};

	using IPSubnetConnectionMap = std::unordered_map<Network::BinaryIPAddress, IPSubnetConnection>;

	struct IPSubnetAF
	{
		IPSubnetLimitMap Limits{ &IPSubnetLimitImpl::Compare };
		IPSubnetConnectionMap Connections;

		void Clear() noexcept
		{
			Limits.clear();
			Connections.clear();
		}
	};

	class Export IPSubnetLimits
	{
	public:
		IPSubnetLimits() = default;
		IPSubnetLimits(const IPSubnetLimits&) = delete;
		IPSubnetLimits(IPSubnetLimits&&) = default;
		~IPSubnetLimits() = default;
		IPSubnetLimits& operator=(const IPSubnetLimits&) = delete;
		IPSubnetLimits& operator=(IPSubnetLimits&&) = default;

		Result<> AddLimit(const IPAddressFamily af, const String& cidr_lbits, const Size max_con) noexcept;
		Result<> AddLimit(const IPAddressFamily af, const UInt8 cidr_lbits, const Size max_con) noexcept;
		Result<> RemoveLimit(const IPAddressFamily af, const String& cidr_lbits) noexcept;
		Result<> RemoveLimit(const IPAddressFamily af, const UInt8 cidr_lbits) noexcept;

		Result<Vector<IPSubnetLimit>> GetLimits() const noexcept;

		void Clear() noexcept;

		[[nodiscard]] const bool HasLimit(const IPAddressFamily af, const UInt8 cidr_lbits) const noexcept;

		[[nodiscard]] const bool AddConnection(const IPAddress& ip) noexcept;
		[[nodiscard]] const bool RemoveConnection(const IPAddress& ip) noexcept;

		[[nodiscard]] const bool HasConnectionOverflow(const IPAddress& ip) const noexcept;
		[[nodiscard]] const bool CanAcceptConnection(const IPAddress& ip) const noexcept;

	private:
		IPSubnetAF* GetSubnets(const IPAddressFamily af) noexcept;
		const IPSubnetAF* GetSubnets(const IPAddressFamily af) const noexcept;

		[[nodiscard]] const bool AddSubnetConnection(IPSubnetConnectionMap& connections, const IPAddress& ip) noexcept;
		[[nodiscard]] const bool RemoveSubnetConnection(IPSubnetConnectionMap& connections, const IPAddress& ip) noexcept;

		[[nodiscard]] const bool AddLimitsConnection(const IPSubnetLimitMap& limits, const IPAddress& ip) noexcept;
		[[nodiscard]] const bool AddLimitConnection(const IPSubnetLimitImpl& limit, const IPAddress& ip,
													const Size num, const bool allow_overflow) noexcept;
		[[nodiscard]] const bool RemoveLimitsConnection(const IPSubnetLimitMap& limits, const IPAddress& ip) noexcept;
		[[nodiscard]] const bool RemoveLimitConnection(const IPSubnetLimitImpl& limit, const IPAddress& ip) noexcept;

		[[nodiscard]] const bool CanAcceptConnection(const IPSubnetLimitMap& map, const IPAddress& ip) const noexcept;

	private:
		IPSubnetAF m_IPv4Subnets;
		IPSubnetAF m_IPv6Subnets;
		IPSubnetLimitDetailMap m_IPSubnetLimitDetails;
	};

	using IPSubnetLimits_ThS = Concurrency::ThreadSafe<IPSubnetLimits, std::shared_mutex>;
}
