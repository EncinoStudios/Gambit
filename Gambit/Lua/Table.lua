-- Table: helpers for plain-data tables. Keys are visited in SortedPairs order, so the results never depend on the
-- order of `pairs` (D-011).

local SortedPairs = require("Gambit.SortedPairs")
local next, type, getmetatable, setmetatable, error = next, type, getmetatable, setmetatable, error

local Table = {}

local function CopyValue(value, copies)
	if type(value) ~= "table" then
		return value
	end
	local existing = copies[value]
	if existing ~= nil then
		return existing
	end
	local copy = {}
	copies[value] = copy
	-- The copy has the same entries whatever the order of the visit.
	for key, entry in next, value do
		copy[CopyValue(key, copies)] = CopyValue(entry, copies)
	end
	return setmetatable(copy, getmetatable(value))
end

-- Table.Copy(t): a deep copy of `t`. Nested tables are copied too (a table that appears twice, or contains itself, is
-- copied once); other values are shared. The copies keep the metatables of the originals.
function Table.Copy(t)
	if type(t) ~= "table" then
		error("Table.Copy: expected a table, got " .. type(t), 2)
	end
	return CopyValue(t, {})
end

-- Table.Keys(t): the keys of `t` as an array, in SortedPairs order.
function Table.Keys(t)
	if type(t) ~= "table" then
		error("Table.Keys: expected a table, got " .. type(t), 2)
	end
	local keys = {}
	for key in SortedPairs(t) do
		keys[#keys + 1] = key
	end
	return keys
end

-- Table.Count(t): the number of entries of `t` (all keys, not only the sequence that `#` counts).
function Table.Count(t)
	if type(t) ~= "table" then
		error("Table.Count: expected a table, got " .. type(t), 2)
	end
	local count = 0
	for _ in next, t do
		count = count + 1
	end
	return count
end

return Table
