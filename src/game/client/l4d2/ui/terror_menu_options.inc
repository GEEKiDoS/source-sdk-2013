// Included by the sole existing menu.cpp, for L4D2 only. Replaces its Paint and
// ProcessText definitions, not a second HUD element or separately compiled TU.
CHudMenu::~CHudMenu() { delete m_options; }
#pragma float_control( precise, on, push )
void CHudMenu::ShowMenu( const char *text, int validSlots, float duration, CHudMenuOptions *options )
{
	ShowMenu( text, validSlots );
	m_flShutoffTime = duration <= 0.0f ? -1.0f : ( m_flOpenCloseTime + duration ) + gpGlobals->realtime;
	if ( options ) { delete m_options; m_options = options; }
}
#pragma float_control( pop )
void CHudMenu::ProcessText()
{
	m_Processed.RemoveAll(); m_nMaxPixels = 0; m_nHeight = 0;
	int i = 0, start = 0, item = 0, hotspotX = 0, hotspotY = 0;
	while ( i < MAX_MENU_STRING && g_szMenuString[i] )
	{
		ProcessedLine line;
		if ( i == start && g_szMenuString[i] == L'-' && i+1 < MAX_MENU_STRING && g_szMenuString[i+1] == L'>' )
		{
			if ( i+2 < MAX_MENU_STRING ) swscanf( g_szMenuString+i+2, L"%d", &item ); i += 2; start += 2; continue;
		}
		if ( i == start && g_szMenuString[i] == L'[' )
		{
			if ( i+1 < MAX_MENU_STRING ) swscanf( g_szMenuString+i+1, L"%d", &hotspotX );
			while ( i < MAX_MENU_STRING && g_szMenuString[i] != L',' ) ++i;
			if ( i+1 >= MAX_MENU_STRING ) break; // Skip only native out-of-array parser steps.
			swscanf( g_szMenuString+i+1, L"%d", &hotspotY );
			while ( i < MAX_MENU_STRING && g_szMenuString[i] != L']' ) ++i;
			if ( i >= MAX_MENU_STRING ) break;
			start = ++i; line.hotspotX = hotspotX; line.hotspotY = hotspotY;
		}
		while ( i < MAX_MENU_STRING && g_szMenuString[i] && g_szMenuString[i] != L'\n' ) ++i;
		if ( i-start >= 1 ) { line.menuitem = item; line.startchar = start; line.length = i-start; m_Processed.AddToTail( line ); }
		item = 0; if ( i < MAX_MENU_STRING && g_szMenuString[i] == L'\n' ) ++i; start = i;
	}
	if ( i-start >= 1 ) { ProcessedLine line; line.menuitem=item; line.startchar=start; line.length=i-start; m_Processed.AddToTail( line ); }
	FOR_EACH_VEC( m_Processed, index )
	{
		ProcessedLine &line = m_Processed[index]; const vgui::HFont font = line.menuitem ? m_hItemFont : m_hTextFont;
		int width = 0;
		for ( int c = 0; c < line.length; ++c ) width += vgui::surface()->GetCharacterWidth( font, g_szMenuString[line.startchar+c] );
		line.pixels = width; line.height = vgui::surface()->GetFontTall( font );
		if ( line.hotspotX != 9999 )
		{
			line.hotspotX -= line.pixels / 2;
			if ( line.hotspotX > 0 ) width += line.hotspotX;
			else if ( line.hotspotX < 0 && abs( line.hotspotX ) > width ) width = abs( line.hotspotX );
		}
		if ( width > m_nMaxPixels ) m_nMaxPixels = width;
		if ( line.hotspotY == 9999 ) m_nHeight += line.height;
		else { line.hotspotY -= line.height / 2; m_nHeight = MAX( m_nHeight, line.hotspotY > 0 ? line.hotspotY + line.height : abs( line.hotspotY ) ); }
	}
}
#pragma float_control( precise, on, push )
void CHudMenu::Paint()
{
	if ( !m_bMenuDisplayed ) return;
	if ( m_options ) m_options->OnBeforePaint();
	int px, py, panelW, panelH; GetBounds( px, py, panelW, panelH );
	int x = px + 20, y = _mm_cvttss_si32( _mm_set_ss( float( panelH - ( m_nHeight + 20 ) ) * 0.5f + float( py ) ) );
	const int cx = _mm_cvttss_si32( _mm_set_ss( float( panelW ) * 0.5f ) ), cy = _mm_cvttss_si32( _mm_set_ss( float( panelH ) * 0.5f ) );
	if ( m_options && m_options->m_center ) DrawBox( cx-(m_nMaxPixels+20),cy-(m_nHeight+20),2*(m_nMaxPixels+20),2*(m_nHeight+20),m_BoxColor,m_flSelectionAlphaOverride * 0.0039215689f );
	else DrawBox( x-10,y-10,m_nMaxPixels+20,m_nHeight+20,m_BoxColor,m_flSelectionAlphaOverride * 0.0039215689f );
	Color textColor = m_MenuColor, itemColor = m_ItemColor;
	textColor[3] = _mm_cvttss_si32( _mm_set_ss( float(textColor[3]) * (m_flSelectionAlphaOverride * 0.0039215689f) ) ); itemColor[3] = _mm_cvttss_si32( _mm_set_ss( float(itemColor[3]) * (m_flSelectionAlphaOverride * 0.0039215689f) ) );
	FOR_EACH_VEC( m_Processed, i )
	{
		if ( i == 0 && m_options && m_options->m_center ) continue;
		ProcessedLine &line = m_Processed[i];
		if ( m_options ) m_options->ProcessLine( i, line, g_szMenuString, m_nSelectedItem, (unsigned char)_mm_cvttss_si32( _mm_set_ss( m_flSelectionAlphaOverride ) ) );
		if ( line.hotspotX != 9999 && line.hotspotY != 9999 ) { x = cx+line.hotspotX; y = cy+line.hotspotY; }
		Color color = line.customColor ? line.color : line.menuitem ? itemColor : textColor;
		vgui::surface()->DrawSetTextColor( color );
		const int length = line.menuitem ? _mm_cvttss_si32( _mm_set_ss( float(line.length) * m_flTextScan ) ) : line.length;
		PaintString( g_szMenuString+line.startchar, length, line.menuitem ? m_hItemFont : m_hTextFont, x, y );
		if ( line.menuitem && m_nSelectedItem >= 0 && line.menuitem == m_nSelectedItem )
		{
			for ( float blur=m_flBlur; blur>0.0f; blur-=1.0f )
			{
				if ( blur<1.0f ) { Color faded=color; faded[3]=_mm_cvttss_si32( _mm_set_ss( float(color[3])*blur ) ); vgui::surface()->DrawSetTextColor(faded); }
				PaintString( g_szMenuString+line.startchar,length,m_hItemFontPulsing,x,y );
			}
		}
		y += line.height;
	}
}
#pragma float_control( pop )
