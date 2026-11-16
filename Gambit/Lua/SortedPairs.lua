-- SortedPairs(t [, compare]): iterates the entries of `t` in a deterministic order (D-011). `pairs` visits keys in an
-- order that depends on how the table was built, and on memory addresses for table, function and userdata keys, so
-- gameplay code whose result depends on the order uses SortedPairs. Without `compare`, keys are ordered by type
-- (booleans, then numbers, then strings) and then by value; other kinds of keys need a `compare` function. Entries added
-- or removed while iterating are not seen; the values are read when the iteration reaches their key.

local next, type, error, sort = next, type, error, table.sort

local KeyRanks = { boolean = 1, number = 2, string = 3 }

local function DefaultLess(a, b)
	local rankA, rankB = KeyRanks[type(a)], KeyRanks[type(b)]
	if rankA ~= rankB then
		return rankA < rankB
	end
	if rankA == 1 then
		return not a and b
	end
	return a < b
end

local function SortedPairs(t, compare)
	if type(t) ~= "table" then
		error("SortedPairs: expected a table, got " .. type(t), 2)
	end
	local keys = {}
	for key in next, t do
		if compare == nil and KeyRanks[type(key)] == nil then
			error("SortedPairs: a key of type " .. type(key) .. " has no natural order: pass a compare function", 2)
		end
		keys[#keys + 1] = key
	end
	sort(keys, compare or DefaultLess)
	local index = 0
	return function()
		index = index + 1
		local key = keys[index]
		if key ~= nil then
			return key, t[key]
		end
	end
end

return SortedPairs
