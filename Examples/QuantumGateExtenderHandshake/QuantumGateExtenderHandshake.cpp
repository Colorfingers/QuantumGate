﻿// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"

// Include the QuantumGate main header with API definitions
#include <QuantumGate.h>

// Link with the QuantumGate library depending on architecture
#if defined(_DEBUG)
#if !defined(_WIN64)
#pragma comment (lib, "QuantumGate32D.lib")
#else
#pragma comment (lib, "QuantumGate64D.lib")
#endif
#else
#if !defined(_WIN64)
#pragma comment (lib, "QuantumGate32.lib")
#else
#pragma comment (lib, "QuantumGate64.lib")
#endif
#endif

#include <iostream>
#include <thread>
#include <chrono>

#include "HandshakeExtender.h"

using namespace std::literals;

std::wstring GetInput()
{
	std::wstring input;
	std::getline(std::wcin, input);
	return input;
}

int main()
{
	auto first_instance = false;
	auto enable_console = false;

	std::wcout <<
		L"You should start two separate instances of QuantumGateExtender.exe on the same PC.\r\n"
		L"The second one will connect to the first one. Is this the first instance? (Y/N): ";

	auto answer = GetInput();
	if (answer.size() > 0)
	{
		if (answer.find(L'y') != std::wstring::npos ||
			answer.find(L'Y') != std::wstring::npos)
		{
			first_instance = true;
		}
	}
	else return -1;

	std::wcout << L"Would you like to enable QuantumGate console output? (Y/N): ";
	answer = GetInput();
	if (answer.size() > 0)
	{
		if (answer.find(L'y') != std::wstring::npos ||
			answer.find(L'Y') != std::wstring::npos)
		{
			enable_console = true;
		}
	}

	if (enable_console)
	{
		// Send console output to terminal;
		QuantumGate::Console::SetOutput(std::make_shared<QuantumGate::Console::TerminalOutput>());
		QuantumGate::Console::SetVerbosity(QuantumGate::Console::Verbosity::Debug);
	}
	else
	{
		// The code below enables use of special color codes
		// in the standard terminal output.
		HANDLE stdhandle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (stdhandle != INVALID_HANDLE_VALUE)
		{
			DWORD mode = 0;
			if (GetConsoleMode(stdhandle, &mode))
			{
				mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

				SetConsoleMode(stdhandle, mode);
			}
		}
	}

	QuantumGate::StartupParameters params;

	// Create a UUID for the local instance with matching keypair;
	// normally you should do this once and save and reload the UUID
	// and keys. The UUID and public key can be shared with other peers,
	// while the private key should be protected and kept private.
	{
		auto[success, uuid, keys] = QuantumGate::UUID::Create(QuantumGate::UUID::Type::Peer,
															  QuantumGate::UUID::SignAlgorithm::EDDSA_ED25519);
		if (success)
		{
			params.UUID = uuid;
			params.Keys = std::move(*keys);
		}
		else
		{
			std::wcout << L"Failed to create peer UUID\r\n";
			return -1;
		}
	}

	// Set the supported algorithms
	params.SupportedAlgorithms.Hash = {
		QuantumGate::Algorithm::Hash::BLAKE2B512
	};
	params.SupportedAlgorithms.PrimaryAsymmetric = {
		QuantumGate::Algorithm::Asymmetric::ECDH_X25519
	};
	params.SupportedAlgorithms.SecondaryAsymmetric = {
		QuantumGate::Algorithm::Asymmetric::KEM_NTRUPRIME
	};
	params.SupportedAlgorithms.Symmetric = {
		QuantumGate::Algorithm::Symmetric::CHACHA20_POLY1305
	};
	params.SupportedAlgorithms.Compression = {
		QuantumGate::Algorithm::Compression::ZSTANDARD
	};

	// Listen for incoming connections on startup
	params.Listeners.Enable = true;

	// Listen for incoming connections on these ports
	if (first_instance)
	{
		params.Listeners.TCPPorts = { 999 };
	}
	else params.Listeners.TCPPorts = { 9999 };

	// Start extenders on startup
	params.EnableExtenders = true;

	// For testing purposes we disable authentication requirement; when
	// authentication is required we would need to add peers to the instance
	// via QuantumGate::Local::GetAccessManager().AddPeer() including their
	// UUID and public key so that they can be authenticated when connecting
	params.RequireAuthentication = false;

	QuantumGate::Local qg;

	// For testing purposes we allow access by default
	qg.GetAccessManager().SetPeerAccessDefault(QuantumGate::PeerAccessDefault::Allowed);

	// For testing purposes we allow all IP addresses to connect
	if (!qg.GetAccessManager().AddIPFilter(L"0.0.0.0/0", QuantumGate::IPFilterType::Allowed) ||
		!qg.GetAccessManager().AddIPFilter(L"::/0", QuantumGate::IPFilterType::Allowed))
	{
		std::wcout << L"Failed to add an IP filter\r\n";
		return -1;
	}

	auto extender = std::make_shared<HandshakeExtender>();
	if (const auto result = qg.AddExtender(extender); result.Failed())
	{
		std::wcout << L"Failed to add HandshakeExtender (" << result << L")\r\n";
		return -1;
	}

	std::wcout << L"\r\nStarting QuantumGate...\r\n";

	if (const auto result = qg.Startup(params); result.Succeeded())
	{
		if (!enable_console)
		{
			std::wcout << L"QuantumGate startup successful\r\n\r\n";
		}

		if (!first_instance)
		{
			std::wcout << L"Connecting to first instance...\r\n";

			QuantumGate::ConnectParameters params;

			// Connect to the first instance on the local host
			params.PeerIPEndpoint = QuantumGate::IPEndpoint(QuantumGate::IPAddress(L"127.0.0.1"), 999);

			// This version of the ConnectTo function will block until connection succeeds or fails;
			// use a second parameter to supply a callback function (may be nullptr) for async connect
			const auto connect_result = qg.ConnectTo(std::move(params));
			if (connect_result.Failed())
			{
				std::wcout << L"Failed to connect to first instance (" << connect_result << L")\r\n";
			}
		}

		if (first_instance)
		{
			std::wcout << L"\r\nWaiting for peers to connect...\r\n";
		}
		else std::this_thread::sleep_for(5s);

		std::wcout << L"\x1b[93m" << L"\r\n";
		std::wcout << L"Type 'quit' and press Enter to shut down;\r\n";
		std::wcout << L"type any other text and press Enter to send to all connected peers.\r\n";
		std::wcout << L"\x1b[39m" << L"\r\n";

		while (true)
		{
			std::wcout << L"\x1b[106m\x1b[30m" << L" >> " << L"\x1b[40m\x1b[39m ";

			std::wstring cmdline;
			std::getline(std::wcin, cmdline);

			if (cmdline == L"quit") break;
			else
			{
				extender->BroadcastToConnectedPeers(cmdline);
			}
		}

		std::wcout << L"\r\nShutting down QuantumGate...\r\n";
		if (!qg.Shutdown())
		{
			std::wcout << L"Shutdown failed\r\n";
		}
	}
	else
	{
		std::wcout << L"Startup failed (" << result << L")\r\n";
		return -1;
	}

	return 0;
}