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
sub:SetText("M2 · 中文图形化诊断 · 手动刷新")
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
local function refresh(unit)
    unit=unit or "target"
    for i=1,9 do showLine(i,"") end
    if type(TaiYangShenDian)~="function" then showLine(1,"|cffff5555未找到 TaiYangShenDian DLL|r");return end
    local ok,id=pcall(TaiYangShenDian,"Unit.Identity",unit)
    if not ok or type(id)~="table" then showLine(1,"身份接口调用失败："..tostring(id));return end
    showLine(1,"身份状态："..tostring(id.code).."  /  结构版本："..tostring(id.schemaVersion))
    local d=id.data or {}
    if not id.ok then
        showLine(2,"当前目标：无有效单位（请选中怪物后刷新）")
        return
    end
    showLine(2,"单位名称："..tostring(d.name or "未知"))
    showLine(3,"NPC Entry："..tostring(d.entry or "未知").."    等级："..tostring(d.level or "未知"))
    showLine(4,"单位分类："..tostring(d.classification or "未知"))
    showLine(5,"GUID："..tostring(d.guid or "未知"))
    if not d.guid then return end
    local ok2,state=pcall(TaiYangShenDian,"GroundProbe.UnitStateByGuid",d.guid,unit)
    if not ok2 or type(state)~="table" then
        showLine(6,"状态接口调用失败："..tostring(state));return
    end
    showLine(6,"可见性："..boolText(true,state.visible,"可见","不可见").."    状态码："..tostring(state.code))
    showLine(7,"生命状态："..boolText(state.deadKnown,state.dead,"已死亡","存活").."   ("..tostring(state.lifeState or "UNKNOWN")..")")
    showLine(8,"战斗状态："..boolText(state.inCombatKnown,state.inCombat,"战斗中","未战斗").."    战斗依据：单位令牌核对")
    showLine(9,"生命值："..tostring(state.health or "?").." / "..tostring(state.maxHealth or "?").."    采样毫秒："..tostring(state.timestampMS or "?"))
end
local function makeButton(label,x,callback)
    local b=CreateFrame("Button",nil,frame,"UIPanelButtonTemplate")
    b:SetWidth(110);b:SetHeight(24)
    b:SetPoint("BOTTOM",frame,"BOTTOM",x,22)
    b:SetText(label);b:SetScript("OnClick",callback)
    return b
end
makeButton("刷新当前目标",-120,function() refresh("target") end)
makeButton("检测自身",0,function() refresh("player") end)
makeButton("关闭",120,function() frame:Hide() end)
SLASH_TAIYANGBOSSDIAG1="/bossdiag"
SlashCmdList["TAIYANGBOSSDIAG"]=function(msg)
    frame:Show()
    local unit=msg and string.gsub(msg,"^%s*(.-)%s*$","%1") or ""
    if unit=="" then unit="target" end
    refresh(unit)
end
