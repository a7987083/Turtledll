-- SkillsID: WoW 1.12.1 / Lua 5.0
-- Only display verified spell IDs; spellbook slots are NOT spell IDs.
local SI = { byNameRank = {}, ambiguous = {}, scanned = false, cursor = 1,
             upper = 60000, batch = 200, pending = nil, driver = nil, started = false }
local function normalize(value)
  if type(value) ~= "string" then return "" end
  value = string.gsub(value, "|c%x%x%x%x%x%x%x%x", "")
  value = string.gsub(value, "|r", "")
  value = string.gsub(value, "^%s+", "")
  value = string.gsub(value, "%s+$", "")
  value = string.gsub(value, "%s+", " ")
  return value
end
local function key(name, rank)
  return normalize(name) .. "\001" .. normalize(rank)
end
local function insertSpell(id, rec)
  if not rec or type(rec.name) ~= "string" or rec.name == "" then return end
  local k = key(rec.name, rec.rank)
  local old = SI.byNameRank[k]
  if old and old ~= id then
    SI.ambiguous[k] = true
  else
    SI.byNameRank[k] = id
  end
end
local function lookup(name, rank)
  local k = key(name, rank)
  if not SI.ambiguous[k] then return SI.byNameRank[k] end
  return nil
end
local function missReason(name, rank)
  if not SI.scanned then return "INDEX_INCOMPLETE" end
  local k = key(name, rank)
  if SI.ambiguous[k] then return "AMBIGUOUS_NAME_RANK" end
  if not SI.byNameRank[k] then return "NOT_INDEXED_OR_RANK_MISMATCH" end
  return "OK"
end
local function addLine(tip, id)
  id = tonumber(id)
  if not id or id <= 0 then return end
  if not tip or not tip.GetName then return end
  local name = tip:GetName()
  if not name then return end
  local label = "SpellID: " .. id
  local i
  for i = 1, tip:NumLines() do
    local line = getglobal(name .. "TextLeft" .. i)
    if line and line:GetText() == label then return end
  end
  tip:AddLine(label, 1, 0.82, 0.25)
  tip:Show()
end
SI.finishTooltip = addLine
local function tipName(tip)
  local name = tip and tip:GetName()
  local line = name and getglobal(name .. "TextLeft1")
  return line and line:GetText()
end
-- A complete scan is required before treating name+rank as unique.
-- Start it only after a spellbook tooltip actually needs an ID.
local function startIndex()
  if SI.started or type(GetSpellRec) ~= "function" then return end
  SI.started = true
  local driver = CreateFrame("Frame")
  SI.driver = driver
  driver:SetScript("OnUpdate", function()
    local last = SI.cursor + SI.batch - 1
    if last > SI.upper then last = SI.upper end
    local id
    for id = SI.cursor, last do
      local ok, rec = pcall(GetSpellRec, id)
      if ok then insertSpell(id, rec) end
    end
    SI.cursor = last + 1
    if SI.cursor > SI.upper then
      SI.scanned = true
      this:SetScript("OnUpdate", nil)
      -- Pending tooltip may have changed while the index was building.
      local p = SI.pending
      SI.pending = nil
      if p and p.tip and p.tip:IsShown() and GetSpellName then
        local name, rank = GetSpellName(p.slot, p.bookType)
        if key(name, rank) == key(p.name, p.rank) then
          local line = getglobal(p.tip:GetName() .. "TextLeft1")
          if line and line:GetText() == p.name then
            -- AddIDLine is declared below; deferred injection handled there.
            if SI.finishTooltip then SI.finishTooltip(p.tip, lookup(name, rank)) end
          end
        end
      end
    end
  end)
end
-- Tooltip methods in the original 1.12 client are not retail's modern events.
if GameTooltip and GameTooltip.SetSpell then
  local original = GameTooltip.SetSpell
  GameTooltip.SetSpell = function(tip, slot, bookType)
    local a, b = original(tip, slot, bookType)
    if GetSpellName then
      local name, rank = GetSpellName(slot, bookType)
      SI.lastSpell = { name = name, rank = rank or "", slot = slot, bookType = bookType }
      if SI.scanned then
        addLine(tip, lookup(name, rank))
      else
        SI.pending = { tip = tip, slot = slot, bookType = bookType,
                       name = name, rank = rank or "" }
        startIndex()
      end
    end
    return a, b
  end
end
if GameTooltip and GameTooltip.SetAction then
  local original = GameTooltip.SetAction
  GameTooltip.SetAction = function(tip, slot)
    local a, b = original(tip, slot)
    local id
    if GetActionText then
      local text, actionType, actionId = GetActionText(slot)
      if actionType == "SPELL" then id = actionId end
    end
    -- Never guess an action's rank from its name alone.
    addLine(tip, id)
    return a, b
  end
end
local function hookHyperlink(tip)
  if not tip or not tip.SetHyperlink then return end
  local original = tip.SetHyperlink
  tip.SetHyperlink = function(self, link)
    local a, b = original(self, link)
    if type(link) == "string" then
      local _, _, id = string.find(link, "^spell:(%d+)")
      if id then addLine(self, id) end
    end
    return a, b
  end
end
hookHyperlink(GameTooltip)
hookHyperlink(ItemRefTooltip)
if GameTooltip and GameTooltip.SetPlayerBuff and GetPlayerBuffID then
  local original = GameTooltip.SetPlayerBuff
  GameTooltip.SetPlayerBuff = function(tip, index, filter)
    local a, b = original(tip, index, filter)
    local id = GetPlayerBuffID(index)
    addLine(tip, id)
    return a, b
  end
end
-- SuperWoW extends UnitBuff/UnitDebuff with the exact aura SpellID as return #4.
-- Vanilla clients without that extension do not expose an exact ID here.
local function auraId(unit, index, helpful)
  if type(unit) ~= "string" or type(index) ~= "number" then return nil end
  local api
  if helpful then api = UnitBuff else api = UnitDebuff end
  if type(api) ~= "function" then return nil end
  local ok, texture, stacks, dispelType, id = pcall(api, unit, index)
  if ok and type(id) == "number" and id > 0 then return id end
  return nil
end
local function hookUnitAura(method, helpful)
  if not GameTooltip or type(GameTooltip[method]) ~= "function" then return end
  local original = GameTooltip[method]
  GameTooltip[method] = function(tip, unit, index)
    local a, b = original(tip, unit, index)
    addLine(tip, auraId(unit, index, helpful))
    return a, b
  end
end
hookUnitAura("SetUnitBuff", true)
hookUnitAura("SetUnitDebuff", false)

SLASH_SKILLSID1 = "/skillsid"
SlashCmdList["SKILLSID"] = function(msg)
  if normalize(msg) == "debug" then
    local p = SI.lastSpell
    local status = p and ("name=" .. tostring(p.name) .. ", rank=" .. tostring(p.rank) .. ", result=" .. missReason(p.name, p.rank)) or "NO_SPELLBOOK_HOVER"
    DEFAULT_CHAT_FRAME:AddMessage("|cffffcc33SkillsID|r: " .. status)
    return
  end
  local status
  if type(GetSpellRec) ~= "function" then
    status = "GetSpellRec unavailable; spellbook SpellIDs cannot be resolved reliably."
  elseif SI.scanned then
    status = "SpellID index ready."
  elseif SI.started then
    status = "Indexing on demand: " .. (SI.cursor - 1) .. "/" .. SI.upper
  else
    status = "Idle (index starts on first spellbook hover)."
  end
  DEFAULT_CHAT_FRAME:AddMessage("|cffffcc33SkillsID|r: " .. status)
end
