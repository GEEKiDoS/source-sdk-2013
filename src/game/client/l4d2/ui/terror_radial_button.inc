// Valve/Turtle Rock published control, verified against C2BB340..2BBEC4.
// Copyright (c) 2006 Turtle Rock Studios, Inc.
// Public Alien Swarm SDK CRadialButton foundation (SDK license), split from
// swarm/vgui/radialmenu.cpp; ReactiveDrop/reactivedrop_public_src commit
// 9231d40422194dfecca8b698698df4af7e0d99f1.
class CRadialButton : public CPolygonButton
{
	DECLARE_CLASS_SIMPLE( CRadialButton, CPolygonButton );

public:

	//----------------------------------------------------------------------------------------------------
	CRadialButton( vgui::Panel *parent, const char *panelName )
		: CPolygonButton( parent, panelName )
	{
		SetCursor( vgui::dc_blank );

		m_nMainMaterial = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( m_nMainMaterial, "vgui/white" , true, false );

		m_chosen = false;
		m_hasBorders = true;
		m_armedFont = NULL;
		m_defaultFont = NULL;

		m_unscaledSubmenuPoints.RemoveAll();
		m_submenuPoints = NULL;
		m_numSubmenuPoints = 0;
		m_hasSubmenu = false;
		m_fakeArmed = false;

		m_passthru = NULL;
		m_parent = dynamic_cast< CRadialMenu * >(parent);
	}

	//----------------------------------------------------------------------------------------------------
	void ShowSubmenuIndicator( bool state )
	{
		m_hasSubmenu = state;
	}

	//----------------------------------------------------------------------------------------------------
	void SetPassthru( CRadialButton *button )
	{
		m_passthru = button;
		if ( button )
		{
			SetZPos( -1 );
		}
		else
		{
			SetZPos( 0 );
		}
	}

	//----------------------------------------------------------------------------------------------------
	CRadialButton *GetPassthru( void )
	{
		return m_passthru;
	}

	//----------------------------------------------------------------------------------------------------
	void UpdateHotspots( KeyValues *data )
	{
		BaseClass::UpdateHotspots( data );

		// clear out our old submenu hotspot
		if ( m_submenuPoints )
		{
			delete[] m_submenuPoints;
			m_submenuPoints = NULL;
			m_numSubmenuPoints = 0;
		}
		m_unscaledSubmenuPoints.RemoveAll();

		// read in a new one
		KeyValues *points = data->FindKey( "SubmenuHotspot", false );
		if ( points )
		{
			for ( KeyValues *value = points->GetFirstValue(); value; value = value->GetNextValue() )
			{
				const char *str = value->GetString();

				float x, y;
				if ( 2 == sscanf( str, "%f %f", &x, &y ) )
				{
					m_unscaledSubmenuPoints.AddToTail( Vector2D( x, y ) );
				}
			}
		}

		if ( RadialMenuDebug.GetBool() )
		{
			InvalidateLayout( false, true );
		}
	}

	//----------------------------------------------------------------------------------------------------
	void PerformLayout( void )
	{
		int wide, tall;
		GetSize( wide, tall );

		if ( m_submenuPoints )
		{
			delete[] m_submenuPoints;
			m_submenuPoints = NULL;
			m_numSubmenuPoints = 0;
		}

		// generate scaled points
		m_numSubmenuPoints = m_unscaledSubmenuPoints.Count();
		if ( m_numSubmenuPoints )
		{
			m_submenuPoints = new vgui::Vertex_t[ m_numSubmenuPoints ];
			for ( int i=0; i<m_numSubmenuPoints; ++i )
			{
				float x = m_unscaledSubmenuPoints[i].x * wide;
				float y = m_unscaledSubmenuPoints[i].y * tall;
				m_submenuPoints[i].Init( Vector2D( x, y ), m_unscaledSubmenuPoints[i] );
			}
		}

		bool isArmed = ( IsArmed() || m_fakeArmed );
		vgui::HFont currentFont = (isArmed) ? m_armedFont : m_defaultFont;
		if ( currentFont )
		{
			SetFont( currentFont );
		}

		BaseClass::PerformLayout();
	}

	//----------------------------------------------------------------------------------------------------
	Color GetRadialFgColor( void )
	{
		Color c = BaseClass::GetButtonFgColor();
		if ( !IsEnabled() || GetPassthru() )
		{
			c = m_disabledFgColor;
		}
		else if ( m_chosen )
		{
			c = m_chosenFgColor;
		}
		else if ( m_fakeArmed )
		{
			c = m_armedFgColor;
		}
		c[3] = 64;
		return c;
	}

	//----------------------------------------------------------------------------------------------------
	Color GetRadialBgColor( void )
	{
		Color c = BaseClass::GetButtonBgColor();
		if ( GetPassthru() )
		{
			if ( IsArmed() || m_fakeArmed )
			{
				for ( int i=0; i<4; ++i )
				{
					c[i] = NativeRadialInteger( m_disabledBgColor[i] * 0.8f + m_armedBgColor[i] * 0.2f );
				}
			}
			else
			{
				c = m_disabledBgColor;
			}
		}
		else if ( !IsEnabled() )
		{
			c = m_disabledBgColor;
		}
		else if ( m_chosen )
		{
			c = m_chosenBgColor;
		}
		else if ( m_fakeArmed )
		{
			c = m_armedBgColor;
		}
		return c;
	}

	//----------------------------------------------------------------------------------------------------
	void GetHotspotBounds( int *minX, int *minY, int *maxX, int *maxY )
	{
		if ( minX )
		{
			*minX = NativeRadialInteger( m_hotspotMins.x );
		}

		if ( minY )
		{
			*minY = NativeRadialInteger( m_hotspotMins.y );
		}

		if ( maxX )
		{
			*maxX = NativeRadialInteger( m_hotspotMaxs.x );
		}

		if ( maxY )
		{
			*maxY = NativeRadialInteger( m_hotspotMaxs.y );
		}
	}
	
	//----------------------------------------------------------------------------------------------------
	/**
	 * Paints the polygonal background
	 */
	virtual void PaintBackground( void )
	{
		if ( RadialMenuDebug.GetBool() && (IsArmed() || m_fakeArmed) )
		{
			Color c = GetRadialFgColor();
			vgui::surface()->DrawSetColor( c );
			vgui::surface()->DrawFilledRect( NativeRadialInteger( m_hotspotMins.x ), NativeRadialInteger( m_hotspotMins.y ), NativeRadialInteger( m_hotspotMaxs.x ), NativeRadialInteger( m_hotspotMaxs.y ) );
		}

		Color c = GetRadialBgColor();
		vgui::surface()->DrawSetColor( c );
		vgui::surface()->DrawSetTexture( m_nMainMaterial );

		if ( RadialMenuDebug.GetInt() == 2 )
		{
			vgui::surface()->DrawTexturedPolygon( m_numHotspotPoints, m_hotspotPoints );
		}
		else
		{
			vgui::surface()->DrawTexturedPolygon( m_numVisibleHotspotPoints, m_visibleHotspotPoints );
		}

		if ( m_numSubmenuPoints && m_hasSubmenu )
		{
			vgui::surface()->DrawTexturedPolygon( m_numSubmenuPoints, m_submenuPoints );
		}
	}

	//----------------------------------------------------------------------------------------------------
	/**
	 * Paints the polygonal border
	 */
	virtual void PaintBorder( void )
	{
		if ( !m_hasBorders )
			return;

		Color c = GetRadialFgColor();
		vgui::surface()->DrawSetColor( c );
		vgui::surface()->DrawSetTexture( m_nWhiteMaterial );

		if ( RadialMenuDebug.GetInt() == 2 )
		{
			vgui::surface()->DrawTexturedPolyLine( m_hotspotPoints, m_numHotspotPoints );
		}
		else
		{
			vgui::surface()->DrawTexturedPolyLine( m_visibleHotspotPoints, m_numVisibleHotspotPoints );
		}

		if ( m_numSubmenuPoints && m_hasSubmenu )
		{
			vgui::surface()->DrawTexturedPolyLine( m_submenuPoints, m_numSubmenuPoints );
		}
	}


	//----------------------------------------------------------------------------------------------------
	/**
	 * Hard-coding colors for now
	 */
	virtual void ApplySchemeSettings( vgui::IScheme *scheme )
	{
		BaseClass::ApplySchemeSettings( scheme );
		MEM_ALLOC_CREDIT();
		SetDefaultColor( scheme->GetColor( "Rosetta.DefaultFgColor", Color( 255, 176, 0, 255 ) ), scheme->GetColor( "Rosetta.DefaultBgColor", Color( 0, 0, 0, 128 ) ) );
		m_armedFgColor = scheme->GetColor( "Rosetta.DefaultFgColor", Color( 255, 176, 0, 255 ) );
		m_armedBgColor = scheme->GetColor( "Rosetta.ArmedBgColor", Color( 0, 28, 192, 128 ) );
		SetArmedColor( m_armedFgColor, m_armedBgColor );
		m_disabledBgColor = scheme->GetColor( "Rosetta.DisabledBgColor", Color( 0, 0, 0, 128 ) );
		m_disabledFgColor = scheme->GetColor( "Rosetta.DisabledFgColor", Color( 0, 0, 0, 128 ) );
		m_chosenFgColor = scheme->GetColor( "Rosetta.DefaultFgColor", Color( 255, 176, 0, 255 ) );
		m_chosenBgColor = scheme->GetColor( "Rosetta.ArmedBgColor", Color( 0, 28, 192, 128 ) );
		SetMouseClickEnabled( MOUSE_RIGHT, true );
		SetButtonActivationType( ACTIVATE_ONPRESSED );

		m_hasBorders = false;
		const char *borderStr = scheme->GetResourceString( "Rosetta.DrawBorder" );
		if ( borderStr && atoi( borderStr ) )
		{
			m_hasBorders = true;
		}

		const char *fontStr = scheme->GetResourceString( "Rosetta.DefaultFont" );
		if ( fontStr )
		{
			m_defaultFont = scheme->GetFont( fontStr, true );
		}
		else
		{
			m_defaultFont = scheme->GetFont( "Default", true );
		}

		fontStr = scheme->GetResourceString( "Rosetta.ArmedFont" );
		if ( fontStr )
		{
			m_armedFont = scheme->GetFont( fontStr, true );
		}
		else
		{
			m_armedFont = scheme->GetFont( "Default", true );
		}
		SetFont( m_defaultFont );

		SetCursor( vgui::dc_blank );
	}

	//----------------------------------------------------------------------------------------------------
	/**
	 * Mark this button as "chosen".  Basically, it means we draw a different color as the radial
	 * menu fades out.
	 */
	void SetChosen( bool chosen )
	{
		m_chosen = chosen;
	}

	//----------------------------------------------------------------------------------------------------
	/**
	 * Mark this button as "armed" because of a cursor pos off the screen (low FPS).
	 */
	void SetFakeArmed( bool armed )
	{
		m_fakeArmed = armed; // Native direct store; font changes only in layout.
	}
	bool IsFakeArmed() const { return m_fakeArmed; }

	//----------------------------------------------------------------------------------------------------
	/**
	 * Plays a rollover sound if the cursor has left the center
	 */
	virtual void OnCursorEntered( void )
	{


		int wide, tall;
		GetSize( wide, tall );

		int centerx = wide / 2;
		int centery = tall / 2;

		int x, y;
		vgui::surface()->SurfaceGetCursorPos( x, y );
		ScreenToLocal( x, y );

		if ( x != centerx || y != centery )
		{
			C_BasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();
			if ( localPlayer )
			{
				if ( !m_fakeArmed )
				{
					localPlayer->EmitSound("MouseMenu.rollover");
				}
			}

			if ( m_parent )
			{
				m_parent->OnCursorEnteredButton( x, y, this );
			}
		}

		m_fakeArmed = false;

		BaseClass::OnCursorEntered();
	}

	//----------------------------------------------------------------------------------------------------
	/**
	 * Right-click cancels, otherwise, the normal button logic applies
	 */
	virtual void OnMousePressed( vgui::MouseCode code )
	{
		if ( code == MOUSE_RIGHT )
		{
			SetCommand( "done" );
		}
		else
		{
			SetChosen( true );
		}
		BaseClass::OnMousePressed( code );
	}


private:
	CUtlVector< Vector2D > m_unscaledSubmenuPoints;
	vgui::Vertex_t *m_submenuPoints;
	int m_numSubmenuPoints;

	int m_nMainMaterial;
	bool m_hasBorders;

	Color m_disabledBgColor;
	Color m_disabledFgColor;

	Color m_chosenBgColor;
	Color m_chosenFgColor;
	bool m_chosen;

	Color m_armedBgColor;
	Color m_armedFgColor;
	bool m_fakeArmed;

	bool m_hasSubmenu;

	vgui::HFont m_armedFont;
	vgui::HFont m_defaultFont;

	CRadialButton *m_passthru;
	CRadialMenu *m_parent;
};


