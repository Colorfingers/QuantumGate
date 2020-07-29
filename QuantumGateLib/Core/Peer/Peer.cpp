// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "Peer.h"
#include "PeerManager.h"
#include "..\..\Common\Random.h"
#include "..\..\API\Access.h"

#include <thread>
#include <chrono>
#include <algorithm>

using namespace std::literals;

namespace QuantumGate::Implementation::Core::Peer
{
	Peer::Peer(Manager& peers, const GateType pgtype, const PeerConnectionType pctype,
			   std::optional<ProtectedBuffer>&& shared_secret) :
		Gate(pgtype), m_PeerManager(peers)
	{
		if (shared_secret) m_GlobalSharedSecret = std::move(shared_secret);

		m_PeerData.WithUniqueLock([&](Data& peer_data) noexcept
		{
			peer_data.Type = pctype;
			peer_data.IsRelayed = (GetGateType() == GateType::RelaySocket);
			peer_data.IsUsingGlobalSharedSecret = !GetGlobalSharedSecret().IsEmpty();
		});
	}

	Peer::Peer(Manager& peers, const IP::AddressFamily af, const Socket::Type type,
			   const IP::Protocol protocol, const PeerConnectionType pctype,
			   std::optional<ProtectedBuffer>&& shared_secret) :
		Gate(af, type, protocol), m_PeerManager(peers)
	{
		if (shared_secret) m_GlobalSharedSecret = std::move(shared_secret);

		m_PeerData.WithUniqueLock([&](Data& peer_data) noexcept
		{
			peer_data.Type = pctype;
			peer_data.IsUsingGlobalSharedSecret = !GetGlobalSharedSecret().IsEmpty();
		});
	}

	bool Peer::Initialize(PeerWeakPointer&& peer_ths) noexcept
	{
		// Delay for a few random milliseconds before we begin communication;
		// this gives the peer a chance to sometimes start communicating first.
		// This is for traffic analyzers to make initiation of communication random.
		if (GetConnectionType() == PeerConnectionType::Inbound &&
			Random::GetPseudoRandomNumber(0, 1) == 1)
		{
			SetFlag(Flags::HandshakeStartDelay, true);
			DisableSend();
		}

		// If we have a global shared secret
		if (!GetGlobalSharedSecret().IsEmpty())
		{
			// We can start with symmetric keys generated with the global shared secret
			if (m_Keys.GenerateAndAddSymmetricKeyPair(GetGlobalSharedSecret(),
													  ProtectedBuffer(), GetAlgorithms(),
													  GetConnectionType()))
			{
				// We need to have symmetric keys already if we get here
				assert(!m_Keys.GetSymmetricKeyPairs().empty());

				SetInitialConditionsWithGlobalSharedSecret(m_Keys.GetSymmetricKeyPairs()[0]->EncryptionKey->AuthKey,
														   m_Keys.GetSymmetricKeyPairs()[0]->DecryptionKey->AuthKey);
			}
			else return false;
		}

		m_PeerPointer = std::move(peer_ths);

		return SetStatus(Status::Initialized);
	}

	void Peer::SetInitialConditionsWithGlobalSharedSecret(const ProtectedBuffer& encr_authkey,
														  const ProtectedBuffer& decr_authkey) noexcept
	{
		const auto seed = static_cast<float>(std::max(static_cast<UInt8>(encr_authkey[0]),
													  static_cast<UInt8>(decr_authkey[0]))) / 255.f;

		const auto mtds = seed * static_cast<float>(MessageTransport::MaxMessageDataSizeOffset);
		m_MessageTransportDataSizeSettings.Offset = static_cast<UInt8>(std::floor(mtds));

		m_MessageTransportDataSizeSettings.XOR = *(reinterpret_cast<const UInt32*>(encr_authkey.GetBytes())) ^
			*(reinterpret_cast<const UInt32*>(decr_authkey.GetBytes()));

		// With a Global Shared Secret known to both peers we can start the first
		// Message Transport with a random data prefix of a length that's only
		// known to the peers; in this case between 0 - 64 bytes depending on
		// the Global Shared Secret. This overrides the Min/MaxRandomDataPrefixSize
		// in the Settings for the first Message Transport being sent.
		m_NextLocalRandomDataPrefixLength = static_cast<UInt16>(std::floor(seed * 64.f));
		m_NextPeerRandomDataPrefixLength = m_NextLocalRandomDataPrefixLength;

		Dbg(L"\r\nGSS initial conditions:");
		Dbg(L"Seed: %f ", seed);
		Dbg(L"MsgTDSOffset: %u bits", m_MessageTransportDataSizeSettings.Offset);
		Dbg(L"RndDPrefixLen: %u bytes\r\n", m_NextLocalRandomDataPrefixLength);
	}

	void Peer::EnableSend() noexcept
	{
		m_SendDisabledDuration = 0ms;
		SetFlag(Flags::SendDisabled, false);
	}

	void Peer::DisableSend() noexcept
	{
		m_SendDisabledDuration = 0ms;
		SetFlag(Flags::SendDisabled, true);
	}

	void Peer::DisableSend(const std::chrono::milliseconds duration) noexcept
	{
		m_SendDisabledSteadyTime = Util::GetCurrentSteadyTime();
		m_SendDisabledDuration = duration;

		if (duration > 0ms) SetFlag(Flags::SendDisabled, true);
		else SetFlag(Flags::SendDisabled, false);
	}

	std::chrono::milliseconds Peer::GetHandshakeDelayPerMessage() const noexcept
	{
		return std::chrono::milliseconds(GetSettings().Local.MaxHandshakeDelay.count() / NumHandshakeDelayMessages);
	}

	const Settings& Peer::GetSettings() const noexcept
	{
		return GetPeerManager().GetSettings();
	}

	Manager& Peer::GetPeerManager() const noexcept
	{
		return m_PeerManager;
	}

	Relay::Manager& Peer::GetRelayManager() noexcept
	{
		return GetPeerManager().GetRelayManager();
	}

	Extender::Manager& Peer::GetExtenderManager() const noexcept
	{
		return GetPeerManager().GetExtenderManager();
	}

	KeyGeneration::Manager& Peer::GetKeyGenerationManager() const noexcept
	{
		return GetPeerManager().GetKeyGenerationManager();
	}

	Access::Manager& Peer::GetAccessManager() const noexcept
	{
		return GetPeerManager().GetAccessManager();
	}

	const Extender::ActiveExtenderUUIDs& Peer::GetLocalExtenderUUIDs() noexcept
	{
		return GetExtenderManager().GetActiveExtenderUUIDs();
	}

	void Peer::ProcessLocalExtenderUpdate(const Vector<ExtenderUUID>& extuuids)
	{
		if (IsReady())
		{
			for (const auto& extuuid : extuuids)
			{
				if (GetPeerExtenderUUIDs().HasExtender(extuuid))
				{
					ProcessEvent({ extuuid }, Event::Type::Connected);
				}
			}
		}
	}

	bool Peer::ProcessPeerExtenderUpdate(Vector<ExtenderUUID>&& uuids) noexcept
	{
		auto success = false;

		switch (GetStatus())
		{
			case Status::SessionInit:
			{
				success = GetPeerExtenderUUIDs().Set(std::move(uuids));
				break;
			}
			case Status::Ready:
			{
				// Process extender updates
				const auto updates = GetPeerExtenderUUIDs().Update(std::move(uuids));
				if (updates.Succeeded())
				{
					// Notify local extenders of changes in peer extender support
					if (!updates->first.empty()) ProcessEvent(updates->first, Event::Type::Connected);
					if (!updates->second.empty()) ProcessEvent(updates->second, Event::Type::Disconnected);
					success = true;
				}

				break;
			}
			default:
			{
				// Shouldn't get here
				assert(false);
				break;
			}
		}

		if (success)
		{
			// Update cache
			success = m_PeerData.WithUniqueLock()->Cached.PeerExtenderUUIDs.Copy(GetPeerExtenderUUIDs());
		}

		if (!success)
		{
			LogErr(L"Couldn't update peer extender UUIDs for peer %s", GetPeerName().c_str());
		}

		return success;
	}

	bool Peer::UpdateSocketStatus() noexcept
	{
		if (NeedsAccessCheck()) CheckAccess();

		if (ShouldDisconnect()) return false;

		if (UpdateIOStatus(0ms))
		{
			if (GetIOStatus().HasException())
			{
				// There was an error on the socket
				LogErr(L"Socket error for peer %s (%s)",
					   GetPeerName().c_str(), GetSysErrorString(GetIOStatus().GetErrorCode()).c_str());

				SetDisconnectCondition(DisconnectCondition::SocketError);
				return false;
			}

			return true;
		}
		else SetDisconnectCondition(DisconnectCondition::SocketError);

		return false;
	}

	bool Peer::CheckStatus(const bool noise_enabled, const std::chrono::seconds max_connect_duration,
						   std::chrono::seconds max_handshake_duration) noexcept
	{
		if (!UpdateSocketStatus()) return false;

		const auto status = GetStatus();

		if (status >= Status::Connected)
		{
			// Check if send disable period has expired
			if (IsFlagSet(Flags::SendDisabled) && m_SendDisabledDuration > 0ms &&
				(Util::GetCurrentSteadyTime() - m_SendDisabledSteadyTime) > m_SendDisabledDuration)
			{
				EnableSend();
			}

			if (noise_enabled && m_NoiseQueue.IsEmpty())
			{
				// Queue more noise
				const auto inhandshake = (status < Status::Ready);
				if (!m_NoiseQueue.QueueNoise(GetSettings(), inhandshake)) return false;
			}
		}

		if (status < Status::Ready)
		{
			// If handshake was delayed begin communication as soon
			// as we received some data from the peer
			if (IsFlagSet(Flags::HandshakeStartDelay) && GetBytesReceived() > 0)
			{
				SetFlag(Flags::HandshakeStartDelay, false);
				EnableSend();
			}

			if (IsRelayed())
			{
				// Minimum of 2 times the maximum handshake duration setting for
				// relayed peer connections (because of all the delays in between peers)
				const auto hops = GetPeerEndpoint().GetRelayHop();
				max_handshake_duration = max_handshake_duration * (hops > 2 ? hops : 2);
			}

			if (GetIOStatus().IsConnecting() &&
				((Util::GetCurrentSteadyTime() - GetConnectedSteadyTime()) > max_connect_duration))
			{
				// If the peer couldn't connect
				LogErr(L"Peer %s could not establish connection quick enough; will remove", GetPeerName().c_str());

				SetDisconnectCondition(DisconnectCondition::TimedOutError);
				return false;
			}
			else if (!GetIOStatus().IsConnecting() &&
				((Util::GetCurrentSteadyTime() - GetConnectedSteadyTime()) > max_handshake_duration))
			{
				// If the peer was accepted/connected but did not reach the ready state quick enough remove it
				LogErr(L"Peer %s did not complete handshake quick enough; will disconnect", GetPeerName().c_str());

				SetDisconnectCondition(DisconnectCondition::TimedOutError);

				// This might be an attack ("slowloris" for example) so limit the
				// number of times this may happen by updating the IP reputation
				UpdateReputation(Access::IPReputationUpdate::DeteriorateMinimal);

				return false;
			}
			else if (status == Status::Connected && GetIOStatus().CanWrite())
			{
				// We get here if a new connection was accepted; begin handshake
				return SetStatus(Status::MetaExchange);
			}
			else if (GetIOStatus().IsConnecting() && GetIOStatus().CanWrite())
			{
				// If a connection attempt was locally started and the socket becomes
				// writable then the connection succeeded; complete the connection attempt
				if (CompleteConnect())
				{
					LogInfo(L"Connected to peer %s", GetPeerName().c_str());
					return SetStatus(Status::MetaExchange);
				}
				else
				{
					LogErr(L"CompleteConnect failed for peer %s", GetPeerName().c_str());
					SetDisconnectCondition(DisconnectCondition::ConnectError);
					return false;
				}
			}
		}

		return true;
	}

	void Peer::UpdateReputation(const Access::IPReputationUpdate rep_update) noexcept
	{
		const auto result = GetAccessManager().UpdateIPReputation(GetPeerIPAddress(), rep_update);
		if (result.Succeeded() && !result->second)
		{
			// Peer IP has an unacceptable reputation after the update;
			// disconnect the peer as soon as possible
			SetDisconnectCondition(DisconnectCondition::IPNotAllowed);
		}
		else if (!result.Succeeded())
		{
			LogErr(L"Couldn't update IP reputation for peer %s", GetPeerName().c_str());
		}
	}

	bool Peer::ProcessEvents()
	{
		if (ShouldDisconnect()) return false;

		const auto& settings = GetSettings();

		// First we check if we have data waiting to be received;
		// if so receive and process any received messages
		if (HasReceiveEvents())
		{
			if (ReceiveAndProcess(settings))
			{
				m_PeerData.WithUniqueLock()->Cached.BytesReceived = GetBytesReceived();
			}
			else
			{
				SetDisconnectCondition(DisconnectCondition::ReceiveError);
				return false;
			}
		}

		// Prepare and add noise messages to the send queue
		if (!m_NoiseQueue.IsEmpty())
		{
			if (!SendFromNoiseQueue(settings))
			{
				SetDisconnectCondition(DisconnectCondition::SendError);
				return false;
			}
		}

		if (NeedsExtenderUpdate() && IsReady())
		{
			if (auto result = m_PeerManager.GetExtenderUpdateData(); result.Succeeded())
			{
				SetFlag(Flags::NeedsExtenderUpdate, false);

				if (!Send(MessageType::ExtenderUpdate, std::move(*result)))
				{
					SetDisconnectCondition(DisconnectCondition::SendError);
					return false;
				}
			}
		}

		// If we have messages to send do so; note that we do this
		// after receiving messages and processing those received
		// messages above
		if (HasSendEvents())
		{
			if (SendFromQueues(settings))
			{
				m_PeerData.WithUniqueLock()->Cached.BytesSent = GetBytesSent();
			}
			else
			{
				SetDisconnectCondition(DisconnectCondition::SendError);
				return false;
			}
		}

		// Check if we need to update the symmetric keys
		// and handle the update process
		if (!CheckAndProcessKeyUpdate())
		{
			SetDisconnectCondition(DisconnectCondition::GeneralFailure);
			return false;
		}

		return true;
	}

	bool Peer::InitializeKeyExchange() noexcept
	{
		assert(m_KeyExchange == nullptr);

		if (m_KeyExchange == nullptr)
		{
			try
			{
				m_KeyExchange = std::make_unique<KeyExchange>(GetKeyGenerationManager());
				return true;
			}
			catch (const std::exception& e)
			{
				LogErr(L"Couldn't initialize key exchange for peer %s due to exception - %s",
					   GetPeerName().c_str(), Util::ToStringW(e.what()));
			}
		}
		else LogErr(L"Couldn't initialize key exchange for peer %s because there's already one in progress",
					GetPeerName().c_str());

		return false;
	}

	void Peer::ReleaseKeyExchange() noexcept
	{
		m_KeyExchange.reset();

		// After the key exchange has finished we end up
		// with new session keys and we can expire the old ones
		GetKeys().ExpireAllExceptLatestKeyPair();
	}

	bool Peer::SetStatus(const Status status) noexcept
	{
		auto success = true;
		const auto prev_status = m_PeerData.WithSharedLock()->Status;

		switch (status)
		{
			case Status::Initialized:
				assert(prev_status == Status::Unknown);
				if (prev_status == Status::Unknown) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::Connecting:
				assert(prev_status == Status::Initialized);
				if (prev_status == Status::Initialized) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::Accepted:
				assert(prev_status == Status::Initialized);
				if (prev_status == Status::Initialized) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::Connected:
				assert(prev_status == Status::Accepted || prev_status == Status::Connecting);
				if (prev_status == Status::Accepted || prev_status == Status::Connecting) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::MetaExchange:
				assert(prev_status == Status::Connected);
				if (prev_status == Status::Connected) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::PrimaryKeyExchange:
				assert(prev_status == Status::MetaExchange);
				if (prev_status == Status::MetaExchange) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::SecondaryKeyExchange:
				assert(prev_status == Status::PrimaryKeyExchange);
				if (prev_status == Status::PrimaryKeyExchange) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::Authentication:
				assert(prev_status == Status::SecondaryKeyExchange);
				if (prev_status == Status::SecondaryKeyExchange) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::SessionInit:
				assert(prev_status == Status::Authentication);
				if (prev_status == Status::Authentication) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::Ready:
				assert(prev_status == Status::SessionInit);
				if (prev_status == Status::SessionInit) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			case Status::Disconnected:
				assert(prev_status != Status::Disconnected);
				if (prev_status != Status::Disconnected) m_PeerData.WithUniqueLock()->Status = status;
				else success = false;
				break;
			default:
				assert(false);
				success = false;
				break;
		}

		if (success && OnStatusChange(prev_status, status))
		{
			m_LastStatusChangeSteadyTime = Util::GetCurrentSteadyTime();
		}
		else
		{
			// If we fail to change the status disconnect as soon as possible
			LogErr(L"Failed to change status for peer %s to %d", GetPeerName().c_str(), status);
			SetDisconnectCondition(DisconnectCondition::GeneralFailure);
			success = false;
		}

		return success;
	}

	bool Peer::OnStatusChange(const Status old_status, const Status new_status)
	{
		switch (new_status)
		{
			case Status::MetaExchange:
			{
				if (InitializeKeyExchange())
				{
					if (GetConnectionType() == PeerConnectionType::Inbound)
					{
						// For inbound peers we initiate the handshake sequence
						if (!m_MessageProcessor.SendBeginHandshake())
						{
							SetDisconnectCondition(DisconnectCondition::ConnectError);
							return false;
						}
					}
					else if (GetConnectionType() == PeerConnectionType::Outbound)
					{
						// For outbound peers we send a noise message; this is specifically
						// to make it so that initiation of communications (first message sent)
						// will appear random to make life more difficult for traffic analyzers.
						// This is sent even if noise is disabled and max message size is 0,
						// in which case a noise message with 0 bytes is sent.
						const auto& settings = GetSettings();
						if (!SendNoise(settings.Noise.MinMessageSize,
									   settings.Noise.MaxMessageSize, GetHandshakeDelayPerMessage()))
						{
							SetDisconnectCondition(DisconnectCondition::SendError);
							return false;
						}
					}
				}
				else return false;

				break;
			}
			case Status::Ready:
			{
				// Key exchange data not needed anymore for now
				ReleaseKeyExchange();

				if (!m_KeyUpdate.SetStatus(KeyUpdate::Status::UpdateWait))
				{
					LogErr(L"Unable to set key update status for peer %s", GetPeerName().c_str());
					SetDisconnectCondition(DisconnectCondition::GeneralFailure);
					return false;
				}
				else
				{
					LogInfo(L"Peer %s is ready", GetPeerName().c_str());

					// We went to the ready state; this means the connection attempt succeeded
					// From now on concatenate messages when possible
					SetFlag(Flags::ConcatenateMessages, true);

					if (m_ConnectCallbacks)
					{
						ConnectDetails cdetails;
						cdetails.PeerLUID = GetLUID();
						cdetails.PeerUUID = GetPeerUUID();
						cdetails.IsAuthenticated = IsAuthenticated();
						cdetails.IsUsingGlobalSharedSecret = IsUsingGlobalSharedSecret();
						cdetails.IsRelayed = IsRelayed();

						ScheduleCallback([cdetails, dispatcher = std::move(m_ConnectCallbacks)]() mutable
						{
							dispatcher(cdetails.PeerLUID, cdetails);
						});
					}

					// Notify extenders of connected peer
					ProcessEvent(Event::Type::Connected);
				}

				break;
			}
			case Status::Disconnected:
			{
				// If state went to disconnected before we got to the ready state then
				// the connection attempt or handshake probably failed
				if (m_ConnectCallbacks && old_status < Status::Ready)
				{
					std::optional<std::error_code> error;

					if (UpdateIOStatus(0ms) && GetIOStatus().HasException())
					{
						error = std::error_code(GetIOStatus().GetErrorCode(), std::system_category());
					}

					if (!error) error = GetDisconnectConditionResultCode();

					ScheduleCallback([pluid = GetLUID(), cdetails = *error,
									 dispatcher = std::move(m_ConnectCallbacks)]() mutable
					{
						dispatcher(pluid, cdetails);
					});
				}
				else if (m_DisconnectCallbacks && old_status < Status::Disconnected)
				{
					ScheduleCallback([pluid = GetLUID(), puuid = GetPeerUUID(),
									 dispatcher = std::move(m_DisconnectCallbacks)]() mutable
					{
						dispatcher(pluid, puuid);
					});
				}

				if (old_status == Status::Ready)
				{
					// Notify extenders of disconnected peer
					ProcessEvent(Event::Type::Disconnected);
				}

				break;
			}
			default:
			{
				break;
			}
		}

		return true;
	}

	void Peer::ScheduleCallback(Callback<void()>&& callback) noexcept
	{
		m_PeerManager.SchedulePeerCallback(m_ThreadPoolKey, std::move(callback));
	}

	void Peer::SetAuthenticated(const bool auth) noexcept
	{
		// Should be in authentication state or later
		assert(GetStatus() >= Status::Authentication);

		// Should have a peer UUID by now
		assert(GetPeerUUID().IsValid());

		m_PeerData.WithUniqueLock()->IsAuthenticated = auth;
		if (auth)
		{
			LogInfo(L"Peer %s is authenticated with UUID %s",
					GetPeerName().c_str(), GetPeerUUID().GetString().c_str());
		}
		else
		{
			LogWarn(L"Peer %s with UUID %s is NOT authenticated",
					GetPeerName().c_str(), GetPeerUUID().GetString().c_str());
		}
	}

	Result<> Peer::SendNoise(const Size minsize, const Size maxsize, const std::chrono::milliseconds delay) noexcept
	{
		try
		{
			const auto data_size = static_cast<Size>(std::abs(Random::GetPseudoRandomNumber(minsize, maxsize)));

			if (GetAvailableNoiseSendBufferSize() >= data_size)
			{
				auto data = Random::GetPseudoRandomBytes(data_size);

				Dbg(L"Sending %u byte noise message to peer %s", data_size, GetPeerName().c_str());

				// Note that noise messages don't get compressed because the data
				// is random and doesn't get any smaller with compression; in addition
				// their length shouldn't be changed anyway
				return Send(MessageType::Noise, std::move(data), SendParameters::PriorityOption::Delayed, delay, false);
			}
			else return ResultCode::PeerSendBufferFull;
		}
		catch (...) {}

		return ResultCode::OutOfMemory;
	}

	Result<> Peer::SendNoise(const Size maxnum, const Size minsize, const Size maxsize) noexcept
	{
		const auto max = static_cast<Size>(Random::GetPseudoRandomNumber(0, maxnum));

		for (Size x = 0u; x < max; ++x)
		{
			if (auto result = SendNoise(minsize, maxsize); result.Failed())
			{
				return result;
			}
		}

		return ResultCode::Succeeded;
	}

	bool Peer::SendFromNoiseQueue(const Settings& settings) noexcept
	{
		Size num{ 0 };

		// Send queued noise as long as we have items
		auto noiseitm = m_NoiseQueue.GetQueuedNoise();
		while (noiseitm)
		{
			++num;

			if (const auto result = SendNoise(noiseitm->MinSize, noiseitm->MaxSize); result.Succeeded())
			{
				// Check if the processing limit has been reached; in that case break
				// so that we'll return to continue processing later. This prevents 
				// this peer from hoarding all the processing capacity.
				if (num < settings.Local.Concurrency.WorkerThreadsMaxBurst)
				{
					noiseitm = m_NoiseQueue.GetQueuedNoise();
				}
				else break;
			}
			else
			{
				// If send buffer is full we may have too much noise queued up;
				// this can occur, for example, when the upload bandwidth is
				// completely maxed out; this is not a fatal error
				if (result == ResultCode::PeerSendBufferFull)
				{
					LogDbg(L"Failed to send noise message to peer %s; send buffer is full", GetPeerName().c_str());
					break;
				}
				else return false;
			}
		}

		return true;
	}

	Result<> Peer::Send(Message&& msg, const SendParameters::PriorityOption priority,
						const std::chrono::milliseconds delay, SendCallback&& callback) noexcept
	{
		assert(msg.IsValid());

		Size msg_size{ 0 };

		if (msg.GetMessageType() == MessageType::ExtenderCommunication)
		{
			msg_size = msg.GetMessageData().GetSize();
		}

		auto result = m_SendQueues.AddMessage(std::move(msg), priority, delay, std::move(callback));
		if (result.Succeeded())
		{
			if (msg_size > 0) m_PeerData.WithUniqueLock()->ExtendersBytesSent += msg_size;

			switch (GetGateType())
			{
				case GateType::Socket:
					if (!GetSocket<Socket>().GetEvent().Set())
					{
						LogErr(L"Failed to set event on socket (%s)", GetLastSysErrorString().c_str());
					}
					break;
				case GateType::RelaySocket:
					if (!GetSocket<Relay::Socket>().GetReceiveEvent().Set())
					{
						LogErr(L"Failed to set event on socket (%s)", GetLastSysErrorString().c_str());
					}
					break;
				default:
					break;
			}
		}

		return result;
	}

	Result<> Peer::Send(const MessageType msgtype, Buffer&& buffer, const SendParameters::PriorityOption priority,
						const std::chrono::milliseconds delay, const bool compress, SendCallback&& callback) noexcept
	{
		if (buffer.GetSize() <= Message::MaxMessageDataSize)
		{
			return Send(Message(MessageOptions(msgtype, std::move(buffer), compress)), priority, delay, std::move(callback));
		}
		else
		{
			LogDbg(L"Message (type %u) from peer %s is too large (%zu bytes too much); will send in fragments",
				   msgtype, GetPeerName().c_str(), buffer.GetSize() - Message::MaxMessageDataSize);

			BufferView snd_buf = buffer;
			auto fragment = MessageFragmentType::Unknown;

			while (true)
			{
				SendCallback snd_callback;

				auto snd_size = snd_buf.GetSize();
				if (snd_size > Message::MaxMessageDataSize)
				{
					snd_size = Message::MaxMessageDataSize;

					if (fragment == MessageFragmentType::Unknown)
					{
						fragment = MessageFragmentType::PartialBegin;
					}
					else fragment = MessageFragmentType::Partial;
				}
				else
				{
					fragment = MessageFragmentType::PartialEnd;
					snd_callback = std::move(callback);
				}

				try
				{
					auto result = Send(Message(MessageOptions(msgtype, snd_buf.GetFirst(snd_size), compress, fragment)),
									   priority, delay, std::move(snd_callback));
					if (result.Succeeded())
					{
						snd_buf.RemoveFirst(snd_size);
						if (snd_buf.IsEmpty())
						{
							return ResultCode::Succeeded;
						}
					}
					else
					{
						return result;
					}
				}
				catch (...)
				{
					// Likely out of memory
					return ResultCode::OutOfMemory;
				}
			}
		}

		return ResultCode::Failed;
	}

	Result<> Peer::SendWithRandomDelay(const MessageType msgtype, Buffer&& buffer,
									   const std::chrono::milliseconds maxdelay) noexcept
	{
		const auto delay = std::chrono::milliseconds(Random::GetPseudoRandomNumber(0, maxdelay.count()));

		return Send(msgtype, std::move(buffer), SendParameters::PriorityOption::Delayed, delay);
	}

	bool Peer::SendFromQueues(const Settings& settings)
	{
		// If the send buffer isn't empty yet
		if (!m_SendBuffer.IsEmpty())
		{
			const auto buf_size = m_SendBuffer.GetSize();

			if (!Gate::Send(m_SendBuffer))
			{
				return false;
			}
			else if (!m_SendBuffer.IsEmpty())
			{
				if (buf_size == m_SendBuffer.GetSize())
				{
					// Nothing was sent; buffer probably full so don't come back too soon
					SetFastRequeue(false);
				}

				// If we weren't able to send (all) data we'll try again later
				return true;
			}
			else m_SendBuffer.ResetEvent();
		}

		// If the send buffer is empty get more messages from the send queues

		Size num{ 0 };
		Buffer sndbuf;

		while (m_SendQueues.HaveMessages())
		{
			auto msg = MessageTransport(m_MessageTransportDataSizeSettings, settings);

			// Get the last key we have available to encrypt messages;
			// if we don't have one an autogen key will be used if it's allowed
			const auto& [symkey, nonce] = m_Keys.GetEncryptionKeyAndNonce(msg.GetMessageNonceSeed(),
																		  GetConnectionType(), IsAutoGenKeyAllowed());
			if (symkey == nullptr)
			{
				LogErr(L"Could not get symmetric key to encrypt message");
				return false;
			}

			Buffer msgbuf;

			const auto& [success, nummsg] = m_SendQueues.GetMessages(msgbuf, *symkey, IsFlagSet(Flags::ConcatenateMessages));
			if (!success) return false;

			if (nummsg > 0)
			{
				num += nummsg;

				// Should have data at this point
				assert(!msgbuf.IsEmpty());

				msg.SetMessageData(std::move(msgbuf));

				// If we should use the message counter
				{
					auto counter = GetNextLocalMessageCounter();
					if (counter.has_value())
					{
						msg.SetMessageCounter(counter.value());
					}
				}

				// Add a random data prefix if needed
				{
					msg.SetCurrentRandomDataPrefixLength(m_NextLocalRandomDataPrefixLength);

					UInt16 nrdplen{ 0 };
					if (settings.Message.MaxRandomDataPrefixSize > 0)
					{
						nrdplen = static_cast<UInt16>(Random::GetPseudoRandomNumber(settings.Message.MinRandomDataPrefixSize,
																					settings.Message.MaxRandomDataPrefixSize));
					}

					// Tell the peer what the random data prefix length
					// will be with the next message
					msg.SetNextRandomDataPrefixLength(nrdplen);

					// Save the random data prefix length for use
					// with the next message so that we send
					// what the peer will expect
					m_NextLocalRandomDataPrefixLength = nrdplen;
				}

				if (msg.IsValid() && msg.Write(sndbuf, *symkey, nonce))
				{
					if (!Gate::Send(sndbuf))
					{
						return false;
					}
					else if (sndbuf.GetSize() != 0)
					{
						// If we weren't able to send all
						// data we'll try again later
						m_SendBuffer = std::move(sndbuf);
						m_SendBuffer.SetEvent();
						break;
					}
				}
				else
				{
					LogErr(L"Could not write message");
					return false;
				}

				// Check if the processing limit has been reached; in that case break
				// so that we'll return to continue processing later. This prevents 
				// this peer from hoarding all the processing capacity.
				if (num >= settings.Local.Concurrency.WorkerThreadsMaxBurst) break;
			}
			else break;
		}

		return true;
	}

	bool Peer::ProcessFromReceiveQueues(const Settings& settings)
	{
		Size num{ 0 };

		while (m_ReceiveQueues.HaveMessages())
		{
			if (m_RateLimits.CanAdd<MessageRateLimits::Type::ExtenderCommunicationReceive>(m_ReceiveQueues.GetNextMessageSize()))
			{
				auto msg = m_ReceiveQueues.GetMessage();
				if (!ProcessMessage(msg)) return false;

				++num;

				// Check if the processing limit has been reached; in that case break
				// and set the event again so that we'll return to continue processing later.
				// This prevents this socket from hoarding all the processing capacity.
				if (num >= settings.Local.Concurrency.WorkerThreadsMaxBurst)
				{
					break;
				}
			}
			else
			{
				break;
			}
		}

		DbgInvoke([&]()
		{
			if (num > 0)
			{
				LogDbg(L"Processed %zu messages from receive queue", num);
			}
		});

		return true;
	}

	bool Peer::ReceiveAndProcess(const Settings& settings)
	{
		// Empty receive queues first
		if (!ProcessFromReceiveQueues(settings)) return false;

		// Receive queues need to be empty
		if (m_ReceiveQueues.HaveMessages())
		{
			// Too many messages in flight; don't come back too soon
			SetFastRequeue(false);

			return true;
		}

		m_ReceiveBuffer.ResetEvent();

		// Check if there's a message in the receive buffer
		MessageTransportCheck msgchk = MessageTransport::Peek(m_NextPeerRandomDataPrefixLength,
															  m_MessageTransportDataSizeSettings, m_ReceiveBuffer);

		// If there was no data in the buffer or an incomplete message, check if there's
		// data to receive from the peer, otherwise proceed to process what we have
		if (msgchk != MessageTransportCheck::CompleteMessage)
		{
			if (Receive(m_ReceiveBuffer))
			{
				// Check if we have a complete message now
				msgchk = MessageTransport::Peek(m_NextPeerRandomDataPrefixLength,
												m_MessageTransportDataSizeSettings, m_ReceiveBuffer);
			}
			else return false;
		}

		switch (msgchk)
		{
			case MessageTransportCheck::CompleteMessage:
			{
				Size num{ 0 };
				Buffer msgbuf;

				// Get as many completed messages from the receive buffer
				// as possible and process them
				while (true)
				{
					if (MessageTransport::GetFromBuffer(m_NextPeerRandomDataPrefixLength,
														m_MessageTransportDataSizeSettings,
														m_ReceiveBuffer, msgbuf) == MessageTransportCheck::CompleteMessage)
					{
						const auto& [retval, nump, nrndplen] = ProcessMessageTransport(msgbuf, settings);
						if (retval)
						{
							num += nump;
							m_NextPeerRandomDataPrefixLength = nrndplen;

							// Check if the processing limit has been reached; in that case break
							// and set the event again so that we'll return to continue processing later.
							// This prevents this socket from hoarding all the processing capacity.
							if (num >= settings.Local.Concurrency.WorkerThreadsMaxBurst)
							{
								if (!m_ReceiveBuffer.IsEmpty()) m_ReceiveBuffer.SetEvent();
								return true;
							}
						}
						else
						{
							// Error occured
							return false;
						}
					}
					else
					{
						// No complete message anymore;
						// we'll come back later
						return true;
					}
				}
				break;
			}
			case MessageTransportCheck::NotEnoughData:
			{
				return true;
			}
			case MessageTransportCheck::TooMuchData:
			{
				LogErr(L"Peer %s sent a message that's too large (or contains bad data)", GetPeerName().c_str());
				UpdateReputation(Access::IPReputationUpdate::DeteriorateSevere);
				break;
			}
			default:
			{
				// Shouldn't get here
				assert(false);
				break;
			}
		}

		return false;
	}

	std::tuple<bool, Size, UInt16> Peer::ProcessMessageTransport(const BufferView msgbuf, const Settings& settings)
	{
		const auto nonce_seed = MessageTransport::GetNonceSeedFromBuffer(msgbuf);
		if (nonce_seed)
		{
			// Try to decrypt message using all the keys we have;
			// we'll start with the (first) latest key available
			auto keynum = 0u;

			while (true)
			{
				const auto& [symkey, nonce] = m_Keys.GetDecryptionKeyAndNonce(keynum, *nonce_seed,
																			  GetConnectionType(), IsAutoGenKeyAllowed());

				// The next time we'll try the next
				// key we have until we run out
				++keynum;

				if (symkey != nullptr)
				{
					auto msg = MessageTransport(m_MessageTransportDataSizeSettings, settings);

					Dbg(L"Receive buffer: %d bytes - %s", msgbuf.GetSize(), Util::ToBase64(msgbuf)->c_str());

					const auto& [retval, retry] = msg.Read(msgbuf, *symkey, nonce);
					if (retval && msg.IsValid())
					{
						// MessageTransport counter should match the expected message counter
						// if we have one already; this is to protect against replay attacks
						const auto counter = GetNextPeerMessageCounter();

						Dbg(L"MessageTransport counters %u/%u",
							counter.has_value() ? counter.value() : 0, msg.GetMessageCounter());

						if (counter.has_value() && (counter.value() != msg.GetMessageCounter()))
						{
							// Unexpected message counter
							LogErr(L"Peer %s sent a message with an invalid counter value %u (%u expected)",
								   GetPeerName().c_str(), msg.GetMessageCounter(), counter.value());
							break;
						}
						else if (std::chrono::abs(Util::GetCurrentSystemTime() - msg.GetMessageTime()) >
								 settings.Message.AgeTolerance)
						{
							// Message should not be too old or too far into the future
							LogErr(L"Peer %s sent a message outside time tolerance (%d seconds)",
								   GetPeerName().c_str(), settings.Message.AgeTolerance);
							break;
						}
						else
						{
							const auto retval2 = ProcessMessages(msg.GetMessageData(), *symkey);
							return std::make_tuple(retval2.first, retval2.second, msg.GetNextRandomDataPrefixLength());
						}
					}
					else if (!msg.IsValid() && !retry)
					{
						// Unrecognized message
						LogErr(L"Peer %s sent an invalid message", GetPeerName().c_str());
						break;
					}
				}
				else
				{
					// We have no more keys to try
					LogErr(L"Could not read message using available keys");
					break;
				}
			}
		}
		else LogErr(L"Could not get nonce seed from message buffer");

		// Unrecognized or invalid message; this is a fatal problem and may be an attack
		// so the peer should get disconnected asap
		UpdateReputation(Access::IPReputationUpdate::DeteriorateSevere);

		return std::make_tuple(false, Size{ 0 }, UInt16{ 0 });
	}

	std::pair<bool, Size> Peer::ProcessMessages(BufferView buffer, const Crypto::SymmetricKeyData& symkey)
	{
		auto success = true;
		auto invalid_msg = false;
		Size num{ 0 };

		// For as long as there are messages in the buffer
		while (!buffer.IsEmpty())
		{
			const auto msgbuf = Message::GetFromBuffer(buffer);
			if (msgbuf)
			{
				Message msg;
				if (msg.Read(msgbuf, symkey) && msg.IsValid())
				{
					++num;

					// Noise messages get dropped immediately
					if (msg.GetMessageType() == MessageType::Noise)
					{
						Dbg(L"Dropping noise message from peer %s", GetPeerName().c_str());
					}
					else
					{
						if (!QueueOrProcessMessage(std::move(msg)))
						{
							success = false;
							break;
						}
					}
				}
				else
				{
					// Couldn't read or validate message
					invalid_msg = true;
					break;
				}
			}
			else
			{
				// Couldn't get any messages from the buffer
				// (may contain bad data)
				invalid_msg = true;
				break;
			}
		}

		if (invalid_msg)
		{
			// Unrecognized message; this is a fatal problem and may be an attack
			// so the peer should get disconnected asap
			LogErr(L"Peer %s sent an invalid message", GetPeerName().c_str());

			UpdateReputation(Access::IPReputationUpdate::DeteriorateSevere);
			success = false;
		}

		DbgInvoke([&]()
		{
			if (num > 1)
			{
				LogDbg(L"Processed %zu messages from one transport", num);
			}
		});

		return std::make_pair(success, num);
	}

	bool Peer::QueueOrProcessMessage(Message&& msg) noexcept
	{
		const auto msg_size = msg.GetMessageData().GetSize();

		// Messages have to be processed in the order in which they are received, so
		// if the ReceiveQueues aren't empty then the messages need to go to the back
		// of the queue even if there's room in the receive rate limit
		if (!m_ReceiveQueues.HaveMessages() &&
			m_RateLimits.CanAdd<MessageRateLimits::Type::ExtenderCommunicationReceive>(msg_size))
		{
			// Process immediately
			return ProcessMessage(msg);
		}
		else
		{
			// Rate limit would get exceeded so add to the queue for later processing
			if (m_ReceiveQueues.AddMessage(std::move(msg)))
			{
				// Too many messages in flight; don't come back too soon
				SetFastRequeue(false);

				return true;
			}
		}

		return false;
	}

	bool Peer::ProcessMessage(Message& msg)
	{
		auto msg_sequence_error = false;
		auto msg_complete = false;

		switch (msg.GetMessageFragmentType())
		{
			case MessageFragmentType::Complete:
			{
				msg_complete = true;
				break;
			}
			case MessageFragmentType::PartialBegin:
			{
				if (!m_MessageFragments.has_value())
				{
					LogDbg(L"Message fragment from peer %s (sequence begin)", GetPeerName().c_str());

					m_MessageFragments = MessageDetails(*this, msg.GetMessageType(), msg.GetExtenderUUID(),
														msg.MoveMessageData());
					return true;
				}
				else msg_sequence_error = true;

				break;
			}
			case MessageFragmentType::Partial:
			{
				if (m_MessageFragments.has_value())
				{
					if (m_MessageFragments->GetMessageType() == msg.GetMessageType() &&
						m_MessageFragments->GetExtenderUUID() == msg.GetExtenderUUID())
					{
						LogDbg(L"Message fragment from peer %s (sequence)", GetPeerName().c_str());

						return m_MessageFragments->AddToMessageData(msg.GetMessageData());
					}
					else msg_sequence_error = true;
				}
				else msg_sequence_error = true;

				break;
			}
			case MessageFragmentType::PartialEnd:
			{
				if (m_MessageFragments.has_value())
				{
					if (m_MessageFragments->GetMessageType() == msg.GetMessageType() &&
						m_MessageFragments->GetExtenderUUID() == msg.GetExtenderUUID())
					{
						LogDbg(L"Message fragment from peer %s (sequence end)", GetPeerName().c_str());

						if (m_MessageFragments->AddToMessageData(msg.GetMessageData()))
						{
							msg_complete = true;
						}
						else return false;
					}
					else msg_sequence_error = true;
				}
				else msg_sequence_error = true;

				break;
			}
			default:
			{
				// Shouldn't get here
				assert(false);
				return false;
			}
		}

		if (msg_sequence_error)
		{
			// Unexpected message fragment; this could be an attack
			LogErr(L"Message fragment from peer %s was out of sequence", GetPeerName().c_str());

			UpdateReputation(Access::IPReputationUpdate::DeteriorateSevere);
		}
		else if (msg_complete)
		{
			const auto result = std::invoke([&]()
			{
				if (m_MessageFragments.has_value())
				{
					const auto retval = ProcessMessage(std::move(*m_MessageFragments));

					// Reset so we can begin again
					m_MessageFragments.reset();

					return retval;
				}
				else return ProcessMessage(MessageDetails(*this, msg.GetMessageType(), msg.GetExtenderUUID(),
														  msg.MoveMessageData()));
			});

			if (!result.Handled)
			{
				// Message wasn't recognized; this is a fatal problem and may be an attack
				// so the peer should get disconnected asap
				LogErr(L"Message from peer %s was not recognized", GetPeerName().c_str());
				return false;
			}
			else if (!result.Success && GetStatus() < Status::Ready)
			{
				// Message wasn't handled successfully and we're probably in handshake 
				// state; this is a fatal problem and may be an attack so the peer
				// should get disconnected asap
				LogErr(L"Message from peer %s was not handled successfully", GetPeerName().c_str());
				return false;
			}

			return true;
		}

		return false;
	}

	bool Peer::CheckAndProcessKeyUpdate() noexcept
	{
		if (m_KeyUpdate.ShouldUpdate())
		{
			if (!m_KeyUpdate.BeginKeyUpdate())
			{
				LogErr(L"Couldn't initiate key update for peer %s; will disconnect", GetPeerName().c_str());
				return false;
			}
		}
		else if (m_KeyUpdate.UpdateTimedOut())
		{
			LogErr(L"Key update for peer %s timed out; will disconnect", GetPeerName().c_str());
			return false;
		}

		return true;
	}

	const LocalAlgorithms& Peer::GetSupportedAlgorithms() const noexcept
	{
		return GetSettings().Local.SupportedAlgorithms;
	}

	String Peer::GetLocalName() const noexcept
	{
		return Util::FormatString(L"%s (LUID %llu)", GetLocalEndpoint().GetString().c_str(), GetLUID());
	}

	String Peer::GetPeerName() const noexcept
	{
		return Util::FormatString(L"%s (LUID %llu)", GetPeerEndpoint().GetString().c_str(), GetLUID());
	}

	bool Peer::HasPendingEvents() noexcept
	{
		if (HasReceiveEvents() || HasSendEvents() ||
			m_NoiseQueue.IsQueuedNoiseReady() || m_KeyUpdate.HasEvents() ||
			(NeedsExtenderUpdate() && IsReady()))
		{
			return true;
		}

		return false;
	}

	void Peer::SetLUID() noexcept
	{
		m_PeerData.WithUniqueLock([&](Data& peer_data)
		{
			if (peer_data.LUID == 0)
			{
				peer_data.LUID = MakeLUID(GetPeerEndpoint(), reinterpret_cast<std::uintptr_t>(this));
			}
		});
	}

	PeerLUID Peer::MakeLUID(const IPEndpoint& endpoint, const UInt64 unique_data) noexcept
	{
		assert(endpoint != IPEndpoint());

		struct HashData final
		{
			BinaryIPAddress IP;
			RelayPort RelayPort{ 0 };
			UInt64 UniqueData{ 0 };
			UInt16 Port{ 0 };
			RelayHop RelayHop{ 0 };
		};

		HashData data;
		MemInit(&data, sizeof(data)); // Needed to zero out padding bytes for consistent hash
		data.IP = endpoint.GetIPAddress().GetBinary();
		data.Port = endpoint.GetPort();
		data.RelayPort = endpoint.GetRelayPort();
		data.RelayHop = endpoint.GetRelayHop();
		data.UniqueData = unique_data;

		return Util::GetNonPersistentHash(BufferView(reinterpret_cast<const Byte*>(&data), sizeof(data)));
	}

	void Peer::OnConnecting() noexcept
	{
		Gate::OnConnecting();

		SetLUID();

		DiscardReturnValue(SetStatus(Status::Connecting));
	}

	void Peer::OnAccept() noexcept
	{
		Gate::OnAccept();

		SetLUID();

		DiscardReturnValue(SetStatus(Status::Accepted));
	}

	bool Peer::OnConnect() noexcept
	{
		if (Gate::OnConnect())
		{
			m_PeerData.WithUniqueLock([&](Data& peer_data) noexcept
			{
				peer_data.Cached.ConnectedSteadyTime = GetConnectedSteadyTime();

				peer_data.Cached.BytesReceived = GetBytesReceived();
				peer_data.Cached.BytesSent = GetBytesSent();

				peer_data.Cached.LocalEndpoint = GetLocalEndpoint();
				peer_data.Cached.PeerEndpoint = GetPeerEndpoint();
			});

			// Get a random session ID
			const auto sid = Crypto::GetCryptoRandomNumber();
			if (sid)
			{
				m_PeerData.WithUniqueLock()->LocalSessionID = *sid;

				LogDbg(L"Generated random session ID %llu for peer %s",
					   m_PeerData.WithSharedLock()->LocalSessionID, GetPeerName().c_str());

				return SetStatus(Status::Connected);
			}
			else LogErr(L"Failed to generate random session ID for peer %s", GetPeerName().c_str());
		}

		return false;
	}

	void Peer::OnClose() noexcept
	{
		Gate::OnClose();

		DiscardReturnValue(SetStatus(Status::Disconnected));
	}

	bool Peer::SetAlgorithms(const Algorithm::Hash ha, const Algorithm::Asymmetric paa,
							 const Algorithm::Asymmetric saa, const Algorithm::Symmetric sa,
							 const Algorithm::Compression ca) noexcept
	{
		const auto& algorithms = GetSupportedAlgorithms();

		if (!Crypto::HasAlgorithm(algorithms.Hash, ha))
		{
			LogErr(L"Unsupported hash algorithm requested by peer %s", GetPeerName().c_str());
			return false;
		}
		else if (!Crypto::HasAlgorithm(algorithms.PrimaryAsymmetric, paa))
		{
			LogErr(L"Unsupported primary asymmetric algorithm requested by peer %s", GetPeerName().c_str());
			return false;
		}
		else if (!Crypto::HasAlgorithm(algorithms.SecondaryAsymmetric, saa))
		{
			LogErr(L"Unsupported secondary asymmetric algorithm requested by peer %s", GetPeerName().c_str());
			return false;
		}
		else if (!Crypto::HasAlgorithm(algorithms.Symmetric, sa))
		{
			LogErr(L"Unsupported symmetric algorithm requested by peer %s", GetPeerName().c_str());
			return false;
		}
		else if (!Crypto::HasAlgorithm(algorithms.Compression, ca))
		{
			LogErr(L"Unsupported compression algorithm requested by peer %s", GetPeerName().c_str());
			return false;
		}

		m_Algorithms.Hash = ha;
		m_Algorithms.PrimaryAsymmetric = paa;
		m_Algorithms.SecondaryAsymmetric = saa;
		m_Algorithms.Symmetric = sa;
		m_Algorithms.Compression = ca;

		return true;
	}

	SerializedIPEndpoint Peer::GetPublicIPEndpointToReport() const noexcept
	{
		// Only for normal connections because the reported
		// IPs might not be accurate for relays because there
		// are other peers in between
		if (!IsRelayed())
		{
			return SerializedIPEndpoint{ GetPeerEndpoint() };
		}

		// For relays we send an empty endpoint (all zeroes)
		return SerializedIPEndpoint{};
	}

	bool Peer::AddReportedPublicIPEndpoint(const SerializedIPEndpoint& pub_endpoint) noexcept
	{
		// Only for normal connections because the reported
		// IPs might not be accurate for relays because there
		// are other peers in between
		if (!IsRelayed())
		{
			IPAddress ip;
			if (IPAddress::TryParse(pub_endpoint.IPAddress, ip))
			{
				// Public IP reported by peer should be the same
				// family type as the address used for this connection
				if (ip.GetFamily() == GetLocalIPAddress().GetFamily())
				{
					const auto trusted = IsUsingGlobalSharedSecret() || IsAuthenticated();
					GetPeerManager().AddReportedPublicIPEndpoint(IPEndpoint(ip, pub_endpoint.Port), GetPeerEndpoint(),
																 GetConnectionType(), trusted);
					return true;
				}
			}
		}
		else
		{
			// Should be empty (all zeroes)
			return (pub_endpoint == SerializedIPEndpoint{});
		}

		LogErr(L"Couldn't add public IP endpoint reported by peer %s", GetPeerName().c_str());

		return false;
	}

	const ProtectedBuffer& Peer::GetGlobalSharedSecret() const noexcept
	{
		// If we have a specific global shared secret for this peer use it,
		// otherwise return the default from settings
		if (m_GlobalSharedSecret) return *m_GlobalSharedSecret;

		return GetSettings().Local.GlobalSharedSecret;
	}

	const ProtectedBuffer* Peer::GetPeerPublicKey() const noexcept
	{
		// Should already have PeerUUID
		assert(GetPeerUUID().IsValid());

		return GetAccessManager().GetPeerPublicKey(GetPeerUUID());
	}

	bool Peer::IsAutoGenKeyAllowed() const noexcept
	{
		// Auto generated keys are only allowed during the handshake when we
		// don't have a shared secret yet to derive a key. Note however
		// that we accept auto generated keys until the SecondaryKeyExchange
		// state in order to keep accepting messages that arrive late and
		// were encrypted using an autogen key.
		if (GetStatus() <= Status::SecondaryKeyExchange) return true;

		return false;
	}

	ResultCode Peer::GetDisconnectConditionResultCode() const noexcept
	{
		switch (GetDisconnectCondition())
		{
			case DisconnectCondition::TimedOutError:
				return ResultCode::TimedOut;
			case DisconnectCondition::DisconnectRequest:
				return ResultCode::Aborted;
			case DisconnectCondition::IPNotAllowed:
			case DisconnectCondition::PeerNotAllowed:
				return ResultCode::NotAllowed;
			default:
				break;
		}

		return ResultCode::Failed;
	}

	UInt8 Peer::SetLocalMessageCounter() noexcept
	{
		// Message counter begins with a pseudorandom value in the range 0-255
		m_LocalMessageCounter = static_cast<UInt8>(std::abs(
			Random::GetPseudoRandomNumber(std::numeric_limits<UInt8>::min(),
										  std::numeric_limits<UInt8>::max())
		));

		// Return next value
		auto rval = m_LocalMessageCounter.value();
		if (rval < std::numeric_limits<UInt8>::max())
		{
			++rval;
		}
		else rval = 0;

		return rval;
	}

	void Peer::SetPeerMessageCounter(const UInt8 counter) noexcept
	{
		m_PeerMessageCounter = counter;
	}

	std::optional<UInt8> Peer::GetNextLocalMessageCounter() noexcept
	{
		if (m_LocalMessageCounter.has_value())
		{
			if (*m_LocalMessageCounter < std::numeric_limits<UInt8>::max())
			{
				++(*m_LocalMessageCounter);
			}
			else m_LocalMessageCounter = 0;

			return *m_LocalMessageCounter;
		}

		return std::nullopt;
	}

	std::optional<UInt8> Peer::GetNextPeerMessageCounter() noexcept
	{
		if (m_PeerMessageCounter.has_value())
		{
			if (*m_PeerMessageCounter < std::numeric_limits<UInt8>::max())
			{
				++(*m_PeerMessageCounter);
			}
			else m_PeerMessageCounter = 0;

			return *m_PeerMessageCounter;
		}

		return std::nullopt;
	}

	void Peer::ProcessEvent(const Event::Type etype) noexcept
	{
		// Notify peer manager of new peer event
		GetPeerManager().OnPeerEvent(*this, Event(etype, GetLUID(), GetLocalUUID(), m_PeerPointer));

		// Notify extenders of new peer event
		ProcessEvent(GetPeerExtenderUUIDs().Current(), etype);
	}

	void Peer::ProcessEvent(const Vector<ExtenderUUID>& extuuids, const Event::Type etype) noexcept
	{
		GetExtenderManager().OnPeerEvent(extuuids, Event(etype, GetLUID(), GetLocalUUID(), m_PeerPointer));
	}

	MessageProcessor::Result Peer::ProcessMessage(MessageDetails&& msg) noexcept
	{
		if (IsReady() && msg.GetMessageType() == MessageType::ExtenderCommunication)
		{
			// Does the peer actually have the extender? This check might be overkill
			// since the peer probably has the extender otherwise we would not be
			// getting a message from it with that extender UUID. However, consider attacks.
			if (GetPeerExtenderUUIDs().HasExtender(msg.GetExtenderUUID()))
			{
				m_PeerData.WithUniqueLock()->ExtendersBytesReceived += msg.GetMessageData().GetSize();

				// Allow extenders to process received message
				const auto retval = GetExtenderManager().OnPeerMessage(Event(Event::Type::Message, GetLUID(),
																			 GetLocalUUID(), m_PeerPointer, std::move(msg)));
				if (!retval.first)
				{
					// Peer sent a message for an extender that's not running locally or
					// message arrived way too late (could be an attack)
					UpdateReputation(Access::IPReputationUpdate::DeteriorateModerate);
				}

				return MessageProcessor::Result{ .Handled = retval.first, .Success = retval.second };
			}
			else
			{
				LogErr(L"Received a message from peer %s for an invalid extender", GetPeerName().c_str());

				UpdateReputation(Access::IPReputationUpdate::DeteriorateModerate);

				return MessageProcessor::Result{ .Handled = false, .Success = false };
			}
		}
		else
		{
			const auto result = m_MessageProcessor.ProcessMessage(msg);
			if (!result.Handled)
			{
				// Unhandled message; the message may not have been recognized;
				// this could be an attack
				UpdateReputation(Access::IPReputationUpdate::DeteriorateSevere);
			}
			else if (!result.Success)
			{
				// Message was not successfully handled
				UpdateReputation(Access::IPReputationUpdate::DeteriorateModerate);
			}

			return result;
		}
	}

	void Peer::CheckAccess() noexcept
	{
		SetFlag(Flags::NeedsAccessCheck, false);

		// If peer is already flagged to be disconnected no use checking at this time
		if (ShouldDisconnect()) return;

		LogDbg(L"Checking access for peer %s", GetPeerName().c_str());

		// Check if peer IP is still allowed access
		const auto result = GetAccessManager().IsIPAllowed(GetPeerIPAddress(), Access::CheckType::All);
		if (!result || !(*result))
		{
			// Peer IP isn't allowed anymore; disconnect the peer as soon as possible
			SetDisconnectCondition(DisconnectCondition::IPNotAllowed);

			LogWarn(L"IP for peer %s is not allowed anymore; will disconnect peer", GetPeerName().c_str());
		}
		else
		{
			const auto status = GetStatus();

			// Should have a valid PeerUUID in the following states
			if (status == Status::Ready || status == Status::SessionInit)
			{
				// Check if peer UUID is still allowed access
				const auto result2 = GetAccessManager().IsPeerAllowed(GetPeerUUID());
				if (!result2 || !(*result2))
				{
					// Peer UUID isn't allowed anymore; disconnect the peer as soon as possible
					SetDisconnectCondition(DisconnectCondition::PeerNotAllowed);

					LogWarn(L"Peer UUID %s is not allowed anymore; will disconnect peer %s",
							GetPeerUUID().GetString().c_str(), GetPeerName().c_str());
				}
			}
		}
	}

	void Peer::OnUnhandledExtenderMessage(const ExtenderUUID& extuuid, const API::Extender::PeerEvent::Result& result) noexcept
	{
		if (!result.Handled)
		{
			// Message was not handled or unrecognized by an extender; if the peer is still
			// connected then disconnect it as soon as possible (may be misbehaving)
			LogErr(L"Message from peer %s was not recognized by extender with UUID %s",
				   GetPeerName().c_str(), extuuid.GetString().c_str());

			SetDisconnectCondition(DisconnectCondition::UnknownMessageError);

			UpdateReputation(Access::IPReputationUpdate::DeteriorateModerate);
		}
		else if (!result.Success)
		{
			// Message was recognized but wasn't handled successfully for some reason
			LogWarn(L"Message from peer %s was not successfully handled by extender with UUID %s",
					GetPeerName().c_str(), extuuid.GetString().c_str());

			UpdateReputation(Access::IPReputationUpdate::DeteriorateMinimal);
		}
	}
}
