-- Optional FuBar entry for TaiYangBossMemo 0.9.2.
-- Core addon does not require Ace2 or FuBar. This file exits cleanly if libraries are absent.
if not AceLibrary or type(AceLibrary.HasInstance) ~= "function" then return end
if not AceLibrary:HasInstance("AceAddon-2.0") then return end
if not AceLibrary:HasInstance("AceDB-2.0") then return end
if not AceLibrary:HasInstance("FuBarPlugin-2.0") then return end
if not AceLibrary:HasInstance("Tablet-2.0") then return end

local Tablet = AceLibrary("Tablet-2.0")
TaiYangBossMemoFu = AceLibrary("AceAddon-2.0"):new("AceDB-2.0", "FuBarPlugin-2.0")
TaiYangBossMemoFu.name = "BossMemo"
TaiYangBossMemoFu.hasIcon = "Interface\\Icons\\INV_Misc_Book_09"
TaiYangBossMemoFu.hasNoColor = true
TaiYangBossMemoFu.cannotDetachTooltip = true
TaiYangBossMemoFu.clickableTooltip = false
TaiYangBossMemoFu.defaultMinimapPosition = 210
TaiYangBossMemoFu.overrideMenu = true

function TaiYangBossMemoFu:OnMenuRequest(level, value)
    if not AceLibrary:HasInstance("Dewdrop-2.0") then return end
    local Dewdrop = AceLibrary("Dewdrop-2.0")
    if level == 1 then
        Dewdrop:AddLine("text", "关于", "hasArrow", true, "value", "about")
    elseif level == 2 and value == "about" then
        Dewdrop:AddLine("text", "太阳神殿 Boss 备忘录", "isTitle", true)
        Dewdrop:AddLine("text", "左键点击图标：打开 / 关闭 Boss 管理", "disabled", true)
        Dewdrop:AddLine("text", "仅战前提醒 · v0.9.2", "disabled", true)
    end
end

function TaiYangBossMemoFu:OnInitialize()
    self:RegisterDB("TaiYangBossMemoFuDB")
end

function TaiYangBossMemoFu:OnTextUpdate()
    self:SetText("BossMemo")
end

function TaiYangBossMemoFu:OnClick()
    if type(TaiYangBossMemo_ToggleSettings) == "function" then
        TaiYangBossMemo_ToggleSettings()
    end
end

function TaiYangBossMemoFu:OnTooltipUpdate()
    local status, active, total, before, reset, dll, interval = "--", 0, 0, false, false, false, "--"
    if type(TaiYangBossMemo_GetFuBarSummary) == "function" then
        status, active, total, before, reset, dll, interval = TaiYangBossMemo_GetFuBarSummary()
    end
    local cat = Tablet:AddCategory('columns', 2)
    cat:AddLine('text', '运行状态', 'text2', tostring(status or '--'))
    cat:AddLine('text', '已配置 Boss', 'text2', tostring(total or 0))
    cat:AddLine('text', '启用且有战前备注', 'text2', tostring(active or 0))
    cat:AddLine('text', '战前提醒', 'text2', before and '开启' or '关闭')
    cat:AddLine('text', '重置后再次提醒', 'text2', reset and '开启' or '关闭')
    cat:AddLine('text', 'DLL', 'text2', dll and '已检测' or '未检测')
    cat:AddLine('text', '检测间隔', 'text2', tostring(interval or '--')..' 秒')
    Tablet:SetHint('左键：打开 / 关闭 Boss 管理')
end