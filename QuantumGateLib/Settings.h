// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "Concurrency\ThreadLocalCache.h"

namespace QuantumGate::Implementation
{
	struct MessageSettings final
	{
		std::chrono::seconds AgeTolerance{ 600 };			// Maximum age of a message in seconds before it's not accepted
		std::chrono::seconds ExtenderGracePeriod{ 60 };		// Number of seconds after an extender is removed to still silently accept messages for that extender
		Size MinRandomDataPrefixSize{ 0 };					// Minimum size in bytes of random data prefix sent with messages
		Size MaxRandomDataPrefixSize{ 0 };					// Maximum size in bytes of random data prefix sent with messages
		Size MinInternalRandomDataSize{ 0 };				// Minimum size in bytes of random data sent with each message
		Size MaxInternalRandomDataSize{ 64 };				// Maximum size in bytes of random data sent with each message
	};

	struct NoiseSettings final
	{
		bool Enabled{ false };							// Whether sending of noise messages is enabled
		std::chrono::seconds TimeInterval{ 0 };			// Noise time interval in seconds
		Size MinMessagesPerInterval{ 0 };				// Minimum number of noise messages to send in given time interval
		Size MaxMessagesPerInterval{ 0 };				// Maximum number of noise messages to send in given time interval
		Size MinMessageSize{ 0 };						// Minimum size of noise message
		Size MaxMessageSize{ 0 };						// Maximum size of noise message
	};

	struct RelaySettings final
	{
		std::chrono::seconds ConnectTimeout{ 60 };					// Maximum number of seconds to wait for a relay link to be established
		std::chrono::seconds GracePeriod{ 60 };						// Number of seconds after a relay is closed to still silently accept messages for that relay link

		UInt8 IPv4ExcludedNetworksCIDRLeadingBits{ 16 };			// The CIDR leading bits of the IPv4 network address spaces of the source and destination endpoints to exclude from the relay link
		UInt8 IPv6ExcludedNetworksCIDRLeadingBits{ 48 };			// The CIDR leading bits of the IPv6 network address spaces of the source and destination endpoints to exclude from the relay link

		struct
		{
			Size MaxPerInterval{ 10 };								// Maximum number of allowed relay connection attempts per interval before IP gets blocked
			std::chrono::seconds Interval{ 10 };					// Period of time after which the relay connection attempts are reset to 0 for an IP
		} IPConnectionAttempts;
	};

	struct LocalAlgorithms final
	{
		Vector<Algorithm::Hash> Hash;
		Vector<Algorithm::Asymmetric> PrimaryAsymmetric;
		Vector<Algorithm::Asymmetric> SecondaryAsymmetric;
		Vector<Algorithm::Symmetric> Symmetric;
		Vector<Algorithm::Compression> Compression;
	};

	struct LocalSettings final
	{
		PeerUUID UUID;														// The UUID of the local peer
		PeerKeys Keys;														// The private and public keys for the local peer
		ProtectedBuffer GlobalSharedSecret;									// Global shared secret to use for all connections with peers (in addition to each individual secret key for every peer)
		bool RequireAuthentication{ true };									// Whether authentication is required for connecting peers

		LocalAlgorithms SupportedAlgorithms;								// The supported algorithms
		Size NumPreGeneratedKeysPerAlgorithm{ 5 };							// The number of pregenerated keys per supported algorithm

		Vector<UInt16> ListenerPorts{ 999 };								// Which ports to listen on
		bool NATTraversal{ false };											// Whether NAT traversal is enabled
		bool UseConditionalAcceptFunction{ true };							// Whether to use the conditional accept function before accepting connections

		std::chrono::seconds ConnectTimeout{ 60 };							// Maximum number of seconds to wait for a connection to be established
		std::chrono::milliseconds MaxHandshakeDelay{ 0 };					// Maximum number of milliseconds to wait in between handshake messages
		std::chrono::seconds MaxHandshakeDuration{ 30 };					// Maximum number of seconds a handshake may last after connecting before peer is disconnected

		std::chrono::seconds IPReputationImprovementInterval{ 600 };		// Period of time after which the reputation of an IP address gets slightly improved

		struct
		{
			Size MaxPerInterval{ 2 };										// Maximum number of allowed connection attempts per interval before IP gets blocked
			std::chrono::seconds Interval{ 10 };							// Period of time after which the connection attempts are reset to 0 for an IP
		} IPConnectionAttempts;

		struct
		{
			std::chrono::seconds MinInterval{ 300 };						// Minimum number of seconds to wait before initiating an encryption key update
			std::chrono::seconds MaxInterval{ 1200 };						// Maximum number of seconds to wait before initiating an encryption key update
			std::chrono::seconds MaxDuration{ 240 };						// Maximum number of seconds that an encryption key update may last after initiation
			Size RequireAfterNumProcessedBytes{ 4'200'000'000 };			// Number of bytes that may be encrypted and transfered using a single symmetric key after which to require a key update
		} KeyUpdate;

		struct
		{
			Size MinThreadPools{ 1 };										// Minimum number of thread pools
			Size MinThreadsPerPool{ 4 };									// Minimum number of worker threads per pool
			std::chrono::milliseconds WorkerThreadsMaxSleep{ 1 };			// Maximum number of milliseconds to sleep when there's no work
			Size WorkerThreadsMaxBurst{ 64 };								// Maximum number of work items to process in a single burst
		} Concurrency;
	};

	struct Settings final
	{
		LocalSettings Local;
		MessageSettings Message;
		NoiseSettings Noise;
		RelaySettings Relay;
	};

	template<UInt64 ID>
	using ThreadLocalSettings = Concurrency::ThreadLocalCache<Settings, Concurrency::SpinMutex, ID>;

	using Settings_CThS = ThreadLocalSettings<369>;
}