-- TaiYangDrinkWalk item management - WoW 1.12.1 / Lua 5.0.
-- Read-only: no inventory item use, no auto-selection by tooltip guessing.
local DW = TaiYangDrinkWalk
local I = { implemented = true }
DW.Items = I
local function DB()
    if type(TaiYangDrinkWalkDB) ~= "table" then TaiYangDrinkWalkDB = {} end
    local db = TaiYangDrinkWalkDB
    if type(db.itemRules) ~= "table" then db.itemRules = {} end
    return db
end
local function Trim(s)
    return string.gsub(string.gsub(s or "", "^%s+", ""), "%s+$", "")
end
local function ID(link)
    if type(link) ~= "string" then return nil end
    local s = string.match(link, "item:(%d+)")
    if s then return tonumber(s) end
    return nil
end
function I.Add(kind, query)
    kind = string.lower(kind or "")
    if kind ~= "food" and kind ~= "drink" and kind ~= "both" then return false, "分类只能 food/drink/both" end
    query = Trim(query)
    if query == "" or string.len(query) > 100 then return false, "物品 ID/名称不合法" end
    local id = tonumber(query)
    if id and (id < 1 or id ~= math.floor(id)) then return false, "物品 ID 不合法" end
    local db = DB()
    local key = id and tostring(id) or string.lower(query)
    local i
    for i = 1, table.getn(db.itemRules) do
        local v = db.itemRules[i]
        if type(v) == "table" then
            local k = v.id and tostring(v.id) or string.lower(v.name or "")
            if k == key and v.kind == kind then return false, "该规则已存在" end
        end
    end
    table.insert(db.itemRules, {kind=kind, id=id, name=id and nil or query})
    return true, "已添加 " .. kind .. ": " .. query
end
function I.Remove(index)
    local db = DB()
    local n = tonumber(index)
    if not n or n ~= math.floor(n) or n < 1 or n > table.getn(db.itemRules) then return false,"序号无效" end
    table.remove(db.itemRules,n)
    return true,"规则已删除"
end
function I.List()
    local db = DB()
    if table.getn(db.itemRules) == 0 then DW.Print("暂无物品规则。使用 /tsdw add drink 物品ID") end
    local i
    for i = 1, table.getn(db.itemRules) do
        local r=db.itemRules[i]
        if type(r)=="table" then DW.Print(i .. ". [" .. tostring(r.kind) .. "] " .. tostring(r.id or r.name)) end
    end
end
function I.Scan()
    local db=DB()
    local matches={}
    local i,bag,slot
    if type(GetContainerNumSlots)~="function" or type(GetContainerItemLink)~="function" then
        return matches,"背包 API 不可用"
    end
    for bag=0,4 do
        local count=GetContainerNumSlots(bag) or 0
        for slot=1,count do
            local link=GetContainerItemLink(bag,slot)
            if link then
                local id=ID(link)
                local display=string.match(link,"%[(.-)%]") or link
                local lower=string.lower(display)
                for i=1,table.getn(db.itemRules) do
                    local rule=db.itemRules[i]
                    if type(rule)=="table" then
                        local match=(rule.id and id==rule.id) or
                           (rule.name and string.lower(rule.name)==lower)
                        if match then
                            local texture,stack=GetContainerItemInfo(bag,slot)
                            table.insert(matches,{kind=rule.kind,id=id,name=display,bag=bag,slot=slot,count=stack or 1,rule=i})
                            break
                        end
                    end
                end
            end
        end
    end
    return matches,nil
end
function I.Report()
    local matches,err=I.Scan()
    if err then DW.Print(err);return end
    if table.getn(matches)==0 then DW.Print("扫描完成：未发现匹配规则的物品"); return end
    DW.Print("扫描完成：匹配格子 " .. table.getn(matches))
    local i
    for i=1,math.min(table.getn(matches),12) do
        local m=matches[i]
        DW.Print(m.kind.." "..m.name.." (ID "..tostring(m.id)..") 包"..m.bag.." 格"..m.slot.." x"..m.count)
    end
    if table.getn(matches)>12 then DW.Print("仅展示前12项") end
end
DW.RegisterModule("Items")
