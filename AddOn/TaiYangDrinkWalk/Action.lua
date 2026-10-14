-- TaiYangDrinkWalk 0.5.0: explicit keypress -> native DLL item use.
-- WoW 1.12.1 Lua 5.0. No scheduler, no Lua UseContainerItem.
local DW=TaiYangDrinkWalk
local A={implemented=true,lastCode="NOT_CALLED",state="IDLE",lastTry=-100000,attempts=0,confirmed=0}
DW.Action=A
local function Now() return GetTime()*1000 end
local function Native(cmd,bag,slot)
    if type(TaiYangShenDian)~="function" then return false,"DLL_MISSING" end
    if bag~=nil then
        local ok,a,b,c,d=pcall(TaiYangShenDian,cmd,bag,slot)
        if not ok then return false,"CALL_ERROR:"..tostring(a) end
        return a,b,c,d
    end
    local ok,a=pcall(TaiYangShenDian,cmd)
    if not ok then return false,"CALL_ERROR:"..tostring(a) end
    return true,a
end
local function Stop(code)
    A.lastCode=tostring(code)
    A.state="BLOCKED"
    return false
end
local function CheckConfirm()
    local p=A.pending
    if not p then return end
    local now=Now()
    if now>p.untilTime then
        A.state="UNCONFIRMED";A.lastCode="CONFIRM_TIMEOUT"
        A.pending=nil;return
    end
    local link=GetContainerItemLink(p.bag,p.slot)
    local id=link and string.match(link,"item:(%d+)") or nil
    local _,count=GetContainerItemInfo(p.bag,p.slot)
    count=tonumber(count) or 0
    if tostring(id or "")~=tostring(p.id or "") or count<p.count then
        A.state="CONFIRMED";A.lastCode="STACK_DECREASED";A.confirmed=A.confirmed+1
        A.pending=nil
    end
end
function A.Check()
    CheckConfirm()
    local ok,status=Native("DrinkWalk.Status")
    if not ok or type(status)~="table" then
        return false,"DLL_STATUS_FAILED"
    end
    if status.available~=true then return false,"DLL_UNAVAILABLE:"..tostring(status.lastCode) end
    return true,status.mode or "READY"
end
local function CanWindow()
    if not DW.HUD or not DW.ManaTick or not DW.EnergyTick then return false,"TICK_MISSING" end
    local power=type(UnitPowerType)=="function" and UnitPowerType("player") or -1
    local source
    if power==0 then source=DW.ManaTick
    elseif power==3 then source=DW.EnergyTick
    else return false,"NO_MANA_OR_ENERGY_TICK" end
    if source.state~="LOCKED" or not source.Next then return false,"TICK_NOT_LOCKED" end
    local now=Now()
    local nextTick=source.Next(now)
    if not nextTick then return false,"NO_TICK_FORECAST" end
    local inside=DW.HUD.Window(nextTick,now,true)
    if not inside then return false,"OUTSIDE_TICK_WINDOW" end
    return true,"WINDOW_OK"
end
function A.Activate()
    CheckConfirm()
    if not DW.ready then return Stop("NOT_READY") end
    if type(TaiYangDrinkWalkDB)~="table" or TaiYangDrinkWalkDB.mode~="SPAM" then
        return Stop("WATCH_MODE_NO_USE")
    end
    if A.pending then return Stop("AWAITING_CONFIRMATION") end
    local now=Now()
    if now-A.lastTry<75 then return Stop("RATE_LIMIT_75MS") end
    -- One native dispatch per expected tick, regardless of repeated key presses.
    if A.nextAllowed and now<A.nextAllowed then return Stop("TICK_RETRY_BLOCKED") end
    local ready,reason=A.Check()
    if not ready then return Stop(reason) end
    local window,why=CanWindow()
    if not window then return Stop(why) end
    if not DW.Items or not DW.Items.Find then return Stop("ITEMS_MISSING") end
    local item=DW.Items.Find()
    if not item or not item.id or not item.bag or not item.slot then return Stop("NO_CONFIGURED_ITEM") end
    if item.kind=="UNKNOWN" then return Stop("UNKNOWN_ITEM_KIND") end
    local beforeLink=GetContainerItemLink(item.bag,item.slot)
    local beforeId=beforeLink and tonumber(string.match(beforeLink,"item:(%d+)"))
    local _,beforeCount,beforeLocked=GetContainerItemInfo(item.bag,item.slot)
    if beforeId~=item.id or beforeLocked then return Stop("SLOT_CHANGED_OR_LOCKED") end
    beforeCount=tonumber(beforeCount) or 0
    if beforeCount<1 then return Stop("EMPTY_STACK") end
    local resolved,code=Native("DrinkWalk.ResolveItem",item.bag,item.slot)
    if resolved~=true then return Stop("RESOLVE:"..tostring(code)) end
    -- Verify a second time after native resolution.
    local afterLink=GetContainerItemLink(item.bag,item.slot)
    local afterId=afterLink and tonumber(string.match(afterLink,"item:(%d+)"))
    local _,afterCount,afterLocked=GetContainerItemInfo(item.bag,item.slot)
    if afterId~=beforeId or tonumber(afterCount)~=beforeCount or afterLocked then
        return Stop("SLOT_CHANGED_AFTER_RESOLVE")
    end
    A.lastTry=now
    local used,code,engineRet=Native("DrinkWalk.UseItem",item.bag,item.slot)
    A.lastCode=tostring(code);A.engineReturn=engineRet
    if used~=true then A.state="FAILED";return false end
    A.state="DISPATCHED_UNCONFIRMED";A.attempts=A.attempts+1
    A.nextAllowed=now+math.max(700,(type(DW.ManaTick)=="table" and DW.ManaTick.period or 2000))
    A.pending={bag=item.bag,slot=item.slot,id=item.id,count=beforeCount,untilTime=now+900}
    return true
end
function A.Summary()
    CheckConfirm()
    return "Native: "..A.state.." / "..A.lastCode.." / sent="..A.attempts.." confirmed="..A.confirmed
end
function A.ObserveBag()
    CheckConfirm()
end
function DW.ActionKey()
    local ok,err=pcall(A.Activate)
    if not ok then
        A.state="ERROR";A.lastCode=tostring(err)
        DW.Print("动作异常: "..A.lastCode)
    end
end
TaiYangDrinkWalk_ActionKey=function() DW.ActionKey() end
DW.RegisterModule("Action")
