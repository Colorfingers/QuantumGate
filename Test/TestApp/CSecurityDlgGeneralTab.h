// This file is part of the QuantumGate project. For copyright and
// licensing information refer to the license file(s) in the project root.

#pragma once

#include "CSecurityDlgTabCtrlPage.h"

class CSecurityDlgGeneralTab : public CSecurityDlgTabCtrlPage
{
	DECLARE_DYNCREATE(CSecurityDlgGeneralTab)

public:
	CSecurityDlgGeneralTab(CWnd* pParent = nullptr);   // standard constructor
	virtual ~CSecurityDlgGeneralTab();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_SECURITY_SETTINGS_GENERAL_TAB };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()

private:
	bool LoadData() noexcept override;
	bool SaveData() noexcept override;
};
