// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "TestApp.h"
#include "CTestAppDlgMainTab.h"
#include "CInformationDlg.h"
#include "TestAppDlg.h"

#include "Console.h"
#include "Common\Util.h"
#include "Crypto\Crypto.h"

using namespace QuantumGate::Implementation;

IMPLEMENT_DYNCREATE(CTestAppDlgMainTab, CTestAppDlgTabCtrlPage)

void CTestAppDlgMainTab::UpdateControls() noexcept
{
	auto local = GetQuantumGateInstance();

	((CEdit*)GetDlgItem(IDC_SERVERPORT))->SetReadOnly(local->IsRunning());
	((CEdit*)GetDlgItem(IDC_SERVERPORT_BTH))->SetReadOnly(local->IsRunning());
	((CButton*)GetDlgItem(IDC_BTH_AUTH2))->EnableWindow(!local->IsRunning());
	((CEdit*)GetDlgItem(IDC_LOCAL_UUID))->SetReadOnly(local->IsRunning());
	GetDlgItem(IDC_CREATE_UUID)->EnableWindow(!local->IsRunning());
	((CEdit*)GetDlgItem(IDC_PASSPHRASE))->SetReadOnly(local->IsRunning());
}

void CTestAppDlgMainTab::DoDataExchange(CDataExchange* pDX)
{
	CTestAppDlgTabCtrlPage::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CTestAppDlgMainTab, CTestAppDlgTabCtrlPage)
	ON_WM_TIMER()
	ON_WM_CTLCOLOR()
	ON_COMMAND(ID_PEERLIST_VIEW_DETAILS, &CTestAppDlgMainTab::OnPeerlistViewDetails)
	ON_NOTIFY(NM_RCLICK, IDC_ALL_PEERS_LIST, &CTestAppDlgMainTab::OnNMRClickAllPeersList)
	ON_NOTIFY(NM_DBLCLK, IDC_ALL_PEERS_LIST, &CTestAppDlgMainTab::OnNMDblclkAllPeersList)
	ON_COMMAND(ID_PEERLIST_DISCONNECT, &CTestAppDlgMainTab::OnPeerlistDisconnect)
	ON_COMMAND(ID_CONSOLE_ENABLED, &CTestAppDlgMainTab::OnConsoleEnabled)
	ON_UPDATE_COMMAND_UI(ID_CONSOLE_ENABLED, &CTestAppDlgMainTab::OnUpdateConsoleEnabled)
	ON_COMMAND(ID_CONSOLE_TERMINALWINDOW, &CTestAppDlgMainTab::OnConsoleTerminalwindow)
	ON_UPDATE_COMMAND_UI(ID_CONSOLE_TERMINALWINDOW, &CTestAppDlgMainTab::OnUpdateConsoleTerminalwindow)
	ON_UPDATE_COMMAND_UI(ID_VERBOSITY_SILENT, &CTestAppDlgMainTab::OnUpdateVerbositySilent)
	ON_UPDATE_COMMAND_UI(ID_VERBOSITY_MINIMAL, &CTestAppDlgMainTab::OnUpdateVerbosityMinimal)
	ON_UPDATE_COMMAND_UI(ID_VERBOSITY_NORMAL, &CTestAppDlgMainTab::OnUpdateVerbosityNormal)
	ON_UPDATE_COMMAND_UI(ID_VERBOSITY_VERBOSE, &CTestAppDlgMainTab::OnUpdateVerbosityVerbose)
	ON_UPDATE_COMMAND_UI(ID_VERBOSITY_DEBUG, &CTestAppDlgMainTab::OnUpdateVerbosityDebug)
	ON_COMMAND(ID_VERBOSITY_SILENT, &CTestAppDlgMainTab::OnVerbositySilent)
	ON_COMMAND(ID_VERBOSITY_MINIMAL, &CTestAppDlgMainTab::OnVerbosityMinimal)
	ON_COMMAND(ID_VERBOSITY_NORMAL, &CTestAppDlgMainTab::OnVerbosityNormal)
	ON_COMMAND(ID_VERBOSITY_VERBOSE, &CTestAppDlgMainTab::OnVerbosityVerbose)
	ON_COMMAND(ID_VERBOSITY_DEBUG, &CTestAppDlgMainTab::OnVerbosityDebug)
	ON_UPDATE_COMMAND_UI(ID_PEERLIST_VIEW_DETAILS, &CTestAppDlgMainTab::OnUpdatePeerlistViewDetails)
	ON_UPDATE_COMMAND_UI(ID_PEERLIST_DISCONNECT, &CTestAppDlgMainTab::OnUpdatePeerlistDisconnect)
	ON_UPDATE_COMMAND_UI(ID_PEERLIST_CREATE_RELAY, &CTestAppDlgMainTab::OnUpdatePeerlistCreateRelay)
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_ONLY_RELAYED_CHECK, &CTestAppDlgMainTab::OnBnClickedOnlyRelayedCheck)
	ON_BN_CLICKED(IDC_ONLY_AUTHENTICATED_CHECK, &CTestAppDlgMainTab::OnBnClickedOnlyAuthenticatedCheck)
	ON_BN_CLICKED(IDC_EXCLUDE_INBOUND_CHECK, &CTestAppDlgMainTab::OnBnClickedExcludeInboundCheck)
	ON_BN_CLICKED(IDC_EXCLUDE_OUTBOUND_CHECK, &CTestAppDlgMainTab::OnBnClickedExcludeOutboundCheck)
	ON_BN_CLICKED(IDC_CREATE_UUID, &CTestAppDlgMainTab::OnBnClickedCreateUuid)
	ON_BN_CLICKED(IDC_HAS_TEST_EXTENDER, &CTestAppDlgMainTab::OnBnClickedHasTestExtender)
	ON_BN_CLICKED(IDC_HAS_STRESS_EXTENDER, &CTestAppDlgMainTab::OnBnClickedHasStressExtender)
	ON_COMMAND(ID_PEERLIST_CREATE_RELAY, &CTestAppDlgMainTab::OnPeerlistCreateRelay)
END_MESSAGE_MAP()

BOOL CTestAppDlgMainTab::OnInitDialog()
{
	CTestAppDlgTabCtrlPage::OnInitDialog();

	SetValue(IDC_VERSION_INFO, GetApp()->GetAppVersion());

	m_Console = std::make_shared<TestAppConsole>();
	Console::SetOutput(m_Console);
	Console::SetVerbosity(Console::Verbosity::Debug);

	auto pctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);
	pctrl->SetExtendedStyle(LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);
	pctrl->InsertColumn(0, _T("Peer LUID"), LVCFMT_LEFT, GetApp()->GetScaledWidth(150));
	pctrl->InsertColumn(1, _T("Relayed"), LVCFMT_LEFT, GetApp()->GetScaledWidth(60));
	pctrl->InsertColumn(2, _T("Auth."), LVCFMT_LEFT, GetApp()->GetScaledWidth(50));
	pctrl->InsertColumn(3, _T("Peer endpoint"), LVCFMT_LEFT, GetApp()->GetScaledWidth(150));
	pctrl->InsertColumn(4, _T("Sent"), LVCFMT_LEFT, GetApp()->GetScaledWidth(70));
	pctrl->InsertColumn(5, _T("Received"), LVCFMT_LEFT, GetApp()->GetScaledWidth(70));

	LOGBRUSH br;
	br.lbStyle = BS_SOLID;
	br.lbColor = RGB(0, 0, 0);
	br.lbHatch = 0;
	m_ConsoleBrush.CreateBrushIndirect(&br);

	LOGFONT lf;
	((CEdit*)GetDlgItem(IDC_CONSOLE))->GetFont()->GetLogFont(&lf);
	const WCHAR font_name[] = L"Consolas";
	_tcsncpy_s(lf.lfFaceName, LF_FACESIZE, font_name, sizeof(font_name) / sizeof(WCHAR));

	if (m_ConsoleFont.CreateFontIndirect(&lf) != 0)
	{
		((CEdit*)GetDlgItem(IDC_CONSOLE))->SetFont(&m_ConsoleFont);
	}

	m_ConsoleTimer = SetTimer(CONSOLE_TIMER, 500, NULL);
	m_PeerActivityTimer = SetTimer(PEER_ACTIVITY_TIMER, 500, NULL);

	return TRUE;  // return TRUE unless you set the focus to a control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CTestAppDlgMainTab::UpdatePeers()
{
	auto local = GetQuantumGateInstance();
	const auto result = local->QueryPeers(m_PeerQueryParams, m_PeerLUIDs);
	if (result.Succeeded())
	{
		auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);

		for (const auto& pluid : m_PeerLUIDs)
		{
			local->GetPeer(pluid).Succeeded([&](auto& result)
			{
				const auto retval = result->GetDetails();
				if (retval.Succeeded())
				{
					auto index = GetPeerIndex(pluid);
					if (index == -1)
					{
						const auto pluidstr = Util::FormatString(L"%llu", pluid);
						const auto relayed = retval->IsRelayed ? L"Yes" : L"No";
						const auto auth = retval->IsAuthenticated ? L"Yes" : L"No";

						const auto pos = lctrl->InsertItem(0, pluidstr.c_str());
						if (pos != -1)
						{
							if (retval->IsSuspended) lctrl->SetItemData(pos, 1);
							else lctrl->SetItemData(pos, 0);

							lctrl->SetItemText(pos, 1, relayed);
							lctrl->SetItemText(pos, 2, auth);
							lctrl->SetItemText(pos, 3, retval->PeerEndpoint.GetString().c_str());
						}

						index = pos;
					}

					if (index != -1)
					{
						const auto itmd = lctrl->GetItemData(index);
						if (retval->IsSuspended && itmd == 0)
						{
							// Add hourglass emoji
							lctrl->SetItemText(index, 0, Util::FormatString(L"%llu | \u231B", pluid).c_str());
							lctrl->SetItemData(index, 1);
						}
						else if (!retval->IsSuspended && itmd == 1)
						{
							lctrl->SetItemText(index, 0, Util::FormatString(L"%llu", pluid).c_str());
							lctrl->SetItemData(index, 0);
						}

						lctrl->SetItemText(index, 4,
										   Util::FormatString(L"%.2lf KB",
															  static_cast<double>(retval->BytesSent) / 1024.0).c_str());
						lctrl->SetItemText(index, 5,
										   Util::FormatString(L"%.2lf KB",
															  static_cast<double>(retval->BytesReceived) / 1024.0).c_str());
					}
				}
			});
		}

		for (int x = 0; x < lctrl->GetItemCount(); ++x)
		{
			wchar_t* end = nullptr;
			PeerLUID pluid = wcstoull(lctrl->GetItemText(x, 0), &end, 10);

			if (std::find(m_PeerLUIDs.begin(), m_PeerLUIDs.end(), pluid) == m_PeerLUIDs.end())
			{
				lctrl->DeleteItem(x);
				--x;
			}
		}
	}
	else
	{
		auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);
		if (lctrl->GetItemCount() > 0) lctrl->DeleteAllItems();
	}
}

int CTestAppDlgMainTab::GetPeerIndex(const PeerLUID pluid)
{
	const auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);

	for (int x = 0; x < lctrl->GetItemCount(); ++x)
	{
		wchar_t* end = nullptr;
		PeerLUID id = wcstoull(lctrl->GetItemText(x, 0), &end, 10);

		if (id == pluid) return x;
	}

	return -1;
}

PeerLUID CTestAppDlgMainTab::GetSelectedPeerLUID()
{
	PeerLUID pluid{ 0 };

	const auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);
	auto pos = lctrl->GetFirstSelectedItemPosition();
	if (pos != nullptr)
	{
		const auto selitm = lctrl->GetNextSelectedItem(pos);
		wchar_t* end = nullptr;
		pluid = wcstoull(lctrl->GetItemText(selitm, 0), &end, 10);
	}

	return pluid;
}

void CTestAppDlgMainTab::OnTimer(UINT_PTR nIDEvent)
{
	if (IsWindowVisible())
	{
		if (nIDEvent == CONSOLE_TIMER) UpdateConsole();
		else if (nIDEvent == PEER_ACTIVITY_TIMER)
		{
			UpdatePeers();
		}
	}

	CTestAppDlgTabCtrlPage::OnTimer(nIDEvent);
}

HBRUSH CTestAppDlgMainTab::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if ((nCtlColor == CTLCOLOR_STATIC) && (GetDlgItem(IDC_CONSOLE)->GetSafeHwnd() == pWnd->GetSafeHwnd()))
	{
		pDC->SetTextColor(RGB(0, 255, 0));
		pDC->SetBkColor(RGB(0, 0, 0));
		return (HBRUSH)m_ConsoleBrush.GetSafeHandle();
	}
	else if (pWnd->GetExStyle() & WS_EX_TRANSPARENT)
	{
		pDC->SetBkMode(TRANSPARENT);
		return (HBRUSH)GetStockObject(HOLLOW_BRUSH);
	}

	return CTestAppDlgTabCtrlPage::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CTestAppDlgMainTab::OnPeerlistViewDetails()
{
	const auto pluid = GetSelectedPeerLUID();
	if (pluid > 0)
	{
		GetQuantumGateInstance()->GetPeer(pluid).Succeeded([&](auto& result)
		{
			const auto retval = result->GetDetails();
			if (retval.Succeeded())
			{
				LogPeerDetails(*result);

				String pitxt;
				pitxt += Util::FormatString(L"Peer LUID:\t\t%llu\r\n", pluid);
				pitxt += Util::FormatString(L"Peer UUID:\t\t%s\r\n\r\n", retval->PeerUUID.GetString().c_str());

				pitxt += Util::FormatString(L"Suspended:\t\t%s\r\n\r\n",
											retval->IsSuspended ? L"Yes" : L"No");

				pitxt += Util::FormatString(L"Authenticated:\t\t%s\r\n",
											retval->IsAuthenticated ? L"Yes" : L"No");
				pitxt += Util::FormatString(L"Relayed:\t\t\t%s\r\n",
											retval->IsRelayed ? L"Yes" : L"No");
				pitxt += Util::FormatString(L"Global shared secret:\t%s\r\n\r\n",
											retval->IsUsingGlobalSharedSecret ? L"Yes" : L"No");

				pitxt += Util::FormatString(L"Connection type:\t\t%s\r\n",
											retval->ConnectionType == QuantumGate::Peer::ConnectionType::Inbound ? L"Inbound" : L"Outbound");

				String alg = Util::FormatString(L"Hash: %s\r\n", Crypto::GetAlgorithmName(retval->ConnectionAlgorithms.Hash));
				alg += Util::FormatString(L"\t\t\tPrimary Asymmetric: %s\r\n", Crypto::GetAlgorithmName(retval->ConnectionAlgorithms.PrimaryAsymmetric));
				alg += Util::FormatString(L"\t\t\tSecondary Asymmetric: %s\r\n", Crypto::GetAlgorithmName(retval->ConnectionAlgorithms.SecondaryAsymmetric));
				alg += Util::FormatString(L"\t\t\tSymmetric: %s\r\n", Crypto::GetAlgorithmName(retval->ConnectionAlgorithms.Symmetric));
				alg += Util::FormatString(L"\t\t\tCompression: %s\r\n", Crypto::GetAlgorithmName(retval->ConnectionAlgorithms.Compression));

				pitxt += Util::FormatString(L"Connection algorithms:\t%s\r\n", alg.c_str());

				pitxt += Util::FormatString(L"Local endpoint:\t\t%s\r\n",
											retval->LocalEndpoint.GetString().c_str());
				pitxt += Util::FormatString(L"Peer endpoint:\t\t%s\r\n",
											retval->PeerEndpoint.GetString().c_str());
				pitxt += Util::FormatString(L"Peer protocol version:\t%u.%u\r\n",
											retval->PeerProtocolVersion.first, retval->PeerProtocolVersion.second);
				pitxt += Util::FormatString(L"Local session ID:\t\t%llu\r\n", retval->LocalSessionID);
				pitxt += Util::FormatString(L"Peer session ID:\t\t%llu\r\n", retval->PeerSessionID);
				pitxt += Util::FormatString(L"Connected time:\t\t%jd seconds\r\n",
											std::chrono::duration_cast<std::chrono::seconds>(retval->ConnectedTime).count());
				pitxt += Util::FormatString(L"Bytes received:\t\t%zu\r\n", retval->BytesReceived);
				pitxt += Util::FormatString(L"Bytes sent:\t\t%zu\r\n", retval->BytesSent);
				pitxt += Util::FormatString(L"Extenders bytes received:\t%zu\r\n", retval->ExtendersBytesReceived);
				pitxt += Util::FormatString(L"Extenders bytes sent:\t%zu\r\n", retval->ExtendersBytesSent);

				auto roh = 0.0;
				if (retval->BytesReceived > 0)
				{
					roh = (((double)retval->BytesReceived - (double)retval->ExtendersBytesReceived) /
						(double)retval->BytesReceived) * 100.0;
				}

				auto soh = 0.0;
				if (retval->BytesSent > 0)
				{
					soh = (((double)retval->BytesSent - (double)retval->ExtendersBytesSent) /
						(double)retval->BytesSent) * 100.0;
				}

				pitxt += Util::FormatString(L"Receive overhead:\t\t%.2lf%%\r\n", roh);
				pitxt += Util::FormatString(L"Send overhead:\t\t%.2lf%%\r\n", soh);

				CInformationDlg dlg;
				dlg.SetWindowTitle(L"Peer Information");
				dlg.SetInformationText(pitxt.c_str());
				dlg.DoModal();
			}
		});
	}
}

void CTestAppDlgMainTab::LogPeerDetails(const QuantumGate::Peer& peer)
{
	// We're using the individual member functions from QuantumGate::Peer
	// instead of the more efficient GetDetails() member as a test

	String pitxt{ L"Logging peer details:\r\n\r\n" };
	pitxt += Util::FormatString(L"Peer LUID:\t\t\t%llu\r\n", peer.GetLUID());

	peer.GetUUID().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Peer UUID:\t\t\t%s\r\n\r\n", result->GetString().c_str());
	});

	peer.GetSuspended().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Suspended:\t\t\t%s\r\n\r\n", *result ? L"Yes" : L"No");
	});

	peer.GetAuthenticated().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Authenticated:\t\t\t%s\r\n", *result ? L"Yes" : L"No");
	});

	peer.GetRelayed().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Relayed:\t\t\t%s\r\n", *result ? L"Yes" : L"No");
	});

	peer.GetUsingGlobalSharedSecret().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Global shared secret:\t\t%s\r\n\r\n", *result ? L"Yes" : L"No");
	});

	peer.GetConnectionType().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Connection type:\t\t%s\r\n",
									*result == QuantumGate::Peer::ConnectionType::Inbound ? L"Inbound" : L"Outbound");
	});

	peer.GetConnectionAlgorithms().Succeeded([&](auto& result)
	{
		String alg = Util::FormatString(L"Hash: %s\r\n", Crypto::GetAlgorithmName(result->Hash));
		alg += Util::FormatString(L"\t\t\t\tPrimary Asymmetric: %s\r\n", Crypto::GetAlgorithmName(result->PrimaryAsymmetric));
		alg += Util::FormatString(L"\t\t\t\tSecondary Asymmetric: %s\r\n", Crypto::GetAlgorithmName(result->SecondaryAsymmetric));
		alg += Util::FormatString(L"\t\t\t\tSymmetric: %s\r\n", Crypto::GetAlgorithmName(result->Symmetric));
		alg += Util::FormatString(L"\t\t\t\tCompression: %s\r\n", Crypto::GetAlgorithmName(result->Compression));

		pitxt += Util::FormatString(L"Connection algorithms:\t\t%s\r\n", alg.c_str());
	});

	peer.GetLocalEndpoint().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Local endpoint:\t\t\t%s\r\n", result->GetString().c_str());
	});

	peer.GetPeerEndpoint().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Peer endpoint:\t\t\t%s\r\n", result->GetString().c_str());
	});

	peer.GetPeerProtocolVersion().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Peer protocol version:\t\t%u.%u\r\n", result->first, result->second);
	});

	peer.GetLocalSessionID().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Local session ID:\t\t%llu\r\n", *result);
	});

	peer.GetPeerSessionID().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Peer session ID:\t\t%llu\r\n", *result);
	});

	peer.GetConnectedTime().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Connected time:\t\t\t%jd seconds\r\n",
									std::chrono::duration_cast<std::chrono::seconds>(*result).count());
	});

	peer.GetBytesReceived().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Bytes received:\t\t\t%zu\r\n", *result);
	});

	peer.GetBytesSent().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Bytes sent:\t\t\t%zu\r\n", *result);
	});

	peer.GetExtendersBytesReceived().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Extenders bytes received:\t%zu\r\n", *result);
	});

	peer.GetExtendersBytesSent().Succeeded([&](auto& result)
	{
		pitxt += Util::FormatString(L"Extenders bytes sent:\t\t%zu\r\n\r\n", *result);
	});

	LogInfo(pitxt.c_str());
}

void CTestAppDlgMainTab::OnNMRClickAllPeersList(NMHDR* pNMHDR, LRESULT* pResult)
{
	CMenu menu;
	if (menu.LoadMenu(IDR_POPUPS))
	{
		auto submenu = menu.GetSubMenu(0);

		submenu->SetDefaultItem(ID_PEERLIST_VIEW_DETAILS);

		CPoint pos;
		GetCursorPos(&pos);

		const auto cmd = submenu->TrackPopupMenuEx(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
												   pos.x, pos.y, this, NULL);
		if (cmd) SendMessage(WM_COMMAND, cmd);
	}

	*pResult = 0;
}

void CTestAppDlgMainTab::OnUpdatePeerlistViewDetails(CCmdUI* pCmdUI)
{
	const auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);
	pCmdUI->Enable(GetQuantumGateInstance()->IsRunning() && (lctrl->GetSelectedCount() > 0));
}

void CTestAppDlgMainTab::OnUpdatePeerlistDisconnect(CCmdUI* pCmdUI)
{
	const auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);
	pCmdUI->Enable(GetQuantumGateInstance()->IsRunning() && (lctrl->GetSelectedCount() > 0));
}

void CTestAppDlgMainTab::OnUpdatePeerlistCreateRelay(CCmdUI* pCmdUI)
{
	const auto lctrl = (CListCtrl*)GetDlgItem(IDC_ALL_PEERS_LIST);
	pCmdUI->Enable(GetQuantumGateInstance()->IsRunning() && (lctrl->GetSelectedCount() > 0));
}

void CTestAppDlgMainTab::OnNMDblclkAllPeersList(NMHDR* pNMHDR, LRESULT* pResult)
{
	OnPeerlistViewDetails();
	*pResult = 0;
}

void CTestAppDlgMainTab::OnPeerlistDisconnect()
{
	const auto pluid = GetSelectedPeerLUID();
	if (pluid != 0)
	{
		if (!GetQuantumGateInstance()->DisconnectFrom(pluid, [](QuantumGate::PeerLUID pluid, const PeerUUID puuid) mutable
		{
			LogInfo(L"Peer LUID %llu manually disconnected", pluid);
		}))
		{
			LogErr(L"Failed to disconnect from peer LUID %llu", pluid);
		}
	}
}

void CTestAppDlgMainTab::OnPeerlistCreateRelay()
{
	const auto pluid = GetSelectedPeerLUID();
	if (pluid != 0)
	{
		((CTestAppDlg*)GetParent()->GetParent())->CreateRelayedConnection(pluid);
	}
}

void CTestAppDlgMainTab::UpdateConsole()
{
	if (m_Console->GetNewMessageEvent()->IsSet())
	{
		if (m_Console->TryLock())
		{
			SetValue(IDC_CONSOLE, *m_Console->GetMessages());

			CEdit* pEdit = (CEdit*)GetDlgItem(IDC_CONSOLE);
			pEdit->LineScroll(pEdit->GetLineCount());

			m_Console->GetNewMessageEvent()->Reset();

			m_Console->Unlock();
		}
	}
}

void CTestAppDlgMainTab::OnConsoleEnabled()
{
	if (m_ConsoleState == ConsoleState::Enabled || m_ConsoleState == ConsoleState::EnabledWindow)
	{
		m_ConsoleState = ConsoleState::Disabled;
	}
	else m_ConsoleState = ConsoleState::Enabled;

	UpdateConsoleState();
}

void CTestAppDlgMainTab::OnUpdateConsoleEnabled(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck((m_ConsoleState == ConsoleState::Enabled || m_ConsoleState == ConsoleState::EnabledWindow) ? 1 : 0);
}

void CTestAppDlgMainTab::OnConsoleTerminalwindow()
{
	if (m_ConsoleState == ConsoleState::EnabledWindow) m_ConsoleState = ConsoleState::Enabled;
	else m_ConsoleState = ConsoleState::EnabledWindow;

	UpdateConsoleState();
}

void CTestAppDlgMainTab::OnUpdateConsoleTerminalwindow(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_ConsoleState == ConsoleState::Enabled || m_ConsoleState == ConsoleState::EnabledWindow);
	pCmdUI->SetCheck(m_ConsoleState == ConsoleState::EnabledWindow ? 1 : 0);
}

void CTestAppDlgMainTab::UpdateConsoleState()
{
	if (m_ConsoleState == ConsoleState::EnabledWindow)
	{
		Console::SetOutput(std::make_shared<Console::WindowOutput>());
	}
	else if (m_ConsoleState == ConsoleState::Enabled)
	{
		Console::SetOutput(m_Console);
	}
	else
	{
		Console::SetOutput(nullptr);
	}
}

void CTestAppDlgMainTab::OnUpdateVerbositySilent(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(Console::GetVerbosity() == Console::Verbosity::Silent);
}

void CTestAppDlgMainTab::OnUpdateVerbosityMinimal(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(Console::GetVerbosity() == Console::Verbosity::Minimal);
}

void CTestAppDlgMainTab::OnUpdateVerbosityNormal(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(Console::GetVerbosity() == Console::Verbosity::Normal);
}

void CTestAppDlgMainTab::OnUpdateVerbosityVerbose(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(Console::GetVerbosity() == Console::Verbosity::Verbose);
}

void CTestAppDlgMainTab::OnUpdateVerbosityDebug(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(Console::GetVerbosity() == Console::Verbosity::Debug);
}

void CTestAppDlgMainTab::OnVerbositySilent()
{
	Console::SetVerbosity(Console::Verbosity::Silent);
}

void CTestAppDlgMainTab::OnVerbosityMinimal()
{
	Console::SetVerbosity(Console::Verbosity::Minimal);
}

void CTestAppDlgMainTab::OnVerbosityNormal()
{
	Console::SetVerbosity(Console::Verbosity::Normal);
}

void CTestAppDlgMainTab::OnVerbosityVerbose()
{
	Console::SetVerbosity(Console::Verbosity::Verbose);
}

void CTestAppDlgMainTab::OnVerbosityDebug()
{
	Console::SetVerbosity(Console::Verbosity::Debug);
}

void CTestAppDlgMainTab::OnDestroy()
{
	if (m_ConsoleTimer != 0)
	{
		KillTimer(m_ConsoleTimer);
		m_ConsoleTimer = 0;
	}

	if (m_PeerActivityTimer != 0)
	{
		KillTimer(m_PeerActivityTimer);
		m_PeerActivityTimer = 0;
	}

	CTestAppDlgTabCtrlPage::OnDestroy();
}

void CTestAppDlgMainTab::OnBnClickedOnlyRelayedCheck()
{
	if (((CButton*)GetDlgItem(IDC_ONLY_RELAYED_CHECK))->GetCheck() == BST_CHECKED)
	{
		m_PeerQueryParams.Relays = PeerQueryParameters::RelayOption::Relayed;
	}
	else m_PeerQueryParams.Relays = PeerQueryParameters::RelayOption::Both;
}

void CTestAppDlgMainTab::OnBnClickedOnlyAuthenticatedCheck()
{
	if (((CButton*)GetDlgItem(IDC_ONLY_AUTHENTICATED_CHECK))->GetCheck() == BST_CHECKED)
	{
		m_PeerQueryParams.Authentication = PeerQueryParameters::AuthenticationOption::Authenticated;
	}
	else m_PeerQueryParams.Authentication = PeerQueryParameters::AuthenticationOption::Both;
}

void CTestAppDlgMainTab::OnBnClickedExcludeInboundCheck()
{
	if (((CButton*)GetDlgItem(IDC_EXCLUDE_INBOUND_CHECK))->GetCheck() == BST_CHECKED)
	{
		((CButton*)GetDlgItem(IDC_EXCLUDE_OUTBOUND_CHECK))->SetCheck(BST_UNCHECKED);

		m_PeerQueryParams.Connections = PeerQueryParameters::ConnectionOption::Outbound;
	}
	else m_PeerQueryParams.Connections = PeerQueryParameters::ConnectionOption::Both;
}

void CTestAppDlgMainTab::OnBnClickedExcludeOutboundCheck()
{
	if (((CButton*)GetDlgItem(IDC_EXCLUDE_OUTBOUND_CHECK))->GetCheck() == BST_CHECKED)
	{
		((CButton*)GetDlgItem(IDC_EXCLUDE_INBOUND_CHECK))->SetCheck(BST_UNCHECKED);

		m_PeerQueryParams.Connections = PeerQueryParameters::ConnectionOption::Inbound;
	}
	else m_PeerQueryParams.Connections = PeerQueryParameters::ConnectionOption::Both;
}

void CTestAppDlgMainTab::OnBnClickedCreateUuid()
{
	const auto ret = AfxMessageBox(L"Are you sure you want to create a new UUID for the local instance?",
								   MB_ICONQUESTION | MB_YESNO);
	if (ret == IDYES)
	{
		const auto& [success, uuid, keys] = QuantumGate::UUID::Create(QuantumGate::UUID::Type::Peer,
																	  QuantumGate::UUID::SignAlgorithm::EDDSA_ED25519);
		if (success)
		{
			const auto privname = GetApp()->GetFolder() + L"private_" + uuid.GetString() + L".pem";
			const auto pubname = GetApp()->GetFolder() + L"public_" + uuid.GetString() + L".pem";

			if (GetApp()->SaveKey(privname, keys->PrivateKey) &&
				GetApp()->SaveKey(pubname, keys->PublicKey))
			{
				SetValue(IDC_LOCAL_UUID, uuid.GetString().c_str());

				const auto msg = L"The UUID '" + uuid.GetString() +
					L"' has been created. The associated asymmetric key pair has been saved to the following files in the program folder:\r\n\r\n" +
					pubname + L"\r\n\r\n" + privname;

				AfxMessageBox(msg.c_str(), MB_ICONINFORMATION);
			}
		}
		else
		{
			AfxMessageBox(L"Couldn't create UUID.", MB_ICONERROR);
		}
	}
}

void CTestAppDlgMainTab::OnBnClickedHasTestExtender()
{
	if (((CButton*)GetDlgItem(IDC_HAS_TEST_EXTENDER))->GetCheck() == BST_CHECKED)
	{
		m_PeerQueryParams.Extenders.UUIDs.insert(QuantumGate::UUID(L"40fcae06-d89b-0970-2e63-148521af0aac"));
	}
	else
	{
		m_PeerQueryParams.Extenders.UUIDs.erase(QuantumGate::UUID(L"40fcae06-d89b-0970-2e63-148521af0aac"));
	}

	m_PeerQueryParams.Extenders.Include = PeerQueryParameters::Extenders::IncludeOption::AllOf;
}

void CTestAppDlgMainTab::OnBnClickedHasStressExtender()
{
	if (((CButton*)GetDlgItem(IDC_HAS_STRESS_EXTENDER))->GetCheck() == BST_CHECKED)
	{
		m_PeerQueryParams.Extenders.UUIDs.insert(QuantumGate::UUID(L"2ddd4019-e6d1-09a5-2ec7-9c51af0304cb"));
	}
	else
	{
		m_PeerQueryParams.Extenders.UUIDs.erase(QuantumGate::UUID(L"2ddd4019-e6d1-09a5-2ec7-9c51af0304cb"));
	}

	m_PeerQueryParams.Extenders.Include = PeerQueryParameters::Extenders::IncludeOption::AllOf;
}

