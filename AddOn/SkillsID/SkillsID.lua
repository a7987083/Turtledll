-- SkillsID: WoW 1.12.1 / Lua 5.0
-- Only display verified spell IDs; spellbook slots are NOT spell IDs.
local SI = { byNameRank = {}, ambiguous = {}, scanned = false, cursor = 1,
             upper = 60000, batch = 350, pending = nil }
local function key(name, rank)
  return (name or "") .. "\001" .. (rank or "")
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
local function tipName(tip)
  local name = tip and tip:GetName()
  local line = name and getglobal(name .. "TextLeft1")
  return line and line:GetText()
end
-- Tooltip methods in the original 1.12 client are not retail's modern events.
if GameTooltip and GameTooltip.SetSpell then
  local original = GameTooltip.SetSpell
  GameTooltip.SetSpell = function(tip, slot, bookType)
    local a, b = original(tip, slot, bookType)
    if GetSpellName then
      local name, rank = GetSpellName(slot, bookType)
      addLine(tip, lookup(name, rank))
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
-- Build exact Name+Rank lookup incrementally to avoid a long load-time stall.
-- No GetSpellRec means no verified spellbook mapping; do not display a slot index.
if type(GetSpellRec) == "function" then
  local driver = CreateFrame("Frame")
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
    end
  end)
end
SLASH_SKILLSID1 = "/skillsid"
SlashCmdList["SKILLSID"] = function()
  local status
  if type(GetSpellRec) ~= "function" then
    status = "GetSpellRec unavailable; spellbook SpellIDs cannot be resolved reliably."
  elseif SI.scanned then
    status = "SpellID index ready."
  else
    status = "Indexing spells: " .. (SI.cursor - 1) .. "/" .. SI.upper
  end
  DEFAULT_CHAT_FRAME:AddMessage("|cffffcc33SkillsID|r: " .. status)
end
