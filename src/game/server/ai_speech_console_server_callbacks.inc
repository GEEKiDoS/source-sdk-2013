// Transplant into existing src/game/server/ai_speech.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Deliberately separate from CUtlSymbol's process-wide convenience table.
// Original107D3E30 is created lazily and is case insensitive.
static CUtlSymbolTable *s_pAIConceptSymbols = NULL;
static CUtlLinkedList< AI_Concept, unsigned short > s_ResponseConceptExcludes;

AI_Concept::AI_Concept( const char *name ) : m_Symbol( 0xFFFF ), m_iCriteriaIndex( -1 )
{
 if ( !s_pAIConceptSymbols ) s_pAIConceptSymbols = new CUtlSymbolTable( 64, 64, true );
 if ( !s_pAIConceptSymbols ) return; // Native allocation-failure dependent dereference: ledger306.
 const char *value = s_pAIConceptSymbols->GetNumStrings() <= 32000 ? name : "";
 m_Symbol = static_cast< unsigned short >( s_pAIConceptSymbols->AddString( value ) );
}
const char *AI_Concept::GetStringConcept() const
{
 if ( !s_pAIConceptSymbols ) s_pAIConceptSymbols = new CUtlSymbolTable( 64, 64, true );
 if ( !s_pAIConceptSymbols ) return ""; // Same native allocation-failure case.
 const char *value = m_Symbol == 0xFFFF ? NULL : s_pAIConceptSymbols->String( CUtlSymbol( m_Symbol ) );
 if ( !value )
 {
  Warning( "An AI_Concept couldn't find its string in the symbol table!\n" );
  return "";
 }
 return value;
}
bool IsAIConceptDebugExcluded( const AI_Concept &concept )
{
 return s_ResponseConceptExcludes.Find( concept ) != s_ResponseConceptExcludes.InvalidIndex();
}

// Native callback 0x00020610 (command_function).
static void CC_rr_debugresponseconcept_exclude( const CCommand &args )
{
	if ( !s_pAIConceptSymbols || !args.ArgC() ) return;
	if ( args.ArgC() == 1 )
	{
		Msg( "Usage:  rr_debugresponseconcept_exclude  Concept1 Concept2 Concept3...\n" );
		Msg( "\tseparate multiple concepts with spaces.\n" );
		Msg( "\tcall with no arguments to see this message and a list of current excludes.\n" );
		Msg( "\tto reset the exclude list, type \"rr_debugresponseconcept_exclude !\"\n" );
		Msg( "\nCurrent exclude list:\n" );
		if ( s_ResponseConceptExcludes.Head() != s_ResponseConceptExcludes.InvalidIndex() )
		{
			FOR_EACH_LL( s_ResponseConceptExcludes, i ) Msg( "\t%s\n", s_ResponseConceptExcludes[i].GetStringConcept() );
		}
		else Msg( "\t<none>\n" );
	}
	else if ( args.ArgC() == 2 && args[1][0] == '!' )
	{
		s_ResponseConceptExcludes.RemoveAll();
		Msg( "Exclude list emptied.\n" );
	}
	else if ( s_ResponseConceptExcludes.Count() <= 100 )
	{
		// Native cap is checked once, before the complete argv loop, not per item.
		for ( int i = 1; i < args.ArgC(); ++i )
		{
			if ( !s_pAIConceptSymbols->Find( args[i] ).IsValid() )
				Msg( "\t'%s' is not a known concept (adding it anyway)\n", args[i] );
			s_ResponseConceptExcludes.AddToTail( AI_Concept( args[i] ) );
		}
	}
}

// Native object 0x007CF658; callback 0x00020610; command_function.
static ConCommand rr_debugresponseconcept_exclude_command( "rr_debugresponseconcept_exclude", CC_rr_debugresponseconcept_exclude, "Set a list of concepts to exclude from rr_debugresponseconcept. Separate multiple concepts with spaces. Call with no arguments to see current list. Call 'rr_debug_responseconcept_exclude !' to reset.", 0x00000004 );

#endif
