#include "stdafx.h"
#include "ZModule.h"
#include "crtdbg.h"

void ZModule::Active(bool bActive)	// (��)Ȱ��ȭ ��Ų��
{
	m_pContainer->ActiveModule(GetID(),bActive);
}


//////////////////////////////////////////////
//// ZModuleContainer

MImplementRootRTTI(ZModuleContainer);

ZModuleContainer::~ZModuleContainer()
{
	_ASSERT(m_Modules.empty());
}

ZModule *ZModuleContainer::GetModule(int nID)
{
	ZMODULEMAP::iterator i = m_Modules.find(nID);

	if(m_Modules.end()!=i) {
		return i->second;
	}

	return NULL;
}

void ZModuleContainer::AddModule(ZModule *pModule,bool bInitialActive)
{
	ZMODULEMAP::iterator i = m_Modules.find(pModule->GetID());
	if(i!=m_Modules.end()) {
		_ASSERT(0);	// �ߺ��Ǿ���
	}

	m_Modules.insert(ZMODULEMAP::value_type(pModule->GetID(),pModule));
	if(bInitialActive)
		ActiveModule(pModule->GetID());

	pModule->m_pContainer = this;
	pModule->OnAdd();
}

void ZModuleContainer::RemoveModule(ZModule *pModule)
{
	if (pModule == NULL) return;		// �ΰ��̸� ����
	ZMODULEMAP::iterator i = m_Modules.find(pModule->GetID());
	if(i==m_Modules.end()) return;	// ���� ����̴�

	ActiveModule(pModule->GetID(),false);

	m_Modules.erase(i);
	pModule->OnRemove();
}

void ZModuleContainer::ActiveModule(int nID, bool bActive)	// Ư�� ����� (��)Ȱ��ȭ ��Ų��
{
	// Ȱ��ȭ ��Ų��
	if(bActive) {
		if(IsActiveModule(nID)) return;	// �̹� Ȱ��ȭ �Ǿ��ִ�

		ZMODULEMAP::iterator i = m_Modules.find(nID);
		if(i!= m_Modules.end()) {
			i->second->OnActivate();
			m_ActiveModules.insert(ZMODULEMAP::value_type(nID,i->second));
		}
	}else{
	// ��Ȱ��ȭ ��Ų��
		ZMODULEMAP::iterator i = m_ActiveModules.find(nID);
		if(i==m_ActiveModules.end()) return; // �̹� ��Ȱ��ȭ �Ǿ��ִ�
		i->second->OnDeactivate();
		m_ActiveModules.erase(i);
	}
}

bool ZModuleContainer::IsActiveModule(int nID)
{
	ZMODULEMAP::iterator i = m_ActiveModules.find(nID);
	if(i!=m_ActiveModules.end()) return true;

	return false;
}


void ZModuleContainer::UpdateModules(float fElapsed)
{
	// Ȱ��ȭ �Ǿ��ִ� ��⸸ ������Ʈ�� �ҷ��ش�
	for(ZMODULEMAP::iterator i=m_ActiveModules.begin();i!=m_ActiveModules.end();i++) {
		ZModule *pModule = i->second;
		if(!pModule->Update(fElapsed)) {
			i = m_ActiveModules.erase(i);
		}
	}
}

void ZModuleContainer::InitModuleStatus(void)
{
	for(ZMODULEMAP::iterator i=m_Modules.begin();i!=m_Modules.end();i++) {
		ZModule *pModule = i->second;
		pModule->InitStatus();
	}
}