-- Optional FuBar entry for TaiYang DrinkWalk v0.6.3.
-- Core addon does not require Ace2/FuBar; this file exits cleanly if !Libs is absent.

if not AceLibrary or not AceLibrary.HasInstance then return end
if not AceLibrary:HasInstance("AceAddon-2.0") then return end
if not AceLibrary:HasInstance("AceDB-2.0") then return end
if not AceLibrary:HasInstance("FuBarPlugin-2.0") then return end
if not AceLibrary:HasInstance("Tablet-2.0") then return end

local Tablet = AceLibrary("Tablet-2.0")
TaiYangDrinkWalkFu = AceLibrary("AceAddon-2.0"):new("AceDB-2.0", "FuBarPlugin-2.0")
TaiYangDrinkWalkFu.name = "DrinkWalk"
TaiYangDrinkWalkFu.hasIcon = "Interface\\Icons\\INV_Drink_04"
TaiYangDrinkWalkFu.hasNoColor = true
TaiYangDrinkWalkFu.cannotDetachTooltip = true
TaiYangDrinkWalkFu.clickableTooltip = false
TaiYangDrinkWalkFu.defaultMinimapPosition = 210
-- Use an addon-owned context menu instead of Ace/FuBar implied options.
-- Keep the built-in menu title and Close line; only add About here.
TaiYangDrinkWalkFu.overrideMenu = true

function TaiYangDrinkWalkFu:OnMenuRequest(level, value)
    if not AceLibrary:HasInstance("Dewdrop-2.0") then return end
    local Dewdrop = AceLibrary("Dewdrop-2.0")
    if level == 1 then
        Dewdrop:AddLine("text", "关于", "hasArrow", true, "value", "about")
    elseif level == 2 and value == "about" then
        Dewdrop:AddLine("text", "太阳神殿 DrinkWalk", "isTitle", true)
        Dewdrop:AddLine("text", "左键点击酒杯：打开 / 关闭设置", "disabled", true)
    end
end

function TaiYangDrinkWalkFu:OnInitialize()
    self:RegisterDB("TaiYangDrinkWalkFuDB")
end

function TaiYangDrinkWalkFu:OnTextUpdate()
    self:SetText("DrinkWalk")
end

function TaiYangDrinkWalkFu:OnClick()
    if type(TaiYangDrinkWalk_ToggleSettings) == "function" then
        TaiYangDrinkWalk_ToggleSettings()
    end
end

function TaiYangDrinkWalkFu:OnTooltipUpdate()
    local status, item, key, tick, mana, resource = "--", "--", "--", false, false, "--"
    if type(TaiYangDrinkWalk_GetFuBarSummary) == "function" then
        status, item, key, tick, mana, resource = TaiYangDrinkWalk_GetFuBarSummary()
    end
    local cat = Tablet:AddCategory('columns', 2)
    cat:AddLine('text', '状态', 'text2', tostring(status or '--'))
    cat:AddLine('text', '按键', 'text2', tostring(key or '--'))
    cat:AddLine('text', 'Tick 计时条（Mana/Energy）', 'text2', tick and '开启' or '关闭')
    cat:AddLine('text', '自适应资源条 / Spark', 'text2', mana and '开启' or '关闭')
    cat:AddLine('text', '当前资源', 'text2', tostring(resource or '--'))
    local itemCat = Tablet:AddCategory('columns', 1)
    itemCat:AddLine('text', tostring(item or '--'))
    Tablet:SetHint("左键：打开 / 关闭 DrinkWalk 设置")
end
