#pragma once

#include "Gambit/Reflection/FieldType.h"

#include <vector>

namespace Gambit
{
	// The data of one instance of a script-defined component (D-023): one value per field of its schema, in field
	// order, each holding the FieldValue alternative of the field's type. Instances live in EnTT storages named by the
	// component id (one storage per schema), so script components are first-class: serialized, inspected, hashed and
	// copied through their ComponentInfo like C++ components. Access goes through the schema's FieldInfo; the values
	// are never resized or retyped while the schema is registered.
	struct DynamicComponent
	{
		std::vector<FieldValue> Values;
	};
}
