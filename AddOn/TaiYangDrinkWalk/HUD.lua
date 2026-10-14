-- TaiYangDrinkWalk 0.4.0 HUD: observation and window preview ONLY.
-- WoW 1.12.1 / Lua 5.0. Never invokes item use.
local DW=TaiYangDrinkWalk
local H={implemented=true}
DW.HUD=H
local function DB()
    if type(TaiYangDrinkWalkDB)~="table" then TaiYangDrinkWalkDB={} end
    local d=TaiYangDrinkWalkDB
    if type(d.hud)~="table" then d.hud={} end
    if d.hud.enabled==nil then d.hud.enabled=true end
    if type(d.hud.lead)~="number" or d.hud.lead<170 or d.hud.lead>700 then d.hud.lead=320 end
    if type(d.hud.close)~="number" or d.hud.close<0 or d.hud.close>=d.hud.lead then d.hud.close=170 end
    return d.hud
end
local function Label(parent,text,x,y)
    local t=parent:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    t:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
    t:SetText(text)
    return t
end
function H.Window(nextTick,now,locked)
    if not nextTick or not locked then return false,nil end
    local remaining=nextTick-now
    local d=DB()
    return remaining>=d.close and remaining<=d.lead,remaining
end
local function Selected()
    local power=type(UnitPowerType)=="function" and UnitPowerType("player") or 0
    if power==0 then return DW.ManaTick,"Mana",0.25,0.50,1
    elseif power==3 then return DW.EnergyTick,"Energy",1,0.75,0.15
    elseif power==1 then return nil,"Rage",0.95,0.25,0.2 end
    return nil,"Other",0.6,0.6,0.6
end
function H.Update()
    if not H.frame then return end
    local d=DB()
    if not d.enabled then H.frame:Hide();return end
    H.frame:Show()
    local source,resource,r,g,b=Selected()
    H.bar:SetStatusBarColor(r,g,b)
    local now=GetTime()*1000
    if not source or not source.anchor or source.state=="UNSYNCED" then
        H.bar:SetMinMaxValues(0,1);H.bar:SetValue(0)
        H.label:SetText(resource.." | 未同步")
        H.detail:SetText("窗口: 不可判定 · WATCH/预览")
        return
    end
    local period=source.period or 2000
    if period<=0 then return end
    local nextTick=source.Next(now)
    if not nextTick then return end
    local remaining=nextTick-now
    local elapsed=period-remaining
    if elapsed<0 then elapsed=0 end
    if elapsed>period then elapsed=period end
    H.bar:SetMinMaxValues(0,period);H.bar:SetValue(elapsed)
    H.label:SetText(string.format("%s | %s | 下次 %.2fs",resource,source.state,remaining/1000))
    local inside=H.Window(nextTick,now,source.state=="LOCKED")
    local mode=(type(TaiYangDrinkWalkDB)=="table" and TaiYangDrinkWalkDB.mode) or "WATCH"
    H.detail:SetText("窗口: "..(inside and "|cff55ff55是|r" or "否").." | "..mode.." (仅预览)")
end
function H.Create()
    if H.frame or not UIParent then return end
    local d=DB()
    local f=CreateFrame("Frame","TaiYangDrinkWalkHUD",UIParent)
    f:SetWidth(275);f:SetHeight(78)
    f:SetFrameStrata("MEDIUM")
    f:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",tile=true,tileSize=16,edgeSize=10,insets={left=3,right=3,top=3,bottom=3}})
    f:SetBackdropColor(0,0,0,0.82)
    if type(d.point)=="table" and type(d.point.x)=="number" and type(d.point.y)=="number" then
        f:SetPoint("CENTER",UIParent,"CENTER",d.point.x,d.point.y)
    else f:SetPoint("CENTER",UIParent,"CENTER",0,-175) end
    f:SetMovable(true);f:EnableMouse(true);f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart",function() f:StartMoving() end)
    f:SetScript("OnDragStop",function()
        f:StopMovingOrSizing()
        local x,y=f:GetCenter()
        local px,py=UIParent:GetCenter()
        if x and y and px and py then
            d.point={x=x-px,y=y-py}
        end
    end)
    H.label=Label(f,"Mana | UNSYNCED",12,-12)
    local bar=CreateFrame("StatusBar",nil,f)
    bar:SetPoint("TOPLEFT",f,"TOPLEFT",12,-31)
    bar:SetWidth(249);bar:SetHeight(13)
    bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    bar:SetMinMaxValues(0,1);bar:SetValue(0)
    local bg=bar:CreateTexture(nil,"BACKGROUND")
    bg:SetAllPoints(bar);bg:SetTexture(0.15,0.15,0.15,0.85)
    -- The end of the bar (right edge) is the forecast Tick zero marker.
    local marker=bar:CreateTexture(nil,"OVERLAY")
    marker:SetTexture(1,0.18,0.18,1)
    marker:SetWidth(2);marker:SetPoint("TOPRIGHT",bar,"TOPRIGHT",0,2)
    marker:SetPoint("BOTTOMRIGHT",bar,"BOTTOMRIGHT",0,-2)
    H.bar=bar
    H.detail=Label(f,"窗口: 未同步",12,-53)
    local elapsed=0
    f:SetScript("OnUpdate",function()
        elapsed=elapsed+arg1
        if elapsed>=0.08 then elapsed=0;H.Update() end
    end)
    H.frame=f
    H.Update()
end
function H.Toggle()
    local d=DB();d.enabled=not d.enabled
    if d.enabled then H.Create() end
    H.Update()
    DW.Print("Tick HUD: "..(d.enabled and "开启" or "关闭"))
end
function H.Report()
    local source,name=Selected()
    if not source or not source.anchor then DW.Print(name..": 无可用 Tick 锚点");return end
    local now=GetTime()*1000
    local n=source.Next(now)
    local yes,remaining=H.Window(n,now,source.state=="LOCKED")
    DW.Print(string.format("%s %s | 距下次 %.0f ms | 预览窗口 %s | lead=%d close=%d",
        name,source.state,remaining or -1,yes and "YES" or "NO",DB().lead,DB().close))
end
DW.RegisterModule("HUD")
