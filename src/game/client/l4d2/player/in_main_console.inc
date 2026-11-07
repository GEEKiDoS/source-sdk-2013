// Compose into in_main.cpp before GetButtonBits. C133030..133126.
static kbutton_t in_lookspin;
static kbutton_t in_resetcamera;
// C130BE8 reads the press impulse before GetButtonBits consumes it.
bool IN_IsLookSpinPressed() { return ( in_lookspin.state & 2 ) != 0; }
bool IN_IsResetCameraHeld() { return ( in_resetcamera.state & 1 ) != 0; }
static void IN_LookSpinDown( const CCommand &args ) { KeyDown( &in_lookspin, args[1] ); }
static void IN_LookSpinUp( const CCommand &args ) { KeyUp( &in_lookspin, args[1] ); }
static void IN_ResetCameraDown( const CCommand &args ) { KeyDown( &in_resetcamera, args[1] ); }
static void IN_ResetCameraUp( const CCommand &args ) { KeyUp( &in_resetcamera, args[1] ); }
// C13CB10..C13CB61: the three pressed globals have only command writes and
// C13D7B0 reset writes, no read/address/copy consumer. Their native release
// functions are RET. Ledger318 drops only that unobservable storage, not names.
static void IN_MenuUpDown() {}
static void IN_MenuUpUp() {}
static void IN_MenuDownDown() {}
static void IN_MenuDownUp() {}
static void IN_MenuAcceptDown() {}
static void IN_MenuAcceptUp() {}

// GetButtonBits L4D2 replacement for the SDK IN_ATTACK3 collection line:
// CalcButtonBits( bits, IN_LOOKSPIN, s_ClearInputState, &in_lookspin, bResetState );
// CalcButtonBits( bits, IN_RESETCAMERA, s_ClearInputState, &in_resetcamera, bResetState );
// in_buttons.h: L4D2 names IN_LOOKSPIN=(1<<25), IN_RESETCAMERA=(1<<26).
// Keep SDK IN_ATTACK3 and its collection only in non-L4D2 builds. L4D2 has no
// native +attack3/-attack3 object: remove their non-native registrations there.
