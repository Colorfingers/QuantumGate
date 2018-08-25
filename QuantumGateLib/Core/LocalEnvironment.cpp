// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "LocalEnvironment.h"

#include <ws2tcpip.h>
#include <Iphlpapi.h>

namespace QuantumGate::Implementation::Core
{
	const bool LocalEnvironment::Initialize() noexcept
	{
		if (auto result = GetHostname(); result.Failed()) return false;
		else m_Hostname = std::move(result.Value());

		if (auto result = GetUsername(); result.Failed()) return false;
		else m_Username = std::move(result.Value());

		if (auto result = GetEthernetInterfaces(); result.Failed()) return false;
		else m_Interfaces = std::move(result.Value());

		if (auto result = GetIPAddresses(m_Interfaces); result.Failed()) return false;
		else m_IPAddresses = std::move(result.Value());

		m_Initialized = true;

		return true;
	}

	void LocalEnvironment::Clear() noexcept
	{
		m_Hostname.clear();
		m_Username.clear();
		m_Interfaces.clear();
		m_IPAddresses.clear();

		m_Initialized = false;
	}

	String LocalEnvironment::GetIPAddressesString() const noexcept
	{
		String allips;
		for (const auto& ip : m_IPAddresses)
		{
			if (allips.length() > 0) allips += L", ";

			allips += ip.GetString();
		}

		return allips;
	}

	String LocalEnvironment::GetMACAddressesString() const noexcept
	{
		String alladdr;
		for (const auto& ifs : m_Interfaces)
		{
			if (alladdr.length() > 0) alladdr += L", ";

			alladdr += ifs.MACAddress;
		}

		return alladdr;
	}

	Result<String> LocalEnvironment::GetHostname() noexcept
	{
		char hostname[NI_MAXHOST]{ 0 };

		const auto ret = gethostname(hostname, NI_MAXHOST);

		if (ret == 0)
		{
			return Util::ToStringW(hostname);
		}
		else LogErr(L"Could not get the name of the local host");

		return ResultCode::Failed;
	}

	Result<std::vector<IPAddress>> LocalEnvironment::GetIPAddresses(const String& hostname) noexcept
	{
		try
		{
			std::vector<IPAddress> alladdr;

			ADDRINFOW* result{ nullptr };

			const auto ret = GetAddrInfoW(hostname.c_str(), L"0", nullptr, &result);
			if (ret == 0)
			{
				for (auto ptr = result; ptr != nullptr; ptr = ptr->ai_next)
				{
					if (ptr->ai_family == AF_INET || ptr->ai_family == AF_INET6)
					{
						alladdr.emplace_back(IPAddress(ptr->ai_addr));
					}
				}

				FreeAddrInfoW(result);

				return std::move(alladdr);
			}
			else LogErr(L"Could not get addresses for host %s", hostname.c_str());
		}
		catch (const std::exception& e)
		{
			LogErr(L"Could not get addresses for host %s due to exception: %s",
				   hostname.c_str(), Util::ToStringW(e.what()).c_str());
		}

		return ResultCode::Failed;
	}

	Result<std::vector<IPAddress>> LocalEnvironment::GetIPAddresses(const std::vector<EthernetInterface>& eth_interfaces) noexcept
	{
		try
		{
			std::vector<IPAddress> allips;

			for (const auto& ifs : eth_interfaces)
			{
				for (const auto& ip : ifs.IPAdresses)
				{
					allips.emplace_back(ip);
				}
			}

			return std::move(allips);
		}
		catch (const std::exception& e)
		{
			LogErr(L"Could not get IP addresses due to exception: %s",
				   Util::ToStringW(e.what()).c_str());
		}

		return ResultCode::Failed;
	}

	Result<std::vector<EthernetInterface>> LocalEnvironment::GetEthernetInterfaces() noexcept
	{
		try
		{
			std::vector<EthernetInterface> allifs;

			const ULONG family{ AF_UNSPEC };

			ULONG buflen{ sizeof(IP_ADAPTER_ADDRESSES) };
			auto addresses = std::make_unique<Byte[]>(buflen);

			// Make an initial call to GetAdaptersAddresses to get the necessary size into the buflen variable
			if (GetAdaptersAddresses(family, 0, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addresses.get()),
									 &buflen) == ERROR_BUFFER_OVERFLOW)
			{
				addresses = std::make_unique<Byte[]>(buflen);
			}

			const auto ret = GetAdaptersAddresses(family, 0, nullptr,
												  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addresses.get()),
												  &buflen);
			if (ret == NO_ERROR)
			{
				auto address = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addresses.get());

				while (address != nullptr)
				{
					if (address->IfType == IF_TYPE_ETHERNET_CSMACD || address->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
					{
						auto& ifs = allifs.emplace_back();
						ifs.Name = Util::ToStringW(address->AdapterName);
						ifs.Description = address->Description;

						if (address->OperStatus == IfOperStatusUp) ifs.Operational = true;

						// Get MAC address
						for (auto i = 0u; i < address->PhysicalAddressLength; i++)
						{
							ifs.MACAddress += Util::FormatString(L"%.2X", static_cast<int>(address->PhysicalAddress[i]));
						}

						// Get IP Addresses
						for (auto pUnicast = address->FirstUnicastAddress;
							 pUnicast != nullptr; pUnicast = pUnicast->Next)
						{
							ifs.IPAdresses.emplace_back(IPAddress(pUnicast->Address.lpSockaddr));
						}
					}

					address = address->Next;
				}

				return std::move(allifs);
			}
			else LogErr(L"Could not get addresses for local networking adapters");
		}
		catch (const std::exception& e)
		{
			LogErr(L"Could not get addresses for local networking adapters due to exception: %s",
				   Util::ToStringW(e.what()).c_str());
		}

		return ResultCode::Failed;
	}

	Result<String> LocalEnvironment::GetUsername() noexcept
	{
		unsigned long nlen{ 256u };
		WChar name[256]{ 0 };

		if (GetUserNameEx(EXTENDED_NAME_FORMAT::NameSamCompatible, reinterpret_cast<LPWSTR>(name), &nlen))
		{
			return { name };
		}
		else LogErr(L"Could not get the username for the current user");

		return ResultCode::Failed;
	}
}