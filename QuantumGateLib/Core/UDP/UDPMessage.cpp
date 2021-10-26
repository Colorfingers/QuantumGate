// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "UDPMessage.h"
#include "..\..\Memory\StackBuffer.h"
#include "..\..\Memory\BufferReader.h"
#include "..\..\Memory\BufferWriter.h"
#include "..\..\Common\Random.h"
#include "..\..\Common\Obfuscate.h"
#include "..\..\..\QuantumGateCryptoLib\QuantumGateCryptoLib.h"

using namespace QuantumGate::Implementation::Memory;

namespace QuantumGate::Implementation::Core::UDP
{
	Message::Header::Header(const Type type, const Direction direction) noexcept :
		m_Direction(direction), m_MessageType(type)
	{
		if (m_Direction == Direction::Outgoing)
		{
			m_MessageIV = static_cast<Message::IV>(Random::GetPseudoRandomNumber());

			switch (m_MessageType)
			{
				case Message::Type::Syn:
				{
					// These are not used for outgoing Syn so we fill with random data
					m_MessageAckNumber = static_cast<Message::SequenceNumber>(Random::GetPseudoRandomNumber());
					break;
				}
				case Message::Type::EAck:
				{
					// Not used for the above message type so we fill with random data
					m_MessageSequenceNumber = static_cast<Message::SequenceNumber>(Random::GetPseudoRandomNumber());
					break;
				}
				case Message::Type::MTUD:
				case Message::Type::Null:
				case Message::Type::Reset:
				case Message::Type::Cookie:
				{
					// Not (always) used for the above message types so we fill with random data;
					// MTUD messages override these in some cases
					m_MessageSequenceNumber = static_cast<Message::SequenceNumber>(Random::GetPseudoRandomNumber());
					m_MessageAckNumber = static_cast<Message::SequenceNumber>(Random::GetPseudoRandomNumber());
					break;
				}
				default:
				{
					break;
				}
			}
		}
	}

	bool Message::Header::Read(const BufferView& buffer) noexcept
	{
		assert(m_Direction == Direction::Incoming);

		BufferSpan hmac(reinterpret_cast<Byte*>(&m_MessageHMAC), sizeof(m_MessageHMAC));
		UInt8 msgtype_flags{ 0 };

		Memory::BufferReader rdr(buffer, true);
		if (rdr.Read(hmac,
					 m_MessageIV,
					 m_MessageSequenceNumber,
					 m_MessageAckNumber,
					 msgtype_flags))
		{
			m_MessageType = static_cast<Type>(msgtype_flags & MessageTypeMask);
			m_AckFlag = msgtype_flags & AckFlag;
			m_SeqNumFlag = msgtype_flags & SeqNumFlag;

			return true;
		}

		return false;
	}

	bool Message::Header::Write(Buffer& buffer) const noexcept
	{
		assert(m_Direction == Direction::Outgoing);

		BufferView hmac(reinterpret_cast<const Byte*>(&m_MessageHMAC), sizeof(m_MessageHMAC));

		UInt8 msgtype_flags{ static_cast<UInt8>(m_MessageType) };
		
		if (m_AckFlag)
		{
			msgtype_flags = msgtype_flags | AckFlag;
		}

		if (m_SeqNumFlag)
		{
			msgtype_flags = msgtype_flags | SeqNumFlag;
		}

		Memory::BufferWriter wrt(buffer, true);
		return wrt.WriteWithPreallocation(hmac,
										  m_MessageIV,
										  m_MessageSequenceNumber,
										  m_MessageAckNumber,
										  msgtype_flags);
	}

	void Message::SetMessageData(Buffer&& buffer) noexcept
	{
		assert(m_Header.GetMessageType() == Type::Data ||
			   m_Header.GetMessageType() == Type::MTUD);

		if (!buffer.IsEmpty())
		{
			std::get<Buffer>(m_Data) = std::move(buffer);

			Validate();
		}
	}

	Size Message::GetMaxMessageDataSize() const noexcept
	{
		return std::min(MaxSize::_65KB,
						(m_MaxMessageSize - (Header::GetSize() + BufferIO::GetSizeOfEncodedSize(MaxSize::_65KB))));
	}

	Size Message::GetMaxAckRangesPerMessage() const noexcept
	{
		const auto asize = std::min(MaxSize::_65KB,
									m_MaxMessageSize - (Header::GetSize() + BufferIO::GetSizeOfEncodedSize(MaxSize::_65KB)));
		return (asize / sizeof(Message::AckRange));
	}

	void Message::SetSynData(SynData&& data) noexcept
	{
		assert(m_Header.GetMessageType() == Type::Syn);

		m_Data.emplace<SynData>(std::move(data));
	}

	Message::SynData& Message::GetSynData() noexcept
	{
		assert(m_Header.GetMessageType() == Type::Syn);
		assert(IsValid());

		return std::get<SynData>(m_Data);
	}

	void Message::SetCookieData(CookieData&& data) noexcept
	{
		assert(m_Header.GetMessageType() == Type::Cookie);

		m_Data = std::move(data);
	}

	const Message::CookieData& Message::GetCookieData() const noexcept
	{
		assert(m_Header.GetMessageType() == Type::Cookie);
		assert(IsValid());

		return std::get<CookieData>(m_Data);
	}

	void Message::SetStateData(StateData&& data) noexcept
	{
		assert(m_Header.GetMessageType() == Type::State);

		m_Data = std::move(data);
	}

	const Message::StateData& Message::GetStateData() const noexcept
	{
		assert(m_Header.GetMessageType() == Type::State);
		assert(IsValid());

		return std::get<StateData>(m_Data);
	}

	const Buffer& Message::GetMessageData() const noexcept
	{
		assert(m_Header.GetMessageType() == Type::Data);
		assert(IsValid());

		return std::get<Buffer>(m_Data);
	}

	Buffer&& Message::MoveMessageData() noexcept
	{
		assert(m_Header.GetMessageType() == Type::Data);
		assert(IsValid());

		return std::move(std::get<Buffer>(m_Data));
	}

	void Message::SetAckRanges(Vector<Message::AckRange>&& acks) noexcept
	{
		assert(m_Header.GetMessageType() == Type::EAck);

		m_Data = std::move(acks);
	}

	const Vector<Message::AckRange>& Message::GetAckRanges() noexcept
	{
		assert(m_Header.GetMessageType() == Type::EAck);
		assert(IsValid());

		return std::get<Vector<AckRange>>(m_Data);
	}

	Size Message::GetHeaderSize() const noexcept
	{
		return m_Header.GetSize();
	}

	bool Message::Read(BufferSpan& buffer, const SymmetricKeys& symkey) noexcept
	{
		try
		{
			// Should have enough data for outer message header
			if (buffer.GetSize() < GetHeaderSize()) return false;

			// Deobfuscation and HMAC check
			{
				assert(symkey);

				// Calculate and check HMAC for the message
				{
					BufferView msgview{ buffer };

					HMAC hmac{ 0 };
					std::memcpy(&hmac, msgview.GetBytes(), sizeof(HMAC));
					msgview.RemoveFirst(sizeof(HMAC));

					const auto chmac = CalcHMAC(msgview, symkey.GetPeerAuthKey());
					if (hmac != chmac) return false;
				}

				// Deobfuscate message
				{
					BufferSpan msgspan{ buffer };
					msgspan.RemoveFirst(sizeof(HMAC));

					IV iv{ 0 };
					std::memcpy(&iv, msgspan.GetBytes(), sizeof(IV));
					msgspan.RemoveFirst(sizeof(IV));

					Obfuscate::Undo(msgspan, symkey.GetPeerKey(), iv);
				}
			}

			// Get message outer header from buffer
			if (!m_Header.Read(buffer)) return false;

			// Remove message header from buffer
			buffer.RemoveFirst(GetHeaderSize());

			switch (m_Header.GetMessageType())
			{
				case Type::Data:
				{
					Buffer data;
					Memory::BufferReader rdr(buffer, true);
					if (!rdr.Read(WithSize(data, MaxSize::_65KB))) return false;

					m_Data = std::move(data);
					break;
				}
				case Type::MTUD:
				{
					// Skip reading unneeded data
					m_Data = Buffer();
					break;
				}
				case Type::EAck:
				{
					StackBuffer<MaxSize::_65KB> data;
					Memory::BufferReader rdr(buffer, true);
					if (!rdr.Read(WithSize(data, MaxSize::_65KB))) return false;

					// Size should be exact multiple of size of AckRange
					// otherwise something is wrong
					assert(data.GetSize() % sizeof(AckRange) == 0);
					if (data.GetSize() % sizeof(AckRange) != 0) return false;

					const auto num_ack_ranges = data.GetSize() / sizeof(AckRange);

					Vector<AckRange> eacks;
					eacks.resize(num_ack_ranges);
					std::memcpy(eacks.data(), data.GetBytes(), data.GetSize());
					m_Data = std::move(eacks);
					break;
				}
				case Type::State:
				{
					StateData state_data;
					Memory::BufferReader rdr(buffer, true);
					if (!rdr.Read(state_data.MaxWindowSize, state_data.MaxWindowSizeBytes)) return false;
					m_Data = std::move(state_data);
					break;
				}
				case Type::Syn:
				{
					SynData syn_data;
					UInt8 syn_flags{ 0 };

					Memory::BufferReader rdr(buffer, true);
					if (!rdr.Read(syn_data.ProtocolVersionMajor, syn_data.ProtocolVersionMinor, syn_flags,
								  syn_data.ConnectionID, syn_data.Port, syn_data.Time)) return false;

					if (syn_flags & SynData::CookieFlag)
					{
						syn_data.Cookie.emplace();
						if (!rdr.Read(syn_data.Cookie->CookieID)) return false;
					}

					syn_data.HandshakeDataIn.Emplace();

					if (!rdr.Read(WithSize(*syn_data.HandshakeDataIn, MaxSize::_512B))) return false;

					m_Data.emplace<SynData>(std::move(syn_data));
					break;
				}
				case Type::Cookie:
				{
					CookieData cookie_data;

					Memory::BufferReader rdr(buffer, true);
					if (!rdr.Read(cookie_data.CookieID)) return false;

					m_Data = std::move(cookie_data);
					break;
				}
				default:
				{
					break;
				}
			}

			Validate();

			return true;
		}
		catch (...) {}

		return false;
	}
	
	bool Message::Write(Buffer& buffer, const SymmetricKeys& symkey) noexcept
	{
		try
		{
			DbgInvoke([&]()
			{
				Validate();
				assert(IsValid());
			});

			Buffer msgbuf;

			// Add message header
			if (!m_Header.Write(msgbuf)) return false;

			Dbg(L"\r\nUDPMessageHdr (%s):\r\n0b%s", TypeToString(m_Header.GetMessageType()), Util::ToBinaryString(BufferView(msgbuf)).c_str());

			switch (m_Header.GetMessageType())
			{
				case Type::Data:
				case Type::MTUD:
				{
					// Add message data if any
					const auto& data = std::get<Buffer>(m_Data);
					if (!data.IsEmpty())
					{
						StackBuffer<MaxSize::_65KB> dbuf;
						Memory::StackBufferWriter<MaxSize::_65KB> wrt(dbuf, true);
						if (!wrt.WriteWithPreallocation(WithSize(data, MaxSize::_65KB))) return false;

						msgbuf += dbuf;

						Dbg(L"UDPMessageData: %d bytes - 0b%s", dbuf.GetSize(), Util::ToBinaryString(BufferView(dbuf)).c_str());
					}
					break;
				}
				case Type::EAck:
				{
					const auto& eacks = std::get<Vector<AckRange>>(m_Data);

					StackBuffer<MaxSize::_65KB> ackbuf;
					BufferView ack_view{ reinterpret_cast<const Byte*>(eacks.data()), eacks.size() * sizeof(AckRange) };
					Memory::StackBufferWriter<MaxSize::_65KB> wrt(ackbuf, true);
					if (!wrt.WriteWithPreallocation(WithSize(ack_view, MaxSize::_65KB))) return false;

					msgbuf += ackbuf;

					Dbg(L"UDPMessageData: %d bytes - 0b%s", ackbuf.GetSize(), Util::ToBinaryString(BufferView(ackbuf)).c_str());
					break;
				}
				case Type::State:
				{
					const auto& state_data = std::get<StateData>(m_Data);

					StackBuffer<sizeof(StateData)> statebuf;
					Memory::StackBufferWriter<sizeof(StateData)> wrt(statebuf, true);
					if (!wrt.WriteWithPreallocation(state_data.MaxWindowSize, state_data.MaxWindowSizeBytes)) return false;

					msgbuf += statebuf;

					Dbg(L"UDPMessageData: %d bytes - 0b%s", statebuf.GetSize(), Util::ToBinaryString(BufferView(statebuf)).c_str());
					break;
				}
				case Type::Syn:
				{
					const auto& syn_data = std::get<SynData>(m_Data);

					UInt8 syn_flags{ 0 };
					if (syn_data.Cookie.has_value())
					{
						syn_flags = syn_flags | SynData::CookieFlag;
					}

					StackBuffer<sizeof(SynData)> synbuf;
					Memory::StackBufferWriter<sizeof(SynData)> wrt(synbuf, true);
					if (!wrt.WriteWithPreallocation(syn_data.ProtocolVersionMajor, syn_data.ProtocolVersionMinor, syn_flags,
													syn_data.ConnectionID, syn_data.Port, syn_data.Time)) return false;

					if (syn_data.Cookie.has_value())
					{
						if (!wrt.Write(syn_data.Cookie->CookieID)) return false;
					}

					if (!wrt.Write(WithSize(*syn_data.HandshakeDataOut, MaxSize::_512B))) return false;

					msgbuf += synbuf;

					Dbg(L"UDPMessageData: %d bytes - 0b%s", synbuf.GetSize(), Util::ToBinaryString(BufferView(synbuf)).c_str());
					break;
				}
				case Type::Cookie:
				{
					const auto& cookie_data = std::get<CookieData>(m_Data);

					StackBuffer<sizeof(CookieData)> cookiebuf;
					Memory::StackBufferWriter<sizeof(CookieData)> wrt(cookiebuf, true);
					if (!wrt.WriteWithPreallocation(cookie_data.CookieID)) return false;

					msgbuf += cookiebuf;

					Dbg(L"UDPMessageData: %d bytes - 0b%s", cookiebuf.GetSize(), Util::ToBinaryString(BufferView(cookiebuf)).c_str());
					break;
				}
				default:
				{
					break;
				}
			}

			if (msgbuf.GetSize() > m_MaxMessageSize)
			{
				LogErr(L"Size of UDP message (type %s) combined with header is too large: %zu bytes (Max. is %zu bytes)",
					   TypeToString(m_Header.GetMessageType()), msgbuf.GetSize(), m_MaxMessageSize);

				return false;
			}
			else
			{
				// Add some random padding data at the end of the message
				const auto free_space = m_MaxMessageSize - msgbuf.GetSize();
				if (free_space > 0)
				{
					switch (m_Header.GetMessageType())
					{
						case Type::Cookie: // Excluded to prevent amplification attacks
						case Type::Data: // Excluded for speed
						case Type::EAck: // Excluded for speed
						{
							break;
						}
						case Type::MTUD:
						{
							if (m_Header.HasSequenceNumber())
							{
								// Excluded because MTUD data needs to be precise size (except for MTUD acks)
								break;
							}
							[[fallthrough]];
						}
						case Type::Syn:
						case Type::State:
						case Type::Reset:
						case Type::Null:
						{
							const auto rndnum = static_cast<Size>(Random::GetPseudoRandomNumber(0, free_space));

							Dbg(L"UDPMessageRnd: %zu bytes", rndnum);

							msgbuf += Random::GetPseudoRandomBytes(rndnum);
							break;
						}
						default:
						{
							break;
						}
					}
				}
			}

			// Obfuscation and HMAC
			{
				assert(symkey);

				// Obfuscate message
				{
					BufferSpan msgspan{ msgbuf };
					msgspan.RemoveFirst(sizeof(HMAC));

					IV iv{ 0 };
					std::memcpy(&iv, msgspan.GetBytes(), sizeof(IV));
					msgspan.RemoveFirst(sizeof(IV));

					Obfuscate::Do(msgspan, symkey.GetLocalKey(), iv);
				}

				// Calculate HMAC for the message
				{
					BufferView msgview{ msgbuf };
					msgview.RemoveFirst(sizeof(HMAC));
					const auto hmac = CalcHMAC(msgview, symkey.GetLocalAuthKey());
					std::memcpy(msgbuf.GetBytes(), &hmac, sizeof(hmac));
				}
			}

			Dbg(L"UDPMessageObf:\r\n0b%s", Util::ToBinaryString(BufferView(msgbuf)).c_str());
			Dbg(L"UDPMessageObf (b64): %zu bytes - %s\r\n", msgbuf.GetSize(), Util::ToBase64(msgbuf)->c_str());

			buffer = std::move(msgbuf);

			return true;
		}
		catch (...) {}

		return false;
	}

	Message::HMAC Message::CalcHMAC(const BufferView& data, const BufferView& authkey) noexcept
	{
		// Half SipHash requires key size of 8 bytes
		// and we want 4 byte output size
		assert(authkey.GetSize() == 8);
		assert(sizeof(HMAC) == 4);

		HMAC hmac{ 0 };

		halfsiphash(reinterpret_cast<const uint8_t*>(data.GetBytes()), data.GetSize(),
					reinterpret_cast<const uint8_t*>(authkey.GetBytes()),
					reinterpret_cast<uint8_t*>(&hmac), sizeof(hmac));

		return hmac;
	}

	void Message::Validate() noexcept
	{
		m_Valid = false;

		auto type_ok{ false };

		// Check if we have a valid message type
		switch (GetType())
		{
			case Type::Data:
				type_ok = HasAck() && HasSequenceNumber() && std::holds_alternative<Buffer>(m_Data);
				break;
			case Type::State:
				type_ok = HasAck() && HasSequenceNumber() && std::holds_alternative<StateData>(m_Data);
				break;
			case Type::EAck:
				type_ok = HasAck() && std::holds_alternative<Vector<AckRange>>(m_Data);
				break;
			case Type::Syn:
				type_ok = HasSequenceNumber() && std::holds_alternative<SynData>(m_Data);
				break;
			case Type::Cookie:
				type_ok = !HasAck() && !HasSequenceNumber() && std::holds_alternative<CookieData>(m_Data);
				break;
			case Type::MTUD:
				type_ok = ((HasSequenceNumber() && !HasAck()) || (!HasSequenceNumber() && HasAck())) && std::holds_alternative<Buffer>(m_Data);
				break;
			case Type::Null:
			case Type::Reset:
				type_ok = !HasAck() && !HasSequenceNumber();
				break;
			default:
				LogErr(L"UDP connection: could not validate message: unknown message type %u", GetType());
				break;
		}

		m_Valid = type_ok;
	}
}