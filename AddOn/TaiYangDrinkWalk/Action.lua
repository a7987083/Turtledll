-- TaiYangDrinkWalk 0.5.1. Explicit /zchihe macro only; native DLL item use.
-- Lua 5.0 / WoW 1.12.1. No autonomous scheduler; no key binding.
local DW=TaiYangDrinkWalk
local A={implemented=true,state="IDLE",lastCode="NOT_CALLED",attempts=0,confirmed=0,
         lastTry=-100000,tickKey=nil,tickAttempts=0,rejected=0}
DW.Action=A
local function Now() return GetTime()*1000 end
local function Native(cmd,bag,slot)
    if type(TaiYangShenDian)~="function" then return false,"DLL_MISSING" end
    local ok,a,b,c
    if bag~=nil then ok,a,b,c=pcall(TaiYangShenDian,cmd,bag,slot)
    else
        ok,a=pcall(TaiYangShenDian,cmd)
        if not ok then return false,"DLL_EXCEPTION:"..tostring(a) end
        return true,a
    end
    if not ok then return false,"DLL_EXCEPTION:"..tostring(a) end
    return a,b,c
end
local function Block(reason)
    A.lastCode=tostring(reason)
    A.state="BLOCKED"
    return false
end
local function ConsumableAuraKinds()
    local found={FOOD=false,DRINK=false}
    if type(GetPlayerBuff)~="function" or type(getglobal)~="function" then return found end
    if not A.tooltip then
        A.tooltip=CreateFrame("GameTooltip","TaiYangDrinkWalkAuraTooltip",UIParent,"GameTooltipTemplate")
        A.tooltip:SetOwner(UIParent,"ANCHOR_NONE")
    end
    if not A.tooltip or type(A.tooltip.SetPlayerBuff)~="function" then return found end
    for i=0,31 do
        local index=GetPlayerBuff(i,"HELPFUL")
        if not index or index<0 then break end
        A.tooltip:ClearLines()
        A.tooltip:SetPlayerBuff(index)
        local text=""
        for line=1,8 do
            local l=getglobal("TaiYangDrinkWalkAuraTooltipTextLeft"..line)
            local r=getglobal("TaiYangDrinkWalkAuraTooltipTextRight"..line)
            if l and l:GetText() then text=text.." "..string.lower(l:GetText()) end
            if r and r:GetText() then text=text.." "..string.lower(r:GetText()) end
        end
        if string.find(text,"drink") or string.find(text,"饮用") or string.find(text,"喝水") then found.DRINK=true end
        if string.find(text,"food") or string.find(text,"eating") or string.find(text,"进食") or string.find(text,"食用") then found.FOOD=true end
    end
    return found
end
local function RelevantAura(kind,aura)
    if kind=="FOOD" then return aura.FOOD end
    if kind=="DRINK" then return aura.DRINK end
    if kind=="BOTH" then return aura.FOOD or aura.DRINK end
    return false
end
local function CountIdentity(id)
    local total=0
    if type(GetContainerNumSlots)~="function" then return 0 end
    for bag=0,4 do
        for slot=1,(GetContainerNumSlots(bag) or 0) do
            local link=GetContainerItemLink(bag,slot)
            local found=link and tonumber(string.match(link,"item:(%d+)"))
            if found==id then
                local _,n=GetContainerItemInfo(bag,slot)
                total=total+(tonumber(n) or 1)
            end
        end
    end
    return total
end
local function Confirm()
    local p=A.pending
    if not p then return end
    local n=Now()
    local count=CountIdentity(p.id)
    if count<p.beforeTotal then p.bagEvidence=true end
    local aura=ConsumableAuraKinds()
    if RelevantAura(p.kind,aura) and not RelevantAura(p.kind,p.beforeAura) then
        p.auraEvidence=true
    end
    if p.bagEvidence or p.auraEvidence then
        A.state="CONFIRMED"
        A.lastCode=(p.bagEvidence and "BAG" or "")..
                   (p.bagEvidence and p.auraEvidence and "+" or "")..
                   (p.auraEvidence and "AURA" or "")
        A.lastEvidence=A.lastCode
        A.confirmed=A.confirmed+1
        A.completedTick=p.tickKey
        A.pending=nil
    elseif n>=p.deadline then
        A.state="UNCONFIRMED"
        A.lastCode=p.error and ("GAME_REJECTED:"..p.error) or "CONFIRM_TIMEOUT_900MS"
        A.pending=nil
    end
end
function A.ObserveBag() if A.pending then Confirm() end end
function A.ObserveAura() if A.pending then Confirm() end end
function A.ObserveError(message)
    local p=A.pending
    if p and Now()-p.sentAt<=400 then
        p.error=tostring(message or "UI_ERROR")
        A.rejected=A.rejected+1
        A.lastCode="GAME_REJECTED:"..p.error
        A.state="REJECTED_PENDING_EVIDENCE"
        -- Keep the evidence window until 900ms; do not learn from this as a tick miss.
    end
end
function A.Update() if A.pending then Confirm() end end
function A.Check()
    local ok,status=Native("DrinkWalk.Status")
    if type(status)~="table" then return false,tostring(status or "DLL_STATUS_FAILED") end
    if status.available~=true then return false,"DLL_UNAVAILABLE:"..tostring(status.lastCode) end
    return true,tostring(status.mode or "READY")
end
local function Window()
    if not DW.HUD then return false,"HUD_MISSING" end
    local power=type(UnitPowerType)=="function" and UnitPowerType("player") or -1
    local source
    if power==0 then source=DW.ManaTick
    elseif power==3 then source=DW.EnergyTick
    else return false,"NO_TICK_RESOURCE" end
    if not source or source.state~="LOCKED" or not source.Next then return false,"TICK_NOT_LOCKED" end
    local now=Now()
    local nextTick=source.Next(now)
    if not nextTick then return false,"NO_NEXT_TICK" end
    local inside=DW.HUD.Window(nextTick,now,true)
    if not inside then return false,"OUTSIDE_WINDOW" end
    -- Quantize expected tick to a stable key; do not use raw drifting anchor as key.
    local key=math.floor(nextTick/100+0.5)
    return true,key
end
function A.Activate()
    Confirm()
    if not DW.ready then return Block("NOT_READY") end
    local ready,reason=A.Check()
    if not ready then return Block(reason) end
    local inside,key=Window()
    if not inside then return Block(key) end
    if A.tickKey~=key then A.tickKey=key;A.tickAttempts=0 end
    if A.completedTick==key then return Block("TICK_ALREADY_CONFIRMED") end
    if A.tickAttempts>=2 then return Block("TICK_MAX_2") end
    local now=Now()
    if now-A.lastTry<75 then return Block("RATE_LIMIT_75MS") end
    -- Hold briefly after dispatch for server response; after timeout allow one retry.
    if A.pending then
        A.lastCode="WAIT_CONFIRM_900MS"
        return false
    end
    if not DW.Items or not DW.Items.Find then return Block("ITEM_MODULE_MISSING") end
    local item=DW.Items.Find()
    if not item or not item.id then return Block("NO_CONFIGURED_ITEM") end
    if item.kind~="DRINK" and item.kind~="BOTH" then return Block("NOT_DRINK") end
    if type(UnitPowerType)~="function" or UnitPowerType("player")~=0 then return Block("NOT_MANA_RESOURCE") end
    if (UnitManaMax("player") or 0)<=0 or (UnitMana("player") or 0)>=(UnitManaMax("player") or 0) then return Block("MANA_FULL") end
    local link=GetContainerItemLink(item.bag,item.slot)
    local id=link and tonumber(string.match(link,"item:(%d+)"))
    local _,count,locked=GetContainerItemInfo(item.bag,item.slot)
    if id~=item.id or locked or (tonumber(count) or 0)<1 then return Block("SLOT_INVALID") end
    local beforeTotal=CountIdentity(item.id)
    local beforeAura=ConsumableAuraKinds()
    if RelevantAura(item.kind,beforeAura) then return Block("CONSUMABLE_AURA_ALREADY_ACTIVE") end
    local resolved,resolveCode=Native("DrinkWalk.ResolveItem",item.bag,item.slot)
    if resolved~=true then return Block("RESOLVE:"..tostring(resolveCode)) end
    local check=GetContainerItemLink(item.bag,item.slot)
    local second=check and tonumber(string.match(check,"item:(%d+)"))
    local _,checkCount,checkLocked=GetContainerItemInfo(item.bag,item.slot)
    if second~=id or tonumber(checkCount)~=tonumber(count) or checkLocked then return Block("SLOT_CHANGED") end
    A.lastTry=now
    A.tickAttempts=A.tickAttempts+1
    local sent,code,ret=Native("DrinkWalk.UseItem",item.bag,item.slot)
    A.lastCode=tostring(code)
    A.engineReturn=ret
    if sent~=true then A.state="NATIVE_FAILED";return false end
    A.attempts=A.attempts+1
    A.state="DISPATCHED_UNCONFIRMED"
    -- Short confirmation timeout ensures the second attempt can fit the window,
    -- but do not retry while an unconfirmed dispatch is still pending.
    A.pending={id=item.id,kind=item.kind,beforeTotal=beforeTotal,beforeAura=beforeAura,
               sentAt=now,deadline=now+900,tickKey=key,bagEvidence=false,auraEvidence=false,error=nil}
    return true
end
function A.Summary()
    Confirm()
    return "Macro/DLL: "..A.state.." / "..A.lastCode..
           " | sent="..A.attempts.." confirmed="..A.confirmed..
           " | tickTry="..A.tickAttempts.."/2 | rejected="..A.rejected
end
function DW.ActionKey()
    local ok,err=pcall(A.Activate)
    if not ok then A.state="ERROR";A.lastCode=tostring(err);DW.Print("宏执行异常: "..A.lastCode) end
end
SLASH_TAIYANGZCHIHE1="/zchihe"
SlashCmdList["TAIYANGZCHIHE"]=function() DW.ActionKey() end
DW.RegisterModule("Action")
