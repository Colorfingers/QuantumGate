// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "AVExtender.h"

#include <Common\Util.h>
#include <Memory\BufferWriter.h>
#include <Memory\BufferReader.h>

using namespace QuantumGate::Implementation;
using namespace QuantumGate::Implementation::Memory;
using namespace std::literals;

namespace QuantumGate::AVExtender
{
	void Call::SetStatus(const CallStatus status) noexcept
	{
		m_Status = status;
		m_LastActiveSteadyTime = Util::GetCurrentSteadyTime();
	}

	const WChar* Call::GetStatusString() const noexcept
	{
		switch (GetStatus())
		{
			case CallStatus::NeedAccept:
				return L"Need accept";
			case CallStatus::WaitingForAccept:
				return L"Waiting for accept";
			case CallStatus::Connected:
				return L"Connected";
			case CallStatus::Disconnected:
				return L"Disconnected";
			case CallStatus::Error:
				return L"Failed";
			case CallStatus::Cancelled:
				return L"Cancelled";
			default:
				break;
		}

		return L"Unknown";
	}

	const bool Call::IsInCall() const noexcept
	{
		if (GetStatus() == CallStatus::NeedAccept ||
			GetStatus() == CallStatus::WaitingForAccept ||
			GetStatus() == CallStatus::Connected)
		{
			return true;
		}

		return false;
	}

	std::chrono::milliseconds Call::GetDuration() const noexcept
	{
		if (IsInCall())
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Util::GetCurrentSteadyTime() - GetStartSteadyTime());
		}

		return 0ms;
	}

	Extender::Extender(HWND hwnd) :
		QuantumGate::Extender(UUID, String(L"QuantumGate Audio/Video Extender"))
	{
		m_Window = hwnd;

		if (!SetStartupCallback(MakeCallback(this, &Extender::OnStartup)) ||
			!SetPostStartupCallback(MakeCallback(this, &Extender::OnPostStartup)) ||
			!SetPreShutdownCallback(MakeCallback(this, &Extender::OnPreShutdown)) ||
			!SetShutdownCallback(MakeCallback(this, &Extender::OnShutdown)) ||
			!SetPeerEventCallback(MakeCallback(this, &Extender::OnPeerEvent)) ||
			!SetPeerMessageCallback(MakeCallback(this, &Extender::OnPeerMessage)))
		{
			LogErr(GetName() + L": couldn't set one or more extender callbacks");
		}
	}

	Extender::~Extender()
	{}

	const bool Extender::OnStartup()
	{
		LogDbg(GetName() + L": starting...");

		m_ShutdownEvent.Reset();

		m_Thread = std::thread(Extender::WorkerThreadLoop, this);

		if (m_Window)
		{
			PostMessage(m_Window, static_cast<UINT>(WindowsMessage::ExtenderInit), 0, 0);
		}

		// Return true if initialization was successful, otherwise return false and
		// QuantumGate won't be sending this extender any notifications
		return true;
	}

	void Extender::OnPostStartup()
	{
		LogDbg(GetName() + L": running...");
	}

	void Extender::OnPreShutdown()
	{
		LogDbg(GetName() + L": will begin shutting down...");
	}

	void Extender::OnShutdown()
	{
		LogDbg(GetName() + L": shutting down...");

		// Set the shutdown event to notify thread that we're shutting down
		m_ShutdownEvent.Set();

		if (m_Thread.joinable())
		{
			// Wait for the thread to shut down
			m_Thread.join();
		}

		m_Peers.WithUniqueLock()->clear();

		if (m_Window)
		{
			PostMessage(m_Window, static_cast<UINT>(WindowsMessage::ExtenderDeinit), 0, 0);
		}
	}

	void Extender::OnPeerEvent(PeerEvent&& event)
	{
		String ev(L"Unknown");

		if (event.GetType() == PeerEventType::Connected)
		{
			ev = L"Connect";

			auto peer = std::make_unique<Peer>(event.GetPeerLUID());

			m_Peers.WithUniqueLock()->insert({ event.GetPeerLUID(), std::move(peer) });
		}
		else if (event.GetType() == PeerEventType::Disconnected)
		{
			ev = L"Disconnect";

			m_Peers.WithUniqueLock()->erase(event.GetPeerLUID());
		}

		LogInfo(GetName() + L": got peer event: %s, Peer LUID: %llu", ev.c_str(), event.GetPeerLUID());

		if (m_Window != nullptr)
		{
			// Must be deallocated in message handler
			Event* ev = new Event({ event.GetType(), event.GetPeerLUID() });

			// Using PostMessage because the current QuantumGate worker thread should NOT be calling directly to the UI;
			// only the thread that created the Window should do that, to avoid deadlocks
			if (!PostMessage(m_Window, static_cast<UINT>(WindowsMessage::PeerEvent), reinterpret_cast<WPARAM>(ev), 0))
			{
				delete ev;
			}
		}
	}

	const std::pair<bool, bool> Extender::OnPeerMessage(PeerEvent&& event)
	{
		assert(event.GetType() == PeerEventType::Message);

		auto handled = false;
		auto success = false;

		auto msgdata = event.GetMessageData();
		if (msgdata != nullptr)
		{
		}

		return std::make_pair(handled, success);
	}

	void Extender::WorkerThreadLoop(Extender* extender)
	{
		LogDbg(L"%s worker thread %u starting", extender->GetName().c_str(), std::this_thread::get_id());

		Util::SetCurrentThreadName(extender->GetName() + L" User Thread");

		// If the shutdown event is set quit the loop
		while (!extender->m_ShutdownEvent.IsSet())
		{
			extender->m_Peers.IfSharedLock([&](auto& peers)
			{
				for (auto it = peers.begin(); it != peers.end() && !extender->m_ShutdownEvent.IsSet(); ++it)
				{
				}
			});

			// Sleep for a while or until we have to shut down
			extender->m_ShutdownEvent.Wait(10ms);
		}

		LogDbg(L"%s worker thread %u exiting", extender->GetName().c_str(), std::this_thread::get_id());
	}

	const bool Extender::BeginCall(const PeerLUID pluid) noexcept
	{
		auto success = false;

		IfNotHasCall(pluid, [&](Call& call)
		{
			if (SendCallRequest(pluid, call))
			{
				SLogInfo(SLogFmt(FGBrightCyan) << L"Calling peer " << pluid << SLogFmt(Default));

				success = true;
			}
		});

		return success;
	}

	template<typename Func>
	const bool Extender::IfHasCall(const PeerLUID pluid, const CallID id, Func&& func)
	{
		auto success = false;

		m_Peers.WithSharedLock([&](auto& peers)
		{
			const auto it = peers.find(pluid);
			if (it != peers.end())
			{
				if (it->second->Call)
				{
					auto call = it->second->Call.WithUniqueLock();
					if (call->GetID() == id)
					{
						func(*call);
						success = true;
					}
				}

				if (!success)
				{
					LogErr(L"Call not found");
				}
			}
		});

		return success;
	}

	template<typename Func>
	const bool Extender::IfNotHasCall(const PeerLUID pluid, Func&& func)
	{
		auto success = false;

		m_Peers.WithSharedLock([&](auto& peers)
		{
			const auto it = peers.find(pluid);
			if (it != peers.end())
			{
				it->second->Call.WithUniqueLock([&](auto& call)
				{
					if (call.GetType() == CallType::None)
					{
						func(call);
						success = true;
					}
					else LogErr(L"Call already active");
				});
			}
		});

		return success;
	}

	const bool Extender::SendCallRequest(const PeerLUID pluid, Call& call)
	{
		const UInt16 msgtype = static_cast<const UInt16>(MessageType::CallRequest);

		BufferWriter writer(true);
		if (writer.WriteWithPreallocation(msgtype, call.GetID()))
		{
			if (SendMessageTo(pluid, writer.MoveWrittenBytes(), m_UseCompression).Succeeded())
			{
				call.SetType(CallType::Outgoing);
				call.SetStatus(CallStatus::WaitingForAccept);
				return true;
			}
			else LogErr(L"Could not send CallRequest message to peer");
		}
		else LogErr(L"Could not prepare CallRequest message for peer");

		call.SetStatus(CallStatus::Error);

		return false;
	}
}
