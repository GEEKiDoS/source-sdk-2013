// Append after the native callback bodies in the owner TU; CLIENT_DLL && L4D2 only.
// client:console:1576 object 0x007b02dc callback 0x002217b0
static ConCommand cl_buy_favorite( "cl_buy_favorite", CC_BuyFavorite, "Purchase a favorite weapon/equipment loadout", 0x40000008 );

// client:console:1577 object 0x007b03b4 callback 0x00221a50
static ConCommand cl_buy_favorite_reset( "cl_buy_favorite_reset", CC_ResetBuyFavorites, "Reset favorite loadouts to the default", 0x40000008 );

// client:console:1578 object 0x007b0390 callback 0x00221890
static ConCommand cl_buy_favorite_set( "cl_buy_favorite_set", CC_SetBuyFavorite, "Saves the current loadout as a favorite", 0x40000008 );
