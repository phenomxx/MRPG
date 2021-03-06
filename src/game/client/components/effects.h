/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_EFFECTS_H
#define GAME_CLIENT_COMPONENTS_EFFECTS_H
#include <game/client/component.h>

class CEffects : public CComponent
{
	bool m_Add50hz;
	bool m_Add100hz;

	int m_DamageTaken;
	float m_DamageTakenTick;
public:
	CEffects();

	void AirJump(vec2 Pos);
	void DamageIndicator(vec2 Pos, int Amount);
	void PowerupShine(vec2 Pos, vec2 Size);
	void SmokeTrail(vec2 Pos, vec2 Vel);
	void SkidTrail(vec2 Pos, vec2 Vel);
	void BulletTrail(vec2 Pos);
	void PlayerSpawn(vec2 Pos);
	void PlayerDeath(vec2 Pos, int ClientID);
	void Explosion(vec2 Pos);
	void HammerHit(vec2 Pos);

	// mmotee
	void MmoEffects(vec2 Pos, int EffectID);
	void MmoTextEffect(vec2 Pos, const char* pText, int Flag);
	void EnchantEffect(vec2 Pos, vec2 Vel, vec4 Color, float RandomSize = 0.0f);
	//void BubbleEffect(vec2 Pos, vec2 Vel);

	virtual void OnRender();
	float GetEffectsSpeed();
};
#endif
