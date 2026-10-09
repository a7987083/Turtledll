-- AutoRange FuBar entry for WoW 1.12 / Turtle WoW.
-- Uses shared !Libs Ace2 / FuBarPlugin-2.0.
-- Keep this registration shape aligned with the proven DreamAvatar FuBar entry.

local function AutoRangeFuBarTogglePanel()
    if type(AutoRange_TogglePanel) == "function" then
        AutoRange_TogglePanel()
    end
end

local function AutoRangeFuBarToggleEnabled()
    if type(AutoRange_ToggleEnabled) == "function" then
        AutoRange_ToggleEnabled(true)
    end
end

-- IMPORTANT: AceLibrary in this Ace2 environment may be callable without being
-- type "function". Do not reject it with type(AceLibrary) ~= "function".
if not AceLibrary then return end

-- Proven WoW 1.12 / Ace2 lookup form used by the working FuBar entry.
local okLibrary, AceAddon = pcall(function()
    return AceLibrary("AceAddon-2.0")
end)
if not okLibrary or not AceAddon or type(AceAddon.new) ~= "function" then return end

-- FuBarPlugin-2.0 relies on the original call stack to detect this addon's folder.
-- Do not wrap AceAddon:new() in pcall/function here.
local entry = AceAddon:new("FuBarPlugin-2.0")
if not entry then return end

AutoRange_FuBarEntry = entry
AutoRange_FuBarEntry.name = "AutoRange"
AutoRange_FuBarEntry.hasIcon = "Interface\\Icons\\Ability_Hunter_SniperShot"
AutoRange_FuBarEntry.defaultPosition = "LEFT"
AutoRange_FuBarEntry.defaultMinimapPosition = 230
AutoRange_FuBarEntry.cannotDetachTooltip = true

function AutoRange_FuBarEntry:OnTextUpdate()
    local on = type(AutoRange_IsEnabled) == "function" and AutoRange_IsEnabled()
    if on then
        self:SetText("AutoRange:开")
    else
        self:SetText("AutoRange:关")
    end
end

function AutoRange_FuBarEntry:OnTooltipUpdate()
    GameTooltip:AddLine("AutoRange  ·  工会 太阳神殿", 0.35, 0.82, 1.0)
    local on = type(AutoRange_IsEnabled) == "function" and AutoRange_IsEnabled()
    if on then
        GameTooltip:AddLine("状态：已开启", 0.35, 1.0, 0.45)
    else
        GameTooltip:AddLine("状态：已关闭", 1.0, 0.45, 0.45)
    end
    GameTooltip:AddLine("左键：显示 / 隐藏开关面板", 1.0, 1.0, 1.0)
    GameTooltip:AddLine("右键：直接开启 / 关闭危险圈", 1.0, 1.0, 1.0)
    GameTooltip:AddLine("命令：/arange", 0.70, 0.85, 1.0)
    GameTooltip:AddLine("B3.8R7.5.1c · Stage2.31 ARX1", 0.65, 0.65, 0.65)
end

function AutoRange_FuBarEntry:OnClick()
    if arg1 == "RightButton" then
        AutoRangeFuBarToggleEnabled()
    else
        AutoRangeFuBarTogglePanel()
    end
end
