// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "Common\Util.h"
#include "Settings.h"
#include "Core\Peer\Peer.h"
#include "Core\Peer\PeerLookupMaps.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace QuantumGate::Implementation::Core::Peer;

std::unique_ptr<Data_ThS> MakePeerData(const IPEndpoint& peer_endpoint, const QuantumGate::UUID uuid)
{
	auto peer_data = std::make_unique<Data_ThS>();
	peer_data->WithUniqueLock([&](Data& data)
	{
		data.Status = Status::Ready;
		data.LUID = Core::Peer::Peer::MakeLUID(peer_endpoint, 0);
		data.Cached.PeerEndpoint = peer_endpoint;
		data.PeerUUID = uuid;
	});

	return peer_data;
}

std::unique_ptr<Data_ThS> MakePeerData(const IPEndpoint& peer_endpoint, const QuantumGate::UUID uuid,
									   const PeerConnectionType type, const bool relayed,
									   const bool authenticated, Vector<ExtenderUUID>&& extuuids)
{
	auto peer_data = std::make_unique<Data_ThS>();
	peer_data->WithUniqueLock([&](Data& data)
	{
		data.Status = Status::Ready;
		data.Type = type;
		data.LUID = Core::Peer::Peer::MakeLUID(peer_endpoint, 0);
		data.Cached.PeerEndpoint = peer_endpoint;
		data.PeerUUID = uuid;
		data.IsAuthenticated = authenticated;
		data.IsRelayed = relayed;

		const auto retval = data.Cached.PeerExtenderUUIDs.Set(std::move(extuuids));
		Assert::AreEqual(true, retval);
	});

	return peer_data;
}

bool CheckExpectedPeers(const Vector<PeerLUID>& peers, const Vector<PeerLUID>& expected_peers)
{
	for (const auto peer : peers)
	{
		if (std::find(expected_peers.begin(), expected_peers.end(), peer) == expected_peers.end()) return false;
	}

	return true;
}

namespace UnitTests
{
	TEST_CLASS(PeerLookupTests)
	{
	public:
		TEST_METHOD(UUIDMap)
		{
			LookupMaps lum;

			const auto uuid1 = QuantumGate::UUID(L"3c0c4c02-5ebc-f99a-0b5e-acdd238b1e54");
			const auto uuid2 = QuantumGate::UUID(L"e938194b-52c1-69d4-0b84-75d3d11dbfad");

			const auto ep1 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000), uuid1);
			const auto ep2 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.11"), 9001), uuid1);
			const auto ep2a = MakePeerData(IPEndpoint(IPEndpoint::Protocol::UDP, IPAddress(L"192.168.1.12"), 9002), uuid1);
			const auto ep3 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.10.11"), 8000), uuid2);
			const auto ep4 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.10.11"), 8000), uuid2);

			Assert::AreEqual(true, lum.AddPeerData(*ep1));
			Assert::AreEqual(true, lum.AddPeerData(*ep2));
			Assert::AreEqual(true, lum.AddPeerData(*ep2a));
			Assert::AreEqual(true, lum.AddPeerData(*ep3));
			Assert::AreEqual(false, lum.AddPeerData(*ep4));

			// Should have 2 UUIDs
			Assert::AreEqual(true, lum.GetUUIDMap().size() == 2);

			{
				const auto it = lum.GetUUIDMap().find(uuid1);
				Assert::AreEqual(true, it != lum.GetUUIDMap().end());

				// Should have 3 LUIDs
				Assert::AreEqual(true, it->second.size() == 3);
			}

			{
				const auto it = lum.GetUUIDMap().find(uuid2);
				Assert::AreEqual(true, it != lum.GetUUIDMap().end());

				// Should have 1 LUID
				Assert::AreEqual(true, it->second.size() == 1);

				const auto it2 = std::find(it->second.begin(), it->second.end(), ep3->WithSharedLock()->LUID);
				Assert::AreEqual(true, it2 != it->second.end());
			}

			// Remove
			{
				Assert::AreEqual(true, lum.RemovePeerData(*ep1));

				// Should still have 2 UUIDs
				Assert::AreEqual(true, lum.GetUUIDMap().size() == 2);

				Assert::AreEqual(true, lum.RemovePeerData(*ep2));

				// Should still have 2 UUIDs
				Assert::AreEqual(true, lum.GetUUIDMap().size() == 2);

				Assert::AreEqual(true, lum.RemovePeerData(*ep2a));

				// Should have 1 UUID
				Assert::AreEqual(true, lum.GetUUIDMap().size() == 1);

				Assert::AreEqual(true, lum.RemovePeerData(*ep3));

				// Should have no UUIDs
				Assert::AreEqual(true, lum.GetUUIDMap().empty());

				// Removing nonexisting UUID should fail
				Assert::AreEqual(false, lum.RemovePeerData(*ep4));
			}

			Assert::AreEqual(true, lum.IsEmpty());
		}

		TEST_METHOD(IPEndpointMap)
		{
			LookupMaps lum;

			const auto uuid1 = QuantumGate::UUID(L"3c0c4c02-5ebc-f99a-0b5e-acdd238b1e54");
			const auto uuid2 = QuantumGate::UUID(L"e938194b-52c1-69d4-0b84-75d3d11dbfad");

			const auto ep1 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 1000, 3), uuid1);
			const auto ep2 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 2000, 3), uuid1);
			const auto ep3 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.10.11"), 8000, 1000, 2), uuid2);
			const auto ep4 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::UDP, IPAddress(L"192.168.10.11"), 8000, 2000, 2), uuid2);

			Assert::AreEqual(true, lum.AddPeerData(*ep1));
			Assert::AreEqual(true, lum.AddPeerData(*ep2));
			Assert::AreEqual(true, lum.AddPeerData(*ep3));
			Assert::AreEqual(true, lum.AddPeerData(*ep4));

			// Should have 3 IPEndpoint combinations
			Assert::AreEqual(true, lum.GetIPEndpointMap().size() == 3);

			{
				const auto it = lum.GetIPEndpointMap().find(lum.GetIPEndpointHash(ep1->WithSharedLock()->Cached.PeerEndpoint));
				Assert::AreEqual(true, it != lum.GetIPEndpointMap().end());

				// Should have 2 LUIDs
				Assert::AreEqual(true, it->second.size() == 2);
			}

			{
				const auto it = lum.GetIPEndpointMap().find(lum.GetIPEndpointHash(ep3->WithSharedLock()->Cached.PeerEndpoint));
				Assert::AreEqual(true, it != lum.GetIPEndpointMap().end());

				// Should have 1 LUID
				Assert::AreEqual(true, it->second.size() == 1);

				const auto it2 = std::find(it->second.begin(), it->second.end(), ep3->WithSharedLock()->LUID);
				Assert::AreEqual(true, it2 != it->second.end());
			}

			// Remove
			{
				Assert::AreEqual(true, lum.RemovePeerData(*ep1));

				// Should still have 3 IPEndpoint combinations
				Assert::AreEqual(true, lum.GetIPEndpointMap().size() == 3);

				Assert::AreEqual(true, lum.RemovePeerData(*ep2));

				// Should have 2 IPEndpoint combination
				Assert::AreEqual(true, lum.GetIPEndpointMap().size() == 2);

				Assert::AreEqual(true, lum.RemovePeerData(*ep3));

				// Should have 1 IPEndpoint combination
				Assert::AreEqual(true, lum.GetIPEndpointMap().size() == 1);

				Assert::AreEqual(true, lum.RemovePeerData(*ep4));

				// Should have no IPEndpoint combinations
				Assert::AreEqual(true, lum.GetIPEndpointMap().empty());

				// Removing nonexisting IPEndpoint combination should fail
				Assert::AreEqual(false, lum.RemovePeerData(*ep3));
			}

			Assert::AreEqual(true, lum.IsEmpty());
		}

		TEST_METHOD(IPMap)
		{
			LookupMaps lum;

			const auto uuid1 = QuantumGate::UUID(L"3c0c4c02-5ebc-f99a-0b5e-acdd238b1e54");
			const auto uuid2 = QuantumGate::UUID(L"e938194b-52c1-69d4-0b84-75d3d11dbfad");

			const auto ep1 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 1000, 2), uuid1);
			const auto ep2 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 2000, 3), uuid1);
			const auto ep2a = MakePeerData(IPEndpoint(IPEndpoint::Protocol::UDP, IPAddress(L"192.168.1.10"), 9000, 2000, 3), uuid1);
			const auto ep3 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.10.11"), 8000, 1000, 2), uuid2);
			const auto ep4 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::UDP, IPAddress(L"192.168.10.12"), 8000, 1000, 2), uuid2);

			Assert::AreEqual(true, lum.AddPeerData(*ep1));
			Assert::AreEqual(true, lum.AddPeerData(*ep2));
			Assert::AreEqual(true, lum.AddPeerData(*ep2a));
			Assert::AreEqual(true, lum.AddPeerData(*ep3));
			Assert::AreEqual(true, lum.AddPeerData(*ep4));

			// Should have 3 IPs
			Assert::AreEqual(true, lum.GetIPMap().size() == 3);

			{
				const auto it = lum.GetIPMap().find(IPAddress(L"192.168.1.10").GetBinary());
				Assert::AreEqual(true, it != lum.GetIPMap().end());

				// Should have 3 LUIDs
				Assert::AreEqual(true, it->second.size() == 3);
			}

			{
				const auto it = lum.GetIPMap().find(IPAddress(L"192.168.10.11").GetBinary());
				Assert::AreEqual(true, it != lum.GetIPMap().end());

				// Should have 1 LUID
				Assert::AreEqual(true, it->second.size() == 1);

				const auto it2 = std::find(it->second.begin(), it->second.end(), ep3->WithSharedLock()->LUID);
				Assert::AreEqual(true, it2 != it->second.end());
			}

			{
				const auto it = lum.GetIPMap().find(IPAddress(L"192.168.10.12").GetBinary());
				Assert::AreEqual(true, it != lum.GetIPMap().end());

				// Should have 1 LUID
				Assert::AreEqual(true, it->second.size() == 1);

				const auto it2 = std::find(it->second.begin(), it->second.end(), ep4->WithSharedLock()->LUID);
				Assert::AreEqual(true, it2 != it->second.end());
			}


			// Remove
			{
				Assert::AreEqual(true, lum.RemovePeerData(*ep1));

				// Should still have 3 IPs
				Assert::AreEqual(true, lum.GetIPMap().size() == 3);

				Assert::AreEqual(true, lum.RemovePeerData(*ep2));

				// Should still have 3 IPs
				Assert::AreEqual(true, lum.GetIPMap().size() == 3);

				Assert::AreEqual(true, lum.RemovePeerData(*ep2a));

				// Should have 2 IPs
				Assert::AreEqual(true, lum.GetIPEndpointMap().size() == 2);

				Assert::AreEqual(true, lum.RemovePeerData(*ep3));

				// Should have 1 IP
				Assert::AreEqual(true, lum.GetIPEndpointMap().size() == 1);

				Assert::AreEqual(true, lum.RemovePeerData(*ep4));

				// Should have no IPs
				Assert::AreEqual(true, lum.GetIPEndpointMap().empty());

				// Removing nonexisting IP should fail
				Assert::AreEqual(false, lum.RemovePeerData(*ep3));
			}

			Assert::AreEqual(true, lum.IsEmpty());
		}

		TEST_METHOD(ExcludedNetworks)
		{
			LookupMaps lum;

			{
				const UInt8 cidr_lbits4{ 24 };
				const UInt8 cidr_lbits6{ 48 };

				const Vector<BinaryIPAddress> excl_addr
				{
					IPAddress(L"192.168.1.10").GetBinary(),
					IPAddress(L"192.168.1.20").GetBinary(),
					IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(),
					IPAddress(L"fe80:c11a:3a9c:ef10:e796::").GetBinary()
				};

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"192.168.1.44").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(true, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"fe80:c11a:3a9c:ef11:e795::").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(true, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"192.168.2.44").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(false, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"172.217.7.238").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(false, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"fe80:c11a:4a9c:ef11:e795::").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(false, result2.GetValue());
				}

				// Bad CIDR values
				{
					auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"172.217.7.238").GetBinary(),
																   excl_addr, 40, 96);
					Assert::AreEqual(false, result2.Succeeded());

					result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"fe80:c11a:4a9c:ef11:e795::").GetBinary(),
															  excl_addr, 24, 130);
					Assert::AreEqual(false, result2.Succeeded());
				}
			}

			{
				const UInt8 cidr_lbits4{ 16 };
				const UInt8 cidr_lbits6{ 48 };

				Vector<BinaryIPAddress> excl_addr
				{
					IPAddress(L"192.168.1.10").GetBinary(),
					IPAddress(L"192.168.1.20").GetBinary(),
					IPAddress(L"172.217.7.238").GetBinary(),
					IPAddress(L"172.217.4.138").GetBinary(),
					IPAddress(L"172.117.4.138").GetBinary(),
					IPAddress(L"fe80:c11a:3a9c:ef10:e796::").GetBinary()
				};

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"192.168.1.10").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(true, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"192.168.1.44").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(true, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"fe80:c11a:3a9c:ef11:e795::").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(true, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"192.169.2.44").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(false, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"172.217.7.239").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(true, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"172.218.7.238").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(false, result2.GetValue());
				}

				{
					const auto result2 = LookupMaps::AreIPsInSameNetwork(IPAddress(L"fe80:c11a:4a9c:ef11:e795::").GetBinary(),
																		 excl_addr, cidr_lbits4, cidr_lbits6);
					Assert::AreEqual(true, result2.Succeeded());
					Assert::AreEqual(false, result2.GetValue());
				}
			}
		}

		TEST_METHOD(GetRandomPeer)
		{
			LookupMaps lum;

			const auto uuid1 = QuantumGate::UUID(L"3c0c4c02-5ebc-f99a-0b5e-acdd238b1e54");

			// Connected peers
			const auto ep1 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 1000, 2), uuid1);
			const auto ep2 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 2000, 3), uuid1);
			const auto ep3 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.10.11"), 8000, 1000, 2), uuid1);
			const auto ep4 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.20"), 8000), uuid1);
			const auto ep5 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.5.40"), 9000), uuid1);
			const auto ep6 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"fe80:c11a:3a9c:ef11:e795::"), 9000), uuid1);
			const auto ep7 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"200.168.5.51"), 9000), uuid1);

			Assert::AreEqual(true, lum.AddPeerData(*ep1));
			Assert::AreEqual(true, lum.AddPeerData(*ep2));
			Assert::AreEqual(true, lum.AddPeerData(*ep3));
			Assert::AreEqual(true, lum.AddPeerData(*ep4));
			Assert::AreEqual(true, lum.AddPeerData(*ep5));
			Assert::AreEqual(true, lum.AddPeerData(*ep6));
			Assert::AreEqual(true, lum.AddPeerData(*ep7));

			// Trying to find relay peer for 192.168.1.10 to 200.168.5.40
			{
				const auto dest_ep = IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"200.168.5.40"), 9000);

				const Vector<PeerLUID> excl_pluids =
				{
					ep3->WithSharedLock()->LUID,
					ep4->WithSharedLock()->LUID
				};

				const Vector<BinaryIPAddress> excl_addr1 =
				{
					ep1->WithSharedLock()->Cached.PeerEndpoint.GetIPAddress().GetBinary() // Don't loop back
				};

				const Vector<BinaryIPAddress> excl_addr2 =
				{
					dest_ep.GetIPAddress().GetBinary() // Don't include the final endpoint
				};

				{
					const Vector<PeerLUID> expected_peers
					{
						ep5->WithSharedLock()->LUID,
						ep6->WithSharedLock()->LUID,
						ep7->WithSharedLock()->LUID
					};

					for (auto x = 0u; x < 100u; ++x)
					{
						const auto result = lum.GetRandomPeer(excl_pluids, excl_addr1, excl_addr2, 32, 128);
						Assert::AreEqual(true, result.Succeeded());

						// Check that we got back one of the expected peers
						const auto it = std::find(expected_peers.begin(), expected_peers.end(), result.GetValue());
						Assert::AreEqual(true, it != expected_peers.end());
					}
				}

				{
					const std::vector<PeerLUID> expected_peers
					{
						ep3->WithSharedLock()->LUID,
						ep5->WithSharedLock()->LUID,
						ep6->WithSharedLock()->LUID
					};

					for (auto x = 0u; x < 100u; ++x)
					{
						const auto result = lum.GetRandomPeer({}, excl_addr1, excl_addr2, 24, 96);
						Assert::AreEqual(true, result.Succeeded());

						// Check that we got back one of the expected peers
						const auto it = std::find(expected_peers.begin(), expected_peers.end(), result.GetValue());
						Assert::AreEqual(true, it != expected_peers.end());
					}
				}

				{
					const std::vector<PeerLUID> expected_peers
					{
						ep6->WithSharedLock()->LUID
					};

					for (auto x = 0u; x < 100u; ++x)
					{
						const auto result = lum.GetRandomPeer({}, excl_addr1, excl_addr2, 16, 96);
						Assert::AreEqual(true, result.Succeeded());

						// Check that we got back one of the expected peers
						const auto it = std::find(expected_peers.begin(), expected_peers.end(), result.GetValue());
						Assert::AreEqual(true, it != expected_peers.end());
					}
				}
			}

			// Trying to find relay peer for 200.168.5.51 to fe80:c11a:3a9c:ef10:e795::
			{
				const auto dest_ep = IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"fe80:c11a:3a9c:ef10:e795::"), 9000);

				const Vector<PeerLUID> excl_pluids =
				{
					ep1->WithSharedLock()->LUID,
					ep2->WithSharedLock()->LUID
				};

				const Vector<BinaryIPAddress> excl_addr1 =
				{
					ep7->WithSharedLock()->Cached.PeerEndpoint.GetIPAddress().GetBinary() // Don't loop back
				};

				const Vector<BinaryIPAddress> excl_addr2 =
				{
					dest_ep.GetIPAddress().GetBinary() // Don't include the final endpoint
				};

				{
					const std::vector<PeerLUID> expected_peers
					{
						ep3->WithSharedLock()->LUID,
						ep4->WithSharedLock()->LUID,
						ep5->WithSharedLock()->LUID,
						ep6->WithSharedLock()->LUID
					};

					for (auto x = 0u; x < 100u; ++x)
					{
						const auto result = lum.GetRandomPeer(excl_pluids, excl_addr1, excl_addr2, 32, 64);
						Assert::AreEqual(true, result.Succeeded());

						// Check that we got back one of the expected peers
						const auto it = std::find(expected_peers.begin(), expected_peers.end(), result.GetValue());
						Assert::AreEqual(true, it != expected_peers.end());
					}
				}

				{
					const std::vector<PeerLUID> expected_peers
					{
						ep1->WithSharedLock()->LUID,
						ep2->WithSharedLock()->LUID,
						ep3->WithSharedLock()->LUID,
						ep4->WithSharedLock()->LUID,
						ep5->WithSharedLock()->LUID
					};

					for (auto x = 0u; x < 100u; ++x)
					{
						const auto result = lum.GetRandomPeer({}, excl_addr1, excl_addr2, 24, 48);
						Assert::AreEqual(true, result.Succeeded());

						// Check that we got back one of the expected peers
						const auto it = std::find(expected_peers.begin(), expected_peers.end(), result.GetValue());
						Assert::AreEqual(true, it != expected_peers.end());
					}
				}
			}
		}

		TEST_METHOD(AreIPsInSameNetwork)
		{
			struct IPTest
			{
				BinaryIPAddress ip1;
				BinaryIPAddress ip2;
				UInt8 cidr4{ 0 };
				UInt8 cidr6{ 0 };
				bool result{ false };
			};

			const std::vector<IPTest> iptests
			{
				{ IPAddress(L"192.168.1.10").GetBinary(), IPAddress(L"192.168.1.20").GetBinary(), 32, 128, false },
				{ IPAddress(L"192.168.1.10").GetBinary(), IPAddress(L"192.168.1.20").GetBinary(), 24, 128, true },
				{ IPAddress(L"192.168.1.10").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 24, 128, false },
				{ IPAddress(L"192.168.1.10").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 16, 128, false },
				{ IPAddress(L"192.168.1.10").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 8, 128, false },
				{ IPAddress(L"192.168.1.10").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 0, 128, true },
				{ IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 32, 128, false },
				{ IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 32, 48, false },
				{ IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(), IPAddress(L"200.168.5.51").GetBinary(), 0, 0, false },
				{ IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(), IPAddress(L"fe80:c11a:3a9c:ef11:e795::").GetBinary(), 32, 128, false },
				{ IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(), IPAddress(L"fe80:c11a:3a9c:ef11:e795::").GetBinary(), 32, 64, false },
				{ IPAddress(L"fe80:c11a:3a9c:ef10:e795::").GetBinary(), IPAddress(L"fe80:c11a:3a9c:ef11:e795::").GetBinary(), 32, 48, true }
			};

			for (const auto& test : iptests)
			{
				const auto result = LookupMaps::AreIPsInSameNetwork(test.ip1, test.ip2, test.cidr4, test.cidr6);
				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(test.result, result.GetValue());
			}
		}

		TEST_METHOD(QueryPeers)
		{
			LookupMaps lum;

			const auto puuid1 = QuantumGate::UUID(L"3c0c4c02-5ebc-f99a-0b5e-acdd238b1e54");
			const auto puuid2 = QuantumGate::UUID(L"e938194b-52c1-69d4-0b84-75d3d11dbfad");
			const auto puuid3 = QuantumGate::UUID(L"672e278e-206c-992d-8bcd-6d4d1c489993");
			const auto puuid4 = QuantumGate::UUID(L"df0aec07-4ef6-d979-d3b7-44f60330810f");

			const auto euuid1 = QuantumGate::UUID(L"bbcbb357-1140-d91b-ced5-e78cabc471bc");
			const auto euuid2 = QuantumGate::UUID(L"67871eec-a143-09ed-d636-7b9c5dac0f2d");

			const auto ep1 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.10"), 9000, 1000, 2), puuid1,
										  PeerConnectionType::Outbound, true, true, { euuid1 });
			const auto ep2 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.20"), 9000), puuid2,
										  PeerConnectionType::Inbound, false, false, {});
			const auto ep3 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.30"), 8000), puuid3,
										  PeerConnectionType::Inbound, false, true, { euuid2 });
			const auto ep4 = MakePeerData(IPEndpoint(IPEndpoint::Protocol::TCP, IPAddress(L"192.168.1.40"), 8000, 1000, 2), puuid3,
										  PeerConnectionType::Inbound, true, false, { euuid1, euuid2 });

			Assert::AreEqual(true, lum.AddPeerData(*ep1));
			Assert::AreEqual(true, lum.AddPeerData(*ep2));
			Assert::AreEqual(true, lum.AddPeerData(*ep3));
			Assert::AreEqual(true, lum.AddPeerData(*ep4));

			// Default
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 4);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep2->WithSharedLock()->LUID,
															  ep3->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));
			}

			// Only authenticated
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Authentication = PeerQueryParameters::AuthenticationOption::Authenticated;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 2);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep3->WithSharedLock()->LUID
														  }));

				params.Connections = PeerQueryParameters::ConnectionOption::Outbound;
				const auto result2 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result2.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID
														  }));
			}

			// Only relays
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Relays = PeerQueryParameters::RelayOption::Relayed;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 2);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));

				params.Authentication = PeerQueryParameters::AuthenticationOption::Authenticated;
				const auto result2 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result2.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID
														  }));
			}

			// Only inbound
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Connections = PeerQueryParameters::ConnectionOption::Inbound;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 3);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep2->WithSharedLock()->LUID,
															  ep3->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));

				params.Authentication = PeerQueryParameters::AuthenticationOption::Authenticated;
				const auto result2 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result2.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep3->WithSharedLock()->LUID
														  }));
			}

			// Only outbound
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Connections = PeerQueryParameters::ConnectionOption::Outbound;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID
														  }));
			}

			// Extenders NoneOf
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Extenders.UUIDs = { euuid1 };
				params.Extenders.Include = PeerQueryParameters::Extenders::IncludeOption::NoneOf;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 2);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep2->WithSharedLock()->LUID,
															  ep3->WithSharedLock()->LUID
														  }));

				params.Extenders.UUIDs = { euuid1, euuid2 };
				const auto result2 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result2.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep2->WithSharedLock()->LUID
														  }));
			}

			// Extenders AllOf
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Extenders.UUIDs = { euuid1 };
				params.Extenders.Include = PeerQueryParameters::Extenders::IncludeOption::AllOf;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 2);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));

				params.Extenders.UUIDs = { euuid1, euuid2 };
				const auto result2 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result2.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep4->WithSharedLock()->LUID
														  }));

				params.Connections = PeerQueryParameters::ConnectionOption::Inbound;
				const auto result3 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result3.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep4->WithSharedLock()->LUID
														  }));

				params.Authentication = PeerQueryParameters::AuthenticationOption::Authenticated;
				const auto result4 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result4.Succeeded());
				Assert::AreEqual(true, pluids.size() == 0);
			}

			// Extenders OneOf
			{
				PeerQueryParameters params;
				Vector<PeerLUID> pluids;
				params.Extenders.UUIDs = { euuid1 };
				params.Extenders.Include = PeerQueryParameters::Extenders::IncludeOption::OneOf;
				const auto result = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result.Succeeded());
				Assert::AreEqual(true, pluids.size() == 2);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));

				params.Extenders.UUIDs = { euuid1, euuid2 };
				const auto result2 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result2.Succeeded());
				Assert::AreEqual(true, pluids.size() == 3);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep3->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));

				params.Relays = PeerQueryParameters::RelayOption::Relayed;
				const auto result3 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result3.Succeeded());
				Assert::AreEqual(true, pluids.size() == 2);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID,
															  ep4->WithSharedLock()->LUID
														  }));

				params.Connections = PeerQueryParameters::ConnectionOption::Outbound;
				const auto result4 = lum.QueryPeers(params, pluids);

				Assert::AreEqual(true, result4.Succeeded());
				Assert::AreEqual(true, pluids.size() == 1);
				Assert::AreEqual(true, CheckExpectedPeers(pluids,
														  {
															  ep1->WithSharedLock()->LUID
														  }));
			}
		}
	};
}