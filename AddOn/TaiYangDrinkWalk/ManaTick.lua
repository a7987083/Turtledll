-- Mana tick observer. WoW 1.12.1 / Lua 5.0. Observation only.
local DW=TaiYangDrinkWalk
local M={implemented=true,state="UNSYNCED",period=2000,good=0,bad=0,confidence=0}
DW.ManaTick=M
local function Clock() return GetTime()*1000 end
local function Reset()
    M.state="UNSYNCED";M.anchor=nil;M.lastAccepted=nil;M.good=0;M.bad=0;M.confidence=0
end
function M.Observe(old,new,maximum,now)
    local delta=new-old
    if delta<=0 or maximum<=0 or old>=maximum then return end
    if not M.anchor then
        M.anchor=now;M.lastAccepted=now;M.good=1;M.bad=0
        M.confidence=0.28;M.state="PROVISIONAL";return
    end
    if now-M.lastAccepted<700 then M.bad=M.bad+1;return end
    local gap=now-M.anchor
    local err=gap-math.floor(gap/M.period+0.5)*M.period
    local tol=M.state=="LOCKED" and 280 or 450
    if math.abs(err)<=tol then
        M.anchor=M.anchor+err*(M.state=="LOCKED" and 0.08 or 0.16)
        M.lastAccepted=now;M.good=M.good+1;M.bad=0
        M.confidence=math.min(1,M.confidence+0.16)
        if M.good>=3 then M.state="LOCKED" end
    else
        M.bad=M.bad+1
        if M.state~="LOCKED" and M.bad>=2 then
            M.anchor=now;M.lastAccepted=now;M.good=1;M.bad=0;M.confidence=0.28
        end
    end
end
function M.Next(now)
    if not M.anchor then return nil end
    now=now or Clock()
    local n=math.floor((now-M.anchor)/M.period)+1
    if n<0 then n=0 end
    return M.anchor+n*M.period
end
function M.Summary()
    if not M.anchor then return "Mana: UNSYNCED" end
    return string.format("Mana: %s / %d samples / %.0f%%",M.state,M.good,M.confidence*100)
end
function M.Reset() Reset() end
DW.RegisterModule("ManaTick")
