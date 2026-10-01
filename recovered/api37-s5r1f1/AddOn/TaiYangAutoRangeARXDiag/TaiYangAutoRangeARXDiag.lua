-- TaiYangAutoRangeARXDiag 1.0.1 / WoW 1.12 Lua 5.0
-- API33 acceptance panel.
-- Unit.Guid / UnitState remain event/manual only.
-- Only Unit.InSight is re-queried every 50 ms while this panel is visible.

local F=CreateFrame("Frame","TaiYangAutoRangeARXDiagFrame",UIParent)
F:SetWidth(520); F:SetHeight(310)
F:SetPoint("CENTER",UIParent,"CENTER",0,40)
F:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",tile=true,tileSize=16,edgeSize=16,insets={left=4,right=4,top=4,bottom=4}})
F:SetBackdropColor(0.03,0.05,0.08,0.96)
F:SetMovable(true); F:EnableMouse(true); F:RegisterForDrag("LeftButton")
F:SetScript("OnDragStart",function() this:StartMoving() end)
F:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)

local title=F:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
title:SetPoint("TOPLEFT",F,"TOPLEFT",18,-16)
title:SetText("AutoRange ARX1 · API33 动态 LOS 验收")

local sub=F:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
sub:SetPoint("TOPLEFT",title,"BOTTOMLEFT",0,-5)
sub:SetText("GUID / UnitState 按事件刷新；仅 LOS 每 50ms 实时检测")

local close=CreateFrame("Button",nil,F,"UIPanelCloseButton")
close:SetPoint("TOPRIGHT",F,"TOPRIGHT",-4,-4)

local text=F:CreateFontString(nil,"OVERLAY","GameFontHighlight")
text:SetPoint("TOPLEFT",F,"TOPLEFT",18,-68)
text:SetWidth(484); text:SetHeight(190)
text:SetJustifyH("LEFT"); text:SetJustifyV("TOP")
text:SetText("等待检测...")

local refresh=CreateFrame("Button",nil,F,"UIPanelButtonTemplate")
refresh:SetWidth(110); refresh:SetHeight(24); refresh:SetPoint("BOTTOMLEFT",F,"BOTTOMLEFT",18,16)
refresh:SetText("重新检测")

local staticLines={}
local currentLosLine="[3] 动态 LOS：等待检测"
local targetReady=false
local losElapsed=0
local lastLosKey=nil

local function sval(v)
  if v==nil then return "nil" end
  if v==true then return "true" end
  if v==false then return "false" end
  return tostring(v)
end

local function pos(s)
  if type(s)~="table" or s.x==nil then return "-" end
  return string.format("%.2f, %.2f, %.2f",s.x or 0,s.y or 0,s.z or 0)
end

local function call(cmd,a,b)
  if type(TaiYangShenDian)~="function" then return false,nil,"DLL_API_MISSING" end
  if b~=nil then return pcall(TaiYangShenDian,cmd,a,b) end
  if a~=nil then return pcall(TaiYangShenDian,cmd,a) end
  return pcall(TaiYangShenDian,cmd)
end

local function Render()
  local lines={}
  local i
  for i=1,table.getn(staticLines) do table.insert(lines,staticLines[i]) end
  table.insert(lines,currentLosLine)
  text:SetText(table.concat(lines,"\n"))
end

local function RefreshLOS(force)
  if not targetReady then
    local line="[3] 动态 LOS：请选中一个玩家/NPC目标"
    if force or line~=currentLosLine then
      currentLosLine=line
      lastLosKey=nil
      Render()
    end
    return
  end

  local okLos,los,losCode=call("Unit.InSight","player","target")
  local key=sval(okLos).."|"..sval(los).."|"..sval(losCode)
  if (not force) and key==lastLosKey then return end
  lastLosKey=key

  if okLos then
    local mark=(los==true and "|cff55ff55可见|r") or (los==false and "|cffffaa33被阻挡|r") or "|cffff5555未知|r"
    currentLosLine="[3] 动态 LOS player→target："..mark.."    "..sval(losCode).."    |cff88888850ms|r"
  else
    currentLosLine="|cffff5555[3] 动态 LOS 调用异常|r    "..sval(losCode)
  end
  Render()
end

local function RefreshStatic()
  staticLines={}
  targetReady=false
  lastLosKey=nil

  if type(TaiYangShenDian)~="function" then
    staticLines={"|cffff5555DLL API：未找到 TaiYangShenDian|r"}
    currentLosLine="[3] 动态 LOS：不可用"
    Render()
    return
  end

  local ok,ver,api,build=call("Core.Version")
  if ok then
    table.insert(staticLines,"DLL："..sval(ver).."    API："..sval(api))
    table.insert(staticLines,"Build："..sval(build))
  else
    table.insert(staticLines,"|cffff5555Core.Version 调用失败|r")
  end

  local okPg,pg,pgCode=call("Unit.Guid","player")
  table.insert(staticLines,"")
  table.insert(staticLines,"[1] Unit.Guid(player)："..(okPg and sval(pg) or "调用失败").."    "..sval(pgCode))

  local okTg,tg,tgCode=call("Unit.Guid","target")
  table.insert(staticLines,"目标 GUID："..(okTg and sval(tg) or "无目标").."    "..sval(tgCode))

  if okPg and type(pg)=="string" then
    local okPs,ps=call("GroundProbe.UnitStateByGuid",pg)
    if okPs and type(ps)=="table" then
      table.insert(staticLines,"[2] 玩家 State：visible="..sval(ps.visible).." dead="..sval(ps.dead).." HP="..sval(ps.health).."/"..sval(ps.maxHealth).." code="..sval(ps.code))
      table.insert(staticLines,"    玩家坐标："..pos(ps))
    else
      table.insert(staticLines,"|cffff5555[2] 玩家 UnitState 调用失败|r")
    end
  end

  if okTg and type(tg)=="string" then
    local okTs,ts=call("GroundProbe.UnitStateByGuid",tg)
    if okTs and type(ts)=="table" then
      table.insert(staticLines,"目标 State：visible="..sval(ts.visible).." dead="..sval(ts.dead).." HP="..sval(ts.health).."/"..sval(ts.maxHealth).." code="..sval(ts.code))
      table.insert(staticLines,"    目标坐标："..pos(ts))
    end
    targetReady=true
  else
    table.insert(staticLines,"目标 State：无有效目标")
  end

  RefreshLOS(true)
end

refresh:SetScript("OnClick",RefreshStatic)
F:RegisterEvent("PLAYER_ENTERING_WORLD")
F:RegisterEvent("PLAYER_TARGET_CHANGED")
F:SetScript("OnEvent",function() RefreshStatic() end)

-- Diagnostic-only 20 Hz LOS probe. This does NOT refresh GUID/UnitState and does
-- not belong to the production AutoRange runtime path.
F:SetScript("OnUpdate",function()
  if not F:IsShown() then return end
  losElapsed=losElapsed+(arg1 or 0)
  if losElapsed<0.05 then return end
  losElapsed=0
  RefreshLOS(false)
end)

SLASH_TYSARXDIAG1="/arxdiag"
SlashCmdList["TYSARXDIAG"]=function()
  if F:IsShown() then
    F:Hide()
  else
    F:Show()
    RefreshStatic()
  end
end

F:Show()
