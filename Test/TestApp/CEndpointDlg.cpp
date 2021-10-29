// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#include "pch.h"
#include "CEndpointDlg.h"

CEndpointDlg::CEndpointDlg(CWnd* pParent) : CDialogBase(CEndpointDlg::IDD, pParent)
{}

CEndpointDlg::~CEndpointDlg()
{}

void CEndpointDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CEndpointDlg, CDialogBase)
	ON_BN_CLICKED(IDOK, &CEndpointDlg::OnBnClickedOk)
END_MESSAGE_MAP()

void CEndpointDlg::SetIPAddress(const String& ip) noexcept
{
	if (!IPAddress::TryParse(ip, m_IPAddress)) AfxMessageBox(L"Invalid IP address.", MB_ICONERROR);
}

BOOL CEndpointDlg::OnInitDialog()
{
	CDialogBase::OnInitDialog();

	// Init IP combo
	{
		const auto pcombo = (CComboBox*)GetDlgItem(IDC_IP);

		IPAddress ip;
		String::size_type pos{ 0 };
		String::size_type start{ 0 };

		while ((pos = m_IPAddressHistory.find(L";", start)) != String::npos)
		{
			const auto ipstr = m_IPAddressHistory.substr(start, pos - start);

			if (IPAddress::TryParse(ipstr, ip))
			{
				pcombo->AddString(ipstr.c_str());
			}

			start = pos + 1;
		}

		const auto ipstr = m_IPAddressHistory.substr(start, m_IPAddressHistory.size() - start);
		if (!ipstr.empty())
		{
			if (IPAddress::TryParse(ipstr, ip))
			{
				pcombo->AddString(ipstr.c_str());
			}
		}
	}

	SetValue(IDC_IP, m_IPAddress.GetString());
	SetValue(IDC_PORT, m_Port);

	// Init protocol combo
	{
		const auto pcombo = (CComboBox*)GetDlgItem(IDC_PROTOCOL_COMBO);
		auto pos = pcombo->AddString(L"TCP");
		pcombo->SetItemData(pos, static_cast<DWORD_PTR>(QuantumGate::IPEndpoint::Protocol::TCP));
		if (m_Protocol == QuantumGate::IPEndpoint::Protocol::TCP) pcombo->SetCurSel(pos);
		pos = pcombo->AddString(L"UDP");
		pcombo->SetItemData(pos, static_cast<DWORD_PTR>(QuantumGate::IPEndpoint::Protocol::UDP));
		if (m_Protocol == QuantumGate::IPEndpoint::Protocol::UDP) pcombo->SetCurSel(pos);

		if (!m_ProtocolSelection)
		{
			pcombo->EnableWindow(false);
		}
	}

	SetValue(IDC_HOPS, m_Hops);

	if (m_ReuseConnection) ((CButton*)GetDlgItem(IDC_REUSE_CONNECTION))->SetCheck(BST_CHECKED);
	if (m_RelayGatewayPeer) SetValue(IDC_RELAY_PEER, *m_RelayGatewayPeer);

	if (m_ShowRelay)
	{
		GetDlgItem(IDC_HOPS)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_HOPS_LABEL)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_RELAY_PEER)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_RELAY_PEER_LABEL)->ShowWindow(SW_SHOW);
	}

	return TRUE;
}

void CEndpointDlg::OnBnClickedOk()
{
	const auto ipstr = GetTextValue(IDC_IP);
	if (IPAddress::TryParse(ipstr.GetString(), m_IPAddress))
	{
		const auto sel = ((CComboBox*)GetDlgItem(IDC_PROTOCOL_COMBO))->GetCurSel();
		if (sel == CB_ERR)
		{
			AfxMessageBox(L"Please select a protocol first.", MB_ICONINFORMATION);
			return;
		}

		if (m_IPAddressHistory.find(ipstr) == String::npos)
		{
			if (!m_IPAddressHistory.empty()) m_IPAddressHistory += L";";

			m_IPAddressHistory += ipstr;
		}

		m_Port = static_cast<UInt16>(GetInt64Value(IDC_PORT));

		m_Protocol = static_cast<QuantumGate::IPEndpoint::Protocol>(((CComboBox*)GetDlgItem(IDC_PROTOCOL_COMBO))->GetItemData(sel));
		m_PassPhrase = GetTextValue(IDC_PASSPHRASE);
		m_Hops = static_cast<UInt8>(GetInt64Value(IDC_HOPS));

		const auto id = GetUInt64Value(IDC_RELAY_PEER);
		if (id != 0) m_RelayGatewayPeer = id;

		m_ReuseConnection = (((CButton*)GetDlgItem(IDC_REUSE_CONNECTION))->GetCheck() == BST_CHECKED);

		CDialogBase::OnOK();
	}
	else
	{
		AfxMessageBox(ipstr, MB_ICONERROR);
	}
}
