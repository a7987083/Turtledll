-- WoW 1.12.1 / Lua 5.0. Bootstrap and public API.
TaiYangDrinkWalk = TaiYangDrinkWalk or {}
local DW = TaiYangDrinkWalk
DW.version = "0.1.0-framework"
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
    DW.Print("模块: Items/ManaTick/EnergyTick/Action/HUD/Settings (仅占位)")
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
        if DW.RefreshSettings then DW.RefreshSettings() end
        DW.settingsFrame:Show()
    end
end
SLASH_TAIYANGDRINKWALK1 = "/tsdw"
SLASH_TAIYANGDRINKWALK2 = "/drinkwalk"
SlashCmdList["TAIYANGDRINKWALK"] = function(msg)
    local cmd = string.lower(msg or "")
    if cmd == "status" then DW.ShowStatus()
    elseif cmd == "help" then DW.Print("/tsdw 打开设置; /tsdw status 查看状态; /tsdw help 帮助")
    else DW.Toggle() end
end
SLASH_TAIYANGDRINKWALKDIAG1 = "/tsdwdiag"
SlashCmdList["TAIYANGDRINKWALKDIAG"] = function() DW.ShowStatus() end

local events = CreateFrame("Frame", "TaiYangDrinkWalkEvents")
events:RegisterEvent("VARIABLES_LOADED")
events:RegisterEvent("PLAYER_LOGIN")
events:SetScript("OnEvent", function()
    if event == "VARIABLES_LOADED" then
        if type(TaiYangDrinkWalkDB) ~= "table" then TaiYangDrinkWalkDB = {} end
        if TaiYangDrinkWalkDB.mode ~= "WATCH" and TaiYangDrinkWalkDB.mode ~= "SPAM" then
            TaiYangDrinkWalkDB.mode = "WATCH"
        end
    elseif event == "PLAYER_LOGIN" then
        DW.ready = true
        if DW.BuildSettings then
            local ok, err = pcall(DW.BuildSettings)
            if not ok then DW.uiError = tostring(err); DW.Print("设置界面创建失败: " .. DW.uiError) end
        end
    end
end)
