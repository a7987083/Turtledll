-- TaiYangBossDiag M2: WoW 1.12 / Lua 5.0, Chinese one-shot graphical panel.
-- No OnUpdate scanner and no game memory access.
local frame = CreateFrame("Frame", "TaiYangBossDiagWindow", UIParent)
frame:SetWidth(430); frame:SetHeight(310)
frame:SetPoint("CENTER", UIParent, "CENTER", 0, 65)
frame:SetFrameStrata("DIALOG")
frame:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
frame:EnableMouse(true);frame:SetMovable(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart",function() this:StartMoving() end)
frame:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
frame:Hide()
local title=frame:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
title:SetPoint("TOP",frame,"TOP",0,-22);title:SetText("太阳神殿 · Boss 状态诊断")
local sub=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
sub:SetPoint("TOP",title,"BOTTOM",0,-8)
sub:SetText("M3 · 中文 API 查询器 · 0.5 秒 / 次 · 关闭后停查")
local lines={}
for i=1,9 do
    local font=frame:CreateFontString(nil,"OVERLAY","GameFontHighlight")
    font:SetPoint("TOPLEFT",frame,"TOPLEFT",27,-66-(i-1)*21)
    font:SetWidth(375);font:SetJustifyH("LEFT")
    lines[i]=font
end
local function showLine(i,s) lines[i]:SetText(s or "") end
local function boolText(known,value,yes,no)
    if not known then return "|cffffaa33未知|r" end
    if value then return "|cff55ff55"..yes.."|r" end
    return "|cffffff55"..no.."|r"
end
local function safeCall(api,...)
    if type(TaiYangShenDian)~="function" then return nil,"DLL 未加载" end
    local ok,result=pcall(TaiYangShenDian,api,unpack(arg or {}))
    -- Lua 5.0 implicit arg with vararg is not relied upon: replaced below.
    if not ok then return nil,tostring(result) end
    if type(result)~="table" then return nil,"接口没有返回表" end
    return result,nil
end
local queryCount=0
local queryErrors=0
local running=true
local currentUnit="target"
local stateNames={UNKNOWN="未知",DETECTED="已发现（存活）",ENGAGED="战斗中",DEAD="单位已死亡",KILLED="击杀已确认",RESET="已重置"}
local function refresh(unit)
    unit=unit or "target"; currentUnit=unit
    queryCount=queryCount+1
    for i=1,9 do showLine(i,"") end
    if type(TaiYangShenDian)~="function" then
        queryErrors=queryErrors+1;showLine(1,"|cffff5555DLL 接口不存在|r");return
    end
    local ok,id=pcall(TaiYangShenDian,"Unit.Identity",unit)
    if not ok or type(id)~="table" then
        queryErrors=queryErrors+1;showLine(1,"Unit.Identity 调用失败："..tostring(id));return
    end
    local d=id.data or {}
    showLine(1,"接口 Unit.Identity | "..tostring(id.code).." | 第 "..queryCount.." 次查询")
    if not id.ok then
        showLine(2,"当前单位不可用："..tostring(id.code))
        showLine(9,"累计查询："..queryCount.."  异常："..queryErrors)
        return
    end
    showLine(2,"单位："..tostring(d.name or "未知").."   Entry："..tostring(d.entry or "?"))
    showLine(3,"等级："..tostring(d.level or "?").."   分类："..tostring(d.classification or "?"))
    showLine(4,"GUID："..tostring(d.guid or "?"))
    if not d.guid then return end
    local ok2,g=pcall(TaiYangShenDian,"GroundProbe.UnitStateByGuid",d.guid,unit)
    if not ok2 or type(g)~="table" then
        queryErrors=queryErrors+1;showLine(5,"GroundProbe 查询失败："..tostring(g));return
    end
    showLine(5,"GroundProbe | "..tostring(g.code).." | "..tostring(g.lifeState or "UNKNOWN"))
    showLine(6,"生命："..tostring(g.health or "?").."/"..tostring(g.maxHealth or "?").."  死亡："..boolText(g.deadKnown,g.dead,"是","否"))
    showLine(7,"战斗："..boolText(g.inCombatKnown,g.inCombat,"战斗中","未战斗"))
    local ok3,boss=pcall(TaiYangShenDian,"Boss.State",unit)
    if not ok3 or type(boss)~="table" then
        queryErrors=queryErrors+1;showLine(8,"Boss.State 查询失败："..tostring(boss))
    else
        local flag=(boss.data and boss.data.bossCandidate) and "首领候选" or "普通目标 / 未识别"
        showLine(8,"Boss.State："..tostring(stateNames[boss.state] or boss.state).." | "..flag)
    end
    showLine(9,"查询："..queryCount.."  异常："..queryErrors.."  采样："..tostring(g.timestampMS or "?"))
end
local function makeButton(label,x,callback)
    local b=CreateFrame("Button",nil,frame,"UIPanelButtonTemplate")
    b:SetWidth(110);b:SetHeight(24)
    b:SetPoint("BOTTOM",frame,"BOTTOM",x,22)
    b:SetText(label);b:SetScript("OnClick",callback)
    return b
end
makeButton("立即查询",-120,function() refresh(currentUnit) end)
makeButton("暂停 / 恢复",0,function() running=not running; refresh(currentUnit) end)
makeButton("关闭",120,function() frame:Hide() end)
local clock=0
frame:SetScript("OnUpdate",function()
    if not running then return end
    clock=clock+arg1
    if clock>=0.5 then clock=0;refresh(currentUnit) end
end)
SLASH_TAIYANGBOSSDIAG1="/bossdiag"
SlashCmdList["TAIYANGBOSSDIAG"]=function(msg)
    frame:Show()
    running=true
    clock=0
    local unit=msg and string.gsub(msg,"^%s*(.-)%s*$","%1") or ""
    if unit=="" then unit="target" end
    refresh(unit)
end
