-- TaiYangDrinkWalk phase 2: configured items, tooltip classification, needs scoring.
-- WoW 1.12.1 / Lua 5.0; read-only, never calls UseItem.
local DW = TaiYangDrinkWalk
local I = {implemented=true}
DW.Items = I

local function DB()
    if type(TaiYangDrinkWalkDB) ~= "table" then TaiYangDrinkWalkDB={} end
    if type(TaiYangDrinkWalkDB.itemSpec) ~= "string" then TaiYangDrinkWalkDB.itemSpec="" end
    return TaiYangDrinkWalkDB
end
local function Trim(s)
    return string.gsub(string.gsub(s or "", "^%s+", ""), "%s+$", "")
end
local function Parse()
    local raw=DB().itemSpec
    raw=string.gsub(raw,"，",",")
    raw=string.gsub(raw,"；",",")
    raw=string.gsub(raw,";",",")
    local ids,names={},{}
    for token in string.gfind(raw,"[^,]+") do
        token=Trim(token)
        if token~="" then
            local id=tonumber(token)
            if id and id>0 and id==math.floor(id) then
                table.insert(ids,id)
            else
                table.insert(names,token)
            end
        end
    end
    return ids,names
end
function I.SetSpec(raw)
    if type(raw)~="string" or string.len(raw)>1000 then return false,"物品配置过长或无效" end
    DB().itemSpec=Trim(raw)
    I.current=nil
    return true,"配置已保存（按 ID/名称精确匹配，类型自动识别）"
end
function I.GetSpec() return DB().itemSpec end
local function ItemId(link)
    local _,_,s=string.find(link or "","item:(%d+)")
    return s and tonumber(s) or nil
end
local function ItemName(link)
    if type(GetItemInfo)=="function" then
        local name=GetItemInfo(link)
        if name then return name end
    end
    local _,_,name=string.find(link or "","%[(.-)%]")
    return name
end
local function TooltipText(bag,slot)
    if not I.tooltip then
        I.tooltip=CreateFrame("GameTooltip","TaiYangDrinkWalkScanTooltip",UIParent,"GameTooltipTemplate")
        I.tooltip:SetOwner(UIParent,"ANCHOR_NONE")
    end
    local tip=I.tooltip
    if not tip or type(tip.SetBagItem)~="function" then return "" end
    tip:ClearLines()
    tip:SetBagItem(bag,slot)
    local text=""
    for i=1,12 do
        local left=getglobal("TaiYangDrinkWalkScanTooltipTextLeft"..i)
        local right=getglobal("TaiYangDrinkWalkScanTooltipTextRight"..i)
        if left and left:GetText() then text=text.." "..string.lower(left:GetText()) end
        if right and right:GetText() then text=text.." "..string.lower(right:GetText()) end
    end
    return text
end
local function Classify(bag,slot)
    local t=TooltipText(bag,slot)
    if t=="" then return "UNKNOWN" end
    local mana=string.find(t,"mana") or string.find(t,"法力")
    local health=string.find(t,"health") or string.find(t,"生命")
    local drink=string.find(t,"drink") or string.find(t,"饮用") or string.find(t,"饮料") or string.find(t,"喝")
    local food=string.find(t,"eat") or string.find(t,"food") or string.find(t,"进食") or string.find(t,"食用") or string.find(t,"食物") or string.find(t,"吃")
    local restore=string.find(t,"restore") or string.find(t,"regener") or string.find(t,"恢复") or string.find(t,"回复")
    local d=mana and (drink or restore)
    local f=health and (food or restore)
    if d and f then return "BOTH" end
    if d then return "DRINK" end
    if f then return "FOOD" end
    return "UNKNOWN"
end
local function Score(item,hpNeed,manaNeed)
    if item.kind=="BOTH" then
        if hpNeed and manaNeed then return 120 end
        if hpNeed or manaNeed then return 100 end
        return 10
    elseif item.kind=="FOOD" then
        return hpNeed and 90 or 5
    elseif item.kind=="DRINK" then
        return manaNeed and 90 or 5
    end
    return (hpNeed or manaNeed) and 50 or 1
end
local function Needs()
    local hp=tonumber(UnitHealth("player")) or 0
    local maxhp=tonumber(UnitHealthMax("player")) or 0
    -- WoW 1.12.1 UnitPowerType: 0=mana, 1=rage, 3=energy.
    local isMana=(type(UnitPowerType)=="function" and UnitPowerType("player")==0)
    local mana=tonumber(UnitMana("player")) or 0
    local maxmana=tonumber(UnitManaMax("player")) or 0
    return maxhp>0 and hp<maxhp,isMana and maxmana>0 and mana<maxmana
end
function I.Find()
    I.current=nil
    I.status="未配置物品名/ID"
    local ids,names=Parse()
    if table.getn(ids)==0 and table.getn(names)==0 then return nil end
    if type(GetContainerNumSlots)~="function" or type(GetContainerItemLink)~="function" then
        I.status="背包接口不可用";return nil
    end
    local candidates={}
    local function FindOne(wantedId,wantedName)
        local first=nil
        local total=0
        for bag=0,4 do
            local slots=GetContainerNumSlots(bag) or 0
            for slot=1,slots do
                local link=GetContainerItemLink(bag,slot)
                if link then
                    local id=ItemId(link)
                    local name=ItemName(link)
                    local matched=(wantedId and wantedId==id) or
                        (wantedName and name and string.lower(wantedName)==string.lower(name))
                    if matched then
                        local texture,count,locked=GetContainerItemInfo(bag,slot)
                        count=tonumber(count) or 1
                        total=total+count
                        if not first then
                            first={bag=bag,slot=slot,id=id,name=name or ("item:"..tostring(id)),
                                count=count,locked=locked,match=wantedId and "ID" or "NAME"}
                            first.kind=Classify(bag,slot)
                        end
                    end
                end
            end
        end
        if first then first.totalCount=total;table.insert(candidates,first) end
    end
    for i=1,table.getn(ids) do FindOne(ids[i],nil) end
    for i=1,table.getn(names) do FindOne(nil,names[i]) end
    local hpNeed,manaNeed=Needs()
    local best,bestScore=nil,-1
    if not manaNeed then
        I.status="法力已满或当前不是 Mana 资源，不需要喝水"
        return nil
    end
    for i=1,table.getn(candidates) do
        local candidate=candidates[i]
        -- Drink-only edition: never select FOOD, UNKNOWN, or health-only candidates.
        if candidate.kind=="DRINK" or candidate.kind=="BOTH" then
            local score=candidate.kind=="DRINK" and 100 or 90
            if score>bestScore then best=candidate;bestScore=score end
        end
    end
    I.current=best
    if best then
        I.status=string.format("%s [%s] ID:%s 包%d 格%d x%d (评分:%d)",
            tostring(best.name),best.kind,tostring(best.id),best.bag,best.slot,best.totalCount,bestScore)
    else
        I.status="背包中未找到已配置且可识别的饮料"
    end
    return best
end
function I.Report()
    I.Find()
    DW.Print(I.status)
    return I.current
end
DW.RegisterModule("Items")
