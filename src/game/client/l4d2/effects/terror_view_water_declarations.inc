// Native C1D07F0/C1D6340. This file is included inside viewrender.cpp.
class CAboveWaterSkyboxView : public CSkyboxView
{
	DECLARE_CLASS( CAboveWaterSkyboxView, CSkyboxView );
public:
	CAboveWaterSkyboxView( CViewRender *pMainView ) :
		CSkyboxView( pMainView ), m_ReflectionView( pMainView ) {}

	bool Setup( const CViewSetup &view, int *pClearFlags,
		SkyboxVisibility_t *pSkyboxVisible, const VisibleFogVolumeInfo_t &fogInfo );
	void Draw();

	class CSkyboxReflectionView : public CSkyboxView
	{
		DECLARE_CLASS( CSkyboxReflectionView, CSkyboxView );
	public:
		CSkyboxReflectionView( CViewRender *pMainView ) : CSkyboxView( pMainView ) {}
		bool Setup( const CViewSetup &view, int *pClearFlags, SkyboxVisibility_t *pSkyboxVisible );
		void Draw();
	protected:
		virtual void PushView( float waterHeight );
	private:
		CAboveWaterSkyboxView *GetOuter() { return GET_OUTER( CAboveWaterSkyboxView, m_ReflectionView ); }
	};

private:
	friend class CSkyboxReflectionView;
	CSkyboxReflectionView m_ReflectionView;
	VisibleFogVolumeInfo_t m_fogInfo;
};
