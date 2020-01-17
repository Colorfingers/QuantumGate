// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include <shared_mutex>

namespace QuantumGate::Implementation::Core::Peer
{
	class ExtenderUUIDs final
	{
	public:
		ExtenderUUIDs() noexcept = default;
		ExtenderUUIDs(const ExtenderUUIDs&) = delete;
		ExtenderUUIDs(ExtenderUUIDs&&) noexcept = default;
		~ExtenderUUIDs() = default;
		ExtenderUUIDs& operator=(const ExtenderUUIDs&) = delete;
		ExtenderUUIDs& operator=(ExtenderUUIDs&&) noexcept = default;

		[[nodiscard]] bool HasExtender(const ExtenderUUID& uuid) const noexcept;

		inline const Vector<ExtenderUUID>& Current() const noexcept { return m_ExtenderUUIDs; }

		[[nodiscard]] bool Set(Vector<ExtenderUUID>&& uuids) noexcept;
		[[nodiscard]] bool Copy(const ExtenderUUIDs& uuids) noexcept;
		Result<std::pair<Vector<ExtenderUUID>, Vector<ExtenderUUID>>> Update(Vector<ExtenderUUID>&& update_uuids) noexcept;

	private:
		[[nodiscard]] bool SortAndEnsureUnique(Vector<ExtenderUUID>& uuids) const noexcept;

	private:
		Vector<ExtenderUUID> m_ExtenderUUIDs;
	};
}