#include "Inventory/NoFearItemData.h"

FText UNoFearItemData::GetDisplayName() const
{
	return DisplayName.IsEmpty() ? FText::FromString(GetName()) : DisplayName;
}
