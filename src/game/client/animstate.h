/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_ANIMSTATE_H
#define GAME_CLIENT_ANIMSTATE_H

#include <generated/client_data.h>

class CAnimState
{
	CAnimKeyframe m_Body;
	CAnimKeyframe m_BackFoot;
	CAnimKeyframe m_FrontFoot;
	CAnimKeyframe m_Attach;
	CAnimKeyframe m_Wings;

public:
	CAnimKeyframe *GetBody() { return &m_Body; };
	CAnimKeyframe *GetBackFoot() { return &m_BackFoot; };
	CAnimKeyframe *GetFrontFoot() { return &m_FrontFoot; };
	CAnimKeyframe *GetAttach() { return &m_Attach; };
	CAnimKeyframe* GetWings() { return &m_Wings; };
	void Set(CAnimation *pAnim, float Time);
	void Add(CAnimation *pAdded, float Time, float Amount);

	static CAnimState *GetIdle();
};

#endif
