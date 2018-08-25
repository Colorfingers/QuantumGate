// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "stdafx.h"
#include "TestApp.h"
#include "CTestAppDlgTestExtenderTab.h"

#include "Console.h"
#include "Common\Util.h"

using namespace QuantumGate::Implementation;

IMPLEMENT_DYNAMIC(CTestAppDlgTestExtenderTab, CDialogBase)

CTestAppDlgTestExtenderTab::CTestAppDlgTestExtenderTab(QuantumGate::Local& local, CWnd* pParent /*=nullptr*/)
	: CDialogBase(IDD_QGTESTAPP_DIALOG_TESTEXTENDER_TAB, pParent), m_QuantumGate(local)
{}

CTestAppDlgTestExtenderTab::~CTestAppDlgTestExtenderTab()
{}

void CTestAppDlgTestExtenderTab::UpdateControls() noexcept
{
	auto peerselected = false;
	const auto lbox = (CListBox*)GetDlgItem(IDC_PEERLIST);
	if (lbox->GetCurSel() != LB_ERR) peerselected = true;

	GetDlgItem(IDC_SENDTEXT)->EnableWindow(m_QuantumGate.IsRunning());
	GetDlgItem(IDC_SENDBUTTON)->EnableWindow(m_QuantumGate.IsRunning() && peerselected);
	GetDlgItem(IDC_SENDCHECK)->EnableWindow(m_QuantumGate.IsRunning() && (peerselected || m_SendThread != nullptr));
	GetDlgItem(IDC_SENDSECONDS)->EnableWindow(m_QuantumGate.IsRunning() && m_SendThread == nullptr);
	GetDlgItem(IDC_SENDFILE)->EnableWindow(m_QuantumGate.IsRunning() && peerselected);
	GetDlgItem(IDC_AUTO_SENDFILE)->EnableWindow(m_QuantumGate.IsRunning() && peerselected);

	GetDlgItem(IDC_SENDSTRESS)->EnableWindow(m_QuantumGate.IsRunning() && peerselected);
	GetDlgItem(IDC_NUMSTRESSMESS)->EnableWindow(m_QuantumGate.IsRunning());
}

void CTestAppDlgTestExtenderTab::DoDataExchange(CDataExchange* pDX)
{
	CDialogBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CTestAppDlgTestExtenderTab, CDialogBase)
	ON_MESSAGE(WMQG_PEER_EVENT, &CTestAppDlgTestExtenderTab::OnQGPeerEvent)
	ON_MESSAGE(WMQG_PEER_FILEACCEPT, &CTestAppDlgTestExtenderTab::OnQGPeerFileAccept)
	ON_MESSAGE(WMQG_EXTENDER_INIT, &CTestAppDlgTestExtenderTab::OnQGExtenderInit)
	ON_MESSAGE(WMQG_EXTENDER_DEINIT, &CTestAppDlgTestExtenderTab::OnQGExtenderDeInit)
	ON_BN_CLICKED(IDC_SENDBUTTON, &CTestAppDlgTestExtenderTab::OnBnClickedSendbutton)
	ON_BN_CLICKED(IDC_SENDCHECK, &CTestAppDlgTestExtenderTab::OnBnClickedSendcheck)
	ON_BN_CLICKED(IDC_SENDFILE, &CTestAppDlgTestExtenderTab::OnBnClickedSendfile)
	ON_COMMAND(ID_STRESSEXTENDER_LOAD, &CTestAppDlgTestExtenderTab::OnStressExtenderLoad)
	ON_UPDATE_COMMAND_UI(ID_STRESSEXTENDER_LOAD, &CTestAppDlgTestExtenderTab::OnUpdateStressextenderLoad)
	ON_COMMAND(ID_STRESSEXTENDER_USE, &CTestAppDlgTestExtenderTab::OnStressExtenderUse)
	ON_UPDATE_COMMAND_UI(ID_STRESSEXTENDER_USE, &CTestAppDlgTestExtenderTab::OnUpdateStressExtenderUse)
	ON_COMMAND(ID_STRESSEXTENDER_MESSAGES, &CTestAppDlgTestExtenderTab::OnStressextenderMessages)
	ON_UPDATE_COMMAND_UI(ID_STRESSEXTENDER_MESSAGES, &CTestAppDlgTestExtenderTab::OnUpdateStressextenderMessages)
	ON_COMMAND(ID_TESTEXTENDER_LOAD, &CTestAppDlgTestExtenderTab::OnTestExtenderLoad)
	ON_UPDATE_COMMAND_UI(ID_TESTEXTENDER_LOAD, &CTestAppDlgTestExtenderTab::OnUpdateTestExtenderLoad)
	ON_COMMAND(ID_TESTEXTENDER_USECOMPRESSION, &CTestAppDlgTestExtenderTab::OnTestExtenderUseCompression)
	ON_UPDATE_COMMAND_UI(ID_TESTEXTENDER_USECOMPRESSION, &CTestAppDlgTestExtenderTab::OnUpdateTestExtenderUseCompression)
	ON_COMMAND(ID_STRESSEXTENDER_USECOMPRESSION, &CTestAppDlgTestExtenderTab::OnStressExtenderUseCompression)
	ON_UPDATE_COMMAND_UI(ID_STRESSEXTENDER_USECOMPRESSION, &CTestAppDlgTestExtenderTab::OnUpdateStressExtenderUseCompression)
	ON_BN_CLICKED(IDC_SENDSTRESS, &CTestAppDlgTestExtenderTab::OnBnClickedSendstress)
	ON_LBN_SELCHANGE(IDC_PEERLIST, &CTestAppDlgTestExtenderTab::OnLbnSelchangePeerlist)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_COMMAND(ID_EXCEPTIONTEST_STARTUP, &CTestAppDlgTestExtenderTab::OnExceptiontestStartup)
	ON_UPDATE_COMMAND_UI(ID_EXCEPTIONTEST_STARTUP, &CTestAppDlgTestExtenderTab::OnUpdateExceptiontestStartup)
	ON_COMMAND(ID_EXCEPTIONTEST_POSTSTARTUP, &CTestAppDlgTestExtenderTab::OnExceptiontestPoststartup)
	ON_UPDATE_COMMAND_UI(ID_EXCEPTIONTEST_POSTSTARTUP, &CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPoststartup)
	ON_COMMAND(ID_EXCEPTIONTEST_PRESHUTDOWN, &CTestAppDlgTestExtenderTab::OnExceptiontestPreshutdown)
	ON_UPDATE_COMMAND_UI(ID_EXCEPTIONTEST_PRESHUTDOWN, &CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPreshutdown)
	ON_COMMAND(ID_EXCEPTIONTEST_SHUTDOWN, &CTestAppDlgTestExtenderTab::OnExceptiontestShutdown)
	ON_UPDATE_COMMAND_UI(ID_EXCEPTIONTEST_SHUTDOWN, &CTestAppDlgTestExtenderTab::OnUpdateExceptiontestShutdown)
	ON_COMMAND(ID_EXCEPTIONTEST_PEEREVENT, &CTestAppDlgTestExtenderTab::OnExceptiontestPeerevent)
	ON_UPDATE_COMMAND_UI(ID_EXCEPTIONTEST_PEEREVENT, &CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPeerevent)
	ON_COMMAND(ID_EXCEPTIONTEST_PEERMESSAGE, &CTestAppDlgTestExtenderTab::OnExceptiontestPeermessage)
	ON_UPDATE_COMMAND_UI(ID_EXCEPTIONTEST_PEERMESSAGE, &CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPeermessage)
	ON_BN_CLICKED(IDC_BROWSE, &CTestAppDlgTestExtenderTab::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_AUTO_SENDFILE, &CTestAppDlgTestExtenderTab::OnBnClickedAutoSendfile)
END_MESSAGE_MAP()


BOOL CTestAppDlgTestExtenderTab::OnInitDialog()
{
	CDialogBase::OnInitDialog();

	SetValue(IDC_SENDTEXT, L"Hello world");
	SetValue(IDC_SENDSECONDS, L"10");
	SetValue(IDC_NUMSTRESSMESS, L"100000");

	auto lctrl = (CListCtrl*)GetDlgItem(IDC_FILETRANSFER_LIST);
	lctrl->SetExtendedStyle(LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);
	lctrl->InsertColumn(0, _T("ID"), LVCFMT_LEFT, 0);
	lctrl->InsertColumn(1, _T("Filename"), LVCFMT_LEFT, 200);
	lctrl->InsertColumn(2, _T("Progress"), LVCFMT_LEFT, 75);
	lctrl->InsertColumn(3, _T("Status"), LVCFMT_LEFT, 100);

	m_PeerActivityTimer = SetTimer(EXTENDER_PEER_ACTIVITY_TIMER, 500, NULL);

	return TRUE;  // return TRUE unless you set the focus to a control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CTestAppDlgTestExtenderTab::UpdatePeerActivity()
{
	auto pluid = GetSelectedPeerLUID();
	if (pluid != 0 && m_TestExtender != nullptr)
	{
		m_TestExtender->GetPeers()->IfSharedLock([&](const TestExtender::Peers& peers)
		{
			const auto peer = peers.find(pluid);
			if (peer != peers.end())
			{
				peer->second->FileTransfers.IfSharedLock([&](const TestExtender::FileTransfers& filetransfers)
				{
					UpdateFileTransfers(filetransfers);
				});
			}
		});
	}
	else
	{
		auto lctrl = (CListCtrl*)GetDlgItem(IDC_FILETRANSFER_LIST);
		lctrl->DeleteAllItems();
	}
}

const PeerLUID CTestAppDlgTestExtenderTab::GetSelectedPeerLUID()
{
	PeerLUID pluid{ 0 };

	const auto lbox = (CListBox*)GetDlgItem(IDC_PEERLIST);
	if (lbox->GetCurSel() != LB_ERR)
	{
		CString pluidtxt;
		lbox->GetText(lbox->GetCurSel(), pluidtxt);
		if (pluidtxt.GetLength() != 0)
		{
			wchar_t* end = nullptr;
			pluid = wcstoull(pluidtxt, &end, 10);
		}
	}

	return pluid;
}

void CTestAppDlgTestExtenderTab::UpdateFileTransfers(const TestExtender::FileTransfers& filetransfers)
{
	auto lctrl = (CListCtrl*)GetDlgItem(IDC_FILETRANSFER_LIST);

	for (auto& filetransfer : filetransfers)
	{
		const auto id = filetransfer.second->GetID();

		const auto perc = ((double)filetransfer.second->GetNumBytesTransferred() / (double)filetransfer.second->GetFileSize()) * 100.0;
		const auto percstr = Util::FormatString(L"%.2f%%", perc);
		const auto status = filetransfer.second->GetStatusString();

		const auto index = GetFileTransferIndex(id);
		if (index != -1)
		{
			lctrl->SetItemText(index, 2, percstr.c_str());
			lctrl->SetItemText(index, 3, status.c_str());
		}
		else
		{
			const auto idstr = Util::FormatString(L"%llu", id);

			const auto pos = lctrl->InsertItem(0, idstr.c_str());
			if (pos != -1)
			{
				lctrl->SetItemText(pos, 1, filetransfer.second->GetFileName().c_str());
				lctrl->SetItemText(pos, 2, percstr.c_str());
				lctrl->SetItemText(pos, 3, status.c_str());
			}
		}
	}

	for (int x = 0; x < lctrl->GetItemCount(); x++)
	{
		wchar_t* end = nullptr;
		TestExtender::FileTransferID id = wcstoull(lctrl->GetItemText(x, 0), &end, 10);

		if (filetransfers.find(id) == filetransfers.end())
		{
			lctrl->DeleteItem(x);
			x--;
		}
	}
}

const int CTestAppDlgTestExtenderTab::GetFileTransferIndex(const TestExtender::FileTransferID id)
{
	const auto lctrl = (CListCtrl*)GetDlgItem(IDC_FILETRANSFER_LIST);

	for (int x = 0; x < lctrl->GetItemCount(); x++)
	{
		wchar_t* end = nullptr;
		TestExtender::FileTransferID fid = wcstoull(lctrl->GetItemText(x, 0), &end, 10);

		if (id == fid) return x;
	}

	return -1;
}

void CTestAppDlgTestExtenderTab::LoadTestExtender()
{
	if (m_TestExtender == nullptr)
	{
		m_TestExtender = std::make_shared<TestExtender::Extender>(GetSafeHwnd());
		m_TestExtender->SetAutoFileTransferPath(GetApp()->GetFolder());
		auto extp = std::static_pointer_cast<Extender>(m_TestExtender);
		if (!m_QuantumGate.AddExtender(extp))
		{
			LogErr(L"Failed to add TestExtender");
			m_TestExtender.reset();
		}
	}
}

void CTestAppDlgTestExtenderTab::UnloadTestExtender()
{
	if (m_TestExtender != nullptr)
	{
		auto extp = std::static_pointer_cast<Extender>(m_TestExtender);
		if (!m_QuantumGate.RemoveExtender(extp))
		{
			LogErr(L"Failed to remove TestExtender");
		}
		else m_TestExtender.reset();
	}
}

void CTestAppDlgTestExtenderTab::LoadStressExtender()
{
	if (m_StressExtender == nullptr)
	{
		m_StressExtender = std::make_shared<StressExtender::Extender>();
		auto extp = std::static_pointer_cast<Extender>(m_StressExtender);
		if (!m_QuantumGate.AddExtender(extp))
		{
			LogErr(L"Failed to add StressExtender");
			m_StressExtender.reset();
		}
	}
}

void CTestAppDlgTestExtenderTab::UnloadStressExtender()
{
	if (m_StressExtender != nullptr)
	{
		m_UseStressExtender = false;

		auto extp = std::static_pointer_cast<Extender>(m_StressExtender);
		if (!m_QuantumGate.RemoveExtender(extp))
		{
			LogErr(L"Failed to remove StressExtender");
		}
		else m_StressExtender.reset();
	}
}

void CTestAppDlgTestExtenderTab::UpdateStressExtenderExceptionTest(CCmdUI* pCmdUI, const bool test) const noexcept
{
	pCmdUI->Enable(m_StressExtender != nullptr);
	pCmdUI->SetCheck(m_StressExtender != nullptr && test);
}

void CTestAppDlgTestExtenderTab::OnBnClickedSendbutton()
{
	SendMsgToPeer(GetSelectedPeerLUID(), GetTextValue(IDC_SENDTEXT));
}

void CTestAppDlgTestExtenderTab::OnBnClickedSendcheck()
{
	const CButton* check = (CButton*)GetDlgItem(IDC_SENDCHECK);
	if (check->GetCheck() == BST_CHECKED)
	{
		StartSendThread();
	}
	else StopSendThread();
}

void CTestAppDlgTestExtenderTab::StartSendThread()
{
	if (m_SendThread == nullptr)
	{
		m_SendThreadStop = false;

		const auto ms = static_cast<int>(GetInt64Value(IDC_SENDSECONDS));
		const auto txt = GetTextValue(IDC_SENDTEXT);
		const auto pluid = GetSelectedPeerLUID();

		m_SendThread = std::make_unique<std::thread>(CTestAppDlgTestExtenderTab::SendThreadProc, this, ms, pluid, txt);

		const auto check = (CButton*)GetDlgItem(IDC_SENDCHECK);
		check->SetCheck(BST_CHECKED);

		UpdateControls();
	}
}

void CTestAppDlgTestExtenderTab::StopSendThread()
{
	if (m_SendThread != nullptr)
	{
		m_SendThreadStop = true;
		if (m_SendThread->joinable()) m_SendThread->join();
		m_SendThread.reset();

		auto check = (CButton*)GetDlgItem(IDC_SENDCHECK);
		check->SetCheck(BST_UNCHECKED);

		UpdateControls();
	}
}

void CTestAppDlgTestExtenderTab::SendThreadProc(CTestAppDlgTestExtenderTab* dlg, int interval,
												  PeerLUID pluid, CString txt)
{
	while (!dlg->m_SendThreadStop)
	{
		dlg->SendMsgToPeer(pluid, txt);

		std::this_thread::sleep_for(std::chrono::milliseconds(interval));
	}
}

const bool CTestAppDlgTestExtenderTab::SendMsgToPeer(PeerLUID pluid, CString txt)
{
	if (m_UseStressExtender) return m_StressExtender->SendMessage(pluid, txt.GetString());
	else if (m_TestExtender != nullptr) return m_TestExtender->SendMessage(pluid, txt.GetString());

	return false;
}

void CTestAppDlgTestExtenderTab::OnBnClickedSendfile()
{
	const auto path = GetApp()->BrowseForFile(GetSafeHwnd(), false);
	if (path)
	{
		m_TestExtender->SendFile(GetSelectedPeerLUID(), path->GetString(), false);
	}
}

void CTestAppDlgTestExtenderTab::OnStressExtenderLoad()
{
	if (m_StressExtender == nullptr) LoadStressExtender();
	else UnloadStressExtender();
}

void CTestAppDlgTestExtenderTab::OnUpdateStressextenderLoad(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_StressExtender != nullptr);
}

void CTestAppDlgTestExtenderTab::OnStressExtenderUse()
{
	m_UseStressExtender = !m_UseStressExtender;
}

void CTestAppDlgTestExtenderTab::OnUpdateStressExtenderUse(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_StressExtender != nullptr);
	pCmdUI->SetCheck(m_UseStressExtender);
}


void CTestAppDlgTestExtenderTab::OnStressextenderMessages()
{
	const auto pluid = GetSelectedPeerLUID();
	if (pluid != 0) m_StressExtender->BenchmarkSendMessage(pluid);
	else AfxMessageBox(L"Select a connected peer first from the list.", MB_ICONINFORMATION);
}

void CTestAppDlgTestExtenderTab::OnUpdateStressextenderMessages(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_QuantumGate.IsRunning() && m_StressExtender != nullptr);
}

void CTestAppDlgTestExtenderTab::OnStressExtenderUseCompression()
{
	if (m_StressExtender != nullptr)
	{
		m_StressExtender->SetUseCompression(!m_StressExtender->IsUsingCompression());
	}
}

void CTestAppDlgTestExtenderTab::OnUpdateStressExtenderUseCompression(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_StressExtender != nullptr);
	pCmdUI->SetCheck(m_StressExtender != nullptr && m_StressExtender->IsUsingCompression());
}

void CTestAppDlgTestExtenderTab::OnTestExtenderLoad()
{
	if (m_TestExtender == nullptr) LoadTestExtender();
	else UnloadTestExtender();
}

void CTestAppDlgTestExtenderTab::OnUpdateTestExtenderLoad(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_TestExtender != nullptr);
}

void CTestAppDlgTestExtenderTab::OnTestExtenderUseCompression()
{
	if (m_TestExtender != nullptr)
	{
		m_TestExtender->SetUseCompression(!m_TestExtender->IsUsingCompression());
	}
}

void CTestAppDlgTestExtenderTab::OnUpdateTestExtenderUseCompression(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_TestExtender != nullptr);
	pCmdUI->SetCheck(m_TestExtender != nullptr && m_TestExtender->IsUsingCompression());
}

void CTestAppDlgTestExtenderTab::ProcessMessages()
{
	MSG msg{ 0 };
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void CTestAppDlgTestExtenderTab::OnBnClickedSendstress()
{
	if (m_TestExtender == nullptr) return;

	CString txt;
	const auto pluid = GetSelectedPeerLUID();

	const auto txto = GetTextValue(IDC_SENDTEXT);
	const auto num = GetTextValue(IDC_NUMSTRESSMESS);

	SetValue(IDC_STRESSRESULT, L"--");

	const int nmess = _wtoi((LPCWSTR)num);

	const auto begin = std::chrono::high_resolution_clock::now();

	if (!m_TestExtender->SendBenchmarkStart(pluid)) return;

	for (int x = 0u; x < nmess; x++)
	{
		try
		{
			txt = txto + L" " + Util::FormatString(L"#%d", x).c_str();

			if (!m_TestExtender->SendMessage(pluid, txt.GetString()))
			{
				LogErr(L"Could not send message %d to peer", x);
				break;
			}

			ProcessMessages();
		}
		catch (...)
		{
			AfxMessageBox(L"Exception thrown");
		}
	}

	m_TestExtender->SendBenchmarkEnd(pluid);

	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - begin);

	SetValue(IDC_STRESSRESULT, Util::FormatString(L"%dms", ms.count()));
}

LRESULT CTestAppDlgTestExtenderTab::OnQGPeerEvent(WPARAM w, LPARAM l)
{
	auto event = reinterpret_cast<TestExtender::Event*>(w);

	if (event->Type == PeerEventType::Connected)
	{
		LogInfo(L"Peer %llu connected", event->PeerLUID);

		auto lbox = (CListBox*)GetDlgItem(IDC_PEERLIST);
		lbox->InsertString(-1, Util::FormatString(L"%llu", event->PeerLUID).c_str());

		UpdateControls();
		UpdatePeerActivity();
	}
	else if (event->Type == PeerEventType::Disconnected)
	{
		LogInfo(L"Peer %llu disconnected", event->PeerLUID);

		CString pluid = Util::FormatString(L"%llu", event->PeerLUID).c_str();

		const auto lbox = (CListBox*)GetDlgItem(IDC_PEERLIST);
		const auto pos = lbox->FindString(-1, pluid);
		if (pos != LB_ERR) lbox->DeleteString(pos);

		UpdateControls();
		UpdatePeerActivity();
	}
	else
	{
		LogWarn(L"Opened peer event from %llu: %d", event->PeerLUID, event->Type);
	}

	// Delete allocated event from extender
	delete event;

	return 0;
}

LRESULT CTestAppDlgTestExtenderTab::OnQGPeerFileAccept(WPARAM w, LPARAM l)
{
	auto fa = reinterpret_cast<TestExtender::FileAccept*>(w);
	const auto pluid = fa->PeerLUID;
	const auto ftid = fa->FileTransferID;

	// Delete allocated object from extender
	delete fa;

	const auto path = GetApp()->BrowseForFile(GetSafeHwnd(), true);
	if (path)
	{
		m_TestExtender->AcceptFile(pluid, ftid, path->GetString());
	}
	else m_TestExtender->AcceptFile(pluid, ftid, L"");

	return 0;
}

LRESULT CTestAppDlgTestExtenderTab::OnQGExtenderInit(WPARAM w, LPARAM l)
{
	return 0;
}

LRESULT CTestAppDlgTestExtenderTab::OnQGExtenderDeInit(WPARAM w, LPARAM l)
{
	auto lbox = (CListBox*)GetDlgItem(IDC_PEERLIST);
	lbox->ResetContent();

	UpdateControls();
	UpdatePeerActivity();

	return 0;
}

void CTestAppDlgTestExtenderTab::OnLbnSelchangePeerlist()
{
	UpdateControls();
}

void CTestAppDlgTestExtenderTab::OnTimer(UINT_PTR nIDEvent)
{
	if (IsWindowVisible())
	{
		if (nIDEvent == EXTENDER_PEER_ACTIVITY_TIMER)
		{
			UpdatePeerActivity();
		}
	}

	CDialogBase::OnTimer(nIDEvent);
}

void CTestAppDlgTestExtenderTab::OnDestroy()
{
	if (m_PeerActivityTimer != 0)
	{
		KillTimer(m_PeerActivityTimer);
		m_PeerActivityTimer = 0;
	}

	StopSendThread();

	UnloadTestExtender();
	UnloadStressExtender();

	CDialogBase::OnDestroy();
}

void CTestAppDlgTestExtenderTab::OnExceptiontestStartup()
{
	if (m_StressExtender != nullptr) SetStressExtenderExceptionTest(&m_StressExtender->GetExceptionTest().Startup);
}

void CTestAppDlgTestExtenderTab::OnUpdateExceptiontestStartup(CCmdUI* pCmdUI)
{
	UpdateStressExtenderExceptionTest(pCmdUI, m_StressExtender != nullptr ?
									  m_StressExtender->GetExceptionTest().Startup : false);
}

void CTestAppDlgTestExtenderTab::OnExceptiontestPoststartup()
{
	if (m_StressExtender != nullptr) SetStressExtenderExceptionTest(&m_StressExtender->GetExceptionTest().PostStartup);
}

void CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPoststartup(CCmdUI* pCmdUI)
{
	UpdateStressExtenderExceptionTest(pCmdUI, m_StressExtender != nullptr ?
									  m_StressExtender->GetExceptionTest().PostStartup : false);
}

void CTestAppDlgTestExtenderTab::OnExceptiontestPreshutdown()
{
	if (m_StressExtender != nullptr) SetStressExtenderExceptionTest(&m_StressExtender->GetExceptionTest().PreShutdown);
}

void CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPreshutdown(CCmdUI* pCmdUI)
{
	UpdateStressExtenderExceptionTest(pCmdUI, m_StressExtender != nullptr ?
									  m_StressExtender->GetExceptionTest().PreShutdown : false);
}

void CTestAppDlgTestExtenderTab::OnExceptiontestShutdown()
{
	if (m_StressExtender != nullptr) SetStressExtenderExceptionTest(&m_StressExtender->GetExceptionTest().Shutdown);
}

void CTestAppDlgTestExtenderTab::OnUpdateExceptiontestShutdown(CCmdUI* pCmdUI)
{
	UpdateStressExtenderExceptionTest(pCmdUI, m_StressExtender != nullptr ?
									  m_StressExtender->GetExceptionTest().Shutdown : false);
}

void CTestAppDlgTestExtenderTab::OnExceptiontestPeerevent()
{
	if (m_StressExtender != nullptr) SetStressExtenderExceptionTest(&m_StressExtender->GetExceptionTest().PeerEvent);
}

void CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPeerevent(CCmdUI* pCmdUI)
{
	UpdateStressExtenderExceptionTest(pCmdUI, m_StressExtender != nullptr ?
									  m_StressExtender->GetExceptionTest().PeerEvent : false);
}

void CTestAppDlgTestExtenderTab::OnExceptiontestPeermessage()
{
	if (m_StressExtender != nullptr) SetStressExtenderExceptionTest(&m_StressExtender->GetExceptionTest().PeerMessage);
}

void CTestAppDlgTestExtenderTab::OnUpdateExceptiontestPeermessage(CCmdUI* pCmdUI)
{
	UpdateStressExtenderExceptionTest(pCmdUI, m_StressExtender != nullptr ?
									  m_StressExtender->GetExceptionTest().PeerMessage : false);
}

void CTestAppDlgTestExtenderTab::OnBnClickedBrowse()
{
	const auto path = GetApp()->BrowseForFile(GetSafeHwnd(), false);
	if (path)
	{
		SetValue(IDC_FILE_PATH, path->GetString());
	}
}

void CTestAppDlgTestExtenderTab::OnBnClickedAutoSendfile()
{
	auto path = GetTextValue(IDC_FILE_PATH);
	if (path.GetLength() == 0)
	{
		AfxMessageBox(L"Please select a file first!");
		return;
	}

	if (!std::filesystem::exists(Path(path.GetString())))
	{
		AfxMessageBox(L"The file does not exist!");
		return;
	}

	m_TestExtender->SendFile(GetSelectedPeerLUID(), path.GetString(), true);
}
