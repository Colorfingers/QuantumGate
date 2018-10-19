// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "PublicIPEndpoints.h"

#include <Iphlpapi.h>

namespace QuantumGate::Implementation::Core
{
	class LocalEnvironment final
	{
		using CachedIPAddresses_ThS =
			Concurrency::ThreadLocalCache<Vector<BinaryIPAddress>, Concurrency::SpinMutex, 369>;

	public:
		using ChangedCallback = Callback<void() noexcept>;
		using ChangedCallback_ThS = Concurrency::ThreadSafe<ChangedCallback, std::shared_mutex>;

		LocalEnvironment(const Settings_CThS& settings) noexcept :
			m_PublicIPEndpoints(settings)
		{}

		LocalEnvironment(const LocalEnvironment&) = delete;
		LocalEnvironment(LocalEnvironment&&) = default;
		~LocalEnvironment() { if (IsInitialized()) Deinitialize(); }
		LocalEnvironment& operator=(const LocalEnvironment&) = delete;
		LocalEnvironment& operator=(LocalEnvironment&&) = default;

		[[nodiscard]] const bool Initialize(ChangedCallback&& callback) noexcept;
		[[nodiscard]] inline const bool IsInitialized() const noexcept { return m_Initialized; }
		void Deinitialize() noexcept;

		[[nodiscard]] const bool Update() noexcept;

		inline const String& GetHostname() const noexcept { return m_Hostname; }
		inline const String& GetUsername() const noexcept { return m_Username; }
		Result<Vector<IPAddressDetails>> GetIPAddresses() const noexcept;
		inline const Vector<EthernetInterface>& GetEthernetInterfaces() const noexcept { return m_EthernetInterfaces; }

		const Vector<BinaryIPAddress>* GetTrustedAndVerifiedIPAddresses() const noexcept;

		String GetIPAddressesString() const noexcept;
		String GetMACAddressesString() const noexcept;

		[[nodiscard]] const bool AddPublicIPEndpoint(const IPEndpoint& pub_endpoint,
													 const IPEndpoint& rep_peer,
													 const PeerConnectionType rep_con_type,
													 const bool trusted) noexcept;

	private:
		[[nodiscard]] const bool RegisterIPInterfaceChangeNotification() noexcept;
		void DeregisterIPInterfaceChangeNotification() noexcept;

		static VOID NETIOAPI_API_ IPInterfaceChangeNotificationCallback(PVOID CallerContext, PMIB_IPINTERFACE_ROW Row,
																		MIB_NOTIFICATION_TYPE NotificationType);

		[[nodiscard]] const bool UpdateEnvironmentInformation() noexcept;
		void ClearEnvironmentInformation() noexcept;

		[[nodiscard]] const bool UpdateCachedIPAddresses() noexcept;

		static Result<String> OSGetHostname() noexcept;
		static Result<String> OSGetUsername() noexcept;
		static Result<Vector<EthernetInterface>> OSGetEthernetInterfaces() noexcept;
		static Result<Vector<BinaryIPAddress>> OSGetIPAddresses(const String& hostname) noexcept;

	private:
		bool m_Initialized{ false };

		ChangedCallback_ThS m_ChangedCallback;

		HANDLE m_IPInterfaceChangeNotificationHandle{ NULL };

		String m_Hostname;
		String m_Username;
		Vector<EthernetInterface> m_EthernetInterfaces;

		PublicIPEndpoints m_PublicIPEndpoints;

		CachedIPAddresses_ThS m_CachedIPAddresses;
	};

	using LocalEnvironment_ThS = Concurrency::ThreadSafe<LocalEnvironment, std::shared_mutex>;
}