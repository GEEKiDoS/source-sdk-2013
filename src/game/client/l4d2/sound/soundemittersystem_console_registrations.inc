// Append after the native callback bodies in the owner TU; CLIENT_DLL && L4D2 only.
// client:console:1602 object 0x0079f9c0 callback 0x00198550
static ConCommand cl_soundemitter_reload( "cl_soundemitter_reload", CC_ClientSoundEmitterReload, "Flushes the sounds.txt system", 0x8 );

// client:console:1679 object 0x0079fa08 callback 0x00199d70
static ConCommand snd_playsounds( "snd_playsounds", CC_ClientPlaySounds, "Play sounds from the game sounds txt file at a given location", 0x50000008, ClientPlaySoundsCompletion );

// client:console:1680 object 0x0079fa2c callback 0x00198b20
static ConCommand snd_setsoundparam( "snd_setsoundparam", CC_ClientSetSoundParam, "Set a sound paramater", 0x50000008, ClientSetSoundParamCompletion );
