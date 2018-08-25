// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "..\..\QuantumGateCryptoLib\QuantumGateCryptoLib.h"

namespace QuantumGate::Implementation::Crypto
{
	class NewHope
	{
	private:
		NewHope() = default;

	public:
		[[nodiscard]] static const bool GenerateKey(AsymmetricKeyData& keydata) noexcept
		{
			try
			{
				auto bytes = GetCryptoRandomBytes(48);
				if (bytes)
				{
					randombytes_init(reinterpret_cast<UChar*>(bytes->GetBytes()), NULL, 256);

					keydata.LocalPublicKey.Allocate(m_PublicKeySize);
					keydata.LocalPrivateKey.Allocate(m_PrivateKeySize);

					if (crypto_kem_newhope_keypair(reinterpret_cast<UChar*>(keydata.LocalPublicKey.GetBytes()),
												   reinterpret_cast<UChar*>(keydata.LocalPrivateKey.GetBytes())) == 0)
					{
						return true;
					}
				}
			}
			catch (const std::exception& e)
			{
				LogErr(L"Exception while trying to generate NewHope keypair - %s", Util::ToStringW(e.what()).c_str());
			}

			return false;
		}

		[[nodiscard]] static const bool GenerateSharedSecret(AsymmetricKeyData& keydata) noexcept
		{
			try
			{
				switch (keydata.GetOwner())
				{
					case AsymmetricKeyOwner::Bob:
					{
						// Bob needs to encrypt a shared secret
						// with Alice's public key
						keydata.SharedSecret.Allocate(m_SharedSecretSize);
						keydata.EncryptedSharedSecret.Allocate(m_SharedSecretEncryptedSize);

						if (crypto_kem_newhope_enc(reinterpret_cast<UChar*>(keydata.EncryptedSharedSecret.GetBytes()),
												   reinterpret_cast<UChar*>(keydata.SharedSecret.GetBytes()),
												   reinterpret_cast<UChar*>(keydata.PeerPublicKey.GetBytes())) == 0)
						{
							Dbg(L"\r\nNewHope (Bob):");
							Dbg(L"PSharedSecret: %u bytes - %s", keydata.SharedSecret.GetSize(),
								Util::GetBase64(keydata.SharedSecret)->c_str());
							Dbg(L"ESharedSecret: %u bytes - %s", keydata.EncryptedSharedSecret.GetSize(),
								Util::GetBase64(keydata.EncryptedSharedSecret)->c_str());
							Dbg(L"\r\n");

							return true;
						}

						break;
					}
					case AsymmetricKeyOwner::Alice:
					{
						// Alice needs to decrypt the shared secret sent
						// by Bob with her private key
						keydata.SharedSecret.Allocate(m_SharedSecretSize);

						if (crypto_kem_newhope_dec(reinterpret_cast<UChar*>(keydata.SharedSecret.GetBytes()),
												   reinterpret_cast<UChar*>(keydata.EncryptedSharedSecret.GetBytes()),
												   reinterpret_cast<UChar*>(keydata.LocalPrivateKey.GetBytes())) == 0)
						{
							Dbg(L"\r\nNewHope (Alice):");
							Dbg(L"PSharedSecret: %u bytes - %s", keydata.SharedSecret.GetSize(),
								Util::GetBase64(keydata.SharedSecret)->c_str());
							Dbg(L"ESharedSecret: %u bytes - %s", keydata.EncryptedSharedSecret.GetSize(),
								Util::GetBase64(keydata.EncryptedSharedSecret)->c_str());
							Dbg(L"\r\n");

							return true;
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
			}
			catch (const std::exception& e)
			{
				LogErr(L"Exception while trying to generate NewHope shared secret - %s", Util::ToStringW(e.what()).c_str());
			}

			return false;
		}

	private:
		inline static Size m_PublicKeySize{ NEWHOPE_CCAKEM_PUBLICKEYBYTES };
		inline static Size m_PrivateKeySize{ NEWHOPE_CCAKEM_SECRETKEYBYTES };
		inline static Size m_SharedSecretSize{ NEWHOPE_SYMBYTES };
		inline static Size m_SharedSecretEncryptedSize{ NEWHOPE_CCAKEM_CIPHERTEXTBYTES };
	};
}