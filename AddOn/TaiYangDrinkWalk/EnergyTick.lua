-- Energy tick observer. WoW 1.12.1 / Lua 5.0. Observation only.
local DW=TaiYangDrinkWalk
local E={implemented=true,period=2000,samples={},state="UNSYNCED",good=0}
DW.EnergyTick=E
local function Median(samples)
    local arr={}
    for i=1,table.getn(samples) do arr[i]=samples[i] end
    table.sort(arr)
    local n=table.getn(arr)
    if n==0 then return nil end
    if math.mod(n,2)==0 then return (arr[n/2]+arr[n/2+1])/2 end
    return arr[math.floor(n/2)+1]
end
function E.Observe(old,new,max,now)
    local delta=new-old
    if delta<14 or delta>22 or old>=max then return end
    if not E.last then E.last=now;E.anchor=now;E.state="PROVISIONAL";E.good=1;return end
    local gap=now-E.last
    if gap<350 or gap>E.period*4.5 then return end
    local n=math.floor(gap/E.period+0.5)
    if n<1 then n=1 end
    if n>4 then n=4 end
    local normalized=gap/n
    local tolerance=math.min(360,math.max(140,E.period*0.28))
    if math.abs(normalized-E.period)<=tolerance then
        table.insert(E.samples,normalized)
        if table.getn(E.samples)>8 then table.remove(E.samples,1) end
        if table.getn(E.samples)>=2 then
            E.period=math.min(2200,math.max(500,Median(E.samples)))
            E.state="LOCKED"
        end
        E.last=now;E.anchor=now;E.good=E.good+1
    end
end
function E.Next(now)
    if not E.anchor then return nil end
    now=now or GetTime()*1000
    local n=math.floor((now-E.anchor)/E.period)+1
    if n<0 then n=0 end
    return E.anchor+n*E.period
end
function E.Summary()
    if not E.anchor then return "Energy: UNSYNCED" end
    return string.format("Energy: %s / %.2fs / %d samples",E.state,E.period/1000,E.good)
end
function E.Reset()
    E.period=2000;E.samples={};E.state="UNSYNCED";E.good=0;E.last=nil;E.anchor=nil
end
DW.RegisterModule("EnergyTick")
