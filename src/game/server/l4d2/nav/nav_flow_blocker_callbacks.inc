// Include once at the tail of existing l4d2/terror_nav_flow.cpp so this uses
// that TU's sole static nav_recompute_flow_optimization and existing timer.
// S2E63A0/S2E6450: base list update always precedes the one 3-second schedule.
#pragma float_control( precise, on, push )
void TerrorNavMesh::OnAreaBlocked( CNavArea *area, bool affectsFlow )
{
	BaseClass::OnAreaBlocked( area, affectsFlow );
	if ( ( affectsFlow || !nav_recompute_flow_optimization.GetBool() ) && !( m_flowRecomputeTimer.m_timestamp > 0.0f ) )
		m_flowRecomputeTimer.Start( 3.0f );
}
void TerrorNavMesh::OnAreaUnblocked( CNavArea *area, bool affectsFlow )
{
	BaseClass::OnAreaUnblocked( area, affectsFlow );
	if ( ( affectsFlow || !nav_recompute_flow_optimization.GetBool() ) && !( m_flowRecomputeTimer.m_timestamp > 0.0f ) )
		m_flowRecomputeTimer.Start( 3.0f );
}
#pragma float_control( pop )
