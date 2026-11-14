// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH

#pragma once
#include "shaderlib/cshader.h"
#include "BaseVSShaderDX12.h"
#include "l4d2_spritecard_vs.h"
typedef dx12cb::l4d2_spritecard_vs l4d2_spritecard_vs51_Block;
class l4d2_spritecard_vs51_Static_Index
{
	unsigned int m_nZOOM_ANIMATE_SEQ2 : 2;
	unsigned int m_nDUALSEQUENCE : 2;
	unsigned int m_nEXTRACTGREENALPHA : 2;
#ifdef _DEBUG
	bool m_bZOOM_ANIMATE_SEQ2 : 1;
	bool m_bDUALSEQUENCE : 1;
	bool m_bEXTRACTGREENALPHA : 1;
#endif	// _DEBUG
public:
	void SetZOOM_ANIMATE_SEQ2( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nZOOM_ANIMATE_SEQ2 = i;
#ifdef _DEBUG
		m_bZOOM_ANIMATE_SEQ2 = true;
#endif	// _DEBUG
	}

	void SetDUALSEQUENCE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nDUALSEQUENCE = i;
#ifdef _DEBUG
		m_bDUALSEQUENCE = true;
#endif	// _DEBUG
	}

	void SetEXTRACTGREENALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nEXTRACTGREENALPHA = i;
#ifdef _DEBUG
		m_bEXTRACTGREENALPHA = true;
#endif	// _DEBUG
	}

	l4d2_spritecard_vs51_Static_Index(  )
	{
		m_nZOOM_ANIMATE_SEQ2 = 0;
		m_nDUALSEQUENCE = 0;
		m_nEXTRACTGREENALPHA = 0;
#ifdef _DEBUG
		m_bZOOM_ANIMATE_SEQ2 = false;
		m_bDUALSEQUENCE = false;
		m_bEXTRACTGREENALPHA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bZOOM_ANIMATE_SEQ2 && m_bDUALSEQUENCE && m_bEXTRACTGREENALPHA );
		return ( 3 * m_nZOOM_ANIMATE_SEQ2 ) + ( 6 * m_nDUALSEQUENCE ) + ( 12 * m_nEXTRACTGREENALPHA ) + 0;
	}
};

#define shaderStaticTest_l4d2_spritecard_vs51 vsh_forgot_to_set_static_ZOOM_ANIMATE_SEQ2 + vsh_forgot_to_set_static_DUALSEQUENCE + vsh_forgot_to_set_static_EXTRACTGREENALPHA


class l4d2_spritecard_vs51_Dynamic_Index
{
	unsigned int m_nORIENTATION : 2;
#ifdef _DEBUG
	bool m_bORIENTATION : 1;
#endif	// _DEBUG
public:
	void SetORIENTATION( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nORIENTATION = i;
#ifdef _DEBUG
		m_bORIENTATION = true;
#endif	// _DEBUG
	}

	l4d2_spritecard_vs51_Dynamic_Index(  )
	{
		DX12SelectNativeBlock< l4d2_spritecard_vs51_Block >( dx12native::kStageVertex );
		m_nORIENTATION = 0;
#ifdef _DEBUG
		m_bORIENTATION = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bORIENTATION );
		return ( 1 * m_nORIENTATION ) + 0;
	}
};

#define shaderDynamicTest_l4d2_spritecard_vs51 vsh_forgot_to_set_dynamic_ORIENTATION

