//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESPONSE_RULES_WEAPON_SOURCE_H
#define RESPONSE_RULES_WEAPON_SOURCE_H
#ifdef _WIN32
#pragma once
#endif

class IResponseRulesWeaponSource
{
public:
	virtual const char *GetResponseRulesName() const = 0;
};

#endif // RESPONSE_RULES_WEAPON_SOURCE_H
