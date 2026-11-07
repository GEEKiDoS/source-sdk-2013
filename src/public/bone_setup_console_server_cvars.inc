// Transplant into src/public/bone_setup.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00881220; constructor 0x005AECA9.
ConVar cl_simdbones( "cl_simdbones", "0", 0x00002006, "Use SIMD bone setup." );

// Native object 0x008812C8; constructor 0x005AECD9.
ConVar cl_use_simd_bones( "cl_use_simd_bones", "1", 0x00002006, "1 use SIMD bones 0 use scalar bones." );

// Native object 0x00881270; constructor 0x005AED04.
ConVar BlendBonesMode( "BlendBonesMode", "2", 0x00002006, "" );

#endif
