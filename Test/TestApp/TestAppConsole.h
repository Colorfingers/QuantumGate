// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "QuantumGate.h"
#include "Concurrency\EventCondition.h"

class TestAppConsole final : public QuantumGate::Console::Output
{
public:
	inline QuantumGate::String* GetMessages() noexcept { return &m_Messages; }
	inline QuantumGate::Implementation::Concurrency::EventCondition* GetNewMessageEvent() noexcept { return &m_NewMessageEvent; }

	inline const bool TryLock() { return m_Mutex.try_lock(); }
	inline void Lock() { m_Mutex.lock(); }
	inline void Unlock() { m_Mutex.unlock(); }

	const QuantumGate::WChar* GetFormat(const QuantumGate::Console::MessageType type,
										const QuantumGate::Console::Format fmt) const noexcept override
	{
		return L"";
	}

	void AddMessage(const QuantumGate::Console::MessageType type,
					const QuantumGate::StringView message) override;

protected:
	std::mutex m_Mutex;
	QuantumGate::String m_Messages;
	QuantumGate::Implementation::Concurrency::EventCondition m_NewMessageEvent;
};

