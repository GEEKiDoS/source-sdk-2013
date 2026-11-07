// Compose into c_baseentity.cpp. C04BB00 / C04BCA0, supported single slot.
void UpdateVisibilityAllEntities()
{
	C_BaseEntityIterator iterator;
	for ( C_BaseEntity *entity = iterator.Next(); entity; entity = iterator.Next() )
		entity->UpdateVisibility();
}
