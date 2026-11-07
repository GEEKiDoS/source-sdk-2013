// Included in the existing CClient_Precipitation private section, L4D2 only.
	friend class CSnowFallManager;
	float m_minSpeed, m_maxSpeed; // Native1644/1648, real received values.
	void InitializeParticlePrecipitation();
	void SimulateParticlePrecipitation();
	void StopInnerParticlePrecipitation();
	void StopOuterParticlePrecipitation();
	void CreateOuterParticlePrecipitation( C_BasePlayer *player, const Vector &distance );
	void CreateFullParticlePrecipitation( C_BasePlayer *player, const Vector &forward, const Vector &distance );
	bool m_bParticlePrecipitationInitialized; //1886; allocator zero.
	bool m_bParticleSlotInitialized; //1884; approved native slot0 only.
	float m_flParticleInterval, m_flParticleNextEvent; //1868/1872.
	float m_flParticleNearDistance; //1780.
	const char *m_szParticleNear, *m_szParticleScreen, *m_szParticleOuter; //1784..1792.
	HPARTICLEFFECT m_hParticleNear, m_hParticleScreen, m_hParticleOuter; //1796,1820,1844.
