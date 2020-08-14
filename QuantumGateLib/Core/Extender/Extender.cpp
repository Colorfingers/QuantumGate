// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "Extender.h"
#include "..\Local.h"

namespace QuantumGate::Implementation::Core::Extender
{
	Extender::Extender(const ExtenderUUID& uuid, const String& name) noexcept :
		m_UUID(uuid), m_Name(name)
	{
		assert(uuid.GetType() == UUID::Type::Extender && !name.empty());
	}

	Result<API::Peer> Extender::ConnectTo(ConnectParameters&& params) noexcept
	{
		assert(IsRunning());

		return m_Local.load()->ConnectTo(std::move(params));
	}

	Result<std::pair<PeerLUID, bool>> Extender::ConnectTo(ConnectParameters&& params,
												ConnectCallback&& function) noexcept
	{
		assert(IsRunning());

		return m_Local.load()->ConnectTo(std::move(params), std::move(function));
	}

	Result<> Extender::DisconnectFrom(const PeerLUID pluid) noexcept
	{
		assert(IsRunning());

		return m_Local.load()->DisconnectFrom(pluid);
	}

	Result<> Extender::DisconnectFrom(const PeerLUID pluid, DisconnectCallback&& function) noexcept
	{
		assert(IsRunning());

		return m_Local.load()->DisconnectFrom(pluid, std::move(function));
	}

	Result<> Extender::DisconnectFrom(API::Peer& peer) noexcept
	{
		assert(IsRunning());

		return m_Local.load()->DisconnectFrom(peer);
	}

	Result<> Extender::DisconnectFrom(API::Peer& peer, DisconnectCallback&& function) noexcept
	{
		assert(IsRunning());

		return m_Local.load()->DisconnectFrom(peer, std::move(function));
	}

	Result<Size> Extender::SendMessage(const PeerLUID pluid, const BufferView& buffer,
									   const SendParameters& params, SendCallback&& callback) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->Send(GetUUID(), m_Running, m_Ready, pluid, buffer, params, std::move(callback));
	}

	Result<Size> Extender::SendMessage(API::Peer& peer, const BufferView& buffer,
									   const SendParameters& params, SendCallback&& callback) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->Send(GetUUID(), m_Running, m_Ready, peer, buffer, params, std::move(callback));
	}

	Result<> Extender::SendMessageTo(const PeerLUID pluid, Buffer&& buffer,
									 const SendParameters& params, SendCallback&& callback) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->SendTo(GetUUID(), m_Running, m_Ready, pluid, std::move(buffer), params, std::move(callback));
	}

	Result<> Extender::SendMessageTo(API::Peer& peer, Buffer&& buffer,
									 const SendParameters& params, SendCallback&& callback) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->SendTo(GetUUID(), m_Running, m_Ready, peer, std::move(buffer), params, std::move(callback));
	}

	Result<API::Peer> Extender::GetPeer(const PeerLUID pluid) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->GetPeer(pluid);
	}

	Result<Vector<PeerLUID>> Extender::QueryPeers(const PeerQueryParameters& params) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->QueryPeers(params);
	}

	Result<> Extender::QueryPeers(const PeerQueryParameters& params, Vector<PeerLUID>& pluids) const noexcept
	{
		assert(IsRunning());

		return m_Local.load()->QueryPeers(params, pluids);
	}

	void Extender::OnException() noexcept
	{
		LogErr(L"Unknown exception in extender '%s' (UUID: %s)",
			   GetName().c_str(), GetUUID().GetString().c_str());

		if (!m_Exception)
		{
			m_Exception = true;
			m_Local.load()->OnUnhandledExtenderException(GetUUID());
		}
	}

	void Extender::OnException(const std::exception& e) noexcept
	{
		LogErr(L"Exception in extender '%s' (UUID: %s) - %s",
			   GetName().c_str(), GetUUID().GetString().c_str(), Util::ToStringW(e.what()).c_str());

		if (!m_Exception)
		{
			m_Exception = true;
			m_Local.load()->OnUnhandledExtenderException(GetUUID());
		}
	}

	Result<PeerUUID> Extender::GetLocalUUID() const noexcept
	{
		assert(m_Local.load() != nullptr);

		if (m_Local.load() != nullptr) return m_Local.load()->GetUUID();

		return ResultCode::ExtenderHasNoLocalInstance;
	}

	Result<std::tuple<UInt, UInt, UInt, UInt>> Extender::GetLocalVersion() const noexcept
	{
		assert(m_Local.load() != nullptr);

		if (m_Local.load() != nullptr) return m_Local.load()->GetVersion();

		return ResultCode::ExtenderHasNoLocalInstance;
	}

	Result<std::pair<UInt, UInt>> Extender::GetLocalProtocolVersion() const noexcept
	{
		assert(m_Local.load() != nullptr);

		if (m_Local.load() != nullptr) return m_Local.load()->GetProtocolVersion();

		return ResultCode::ExtenderHasNoLocalInstance;
	}
}
