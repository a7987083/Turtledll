-- TaiYangShenDian DrinkWalk v1.0.0
-- WoW 1.12.1 Lua 5.0. All native requests require an explicit player action.
-- This addon never moves the player, schedules uses, or changes DLL hooks.
TaiYangDrinkWalkDB = TaiYangDrinkWalkDB or {}
local DW = {}
local VERSION = "1.0.0"
local function Say(s)
    if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage("|cffffd200[DrinkWalk]|r "..tostring(s)) end
end
local function Num(x, low, high)
    local n = tonumber(x)
    if not n or n ~= math.floor(n) or n < low or n > high then return nil end
    return n
end
local function Native(command)
    if type(TaiYangShenDian) ~= "function" then return false, "DLL_MISSING" end
    local ok, a, b, c = pcall(TaiYangShenDian, command)
    if not ok then return false, tostring(a) end
    return true, a, b, c
end
local function NativeSlot(command, bag, slot)
    if type(TaiYangShenDian) ~= "function" then return false, "DLL_MISSING" end
    local ok, a, b, c = pcall(TaiYangShenDian, command, bag, slot)
    if not ok then return false, tostring(a) end
    return true, a, b, c
end
local function CoreStatus()
    local ok, ver, api = Native("Core.Version")
    if not ok then return "DLL: "..tostring(ver), false end
    local n = tonumber(api)
    if not n or n < 24 then return "DLL: API "..tostring(api).." (需要 API24+)", false end
    local sok, st = Native("DrinkWalk.Status")
    if not sok or type(st) ~= "table" then return "DrinkWalk.Status: 不可用", false end
    if not st.available then
        return "DLL API"..tostring(api).." / Native 不可用: "..tostring(st.lastCode or "?"), false
    end
    return "DLL API"..tostring(api).." / Native READY", true
end
local function Config()
    if type(TaiYangDrinkWalkDB) ~= "table" then TaiYangDrinkWalkDB = {} end
    if not Num(TaiYangDrinkWalkDB.bag, 0, 4) then TaiYangDrinkWalkDB.bag = 0 end
    if not Num(TaiYangDrinkWalkDB.slot, 1, 36) then TaiYangDrinkWalkDB.slot = 1 end
end
local function PutStatus(s)
    if DW.status then DW.status:SetText(tostring(s)) end
end
local function Refresh()
    Config()
    local status, ready = CoreStatus()
    PutStatus(status)
    if DW.bag then DW.bag:SetText(tostring(TaiYangDrinkWalkDB.bag)) end
    if DW.slot then DW.slot:SetText(tostring(TaiYangDrinkWalkDB.slot)) end
    if DW.use then
        if ready then DW.use:Enable() else DW.use:Disable() end
    end
    return ready
end
local function SaveSlots()
    local bag = Num(DW.bag:GetText(), 0, 4)
    local slot = Num(DW.slot:GetText(), 1, 36)
    if not bag or not slot then
        Say("请输入合法的包编号 0-4 和格子编号 1-36")
        return false
    end
    TaiYangDrinkWalkDB.bag = bag
    TaiYangDrinkWalkDB.slot = slot
    return true
end
local function Resolve()
    if not SaveSlots() then return end
    if not Refresh() then Say("DLL Native 尚不可用"); return end
    local ok, found, code, ptr = NativeSlot("DrinkWalk.ResolveItem", TaiYangDrinkWalkDB.bag, TaiYangDrinkWalkDB.slot)
    if not ok then Say("解析调用错误: "..tostring(found)); return end
    Say("解析 "..(found and "成功" or "失败")..": "..tostring(code).." / "..tostring(ptr))
end
local function Use()
    if not SaveSlots() then return end
    if not Refresh() then Say("DLL Native 尚不可用"); return end
    local ok, sent, code = NativeSlot("DrinkWalk.UseItem", TaiYangDrinkWalkDB.bag, TaiYangDrinkWalkDB.slot)
    if not ok then Say("调用异常: "..tostring(sent)); return end
    Say("使用请求 "..(sent and "已派发" or "失败")..": "..tostring(code))
    if sent then Say("已派发不代表服务器确认饮用，请检查物品数量和食物/饮水状态") end
end
local function Btn(parent, text, x, y, fn)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    b:SetWidth(90); b:SetHeight(24)
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
    b:SetText(text)
    b:SetScript("OnClick", fn)
    return b
end
local function Input(parent, name, x)
    local b = CreateFrame("EditBox", name, parent, "InputBoxTemplate")
    b:SetWidth(42); b:SetHeight(22); b:SetAutoFocus(false)
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", x, -114)
    b:SetScript("OnEnterPressed", function() this:ClearFocus(); SaveSlots() end)
    b:SetScript("OnEscapePressed", function() this:ClearFocus() end)
    return b
end
local function BuildUI()
    if DW.frame then return true end
    if not UIParent then return false end
    local f = CreateFrame("Frame", "TaiYangDrinkWalkFrame", UIParent)
    f:SetWidth(440); f:SetHeight(215)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    f:SetFrameStrata("DIALOG")
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=8,right=8,top=8,bottom=8}})
    f:SetMovable(true); f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", function() this:StartMoving() end)
    f:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
    local title = f:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
    title:SetPoint("TOPLEFT",f,"TOPLEFT",20,-20)
    title:SetText("太阳神殿 DrinkWalk "..VERSION)
    local close = CreateFrame("Button",nil,f,"UIPanelCloseButton")
    close:SetPoint("TOPRIGHT",f,"TOPRIGHT",-4,-4)
    local status = f:CreateFontString(nil,"OVERLAY","GameFontNormal")
    status:SetPoint("TOPLEFT",f,"TOPLEFT",20,-59)
    status:SetWidth(396); status:SetJustifyH("LEFT")
    DW.status = status
    local hint = f:CreateFontString(nil,"OVERLAY","GameFontNormalSmall")
    hint:SetPoint("TOPLEFT",f,"TOPLEFT",20,-84)
    hint:SetText("手动指定背包位置；0=主背包，1-4=其他背包。")
    local bagText = f:CreateFontString(nil,"OVERLAY","GameFontNormal")
    bagText:SetPoint("TOPLEFT",f,"TOPLEFT",20,-117); bagText:SetText("包")
    DW.bag = Input(f,"TaiYangDrinkWalkBag",54)
    local slotText = f:CreateFontString(nil,"OVERLAY","GameFontNormal")
    slotText:SetPoint("TOPLEFT",f,"TOPLEFT",125,-117); slotText:SetText("格")
    DW.slot = Input(f,"TaiYangDrinkWalkSlot",157)
    Btn(f,"保存",228,-110,SaveSlots)
    Btn(f,"检测 DLL",20,-150,Refresh)
    Btn(f,"检查物品",120,-150,Resolve)
    DW.use = Btn(f,"手动使用",220,-150,Use)
    f:Hide()
    DW.frame = f
    Refresh()
    return true
end
local function Toggle()
    if not DW.frame then
        if not BuildUI() then Say("UIParent 尚未就绪，请稍后重试"); return end
    end
    if DW.frame:IsShown() then DW.frame:Hide() else Refresh(); DW.frame:Show() end
end
-- Register commands first: failure of the UI or native API cannot remove them.
SLASH_TYSDRINKWALK1="/tsdw"
SLASH_TYSDRINKWALK2="/drinkwalk"
SlashCmdList["TYSDRINKWALK"] = function(msg)
    local cmd = string.lower(msg or "")
    if cmd == "status" then local s=CoreStatus(); Say(s)
    elseif cmd == "use" then Use()
    elseif cmd == "resolve" then Resolve()
    else Toggle() end
end
SLASH_TYSZCHIHE1="/zchihe"
SlashCmdList["TYSZCHIHE"] = function() Use() end
TaiYangDrinkWalk_ToggleSettings = Toggle
local events = CreateFrame("Frame","TaiYangDrinkWalkEvents")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("VARIABLES_LOADED")
events:SetScript("OnEvent", function()
    Config()
    if event == "PLAYER_LOGIN" then
        local ok, err = pcall(BuildUI)
        if not ok then Say("界面创建失败: "..tostring(err)) end
    end
end)
Say("重制版 "..VERSION.." 已加载。输入 /tsdw 打开，/tsdw status 查看 DLL。")
