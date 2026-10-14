-- Minimal settings UI. No tick algorithm, automatic use, or movement.
local DW = TaiYangDrinkWalk
function DW.RefreshSettings()
    if DW.statusText then DW.statusText:SetText(DW.DLLStatus()) end
    if DW.modeText then
        local mode = "WATCH"
        if type(TaiYangDrinkWalkDB) == "table" then mode = TaiYangDrinkWalkDB.mode or "WATCH" end
        DW.modeText:SetText("模式: " .. mode .. " (仅记录设置)")
    end
end
function DW.BuildSettings()
    if DW.settingsFrame then return end
    if not UIParent then return end
    local f = CreateFrame("Frame", "TaiYangDrinkWalkSettings", UIParent)
    f:SetWidth(370); f:SetHeight(225)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    f:SetFrameStrata("DIALOG")
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border", tile=true, tileSize=32, edgeSize=32, insets={left=8,right=8,top=8,bottom=8}})
    f:SetMovable(true); f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() f:StartMoving() end)
    f:SetScript("OnDragStop", function() f:StopMovingOrSizing() end)
    local title = f:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
    title:SetPoint("TOP", f, "TOP", 0, -19)
    title:SetText("TaiYangDrinkWalk - 框架")
    local close = CreateFrame("Button",nil,f,"UIPanelCloseButton")
    close:SetPoint("TOPRIGHT",f,"TOPRIGHT",-4,-4)
    DW.statusText = f:CreateFontString(nil,"OVERLAY","GameFontHighlight")
    DW.statusText:SetPoint("TOPLEFT",f,"TOPLEFT",21,-63)
    DW.statusText:SetWidth(326); DW.statusText:SetJustifyH("LEFT")
    DW.modeText = f:CreateFontString(nil,"OVERLAY","GameFontNormal")
    DW.modeText:SetPoint("TOPLEFT",f,"TOPLEFT",21,-99)
    local note = f:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    note:SetPoint("TOPLEFT",f,"TOPLEFT",21,-136)
    note:SetWidth(320); note:SetJustifyH("LEFT")
    note:SetText("第一阶段：仅加载框架和界面。物品扫描、Tick 和走喝逻辑暂未接入。")
    local b = CreateFrame("Button",nil,f,"UIPanelButtonTemplate")
    b:SetWidth(95); b:SetHeight(24)
    b:SetPoint("BOTTOMLEFT",f,"BOTTOMLEFT",20,19)
    b:SetText("切换模式")
    b:SetScript("OnClick",function()
        if type(TaiYangDrinkWalkDB) ~= "table" then TaiYangDrinkWalkDB = {} end
        if TaiYangDrinkWalkDB.mode == "WATCH" then TaiYangDrinkWalkDB.mode = "SPAM"
        else TaiYangDrinkWalkDB.mode = "WATCH" end
        DW.RefreshSettings()
    end)
    local status = CreateFrame("Button",nil,f,"UIPanelButtonTemplate")
    status:SetWidth(95);status:SetHeight(24)
    status:SetPoint("LEFT",b,"RIGHT",12,0)
    status:SetText("状态检查")
    status:SetScript("OnClick",function() DW.RefreshSettings(); DW.ShowStatus() end)
    f:Hide()
    DW.settingsFrame = f
    DW.RefreshSettings()
end
DW.RegisterModule("Settings")
