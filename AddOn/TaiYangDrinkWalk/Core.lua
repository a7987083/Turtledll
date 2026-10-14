-- WoW 1.12.1 / Lua 5.0. Bootstrap and public API.
TaiYangDrinkWalk = TaiYangDrinkWalk or {}
local DW = TaiYangDrinkWalk
DW.version = "0.4.0-hud"
DW.modules = {}
DW.ready = false
DW.uiError = nil

function DW.Print(message)
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd200[TaiYangDrinkWalk]|r " .. tostring(message))
    end
end
function DW.DLLStatus()
    if type(TaiYangShenDian) ~= "function" then return "DLL: 未注册" end
    local ok, ver, api = pcall(TaiYangShenDian, "Core.Version")
    if not ok then return "DLL: 调用出错 " .. tostring(ver) end
    return "DLL: " .. tostring(ver) .. " / API " .. tostring(api)
end
function DW.RegisterModule(name)
    DW.modules[name] = true
end
function DW.ShowStatus()
    DW.Print("框架: " .. tostring(DW.version) .. " | " .. DW.DLLStatus())
    DW.Print("模块: Items=只读选择; Tick/HUD=观测预览; Action=未接入")
    if DW.TickReport then DW.TickReport() end
    if DW.uiError then DW.Print("UI error: " .. tostring(DW.uiError)) end
end
function DW.Toggle()
    if DW.BuildSettings and not DW.settingsFrame then
        local ok, err = pcall(DW.BuildSettings)
        if not ok then DW.uiError = tostring(err); DW.Print("设置界面错误: " .. DW.uiError); return end
    end
    if not DW.settingsFrame then DW.Print("设置界面尚不可用"); return end
    if DW.settingsFrame:IsShown() then DW.settingsFrame:Hide()
    else
        if DW.RefreshSettings then
            local ok, err = pcall(DW.RefreshSettings)
            if not ok then
                DW.uiError = tostring(err)
                DW.Print("设置界面刷新失败: "..DW.uiError)
                return
            end
        end
        DW.settingsFrame:Show()
    end
end
SLASH_TAIYANGDRINKWALK1 = "/tsdw"
SLASH_TAIYANGDRINKWALK2 = "/drinkwalk"
SlashCmdList["TAIYANGDRINKWALK"] = function(msg)
    local cmd = string.lower(msg or "")
    if cmd == "status" then DW.ShowStatus()
    elseif cmd == "tick" then if DW.TickReport then DW.TickReport() end
    elseif cmd == "hud" then if DW.HUD then DW.HUD.Toggle() end
    elseif cmd == "window" then if DW.HUD then DW.HUD.Report() end
    elseif cmd == "scan" then DW.Items.Report()
    elseif cmd == "list" then DW.Print("配置: "..DW.Items.GetSpec())
    elseif string.sub(cmd,1,4) == "set " then
        local ok,result=DW.Items.SetSpec(string.sub(msg,5))
        DW.Print(result)
    elseif cmd == "help" then DW.Print("/tsdw /tsdw status /tsdw set 物品ID,名称 /tsdw scan /tsdw list")
    else DW.Toggle() end
end
SLASH_TAIYANGDRINKWALKDIAG1 = "/tsdwdiag"
SlashCmdList["TAIYANGDRINKWALKDIAG"] = function() DW.ShowStatus() end

local events = CreateFrame("Frame", "TaiYangDrinkWalkEvents")
events:RegisterEvent("VARIABLES_LOADED")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("UNIT_MANA")
events:RegisterEvent("UNIT_ENERGY")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function()
    if event == "VARIABLES_LOADED" then
        if type(TaiYangDrinkWalkDB) ~= "table" then TaiYangDrinkWalkDB = {} end
        if TaiYangDrinkWalkDB.mode ~= "WATCH" and TaiYangDrinkWalkDB.mode ~= "SPAM" then
            TaiYangDrinkWalkDB.mode = "WATCH"
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
        DW.lastMana=nil;DW.lastEnergy=nil
        if DW.ManaTick then DW.ManaTick.Reset() end
        if DW.EnergyTick then DW.EnergyTick.Reset() end
    elseif event == "UNIT_MANA" and arg1 == "player" then
        if type(UnitMana) == "function" then
            local now=GetTime()*1000
            local value=UnitMana("player") or 0
            local maximum=UnitManaMax("player") or 0
            if DW.lastMana~=nil and DW.ManaTick then DW.ManaTick.Observe(DW.lastMana,value,maximum,now) end
            DW.lastMana=value
        end
    elseif event == "UNIT_ENERGY" and arg1 == "player" then
        if type(UnitMana) == "function" then
            local now=GetTime()*1000
            local value=UnitMana("player") or 0
            local maximum=UnitManaMax("player") or 0
            if DW.lastEnergy~=nil and DW.EnergyTick then DW.EnergyTick.Observe(DW.lastEnergy,value,maximum,now) end
            DW.lastEnergy=value
        end
    elseif event == "PLAYER_LOGIN" then
        DW.ready = true
        if DW.HUD then
            local ok,err=pcall(DW.HUD.Create)
            if not ok then DW.Print("HUD创建失败: "..tostring(err)) end
        end
        if DW.BuildSettings then
            local ok, err = pcall(DW.BuildSettings)
            if not ok then DW.uiError = tostring(err); DW.Print("设置界面创建失败: " .. DW.uiError) end
        end
    end
end)

function DW.TickReport()
    if DW.ManaTick then DW.Print(DW.ManaTick.Summary()) end
    if DW.EnergyTick then DW.Print(DW.EnergyTick.Summary()) end
end
