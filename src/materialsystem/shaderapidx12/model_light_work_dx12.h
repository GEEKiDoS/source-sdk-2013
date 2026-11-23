#pragma once

#include <windows.h>

namespace shaderapidx12
{
// One outstanding range, submitted and joined by the material thread. No worker ever
// accesses the shader API or the command recorder; all it gets is the range closure.
class ModelLightWorkDX12
{
public:
	ModelLightWorkDX12() = default;
	ModelLightWorkDX12( const ModelLightWorkDX12 & ) = delete;
	~ModelLightWorkDX12() { if ( m_Work ) CloseThreadpoolWork( m_Work ); }

	template<class F> void Run( int count, F &range )
	{
		// A small table has too little independent attenuation/visibility work to amortize a handoff.
		if ( count < 64 ) { range( 0, count, 0 ); return; }
		if ( !m_Work ) m_Work = CreateThreadpoolWork( Invoke, this, nullptr );
		if ( !m_Work ) { range( 0, count, 0 ); return; }
		m_Count = count;
		m_Context = &range;
		m_Run = []( void *context, int n ) { (*static_cast<F *>( context ))( n / 2, n, 1 ); };
		SubmitThreadpoolWork( m_Work );
		range( 0, count / 2, 0 );
		WaitForThreadpoolWorkCallbacks( m_Work, FALSE );
	}
private:
	static void CALLBACK Invoke( PTP_CALLBACK_INSTANCE, void *context, PTP_WORK )
	{
		auto &self = *static_cast<ModelLightWorkDX12 *>( context );
		self.m_Run( self.m_Context, self.m_Count );
	}
	PTP_WORK m_Work = nullptr;
	void *m_Context = nullptr;
	void (*m_Run)( void *, int ) = nullptr;
	int m_Count = 0;
};
}
