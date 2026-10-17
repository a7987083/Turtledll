-- SkillsID: WoW 1.12.1 / Lua 5.0
-- Show SpellIDs from action buttons, auras and spell links; no spellbook scanning.
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

