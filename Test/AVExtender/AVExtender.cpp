// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "AVExtender.h"
#include "AudioCompressor.h"

#include <Common\Util.h>
#include <Common\ScopeGuard.h>
#include <Memory\BufferWriter.h>
#include <Memory\BufferReader.h>

using namespace QuantumGate::Implementation;
using namespace QuantumGate::Implementation::Memory;
using namespace std::literals;

namespace QuantumGate::AVExtender
{
	Extender::Extender(HWND hwnd) :
		m_Window(hwnd),
		QuantumGate::Extender(UUID, String(L"QuantumGate Audio/Video Extender"))
	{
		if (!SetStartupCallback(MakeCallback(this, &Extender::OnStartup)) ||
			!SetPostStartupCallback(MakeCallback(this, &Extender::OnPostStartup)) ||
			!SetPreShutdownCallback(MakeCallback(this, &Extender::OnPreShutdown)) ||
			!SetShutdownCallback(MakeCallback(this, &Extender::OnShutdown)) ||
			!SetPeerEventCallback(MakeCallback(this, &Extender::OnPeerEvent)) ||
			!SetPeerMessageCallback(MakeCallback(this, &Extender::OnPeerMessage)))
		{
			LogErr(L"%s: couldn't set one or more extender callbacks", GetName().c_str());
		}
	}

	Extender::~Extender()
	{}

	void Extender::SetUseCompression(const bool compression) noexcept
	{
		m_Settings.UpdateValue([&](auto& settings)
		{
			settings.UseCompression = compression;
		});
	}

	void Extender::SetUseAudioCompression(const bool compression) noexcept
	{
		m_Settings.UpdateValue([&](auto& settings)
		{
			settings.UseAudioCompression = compression;
		});
	}

	void Extender::SetUseVideoCompression(const bool compression) noexcept
	{
		m_Settings.UpdateValue([&](auto& settings)
		{
			settings.UseVideoCompression = compression;
		});
	}

	void Extender::SetFillVideoScreen(const bool fill) noexcept
	{
		m_Settings.UpdateValue([&](auto& settings)
		{
			settings.FillVideoScreen = fill;
		});
	}

	bool Extender::OnStartup()
	{
		LogDbg(L"%s: starting...", GetName().c_str());

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
		LogDbg(L"%s: running...", GetName().c_str());
	}

	void Extender::OnPreShutdown()
	{
		LogDbg(L"%s: will begin shutting down...", GetName().c_str());

		StopAllCalls();
	}

	void Extender::OnShutdown()
	{
		LogDbg(L"%s: shutting down...", GetName().c_str());

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

	void Extender::OnPeerEvent(const PeerEvent& event)
	{
		String ev(L"Unknown");

		if (event.GetType() == PeerEvent::Type::Connected)
		{
			ev = L"Connect";

			auto peer = std::make_unique<Peer>(event.GetPeerLUID());
			peer->Call = std::make_shared<Call_ThS>(event.GetPeerLUID(), *this, m_Settings, m_AVSource);

			m_Peers.WithUniqueLock()->insert({ event.GetPeerLUID(), std::move(peer) });
		}
		else if (event.GetType() == PeerEvent::Type::Disconnected)
		{
			ev = L"Disconnect";

			m_Peers.WithUniqueLock()->erase(event.GetPeerLUID());

			m_CheckStopAVReaders = true;
		}

		LogInfo(L"%s: got peer event: %s, Peer LUID: %llu", GetName().c_str(), ev.c_str(), event.GetPeerLUID());

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

	QuantumGate::Extender::PeerEvent::Result Extender::OnPeerMessage(const PeerEvent& event)
	{
		assert(event.GetType() == PeerEvent::Type::Message);

		PeerEvent::Result result;

		auto msgdata = event.GetMessageData();
		if (msgdata != nullptr)
		{
			UInt16 mtype{ 0 };
			BufferReader rdr(*msgdata, true);

			// Get message type
			if (rdr.Read(mtype))
			{
				const auto type = static_cast<MessageType>(mtype);
				switch (type)
				{
					case MessageType::CallRequest:
					{
						result.Handled = true;
						result.Success = HandleCallRequest(event.GetPeerLUID());
						break;
					}
					case MessageType::CallAccept:
					{
						result.Handled = true;
						result.Success = HandleCallAccept(event.GetPeerLUID());
						break;
					}
					case MessageType::CallHangup:
					{
						result.Handled = true;
						result.Success = HandleCallHangup(event.GetPeerLUID());
						break;
					}
					case MessageType::CallDecline:
					{
						result.Handled = true;
						result.Success = HandleCallDecline(event.GetPeerLUID());
						break;
					}
					case MessageType::GeneralFailure:
					{
						result.Handled = true;
						result.Success = HandleCallFailure(event.GetPeerLUID());
						break;
					}
					case MessageType::AudioSample:
					{
						result.Handled = true;

						UInt64 timestamp{ 0 };
						Buffer fmt_buffer(sizeof(AudioFormatData));
						Buffer buffer;

						if (rdr.Read(timestamp, fmt_buffer, WithSize(buffer, GetMaximumMessageDataSize())))
						{
							const AudioFormatData* fmtdata = reinterpret_cast<const AudioFormatData*>(fmt_buffer.GetBytes());

							result.Success = HandleCallAudioSample(event.GetPeerLUID(), timestamp, *fmtdata, std::move(buffer));
						}
						break;
					}
					case MessageType::VideoSample:
					{
						result.Handled = true;
						
						UInt64 timestamp{ 0 };
						Buffer fmt_buffer(sizeof(VideoFormatData));
						Buffer buffer;

						if (rdr.Read(timestamp, fmt_buffer, WithSize(buffer, GetMaximumMessageDataSize())))
						{
							const VideoFormatData* fmtdata = reinterpret_cast<const VideoFormatData*>(fmt_buffer.GetBytes());

							result.Success = HandleCallVideoSample(event.GetPeerLUID(), timestamp, *fmtdata, std::move(buffer));
						}
						break;
					}
					default:
					{
						LogErr(L"Received unknown message type from peer %llu: %u", event.GetPeerLUID(), type);
						break;
					}
				}
			}
		}

		return result;
	}

	bool Extender::HandleCallRequest(const PeerLUID pluid)
	{
		auto success = false;

		Dbg(L"Received CallRequest message from %llu", pluid);

		IfGetCall(pluid, [&](auto& call)
		{
			if (call.IsDisconnected())
			{
				SLogInfo(SLogFmt(FGBrightCyan) << L"Incoming call from peer " << pluid << SLogFmt(Default));

				if (call.ProcessIncomingCall())
				{
					success = true;

					if (m_Window != nullptr)
					{
						// Must be deallocated in message handler
						CallAccept* ca = new CallAccept(pluid);

						if (!PostMessage(m_Window, static_cast<UINT>(WindowsMessage::AcceptIncomingCall),
										 reinterpret_cast<WPARAM>(ca), 0))
						{
							delete ca;
						}
					}
				}

				if (!success)
				{
					DiscardReturnValue(SendGeneralFailure(pluid));
				}
			}
		});

		if (!success)
		{
			LogErr(L"Couldn't process incoming call from peer %llu", pluid);
		}

		return success;
	}

	bool Extender::HandleCallAccept(const PeerLUID pluid)
	{
		auto success = false;

		Dbg(L"Received CallAccept message from %llu", pluid);

		auto call_ths = GetCall(pluid);
		if (call_ths != nullptr)
		{
			call_ths->WithUniqueLock([&](auto& call)
			{
				if (call.IsCalling())
				{
					SLogInfo(SLogFmt(FGBrightCyan) << L"Peer " << pluid << L" accepted call" << SLogFmt(Default));

					if (call.AcceptCall())
					{
						success = true;
					}

					if (!success)
					{
						DiscardReturnValue(SendGeneralFailure(pluid));
					}
				}
			});
		}

		if (!success)
		{
			LogErr(L"Couldn't accept outgoing call from peer %llu", pluid);
		}

		return success;
	}

	bool Extender::HandleCallHangup(const PeerLUID pluid)
	{
		auto success = false;

		Dbg(L"Received CallHangup message from %llu", pluid);

		IfGetCall(pluid, [&](auto& call)
		{
			SLogInfo(SLogFmt(FGBrightCyan) << L"Peer " << pluid << L" hung up" << SLogFmt(Default));

			if (call.IsInCall())
			{
				if (call.StopCall())
				{
					m_CheckStopAVReaders = true;
					success = true;
				}
			}
		});

		if (!success)
		{
			LogErr(L"Couldn't hangup call from peer %llu", pluid);
		}

		return success;
	}

	bool Extender::HandleCallDecline(const PeerLUID pluid)
	{
		auto success = false;

		Dbg(L"Received CallDecline message from %llu", pluid);

		IfGetCall(pluid, [&](auto& call)
		{
			SLogInfo(SLogFmt(FGBrightCyan) << L"Peer " << pluid << L" declined call" << SLogFmt(Default));

			if (call.IsCalling())
			{
				if (call.StopCall())
				{
					m_CheckStopAVReaders = true;
					success = true;
				}
			}
		});

		if (!success)
		{
			LogErr(L"Couldn't process call decline from peer %llu", pluid);
		}

		return success;
	}

	bool Extender::HandleCallFailure(const PeerLUID pluid)
	{
		auto success = false;

		Dbg(L"Received GeneralFailure message from %llu", pluid);

		IfGetCall(pluid, [&](auto& call)
		{
			SLogInfo(SLogFmt(FGBrightCyan) << L"Call with Peer " << pluid << SLogFmt(FGBrightRed)
					 << L" failed" << SLogFmt(Default));

			if (call.ProcessCallFailure())
			{
				m_CheckStopAVReaders = true;
				success = true;
			}
		});

		if (!success)
		{
			LogErr(L"Couldn't process call failure from peer %llu", pluid);
		}

		return success;
	}

	bool Extender::HandleCallAudioSample(const PeerLUID pluid, const UInt64 timestamp,
										 const AudioFormatData& fmtdata, Buffer&& sample)
	{
		auto success = false;

		IfGetCall(pluid, [&](auto& call)
		{
			if (call.IsInCall())
			{
				call.OnAudioInSample(fmtdata, timestamp, std::move(sample));
			}

			// Audio samples can still arrive after call has stopped;
			// here we'd need a check to make sure they don't arrive
			// too much later after the call (or no call) because
			// that might be abuse
			success = true;
		});

		return success;
	}

	bool Extender::HandleCallVideoSample(const PeerLUID pluid, const UInt64 timestamp,
										 const VideoFormatData& fmtdata, Buffer&& sample)
	{
		auto success = false;

		IfGetCall(pluid, [&](auto& call)
		{
			if (call.IsInCall())
			{
				call.OnVideoInSample(fmtdata, timestamp, std::move(sample));
			}

			// Video samples can still arrive after call has stopped;
			// here we'd need a check to make sure they don't arrive
			// too much later after the call (or no call) because
			// that might be abuse
			success = true;
		});

		return success;
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
					bool cancel_call{ false };
					CallType call_type{ CallType::None };

					it->second->Call->WithSharedLock([&](auto& call)
					{
						call_type = call.GetType();

						// If we've been waiting too long for a call to be
						// accepted cancel it
						if (call.IsCalling())
						{
							if (call.IsWaitExpired())
							{
								cancel_call = true;
							}
						}
					});

					if (cancel_call)
					{
						LogErr(L"Cancelling expired call %s peer %llu",
							(call_type == CallType::Incoming) ? L"from" : L"to", it->second->ID);

						DiscardReturnValue(it->second->Call->WithUniqueLock()->CancelCall());
					}
				}
			});

			if (extender->m_CheckStopAVReaders)
			{
				extender->m_CheckStopAVReaders = false;

				extender->CheckStopAVReaders();
			}

			// Sleep for a while or until we have to shut down
			extender->m_ShutdownEvent.Wait(1ms);
		}

		LogDbg(L"%s worker thread %u exiting", extender->GetName().c_str(), std::this_thread::get_id());
	}

	bool Extender::BeginCall(const PeerLUID pluid, const bool send_video, const bool send_audio) noexcept
	{
		auto success = false;

		auto call_ths = GetCall(pluid);
		if (call_ths != nullptr)
		{
			call_ths->WithUniqueLock([&](auto& call)
			{
				call.SetSendVideo(send_video);
				call.SetSendAudio(send_audio);

				success = call.BeginCall();
			});

			if (success)
			{
				if (send_audio) StartAudioSourceReader();
				if (send_video) StartVideoSourceReader();

				// Cancel call if we leave scope without success
				auto sg = MakeScopeGuard([&]() noexcept
				{
					DiscardReturnValue(call_ths->WithUniqueLock()->CancelCall());
				});

				if (SendCallRequest(pluid))
				{
					SLogInfo(SLogFmt(FGBrightCyan) << L"Calling peer " << pluid << SLogFmt(Default));

					sg.Deactivate();
				}
				else success = false;
			}
		}

		return success;
	}

	bool Extender::AcceptCall(const PeerLUID pluid) noexcept
	{
		auto success = false;

		auto call_ths = GetCall(pluid);
		if (call_ths != nullptr)
		{
			auto send_audio{ false };
			auto send_video{ false };

			call_ths->WithUniqueLock([&](auto& call)
			{
				// Should be in a call
				if (call.IsCalling())
				{
					send_audio = call.GetSendAudio();
					send_video = call.GetSendVideo();
					success = true;
				}
			});

			if (success)
			{
				if (send_audio) StartAudioSourceReader();
				if (send_video) StartVideoSourceReader();

				// Cancel call if we leave scope without success
				auto sg = MakeScopeGuard([&]() noexcept
				{
					DiscardReturnValue(call_ths->WithUniqueLock()->CancelCall());
					success = false;
				});

				if (call_ths->WithUniqueLock()->AcceptCall())
				{
					if (SendCallAccept(pluid))
					{
						SLogInfo(SLogFmt(FGBrightCyan) << L"Accepted call from peer " << pluid << SLogFmt(Default));

						sg.Deactivate();
					}
				}
			}

			if (!success)
			{
				// Try to let the peer know we couldn't accept the call
				DiscardReturnValue(SendGeneralFailure(pluid));
			}
		}

		return success;
	}

	bool Extender::DeclineCall(const PeerLUID pluid) noexcept
	{
		auto success = false;

		auto call_ths = GetCall(pluid);
		if (call_ths != nullptr)
		{
			call_ths->WithUniqueLock([&](auto& call)
			{
				// Should be in a call
				if (call.IsCalling())
				{
					success = call.CancelCall();
				}
			});

			if (success)
			{
				if (SendCallDecline(pluid))
				{
					SLogInfo(SLogFmt(FGBrightCyan) << L"Declined call from peer " << pluid << SLogFmt(Default));
				}
				else success = false;
			}
		}

		return success;
	}

	bool Extender::HangupCall(const PeerLUID pluid) noexcept
	{
		auto call_ths = GetCall(pluid);
		if (call_ths != nullptr)
		{
			return HangupCall(call_ths);
		}

		return false;
	}

	bool Extender::HangupCall(std::shared_ptr<Call_ThS>& call_ths) noexcept
	{
		auto success = false;
		auto ishangup = true;
		PeerLUID pluid{ 0 };

		call_ths->WithUniqueLock([&](auto& call)
		{
			pluid = call.GetPeerLUID();

			if (call.IsInCall())
			{
				success = call.StopCall();
			}
			else if (call.IsCalling())
			{
				ishangup = false;
				success = call.CancelCall();
			}
		});

		if (success)
		{
			if (ishangup)
			{
				if (SendCallHangup(pluid))
				{
					SLogInfo(SLogFmt(FGBrightCyan) << L"Hung up call to peer " << pluid << SLogFmt(Default));
				}
				else success = false;
			}
			else
			{
				SLogInfo(SLogFmt(FGBrightCyan) << L"Cancelled call to peer " << pluid << SLogFmt(Default));
			}
		}

		return success;
	}

	void Extender::StopAllCalls() noexcept
	{
		m_Peers.WithSharedLock([&](auto& peers)
		{
			for (auto it = peers.begin(); it != peers.end(); ++it)
			{
				Peer& peer = *it->second;

				auto call = peer.Call->WithUniqueLock();
				if (!call->IsDisconnected())
				{
					DiscardReturnValue(call->StopCall());
				}
			}
		});
	}

	void Extender::HangupAllCalls() noexcept
	{
		m_Peers.WithSharedLock([&](auto& peers)
		{
			for (auto it = peers.begin(); it != peers.end(); ++it)
			{
				DiscardReturnValue(HangupCall(it->second->Call));
			}
		});
	}

	template<typename Func>
	void Extender::IfGetCall(const PeerLUID pluid, Func&& func) noexcept(noexcept(func(std::declval<Call&>())))
	{
		m_Peers.WithSharedLock([&](auto& peers)
		{
			if (const auto it = peers.find(pluid); it != peers.end())
			{
				auto call = it->second->Call->WithUniqueLock();
				func(*call);
			}
			else LogErr(L"Peer not found");
		});
	}

	bool Extender::HaveActiveCalls() const noexcept
	{
		bool active_calls{ false };

		m_Peers.WithSharedLock([&](auto& peers)
		{
			for (auto it = peers.begin(); it != peers.end(); ++it)
			{
				Peer& peer = *it->second;

				if (!peer.Call->WithSharedLock()->IsDisconnected())
				{
					active_calls = true;
					return;
				}
			}
		});

		return active_calls;
	}

	std::shared_ptr<Call_ThS> Extender::GetCall(const PeerLUID pluid) const noexcept
	{
		std::shared_ptr<Call_ThS> call;

		m_Peers.WithSharedLock([&](auto& peers)
		{
			if (const auto it = peers.find(pluid); it != peers.end())
			{
				call = it->second->Call;
			}
		});

		return call;
	}

	Result<> Extender::SendSimpleMessage(const PeerLUID pluid, const MessageType type,
										 const SendParameters::PriorityOption priority, const BufferView data) const noexcept
	{
		const UInt16 msgtype = static_cast<const UInt16>(type);

		BufferWriter writer(true);
		if (writer.WriteWithPreallocation(msgtype, data))
		{
			SendParameters params;
			params.Compress = m_Settings->UseCompression;
			params.Priority = priority;

			return SendMessageTo(pluid, writer.MoveWrittenBytes(), params);
		}

		return AVResultCode::FailedPrepareMessage;
	}

	Result<> Extender::SendCallAudioSample(const PeerLUID pluid, const AudioFormat& afmt, const UInt64 timestamp,
										   const BufferView data, const bool compressed) const noexcept
	{
		constexpr UInt16 msgtype = static_cast<const UInt16>(MessageType::AudioSample);

		AudioFormatData fmt_data;
		fmt_data.NumChannels = afmt.NumChannels;
		fmt_data.SamplesPerSecond = afmt.SamplesPerSecond;
		fmt_data.BlockAlignment = afmt.BlockAlignment;
		fmt_data.BitsPerSample = afmt.BitsPerSample;
		fmt_data.AvgBytesPerSecond = afmt.AvgBytesPerSecond;
		fmt_data.Compressed = compressed;

		BufferWriter writer(true);
		if (writer.WriteWithPreallocation(msgtype, timestamp,
										  BufferView(reinterpret_cast<const Byte*>(&fmt_data), sizeof(AudioFormatData)),
										  WithSize(data, GetMaximumMessageDataSize())))
		{
			SendParameters params;
			params.Compress = m_Settings->UseCompression;
			params.Priority = SendParameters::PriorityOption::Expedited;

			return SendMessageTo(pluid, writer.MoveWrittenBytes(), params);
		}

		return AVResultCode::FailedPrepareMessage;
	}

	Result<> Extender::SendCallVideoSample(const PeerLUID pluid, const VideoFormat& vfmt, const UInt64 timestamp,
										   const BufferView data, const bool compressed) const noexcept
	{
		constexpr UInt16 msgtype = static_cast<const UInt16>(MessageType::VideoSample);

		VideoFormatData fmt_data;
		fmt_data.Format = vfmt.Format;
		fmt_data.Width = vfmt.Width;
		fmt_data.Height = vfmt.Height;
		fmt_data.BytesPerPixel = vfmt.BytesPerPixel;
		fmt_data.Compressed = compressed;

		BufferWriter writer(true);
		if (writer.WriteWithPreallocation(msgtype, timestamp,
										  BufferView(reinterpret_cast<const Byte*>(&fmt_data), sizeof(VideoFormatData)),
										  WithSize(data, GetMaximumMessageDataSize())))
		{
			SendParameters params;
			params.Compress = m_Settings->UseCompression;
			params.Priority = SendParameters::PriorityOption::Normal;

			return SendMessageTo(pluid, writer.MoveWrittenBytes(), params);
		}

		return AVResultCode::FailedPrepareMessage;
	}

	bool Extender::SendCallRequest(const PeerLUID pluid) const noexcept
	{
		const auto result = SendSimpleMessage(pluid, MessageType::CallRequest, SendParameters::PriorityOption::Normal);
		if (result.Succeeded())
		{
			return true;
		}
		else LogErr(L"Could not send CallRequest message to peer: %s", result.GetErrorDescription().c_str());

		return false;
	}

	bool Extender::SendCallAccept(const PeerLUID pluid) const noexcept
	{
		const auto result = SendSimpleMessage(pluid, MessageType::CallAccept, SendParameters::PriorityOption::Normal);
		if (result.Succeeded())
		{
			return true;
		}
		else LogErr(L"Could not send CallAccept message to peer: %s", result.GetErrorDescription().c_str());

		return false;
	}


	bool Extender::SendCallHangup(const PeerLUID pluid) const noexcept
	{
		const auto result = SendSimpleMessage(pluid, MessageType::CallHangup, SendParameters::PriorityOption::Normal);
		if (result.Succeeded())
		{
			return true;
		}
		else LogErr(L"Could not send CallHangup message to peer: %s", result.GetErrorDescription().c_str());

		return false;
	}

	bool Extender::SendCallDecline(const PeerLUID pluid) const noexcept
	{
		const auto result = SendSimpleMessage(pluid, MessageType::CallDecline, SendParameters::PriorityOption::Normal);
		if (result.Succeeded())
		{
			return true;
		}
		else LogErr(L"Could not send CallDecline message to peer: %s", result.GetErrorDescription().c_str());

		return false;
	}

	bool Extender::SendGeneralFailure(const PeerLUID pluid) const noexcept
	{
		const auto result = SendSimpleMessage(pluid, MessageType::GeneralFailure, SendParameters::PriorityOption::Normal);
		if (result.Succeeded())
		{
			return true;
		}
		else LogErr(L"Could not send GeneralFailure message to peer: %s", result.GetErrorDescription().c_str());

		return false;
	}

	bool Extender::StartAudioSourceReader() noexcept
	{
		auto avsource = m_AVSource.WithUniqueLock();
		return StartAudioSourceReader(*avsource);
	}

	bool Extender::StartAudioSourceReader(AVSource& avsource) noexcept
	{
		if (avsource.AudioSourceReader.IsOpen()) return true;

		LogDbg(L"Starting audio source reader...");

		if (!avsource.AudioEndpointID.empty())
		{
			const auto result = avsource.AudioSourceReader.Open(avsource.AudioEndpointID.c_str(),
																{ MFAudioFormat_PCM, MFAudioFormat_Float }, nullptr);
			if (result.Succeeded())
			{
				if (avsource.AudioSourceReader.SetSampleFormat(AudioCompressor::GetEncoderInputFormat()))
				{
					return avsource.AudioSourceReader.BeginRead();
				}
				else
				{
					LogErr(L"Failed to set sample format on audio device; peers will not receive audio");
				}
			}
			else
			{
				LogErr(L"Failed to start audio source reader; peers will not receive audio (%s)",
					   result.GetErrorString().c_str());
			}
		}
		else
		{
			LogErr(L"No audio device endpoint ID set; peers will not receive audio");
		}

		return false;
	}

	void Extender::StopAudioSourceReader() noexcept
	{
		auto avsource = m_AVSource.WithUniqueLock();
		StopAudioSourceReader(*avsource);
	}

	void Extender::StopAudioSourceReader(AVSource& avsource) noexcept
	{
		if (!avsource.AudioSourceReader.IsOpen()) return;

		LogDbg(L"Stopping audio source reader...");

		avsource.AudioSourceReader.Close();
	}

	bool Extender::StartVideoSourceReader() noexcept
	{
		auto avsource = m_AVSource.WithUniqueLock();
		return StartVideoSourceReader(*avsource);
	}

	bool Extender::StartVideoSourceReader(AVSource& avsource) noexcept
	{
		if (avsource.VideoSourceReader.IsOpen()) return true;

		LogDbg(L"Starting video source reader...");

		if (!avsource.VideoSymbolicLink.empty())
		{
			auto width = static_cast<UInt16>((static_cast<double>(avsource.MaxVideoResolution) / 3.0) * 4.0);
			width = width - (width % 16);

			avsource.VideoSourceReader.SetPreferredSize(width, avsource.MaxVideoResolution);

			const auto result = avsource.VideoSourceReader.Open(avsource.VideoSymbolicLink.c_str(),
																{ MFVideoFormat_NV12, MFVideoFormat_I420, MFVideoFormat_RGB24 }, nullptr);
			if (result.Succeeded())
			{
				const auto fmt = avsource.VideoSourceReader.GetSampleFormat();

				auto resample{ false };

				auto swidth = fmt.Width;
				auto sheight = fmt.Height;

				LogInfo(L"Camera video resolution is %u x %u", swidth, sheight);

				if (avsource.ForceMaxVideoResolution)
				{
					const auto fheight = avsource.MaxVideoResolution;
					const auto fwidth = static_cast<UInt16>((static_cast<double>(avsource.MaxVideoResolution) /
															 static_cast<double>(sheight))* static_cast<double>(swidth));

					if (swidth != fwidth || sheight != fheight)
					{
						swidth = fwidth;
						sheight = fheight;

						LogWarn(L"Forcing video resolution to %u x %u", swidth, sheight);

						resample = true;
					}
				}

				// Make dimensions multiples of 16 for H.256
				// compression without artifacts
				if (swidth % 16 != 0 || sheight % 16 != 0)
				{
					swidth = swidth - (swidth % 16);
					sheight = sheight - (sheight % 16);

					LogWarn(L"Changing video resolution to %u x %u for compression", swidth, sheight);

					resample = true;
				}

				if (resample && !avsource.VideoSourceReader.SetSampleSize(swidth, sheight))
				{
					LogErr(L"Failed to set sample size on video device");
				}

				return avsource.VideoSourceReader.BeginRead();
			}
			else
			{
				LogErr(L"Failed to start video source reader; peers will not receive video (%s)",
					   result.GetErrorString().c_str());
			}
		}
		else
		{
			LogErr(L"No video device symbolic link set; peers will not receive video");
		}

		return false;
	}

	void Extender::StopVideoSourceReader() noexcept
	{
		auto avsource = m_AVSource.WithUniqueLock();
		StopVideoSourceReader(*avsource);
	}

	void Extender::StopVideoSourceReader(AVSource& avsource) noexcept
	{
		if (!avsource.VideoSourceReader.IsOpen()) return;

		LogDbg(L"Stopping video source reader...");

		avsource.VideoSourceReader.Close();
	}

	void Extender::CheckStopAVReaders() noexcept
	{
		const auto previewing = m_AVSource.WithSharedLock()->Previewing;

		if (!previewing && !HaveActiveCalls())
		{
			StopAVSourceReaders();
		}
	}

	void Extender::StopAVSourceReaders() noexcept
	{
		auto avsource = m_AVSource.WithUniqueLock();
		StopAudioSourceReader(*avsource);
		StopVideoSourceReader(*avsource);
	}

	void Extender::UpdateSendAudio(const PeerLUID pluid, const bool send_audio) noexcept
	{
		std::shared_ptr<Call_ThS> call_ths;

		m_Peers.WithSharedLock([&](auto& peers)
		{
			const auto peer = peers.find(pluid);
			if (peer != peers.end())
			{
				call_ths = peer->second->Call;
			}
		});

		if (call_ths != nullptr)
		{
			if (send_audio) StartAudioSourceReader();

			call_ths->WithUniqueLock([&](auto& call)
			{
				call.SetSendAudio(send_audio);
			});
		}
	}

	void Extender::UpdateSendVideo(const PeerLUID pluid, const bool send_video) noexcept
	{
		std::shared_ptr<Call_ThS> call_ths;

		m_Peers.WithSharedLock([&](auto& peers)
		{
			const auto peer = peers.find(pluid);
			if (peer != peers.end())
			{
				call_ths = peer->second->Call;
			}
		});

		if (call_ths != nullptr)
		{
			if (send_video) StartVideoSourceReader();

			call_ths->WithUniqueLock([&](auto& call)
			{
				call.SetSendVideo(send_video);
			});
		}
	}

	bool Extender::SetAudioEndpointID(const WCHAR* id)
	{
		auto success = true;

		m_AVSource.WithUniqueLock([&](auto& avsource)
		{
			const bool was_open = avsource.AudioSourceReader.IsOpen();

			StopAudioSourceReader(avsource);

			avsource.AudioEndpointID = id;

			if (was_open)
			{
				success = StartAudioSourceReader(avsource);
			}
		});

		m_Peers.WithSharedLock([&](auto& peers)
		{
			for (auto it = peers.begin(); it != peers.end(); ++it)
			{
				const Peer& peer = *it->second;

				auto call = peer.Call->WithUniqueLock();
				if (call->IsInCall())
				{
					call->OnAudioSourceChange();
				}
			}
		});

		return success;
	}

	bool Extender::SetVideoSymbolicLink(const WCHAR* id, const UInt16 max_res, const bool force_res)
	{
		auto success = true;

		m_AVSource.WithUniqueLock([&](auto& avsource)
		{
			const bool was_open = avsource.VideoSourceReader.IsOpen();

			StopVideoSourceReader(avsource);

			avsource.VideoSymbolicLink = id;
			avsource.MaxVideoResolution = max_res;
			avsource.ForceMaxVideoResolution = force_res;

			if (was_open)
			{
				success = StartVideoSourceReader(avsource);
			}
		});

		m_Peers.WithSharedLock([&](auto& peers)
		{
			for (auto it = peers.begin(); it != peers.end(); ++it)
			{
				const Peer& peer = *it->second;

				auto call = peer.Call->WithUniqueLock();
				if (call->IsInCall())
				{
					call->OnVideoSourceChange();
				}
			}
		});

		return success;
	}

	Result<VideoFormat> Extender::StartVideoPreview(SourceReader::SampleEventDispatcher::FunctionType&& callback) noexcept
	{
		auto success = false;
		VideoFormat video_format;

		auto preview_handlers = m_PreviewEventHandlers.WithUniqueLock();

		m_AVSource.WithUniqueLock([&](auto& avsource)
		{
			success = avsource.VideoSourceReader.IsOpen();
			if (!success)
			{
				success = StartVideoSourceReader(avsource);
			}

			if (success)
			{
				preview_handlers->VideoSampleEventFunctionHandle = avsource.VideoSourceReader.AddSampleEventCallback(std::move(callback));
				video_format = avsource.VideoSourceReader.GetSampleFormat();
				avsource.Previewing = true;
			}
		});

		if (success)
		{
			return video_format;
		}

		return AVResultCode::Failed;
	}

	void Extender::StopVideoPreview() noexcept
	{
		auto preview_handlers = m_PreviewEventHandlers.WithUniqueLock();

		if (preview_handlers->VideoSampleEventFunctionHandle)
		{
			m_AVSource.WithUniqueLock([&](auto& avsource)
			{
				avsource.VideoSourceReader.RemoveSampleEventCallback(preview_handlers->VideoSampleEventFunctionHandle);

				if (!preview_handlers->AudioSampleEventFunctionHandle)
				{
					avsource.Previewing = false;
				}
			});

			CheckStopAVReaders();
		}
	}

	Result<AudioFormat> Extender::StartAudioPreview(SourceReader::SampleEventDispatcher::FunctionType&& callback) noexcept
	{
		auto success = false;
		AudioFormat audio_format;

		auto preview_handlers = m_PreviewEventHandlers.WithUniqueLock();

		m_AVSource.WithUniqueLock([&](auto& avsource)
		{
			success = avsource.AudioSourceReader.IsOpen();
			if (!success)
			{
				success = StartAudioSourceReader(avsource);
			}

			if (success)
			{
				preview_handlers->AudioSampleEventFunctionHandle = avsource.AudioSourceReader.AddSampleEventCallback(std::move(callback));
				audio_format = avsource.AudioSourceReader.GetSampleFormat();
				avsource.Previewing = true;
			}
		});

		if (success)
		{
			return audio_format;
		}

		return AVResultCode::Failed;
	}

	void Extender::StopAudioPreview() noexcept
	{
		auto preview_handlers = m_PreviewEventHandlers.WithUniqueLock();

		if (preview_handlers->AudioSampleEventFunctionHandle)
		{
			m_AVSource.WithUniqueLock([&](auto& avsource)
			{
				avsource.AudioSourceReader.RemoveSampleEventCallback(preview_handlers->AudioSampleEventFunctionHandle);

				if (!preview_handlers->VideoSampleEventFunctionHandle)
				{
					avsource.Previewing = false;
				}
			});

			CheckStopAVReaders();
		}
	}
}
