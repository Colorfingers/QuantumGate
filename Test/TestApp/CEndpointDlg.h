// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "CDialogBase.h"

class CEndpointDlg final : public CDialogBase
{
public:
	CEndpointDlg(CWnd* pParent = NULL);
	virtual ~CEndpointDlg();

	enum { IDD = IDD_ENDPOINT_DLG };

	void SetIPAddress(const String& ip) noexcept;

	inline void SetPort(const UInt16 port) noexcept { m_Port = port; }
	inline void SetRelayHops(const RelayHop hops) noexcept { m_Hops = hops; }
	inline void SetRelayGatewayPeer(const PeerLUID pluid) noexcept { m_RelayGatewayPeer = pluid; }

	inline void SetShowRelay(const bool show) noexcept { m_ShowRelay = show; }

	inline const IPAddress& GetIPAddress() const noexcept { return m_IPAddress; }
	inline const UInt16 GetPort() const noexcept { return m_Port; }
	inline const CString& GetPassPhrase() const noexcept { return m_PassPhrase; }
	inline const RelayHop GetRelayHops() const noexcept { return m_Hops; }
	inline const std::optional<PeerLUID>& GetRelayGatewayPeer() const noexcept { return m_RelayGatewayPeer; }

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support

	DECLARE_MESSAGE_MAP()

	afx_msg void OnBnClickedOk();

private:
	IPAddress m_IPAddress;
	UInt16 m_Port{ 9000 };
	CString m_PassPhrase;
	RelayHop m_Hops{ 0 };
	std::optional<PeerLUID> m_RelayGatewayPeer;
	bool m_ShowRelay{ false };
};

