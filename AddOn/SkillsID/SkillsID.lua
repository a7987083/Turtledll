-- SkillsID: WoW 1.12.1 / Lua 5.0
-- Action buttons, auras, and chat spell links only; no spellbook scanning.
-- Track only the tooltip line this addon inserted; never change other addons' lines.
local ownedLines = {}
local function addLine(tip, id)
  if not tip or type(tip.GetName) ~= "function" then return end
  local name = tip:GetName()
  if not name then return end
  local record = ownedLines[tip]
  local line = record and record.line
  if line and line:GetText() ~= record.label then
    ownedLines[tip] = nil
    record = nil
    line = nil
  end
  id = tonumber(id)
  if not id or id <= 0 or id ~= math.floor(id) then
    if line then line:SetText("") end
    ownedLines[tip] = nil
    return
  end
  local label = "SpellID: " .. id
  if line then
    if record.label ~= label then
      line:SetText(label)
      record.label = label
    end
    return
  end
  -- Do not add a second copy if another hook already displayed this value.
  local i
  for i = 1, tip:NumLines() do
    local existing = getglobal(name .. "TextLeft" .. i)
    if existing and existing:GetText() == label then return end
  end
  tip:AddLine(label, 1, 0.82, 0.25)
  line = getglobal(name .. "TextLeft" .. tip:NumLines())
  if line then ownedLines[tip] = { line = line, label = label } end
  if tip.IsShown and not tip:IsShown() then tip:Show() end
end
if GameTooltip and GameTooltip.SetAction then
  local original = GameTooltip.SetAction
  GameTooltip.SetAction = function(tip, slot)
    local a, b = original(tip, slot)
    local id
    if type(GetActionText) == "function" then
      local ok, unused, actionType, actionId = pcall(GetActionText, slot)
      if ok and actionType == "SPELL" then id = actionId end
    end
    -- Never guess an action's rank from its name alone.
    addLine(tip, id)
    return a, b
  end
end
-- Turtle WoW chat spell links can use enchant:<spellID>.
-- Limit parsing to the link type prefix; never use a spell name as an ID.
local function linkSpellID(link)
  if type(link) ~= "string" then return nil end
  local _, _, id = string.find(link, "^spell:(%d+)$")
  if not id then _, _, id = string.find(link, "^enchant:(%d+)$") end
  return id and tonumber(id) or nil
end
local function hookHyperlink(tip)
  if not tip or type(tip.SetHyperlink) ~= "function" then return end
  local original = tip.SetHyperlink
  tip.SetHyperlink = function(self, link)
    local a, b = original(self, link)
    -- Only annotate a tooltip the client actually displayed.
    if self.IsShown and self:IsShown() then addLine(self, linkSpellID(link)) end
    return a, b
  end
end
hookHyperlink(GameTooltip)
hookHyperlink(ItemRefTooltip)
-- Some vanilla chat links are rendered by SetItemRef without calling the
-- exposed ItemRefTooltip:SetHyperlink method. Keep that path covered as well.
if type(SetItemRef) == "function" then
  local originalSetItemRef = SetItemRef
  SetItemRef = function(link, text, button)
    local a, b = originalSetItemRef(link, text, button)
    if ItemRefTooltip and ItemRefTooltip.IsShown and ItemRefTooltip:IsShown() then
      addLine(ItemRefTooltip, linkSpellID(link))
    end
    return a, b
  end
end
if GameTooltip and GameTooltip.SetPlayerBuff and GetPlayerBuffID then
  local original = GameTooltip.SetPlayerBuff
  GameTooltip.SetPlayerBuff = function(tip, index, filter)
    local a, b = original(tip, index, filter)
    local ok, id = pcall(GetPlayerBuffID, index)
    addLine(tip, ok and id or nil)
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
  -- Lua 5.0: placeholders preserve the fourth return value (SpellID).
  local ok, unused1, unused2, unused3, id = pcall(api, unit, index)
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

