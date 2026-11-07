// Include at the final success point of existing nav_generate.cpp testJumpDown,
// immediately before its return true, under L4D2. `from` is elevated destination;
// `to` is the destination at toPos.z+2. NavTraceMins/Maxs are the native hull.
{
	const CUtlVector< CFuncNavConnectionBlocker * > &blockers = TheNavMesh->GetConnectionBlockers();
	if ( blockers.Count() )
	{
		const Vector elevatedStart( fromPos->x, fromPos->y, from.z );
		Ray_t segments[3];
		segments[0].Init( *fromPos, elevatedStart, NavTraceMins, NavTraceMaxs );
		segments[1].Init( elevatedStart, from, NavTraceMins, NavTraceMaxs );
		segments[2].Init( from, to, NavTraceMins, NavTraceMaxs );
		FOR_EACH_VEC( blockers, i )
		{
			CFuncNavConnectionBlocker *blocker = blockers[i];
			if ( blocker && ( blocker->IsBlocking( segments[0] ) || blocker->IsBlocking( segments[1] ) || blocker->IsBlocking( segments[2] ) ) )
				return false;
		}
	}
}
