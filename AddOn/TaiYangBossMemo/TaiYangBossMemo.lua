-- Register slash command before building UI; init failure remains diagnosable.
local TaiYangBossMemoOpen = nil
local TaiYangBossMemoClose = nil
SLASH_TAIYANGBOSSMEMO1="/bossmemo"
SlashCmdList["TAIYANGBOSSMEMO"]=function()
    if type(TaiYangBossMemoOpen)=="function" then
        local ok,err=pcall(TaiYangBossMemoOpen)
        if not ok and DEFAULT_CHAT_FRAME then
            DEFAULT_CHAT_FRAME:AddMessage("|cffff5555[BossMemo] 界面错误: "..tostring(err).."|r")
        end
    elseif DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cffff5555[BossMemo] 界面尚未初始化；请检查加载错误。|r")
    end
end
local function TaiYangBossMemoInitialize()
-- TaiYangBossMemo 1.1.6 | WoW 1.12.1 / Lua 5.0
-- Shared account boss catalog + character-specific notes; old account DB is migration-only.
local PREFIX="|cffffd100[太阳Boss备忘录]|r "
local db,shown=nil,{}
local tracks,lastEntry={},nil
local timer=0
local pendingNewSelection=false
local function say(s) if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(PREFIX..s) end end
local function trim(s) local result=string.gsub(s or "","^%s*(.-)%s*$","%1");return result end
-- The old account table remains read-only as a migration source.
-- Shared boss directory is account-wide; reminders and preferences belong to each character.
local function getAccount()
    if type(TaiYangBossMemoAccountDB)~="table" then TaiYangBossMemoAccountDB={} end
    local a=TaiYangBossMemoAccountDB
    if type(a.bosses)~="table" then a.bosses={} end
    return a
end
local function getDB()
    local account=getAccount()
    if type(TaiYangBossMemoCharDB)~="table" then TaiYangBossMemoCharDB={} end
    db=TaiYangBossMemoCharDB
    if type(db.notes)~="table" then db.notes={} end
    -- Migrate once for each character. Never remove or modify legacy account data.
    -- Import catalog independently so newly created characters can see all bosses.
    if not account.catalogMigrated then
        local legacy=TaiYangBossMemoDB
        if type(legacy)=="table" and type(legacy.notes)=="table" then
            for key,item in pairs(legacy.notes) do
                if tonumber(key) and type(item)=="table" and account.bosses[tostring(key)]==nil then
                    account.bosses[tostring(key)]={name=item.name or ""}
                end
            end
        end
        account.catalogMigrated=true
    end
    if not db.migratedFromAccount then
        local legacy=TaiYangBossMemoDB
        if type(legacy)=="table" then
            if type(legacy.notes)=="table" then
                for key,item in pairs(legacy.notes) do
                    if tonumber(key) and type(item)=="table" and db.notes[tostring(key)]==nil then
                        db.notes[tostring(key)]={before=item.before or "",after=item.after or "",enabled=item.enabled~=false}
                    end
                end
            end
            local fields={"enabled","before","realertAfterReset","alertSeconds","pollInterval"}
            for i=1,table.getn(fields) do
                local f=fields[i]
                if db[f]==nil then db[f]=legacy[f] end
            end
            if db.alertPos==nil and type(legacy.alertPos)=="table" then
                db.alertPos={x=legacy.alertPos.x,y=legacy.alertPos.y}
            end
        end
        db.migratedFromAccount=true
    end
    -- v1.1: stable IDs. Keep legacy snapshots intact and migrate both stores once.
    if not account.bossIdMigrated then
        local changes={}
        for key,boss in pairs(account.bosses) do
            if type(boss)=="table" and tonumber(key) then
                local newKey="boss_"..key
                if not account.bosses[newKey] then
                    boss.entry=tonumber(key)
                    changes[newKey]=boss
                end
                account.bosses[key]=nil
            end
        end
        for key,boss in pairs(changes) do account.bosses[key]=boss end
        account.bossIdMigrated=true
    end
    if not db.bossIdMigrated then
        local moves={}
        for key,item in pairs(db.notes) do
            if tonumber(key) then
                local dest="boss_"..key
                if not db.notes[dest] then moves[dest]=item end
                db.notes[key]=nil
            end
        end
        for key,item in pairs(moves) do db.notes[key]=item end
        db.bossIdMigrated=true
    end
    if db.enabled==nil then db.enabled=true end
    if db.before==nil then db.before=true end
    if db.mapReminder==nil then db.mapReminder=false end
    if db.realertAfterReset==nil then db.realertAfterReset=false end
    if db.alertSeconds~=0 and db.alertSeconds~=5 and db.alertSeconds~=10 and db.alertSeconds~=15 then db.alertSeconds=0 end
    if type(db.alertPos)~="table" then db.alertPos=nil end
    if db.pollInterval~=0.2 and db.pollInterval~=0.5 and db.pollInterval~=1 and db.pollInterval~=2 then db.pollInterval=0.5 end
    return db
end
local function bossInfo(key)
    local boss=getAccount().bosses[tostring(key)]
    if type(boss)=="table" then return boss end
end
local function idByEntry(entry)
    local n=tonumber(entry)
    if not n then return nil end
    for key,boss in pairs(getAccount().bosses) do
        if type(boss)=="table" and tonumber(boss.entry)==n then return key end
    end
end
local function newBossKey()
    local a=getAccount()
    a.nextBossId=tonumber(a.nextBossId) or 0
    local key
    repeat a.nextBossId=a.nextBossId+1;key="boss_new_"..a.nextBossId
    until a.bosses[key]==nil
    return key
end
local function api(cmd,a,b)
    if type(TaiYangShenDian)~="function" then return nil end
    local ok,val
    if b~=nil then ok,val=pcall(TaiYangShenDian,cmd,a,b)
    elseif a~=nil then ok,val=pcall(TaiYangShenDian,cmd,a)
    else ok,val=pcall(TaiYangShenDian,cmd) end
    if ok and type(val)=="table" then return val end
end
local function anyNote(entry)
    local item=getDB().notes[tostring(entry)]
    if type(item)=="table" then return item end
end
local function note(entry)
    local boss=bossInfo(entry)
    local item=anyNote(entry)
    if not boss or not item or item.enabled==false then return nil end
    return {name=boss.name,before=item.before,after=item.after,enabled=item.enabled}
end
-- Run account/character migration before the editor builds its first Boss list.
getDB()
-- Inventory helpers: scan backpack and four bags only; bank is intentionally excluded.
local function itemID(link)
    if type(link)~="string" then return nil end
    local id=string.gsub(link,"^.-item:(%d+):?.*$","%1")
    return tonumber(id)
end
local EQUIP_SLOTS={1,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19}
-- One bag traversal produces consumable totals and optionally exact equipment links.
-- Snapshot lifetime is one refresh only; a click-to-equip never uses stale bag slots.
local function inventory(includeEquipment)
    local result={}
    local available=includeEquipment and {} or nil
    if includeEquipment and type(GetInventoryItemLink)=="function" then
        for i=1,table.getn(EQUIP_SLOTS) do
            local link=GetInventoryItemLink("player",EQUIP_SLOTS[i])
            if link then available[link]=(available[link] or 0)+1 end
        end
    end
    if type(GetContainerNumSlots)~="function" or type(GetContainerItemLink)~="function" then return result,available end
    for bag=0,4 do
        for slot=1,(GetContainerNumSlots(bag) or 0) do
            local link=GetContainerItemLink(bag,slot)
            if link then
                if available then available[link]=(available[link] or 0)+1 end
                local id=itemID(link)
                if id then
                    local _,count=nil,1
                    if type(GetContainerItemInfo)=="function" then _,count=GetContainerItemInfo(bag,slot) end
                    count=count or 1
                    if not result[id] then result[id]={id=id,count=0,link=link,bag=bag,slot=slot} end
                    result[id].count=result[id].count+count
                end
            end
        end
    end
    return result,available
end
local function itemName(id)
    if type(GetItemInfo)=="function" then
        local name=GetItemInfo(id)
        if type(name)=="string" then return name end
    end
    return "物品 #"..tostring(id)
end
-- Keep original slot links for matching; names are display-only metadata.
local function gearDisplayName(link,slot)
    if type(link)=="string" then
        -- Vanilla links may contain a full colorized hyperlink or only item:<id>.
        local fromLink=string.match(link,"|h%[([^%]]+)%]|h")
        if fromLink and fromLink~="" then return fromLink end
    end
    if type(GetItemInfo)=="function" then
        local id=itemID(link)
        local name=id and GetItemInfo(id) or nil
        if type(name)=="string" and name~="" then return name end
        if type(link)=="string" then
            name=GetItemInfo(link)
            if type(name)=="string" and name~="" then return name end
        end
    end
    local id=itemID(link)
    if id then return "物品 #"..tostring(id).."（槽位 "..tostring(slot).."）" end
    return "装备槽位 "..tostring(slot).."（名称未缓存）"
end
local function prepData(entry)
    local item=anyNote(entry)
    if not item then return nil end
    if type(item.consumables)~="table" then item.consumables={} end
    return item
end
-- Gear snapshots store exact inventory item links (enchant/instance identity) per slot.
-- Never auto-equip on targeting; a physical user click is required.
local function equipSnapshot()
    local outfit={}
    if type(GetInventoryItemLink)~="function" then return outfit end
    for i=1,table.getn(EQUIP_SLOTS) do
        local slot=EQUIP_SLOTS[i]
        local link=GetInventoryItemLink("player",slot)
        if link then outfit[tostring(slot)]=link end
    end
    return outfit
end
-- Migrate the former unnamed outfit once per character/Boss without erasing it.
local function outfitSets(entry)
    local d=prepData(entry)
    if not d then return nil end
    if type(d.gearSets)~="table" then d.gearSets={} end
    if type(d.gear)=="table" and next(d.gear) and not d.gearMigrated then
        local found=false
        for _,set in pairs(d.gearSets) do if type(set)=="table" then found=true;break end end
        if not found then
            d.gearSets["旧版 Boss 套装"]={name="旧版 Boss 套装",slots=d.gear}
        else
            -- Preserve old data if named outfits already exist, using a distinct migration name.
            if not d.gearSets["旧版 Boss 套装"] then d.gearSets["旧版 Boss 套装"]={name="旧版 Boss 套装",slots=d.gear} end
        end
        d.gearMigrated=true
    end
    return d.gearSets
end
local function sortedSetNames(entry)
    local names={}
    local sets=outfitSets(entry)
    if sets then for key,val in pairs(sets) do
        if type(key)=="string" and type(val)=="table" and type(val.slots)=="table" then table.insert(names,key) end
    end end
    table.sort(names)
    return names
end
-- Consume an equipment multiset per outfit without duplicating the entire bag snapshot.
local function setMissing(slots,available)
    if type(slots)~="table" then return 0 end
    if not available then
        local _,fresh=inventory(true)
        available=fresh or {}
    end
    local used={}
    local missing=0
    for i=1,table.getn(EQUIP_SLOTS) do
        local wanted=slots[tostring(EQUIP_SLOTS[i])]
        if wanted then
            local taken=used[wanted] or 0
            if taken<(available[wanted] or 0) then used[wanted]=taken+1
            else missing=missing+1 end
        end
    end
    return missing
end
local function equipOutfit(entry,setName)
    local sets=outfitSets(entry)
    local saved=sets and sets[setName]
    if type(saved)~="table" or type(saved.slots)~="table" then return false,"装备方案不存在或已删除" end
    if type(UnitAffectingCombat)=="function" and UnitAffectingCombat("player") then return false,"战斗中禁止执行整套换装" end
    if type(CursorHasItem)=="function" and CursorHasItem() then return false,"请先清空鼠标上的物品" end
    if type(PickupContainerItem)~="function" or type(PickupInventoryItem)~="function" then return false,"当前客户端缺少换装接口" end
    local changed,missing=0,0
    local missingNames={}
    local failedNames={}
    local function recordMissing(link,slot,foundButFailed)
        local stored=saved.slotNames and saved.slotNames[tostring(slot)]
        local display=(type(stored)=="string" and stored~="") and stored or gearDisplayName(link,slot)
        if foundButFailed then table.insert(failedNames,display)
        else table.insert(missingNames,display) end
        missing=missing+1
    end
    for i=1,table.getn(EQUIP_SLOTS) do
        local slot=EQUIP_SLOTS[i]
        local wanted=saved.slots[tostring(slot)]
        if wanted and GetInventoryItemLink("player",slot)~=wanted then
            local location=nil
            for bag=0,4 do
                for n=1,(GetContainerNumSlots(bag) or 0) do
                    if GetContainerItemLink(bag,n)==wanted then location={bag,n};break end
                end
                if location then break end
            end
            if location then
                PickupContainerItem(location[1],location[2])
                PickupInventoryItem(slot)
                if type(CursorHasItem)=="function" and CursorHasItem() then PickupContainerItem(location[1],location[2]) end
                if GetInventoryItemLink("player",slot)==wanted then changed=changed+1 else recordMissing(wanted,slot,true) end
            else recordMissing(wanted,slot,false) end
        end
    end
    if missing==0 then return true,"「"..setName.."」换装：成功 "..changed.." 槽，装备齐全" end
    local parts={"「"..setName.."」换装：成功 "..changed.." 槽"}
    if table.getn(missingNames)>0 then table.insert(parts,"缺失装备："..table.concat(missingNames,"、")) end
    if table.getn(failedNames)>0 then table.insert(parts,"换装失败："..table.concat(failedNames,"、")) end
    return false,table.concat(parts,"；")
end

local frame=CreateFrame("Frame","TaiYangBossMemoWindow",UIParent)
frame:SetWidth(755);frame:SetHeight(510)
frame:SetPoint("CENTER",UIParent,"CENTER",0,30)
frame:SetFrameStrata("DIALOG")
frame:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
frame:SetMovable(true);frame:EnableMouse(true);frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart",function() this:StartMoving() end)
frame:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
local title=frame:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
title:SetPoint("TOP",frame,"TOP",0,-20);title:SetText("太阳神殿 · Boss 战前准备")
local subtitle=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
subtitle:SetPoint("TOP",title,"BOTTOM",0,-7)
subtitle:SetText("Boss 名单账号共享 · 备注及提醒设置角色独立")

local manager=CreateFrame("Frame",nil,frame)
manager:SetPoint("TOPLEFT",frame,"TOPLEFT",20,-69)
manager:SetWidth(714);manager:SetHeight(396)
local function label(parent,x,y,value,font,width)
    local l=parent:CreateFontString(nil,"OVERLAY",font or "GameFontNormal")
    l:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
    if width then l:SetWidth(width);l:SetJustifyH("LEFT") end
    l:SetText(value)
    return l
end
local function button(parent,caption,x,y,w,h,fn)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y);b:SetWidth(w);b:SetHeight(h or 25)
    b:SetText(caption);b:SetScript("OnClick",fn)
    return b
end
local function edit(parent,x,y,w,h)
    local b=CreateFrame("EditBox",nil,parent)
    b:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
    b:SetWidth(w);b:SetHeight(h)
    b:SetFontObject(GameFontHighlight)
    b:SetMultiLine(h>28);b:SetAutoFocus(false)
    local bg=CreateFrame("Frame",nil,parent)
    bg:SetFrameLevel(b:GetFrameLevel()-1)
    bg:SetPoint("TOPLEFT",b,"TOPLEFT",-4,5)
    bg:SetPoint("BOTTOMRIGHT",b,"BOTTOMRIGHT",4,-4)
    bg:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",tile=true,tileSize=16,edgeSize=12,insets={left=3,right=3,top=3,bottom=3}})
    bg:SetBackdropColor(0.07,0.07,0.09,0.96)
    b:SetScript("OnEscapePressed",function() this:ClearFocus() end)
    return b
end
label(manager,10,-7,"已保存的 Boss","GameFontNormalLarge")
local searchBox=edit(manager,10,-38,198,22)
local listRows={}
local page,selectedEntry=1,nil
local dirty=false
local suppressDirty=false
local savedSnapshot=nil
-- Forward declarations must precede closures in Lua 5.0.
local nameBox,entryBox,zoneBox,beforeBox,enabledCheck
local zoneManual=false
local zoneProgrammatic=false
local dirtyLabel,prepButton
local function updateDirtyLabel()
    if dirtyLabel then dirtyLabel:SetText(dirty and "● 未保存" or "已保存") end
    if prepButton then
        local ready=selectedEntry and bossInfo(selectedEntry) and not dirty
        if ready then prepButton:Enable() else prepButton:Disable() end
    end
end
local function editorSnapshot()
    if not entryBox then return nil end
    return {entry=entryBox:GetText() or "",name=nameBox:GetText() or "",zone=zoneBox:GetText() or "",before=beforeBox:GetText() or "",enabled=enabledCheck:GetChecked() and true or false}
end
local function captureSnapshot()
    savedSnapshot=editorSnapshot();dirty=false;updateDirtyLabel()
end
local function recomputeDirty()
    if suppressDirty or not savedSnapshot then return end
    local current=editorSnapshot()
    dirty=current.entry~=savedSnapshot.entry or current.name~=savedSnapshot.name or current.zone~=savedSnapshot.zone or current.before~=savedSnapshot.before or current.enabled~=savedSnapshot.enabled
    updateDirtyLabel()
end
local confirmModal=nil
local function markDirty() recomputeDirty() end
local PAGE_SIZE=7
local function entryList()
    local entries={}
    local filter=string.lower(trim(searchBox:GetText() or ""))
    for key,item in pairs(getAccount().bosses) do
        if type(item)=="table" and (filter=="" or string.find(string.lower(item.name or ""),filter,1,true)
            or string.find(string.lower(item.zone or ""),filter,1,true)
            or string.find(tostring(item.entry or ""),filter,1,true)) then
            table.insert(entries,key)
        end
    end
    table.sort(entries,function(a,b) return tostring(a)<tostring(b) end)
    return entries
end
local status=label(frame,29,-460,"请选择 Boss，或读取当前目标添加。","GameFontHighlightSmall",690)
local function setStatus(s) status:SetText(s) end
local refreshList
local pageInfo,prevButton,nextButton
local function askDiscard(action)
    if not dirty then action();return end
    if confirmModal then confirmModal("未保存的修改", "当前 Boss 的备注尚未保存。放弃修改并继续吗？", action) end
end
local function currentZoneName()
    if type(GetRealZoneText)=="function" then
        local zone=trim(GetRealZoneText() or "")
        if zone~="" then return zone end
    end
    if type(GetZoneText)=="function" then return trim(GetZoneText() or "") end
    return ""
end
-- Compact built-in map/Boss reference. Names are hints, not authoritative NPC Entry IDs.
-- The user's saved mapping always takes precedence over this directory.
local function normalizeBossName(value)
    return string.lower(trim(value or ""))
end
local function inferZone(entry,name,preferName)
    -- Name changes deliberately ignore the old Entry/zone mapping.
    local normalized=normalizeBossName(name)
    if preferName and normalized~="" then
        if type(TaiYangBossZoneDB)=="table" and type(TaiYangBossZoneDB.FindByBossName)=="function" then
            local zone,source=TaiYangBossZoneDB.FindByBossName(name)
            if zone or source=="ambiguous" then return zone,source end
        end
        -- Previously stored names may extend the packaged catalog.
        local unique=nil
        for _,boss in pairs(getAccount().bosses) do
            if type(boss)=="table" and normalizeBossName(boss.name)==normalized and trim(boss.zone or "")~="" then
                if unique and unique~=boss.zone then return nil,"ambiguous" end
                unique=boss.zone
            end
        end
        if unique then return unique,"saved-name" end
        return nil,nil
    end
    local saved=entry and bossInfo(idByEntry(entry))
    if saved and trim(saved.zone or "")~="" then return trim(saved.zone),"entry" end
    if normalized=="" then return nil,nil end
    if type(TaiYangBossZoneDB)=="table" and type(TaiYangBossZoneDB.FindByBossName)=="function" then
        local zone,source=TaiYangBossZoneDB.FindByBossName(name)
        if zone or source=="ambiguous" then return zone,source end
    end
    return nil,nil
end
local function setZoneAutomatically(value)
    zoneProgrammatic=true
    zoneBox:SetText(value or "")
    zoneProgrammatic=false
end
local function autoFillZone(nameChanged)
    if suppressDirty then return end
    -- Name changes must replace a previous manual map entry.
    if nameChanged then zoneManual=false end
    if zoneManual then return end
    local entry=tonumber(trim(entryBox:GetText() or ""))
    local matched,source=inferZone(entry,nameBox:GetText(),nameChanged)
    if matched then setZoneAutomatically(matched)
    elseif source=="ambiguous" then setZoneAutomatically("")
    else setZoneAutomatically(currentZoneName()) end
end
local function loadEntry(key,name,detectedZone)
    local boss=key and bossInfo(key)
    suppressDirty=true
    selectedEntry=key
    zoneManual=false
    local item=key and anyNote(key)
    entryBox:SetText(boss and boss.entry and tostring(boss.entry) or "")
    nameBox:SetText(boss and boss.name or name or "")
    local zone=boss and boss.zone and boss.zone~="" and boss.zone or nil
    if not zone then zone=inferZone(boss and boss.entry,name or (boss and boss.name)) end
    setZoneAutomatically(zone or detectedZone or currentZoneName())
    beforeBox:SetText(item and item.before or "")
    enabledCheck:SetChecked(not item or item.enabled~=false)
    lastEntry=key
    setStatus(key and ("正在编辑 Boss "..(boss and boss.name or key)) or "新建 Boss")
    if refreshList then refreshList() end
    suppressDirty=false;captureSnapshot()
end
for i=1,PAGE_SIZE do
    local n=i
    local b=button(manager,"",10,-74-(i-1)*36,200,31,function()
        local entries=entryList()
        local id=entries[(page-1)*PAGE_SIZE+n]
        if id then askDiscard(function() loadEntry(id) end) end
    end)
    listRows[i]=b
end
refreshList=function()
    local entries=entryList()
    local count=table.getn(entries)
    local total=math.max(1,math.ceil(count/PAGE_SIZE))
    if page>total then page=total end
    if page<1 then page=1 end
    for i=1,PAGE_SIZE do
        local n=entries[(page-1)*PAGE_SIZE+i]
        local b=listRows[i]
        if n then
            local item=anyNote(n)
            local boss=bossInfo(n)
            local tag=(item and item.enabled==false) and "[停用] " or ""
            local name=(boss and boss.name and boss.name~="") and boss.name or ("Boss "..tostring(n))
            b:SetText((selectedEntry==n and "▶ " or "")..tag..name)
            b:Show()
        else b:Hide() end
    end
    if pageInfo then pageInfo:SetText("第 "..page.." / "..total.." 页 · 共 "..count.." 个 Boss") end
    if prevButton then if page<=1 then prevButton:Disable() else prevButton:Enable() end end
    if nextButton then if page>=total then nextButton:Disable() else nextButton:Enable() end end
end
searchBox:SetScript("OnTextChanged",function() page=1;refreshList() end)
prevButton=button(manager,"上一页",10,-335,93,25,function() if page>1 then page=page-1;refreshList() end end)
nextButton=button(manager,"下一页",116,-335,93,25,function() page=page+1;refreshList() end)
pageInfo=label(manager,12,-369,"第 1 / 1 页 · 共 0 个 Boss","GameFontHighlightSmall",200)
refreshList()
label(manager,235,-7,"Boss 详情 / 编辑","GameFontNormalLarge")
label(manager,236,-40,"NPC Entry")
entryBox=edit(manager,235,-61,120,22)
label(manager,375,-40,"Boss 名称")
nameBox=edit(manager,375,-61,325,22)
label(manager,235,-102,"所在地图")
zoneBox=edit(manager,235,-121,465,22)
label(manager,235,-156,"战前提醒")
beforeBox=edit(manager,235,-177,465,96)
entryBox:SetScript("OnTextChanged",function() autoFillZone();markDirty() end)
nameBox:SetScript("OnTextChanged",function() autoFillZone(true);markDirty() end)
zoneBox:SetScript("OnTextChanged",function()
    if not suppressDirty and not zoneProgrammatic then zoneManual=true end
    markDirty()
end)
beforeBox:SetScript("OnTextChanged",markDirty)
enabledCheck=CreateFrame("CheckButton",nil,manager,"UICheckButtonTemplate")
enabledCheck:SetPoint("TOPLEFT",manager,"TOPLEFT",234,-285)
label(manager,269,-294,"启用此 Boss 的提醒","GameFontHighlight")
enabledCheck:SetScript("OnClick",markDirty)
dirtyLabel=label(manager,590,-294,"已保存","GameFontHighlightSmall",114)
local function entryNumber()
    local value=trim(entryBox:GetText())
    if not string.find(value,"^%d+$") then return nil end
    local n=tonumber(value)
    if not n or n<1 or n>4294967295 then return nil end
    return n
end
local function target()
    local id=api("Unit.Identity","target")
    local detectedName=(id and id.ok and id.data and id.data.name) or (type(UnitName)=="function" and UnitName("target"))
    local detectedEntry=id and id.ok and id.data and tonumber(id.data.entry)
    if type(UnitExists)=="function" and UnitExists("target") and detectedName and detectedName~="" then
        if (type(UnitIsPlayer)=="function" and UnitIsPlayer("target")) or
           (type(UnitPlayerControlled)=="function" and UnitPlayerControlled("target")) then
            setStatus("当前目标为玩家或玩家控制单位，不能作为 Boss。");return
        end
        local key=detectedEntry and idByEntry(detectedEntry)
        askDiscard(function()
            loadEntry(key,detectedName,currentZoneName())
            if not key then
                suppressDirty=true
                entryBox:SetText(detectedEntry and tostring(detectedEntry) or "")
                nameBox:SetText(detectedName)
                setZoneAutomatically(inferZone(detectedEntry,detectedName,true) or currentZoneName())
                suppressDirty=false;recomputeDirty()
            end
        end)
    else setStatus("当前没有可读取的 NPC 目标；可以手动填写名称和地图。") end
end
local function save()
    local entryRaw=trim(entryBox:GetText() or "")
    local n=entryNumber()
    if entryRaw~="" and not n then setStatus("保存失败：Entry 必须为空或有效正整数。");return end
    local name=trim(nameBox:GetText())
    local zone=trim(zoneBox:GetText())
    if name=="" or zone=="" then setStatus("保存失败：Boss 名称与所在地图必填。");return end
    local key=selectedEntry or newBossKey()
    if n then
        local conflict=idByEntry(n)
        if conflict and conflict~=key then setStatus("保存失败：Entry 已属于其他 Boss，请先核实。");return end
    end
    local oldItem=anyNote(key)
    local item={before=trim(beforeBox:GetText()),after=oldItem and oldItem.after or "",enabled=enabledCheck:GetChecked() and true or false,consumables=oldItem and oldItem.consumables or {},gear=oldItem and oldItem.gear or nil,gearSets=oldItem and oldItem.gearSets or nil,gearMigrated=oldItem and oldItem.gearMigrated or nil}
    getAccount().bosses[key]={name=name,zone=zone,entry=n}
    getDB().notes[key]=item
    loadEntry(key)
    setStatus("保存成功："..name..(item.enabled and "（已启用）" or "（已停用）"))
    refreshList()
end
local function deleteEntry()
    local key=selectedEntry
    if not key or not bossInfo(key) then setStatus("请先选择已保存的 Boss。");return end
    if confirmModal then
        local item=bossInfo(key)
        local displayName=(item and item.name and item.name~="") and item.name or tostring(key)
        confirmModal("确认删除 Boss", "删除「"..displayName.."」？此操作无法撤销。",function()
            getAccount().bosses[key]=nil
            getDB().notes[key]=nil
            suppressDirty=true
            selectedEntry=nil;lastEntry=nil
            entryBox:SetText("");nameBox:SetText("");zoneBox:SetText("");beforeBox:SetText("")
            enabledCheck:SetChecked(true);refreshList()
            suppressDirty=false;captureSnapshot()
            setStatus("已删除 Boss 「"..displayName.."」。")
        end)
    end
end
button(manager,"读取当前目标",235,-325,147,27,target)
button(manager,"保存 Boss",395,-325,145,27,save)
button(manager,"启用/停用",553,-325,150,27,function()
    local key=selectedEntry
    if not key or not bossInfo(key) then setStatus("请先选择已保存的 Boss") return end
    local data=getDB().notes[key]
    if type(data)~="table" then data={before="",enabled=true};getDB().notes[key]=data end
    data.enabled=not (data.enabled~=false)
    enabledCheck:SetChecked(data.enabled)
    suppressDirty=true;captureSnapshot();suppressDirty=false
    setStatus(data.enabled and "此角色已启用该 Boss 提醒" or "此角色已停用该 Boss 提醒")
    refreshList()
end)
button(manager,"删除",473,-363,107,27,deleteEntry)
button(manager,"新建",593,-363,110,27,function()
    askDiscard(function()
    suppressDirty=true
    selectedEntry=nil;zoneManual=false;entryBox:SetText("");nameBox:SetText("");setZoneAutomatically(currentZoneName())
    beforeBox:SetText("");enabledCheck:SetChecked(true)
    setStatus("新建 Boss：输入 Entry、名称和提醒后保存。");refreshList()
    suppressDirty=false;captureSnapshot()
    end)
end)
refreshList()
-- Preparation configuration page. Changes are character-local and saved immediately.
local prep=CreateFrame("Frame",nil,frame)
prep:SetPoint("TOPLEFT",frame,"TOPLEFT",27,-72)
prep:SetWidth(700);prep:SetHeight(590);prep:Hide()
local prepTitle=label(prep,8,-5,"药剂与装备 · 当前角色","GameFontNormalLarge",620)
local prepHint=label(prep,8,-29,"先在 Boss 管理中保存名称与地图，再配置药剂和装备。","GameFontHighlightSmall",670)
local prepStock=label(prep,8,-52,"","GameFontHighlightSmall",680)
local prepRows={}
local prepTab="bag"
local bagAll=true
local prepPage=1
local prepFilter=""
local prepSearch=edit(prep,9,-75,215,22)
local prepModeLabel=label(prep,240,-79,"","GameFontHighlightSmall",435)
local prepStatus=label(prep,10,-476,"","GameFontHighlightSmall",670)
local prepRefresh
local function currentPrepEntry()
    local key=selectedEntry
    if key and bossInfo(key) then return key end
end
local function validNeed(raw)
    raw=trim(raw)
    if not string.find(raw,"^%d+$") then return nil end
    local n=tonumber(raw)
    if n and n>=1 and n<=999 and math.floor(n)==n then return n end
end
local function appendConsumable(entry,id,quantity)
    local d=prepData(entry)
    if not d or not id or id<1 or id>4294967295 or not quantity then return false end
    d.consumables[tostring(id)]=quantity
    return true
end
local function sortedConsumables(entry)
    local d=prepData(entry)
    local arr={}
    if d then for id,need in pairs(d.consumables) do
        local n=tonumber(id)
        if n then table.insert(arr,n) end
    end end
    table.sort(arr)
    return arr
end
local function usableConsumable(id)
    if type(GetItemInfo)~="function" then return false end
    local _,_,_,_,_,typ=GetItemInfo(id)
    return typ=="消耗品" or typ=="Consumable"
end
local function matchItem(id,filter)
    if filter=="" then return true end
    local name=itemName(id)
    return string.find(string.lower(name),string.lower(filter),1,true) or string.find(tostring(id),filter,1,true)
end
local function prepSetMode(mode)
    prepTab=mode;prepPage=1;prepFilter="";prepSearch:SetText("")
    prepRefresh()
end
local function paintFavorite(row,id)
    local fav=getDB().favorites or {}
    local liked=fav[tostring(id)]==true
    row.star:SetText(liked and "★" or "☆")
    local fs=row.star:GetFontString()
    if fs then if liked then fs:SetTextColor(1,0.18,0.18) else fs:SetTextColor(1,0.82,0.05) end end
end
prepRefresh=function()
    local entry=currentPrepEntry()
    local stock=inventory()
    prepTitle:SetText("药剂与装备 · "..(entry and (bossInfo(entry).name or "Boss") or "未保存 Boss"))
    local selected=entry and prepData(entry) or nil
    local ids=entry and sortedConsumables(entry) or {}
    prepStock:SetText("已配置 "..table.getn(ids).." 种物品 · 默认只统计随身背包（不含银行）")
    local candidates={}
    if prepTab=="bag" then
        for id,v in pairs(stock) do
            if (bagAll or usableConsumable(id)) and matchItem(id,prepFilter) then table.insert(candidates,id) end
        end
    elseif prepTab=="search" then
        local typed=tonumber(prepFilter)
        if typed and typed>0 and typed<=4294967295 then table.insert(candidates,typed) end
        for id,v in pairs(stock) do
            if id~=typed and matchItem(id,prepFilter) then table.insert(candidates,id) end
        end
    elseif prepTab=="favorite" then
        local fav=getDB().favorites or {}
        for key,v in pairs(fav) do
            local id=tonumber(key)
            if v and id and matchItem(id,prepFilter) then table.insert(candidates,id) end
        end

    end
    table.sort(candidates)
    local total=table.getn(candidates)
    local maxPage=math.max(1,math.ceil(total/5))
    if prepPage>maxPage then prepPage=maxPage end
    for i=1,5 do
        local row=prepRows[i]
        local id=candidates[(prepPage-1)*5+i]
        if id then
            row.id=id
            local configured=selected and selected.consumables[tostring(id)]
            row.text:SetText(itemName(id).." ["..id.."] x"..(stock[id] and stock[id].count or 0)..(configured and (" |cff40ff40(已配置 x"..configured..")|r") or ""))
            row.quantity:SetText(tostring(configured or 1))
            row.quantity:Show();row.star:Show();row.add:SetText("添加/修改")
            if configured~=nil then row.remove:Show() else row.remove:Hide() end
            paintFavorite(row,id)
            row.frame:Show()
        else row.id=nil;row.frame:Hide() end
    end
    prepModeLabel:SetText(({bag=(bagAll and "全部物品" or "背包消耗品"),search="搜索全部背包 / ItemID",favorite="当前角色收藏"})[prepTab].." · "..total.." 项 · 第 "..prepPage.."/"..maxPage.." 页")
end
prepSearch:SetScript("OnTextChanged",function() prepFilter=trim(this:GetText() or "");prepPage=1;if prepRefresh then prepRefresh() end end)
local modeLabels={{"全部物品","all"},{"消耗品","consumable"},{"搜索","search"},{"收藏","favorite"}}
for i=1,4 do
    local m=modeLabels[i]
    button(prep,m[1],8+(i-1)*115,-105,108,24,function()
        if m[2]=="all" then bagAll=true;prepSetMode("bag")
        elseif m[2]=="consumable" then bagAll=false;prepSetMode("bag")
        else prepSetMode(m[2]) end
    end)
end
label(prep,10,-139,"背包选择 / 搜索结果", "GameFontNormal", 250)
label(prep,414,-140,"需求","GameFontHighlightSmall",65)
for i=1,5 do
    local rowFrame=CreateFrame("Frame",nil,prep)
    rowFrame:SetPoint("TOPLEFT",prep,"TOPLEFT",8,-158-(i-1)*32)
    rowFrame:SetWidth(690);rowFrame:SetHeight(29)
    local txt=label(rowFrame,3,-6,"","GameFontHighlightSmall",368)
    local countBox=edit(rowFrame,377,-2,52,21)
    local ri={frame=rowFrame,text=txt,quantity=countBox,id=nil}
    prepRows[i]=ri
    ri.add=button(rowFrame,"添加/修改",440,-1,108,23,function()
        local entry=currentPrepEntry();local id=ri.id
        if not entry or not id then prepStatus:SetText("请先保存 Boss");return end
        local number=validNeed(ri.quantity:GetText())
        if not number then prepStatus:SetText("需求数量必须为 1 至 999 的整数。");return end
        appendConsumable(entry,id,number)
        prepStatus:SetText(itemName(id).."：需求已添加/修改为 "..number)
        prepRefresh()
    end)
    ri.remove=button(rowFrame,"移除",637,-1,51,23,function()
        local e=currentPrepEntry();if not e or not ri.id then return end
        prepData(e).consumables[tostring(ri.id)]=nil
        prepStatus:SetText("已移除："..itemName(ri.id));prepRefresh()
    end)
    ri.star=button(rowFrame,"☆",557,-1,40,23,function()
        if not ri.id then return end
        local d=getDB();if type(d.favorites)~="table" then d.favorites={} end
        local key=tostring(ri.id)
        d.favorites[key]=not d.favorites[key]
        prepRefresh()
    end)
end
button(prep,"上一页",8,-321,82,23,function() prepPage=math.max(1,prepPage-1);prepRefresh() end)
button(prep,"下一页",98,-321,82,23,function() prepPage=prepPage+1;prepRefresh() end)
-- A single search/result list is the sole place for editing/removing configured items.
-- Gear configuration has a distinct section, not a shared consumable action.
label(prep,10,-375,"装备方案：仅当前角色 · 保存不会立即换装", "GameFontHighlightSmall",460)
local outfitPage=1
local outfitLabels={}
local refreshOutfits
local outfitNameModal=CreateFrame("Frame",nil,UIParent)
outfitNameModal:SetWidth(410);outfitNameModal:SetHeight(200)
outfitNameModal:SetPoint("CENTER",UIParent,"CENTER",0,100)
outfitNameModal:SetFrameStrata("FULLSCREEN_DIALOG")
outfitNameModal:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
label(outfitNameModal,24,-22,"保存当前穿戴为 Boss 套装","GameFontNormalLarge",360)
label(outfitNameModal,24,-61,"方案名称（最多 12 个字）","GameFontHighlightSmall",360)
local outfitNameInput=edit(outfitNameModal,28,-90,345,25)
outfitNameInput:SetMaxLetters(48)
-- Lua 5.0 has no built-in UTF-8 length function. Count characters, not bytes.
local function gearNameLength(value)
    local count=0
    local i=1
    local bytes=string.len(value)
    while i<=bytes do
        local b=string.byte(value,i)
        local width=1
        if b>=240 and b<=247 then width=4
        elseif b>=224 and b<=239 then width=3
        elseif b>=192 and b<=223 then width=2 end
        if i+width-1>bytes then return nil end
        if width>1 then
            for j=i+1,i+width-1 do
                local c=string.byte(value,j)
                if c<128 or c>191 then return nil end
            end
        elseif b>=128 then return nil end
        count=count+1
        i=i+width
    end
    return count
end
local outfitNameError=label(outfitNameModal,28,-122,"","GameFontHighlightSmall",345)
button(outfitNameModal,"保存",55,-142,125,25,function()
    local e=currentPrepEntry()
    if not e then outfitNameModal:Hide();return end
    local name=trim(outfitNameInput:GetText() or "")
    local length=gearNameLength(name)
    if not length or length<1 or length>12 then
        outfitNameError:SetText("|cffff5555请输入 1–12 个字的套装名称|r")
        return
    end
    outfitNameError:SetText("")
    local slots=equipSnapshot()
    local slotNames={}
    for slot,link in pairs(slots) do slotNames[slot]=gearDisplayName(link,slot) end
    if not next(slots) then prepStatus:SetText("当前未读取到已穿戴装备，不能保存");return end
    local sets=outfitSets(e)
    local function commit()
        sets[name]={name=name,slots=slots,slotNames=slotNames}
        outfitNameModal:Hide()
        outfitPage=1;refreshOutfits()
    end
    if sets[name] and confirmModal then
        outfitNameModal:Hide()
        confirmModal("覆盖装备方案","确定覆盖「"..name.."」吗？",commit)
    else commit() end
end)
button(outfitNameModal,"取消",233,-142,125,25,function() outfitNameModal:Hide() end)
outfitNameModal:Hide()
button(prep,"保存穿戴为 Boss 套装",466,-372,197,25,function()
    local e=currentPrepEntry();if not e then prepStatus:SetText("请先保存 Boss");return end
    outfitNameInput:SetText("");outfitNameError:SetText("");outfitNameModal:Show();outfitNameInput:SetFocus()
end)
for i=1,3 do
    local index=i
    local lbl=label(prep,13,-414-(i-1)*35,"", "GameFontHighlightSmall",410)
    local del=button(prep,"移除",553,-408-(i-1)*35,105,25,function()
        local e=currentPrepEntry();if not e then return end
        local names=sortedSetNames(e)
        local name=names[(outfitPage-1)*3+index]
        if name and confirmModal then confirmModal("移除装备方案","只删除「"..name.."」的保存记录，不改变人物装备？",function()
            local d=prepData(e)
            local sets=outfitSets(e);sets[name]=nil
            -- The old snapshot has already migrated; never recreate a removed legacy set.
            d.gearMigrated=true;d.gear=nil
            refreshOutfits()
        end) end
    end)
    outfitLabels[i]={text=lbl,remove=del}
end
local outfitPager=label(prep,300,-528,"","GameFontHighlightSmall",120)
button(prep,"上一组",12,-523,90,24,function() outfitPage=math.max(1,outfitPage-1);refreshOutfits() end)
button(prep,"下一组",116,-523,90,24,function() outfitPage=outfitPage+1;refreshOutfits() end)
refreshOutfits=function()
    local e=currentPrepEntry()
    local names=e and sortedSetNames(e) or {}
    local total=math.max(1,math.ceil(table.getn(names)/3))
    if outfitPage>total then outfitPage=total end
    outfitPager:SetText(outfitPage.." / "..total.." 页")
    for i=1,3 do
        local name=names[(outfitPage-1)*3+i]
        if name then
            outfitLabels[i].text:SetText(name)
            outfitLabels[i].remove:Show()
        else outfitLabels[i].text:SetText("");outfitLabels[i].remove:Hide() end
    end
end

local prepOpen=false
local function togglePrep()
    if not prepOpen and (not selectedEntry or not bossInfo(selectedEntry) or dirty) then
        setStatus("请先保存当前 Boss，再配置药剂与装备。")
        return
    end
    prepOpen=not prepOpen
    if prepOpen then
        frame:SetHeight(715);manager:Hide();status:Hide();prep:Show();prepRefresh();refreshOutfits()
    else prep:Hide();frame:SetHeight(510);manager:Show();status:Show();refreshList() end
end
prepButton=button(manager,"药剂与装备",235,-363,225,27,function() togglePrep() end)
updateDirtyLabel()
button(prep,"返回管理",607,-5,90,25,function() togglePrep() end)
-- Shared confirmation window for delete, unsaved navigation and closing.
local modal=CreateFrame("Frame",nil,UIParent)
modal:SetWidth(420);modal:SetHeight(185)
modal:SetPoint("CENTER",UIParent,"CENTER",0,100)
modal:SetFrameStrata("FULLSCREEN_DIALOG")
modal:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
local modalTitle=modal:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
modalTitle:SetPoint("TOP",modal,"TOP",0,-24)
local modalBody=modal:CreateFontString(nil,"OVERLAY","GameFontHighlight")
modalBody:SetPoint("TOP",modal,"TOP",0,-70);modalBody:SetWidth(360);modalBody:SetJustifyH("CENTER")
local modalAction=nil
local yes=CreateFrame("Button",nil,modal,"UIPanelButtonTemplate")
yes:SetPoint("BOTTOMRIGHT",modal,"BOTTOM",-8,25);yes:SetWidth(100);yes:SetHeight(27);yes:SetText("确认")
yes:SetScript("OnClick",function() local action=modalAction;modalAction=nil;modal:Hide();if action then action() end end)
local no=CreateFrame("Button",nil,modal,"UIPanelButtonTemplate")
no:SetPoint("BOTTOMLEFT",modal,"BOTTOM",8,25);no:SetWidth(100);no:SetHeight(27);no:SetText("取消")
no:SetScript("OnClick",function() modalAction=nil;modal:Hide() end)
modal:Hide()
confirmModal=function(titleText,bodyText,fn)
    modalTitle:SetText(titleText);modalBody:SetText(bodyText)
    modalAction=fn;modal:Show()
end
local function safeClose()
    if modal:IsShown() then modal:Hide();modalAction=nil;return end
    askDiscard(function() frame:Hide() end)
end
TaiYangBossMemoClose=safeClose
local closeButton=CreateFrame("Button",nil,frame,"UIPanelCloseButton")
closeButton:SetPoint("TOPRIGHT",frame,"TOPRIGHT",-5,-5)
closeButton:SetScript("OnClick",safeClose)
-- WoW 1.12 Escape handling is left to the default UI; the X button checks unsaved data.
local popup=CreateFrame("Frame","TaiYangBossMemoAlert",UIParent)
popup:SetWidth(548);popup:SetHeight(344)
local function restoreAlertPosition()
    popup:ClearAllPoints()
    local p=getDB().alertPos
    if p and type(p.x)=="number" and type(p.y)=="number" and math.abs(p.x)<5000 and math.abs(p.y)<5000 then
        popup:SetPoint("CENTER",UIParent,"CENTER",p.x,p.y)
    else popup:SetPoint("CENTER",UIParent,"CENTER",0,150) end
end
restoreAlertPosition()
popup:SetFrameStrata("FULLSCREEN_DIALOG")
popup:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
popup:SetMovable(true);popup:EnableMouse(true);popup:RegisterForDrag("LeftButton")
popup:SetScript("OnDragStart",function() this:StartMoving() end)
popup:SetScript("OnDragStop",function()
    this:StopMovingOrSizing()
    local cx,cy=this:GetCenter()
    local ux,uy=UIParent:GetCenter()
    if cx and cy and ux and uy then
        local scale=this:GetEffectiveScale()/UIParent:GetEffectiveScale()
        getDB().alertPos={x=cx*scale-ux,y=cy*scale-uy}
    end
end)
local pTitle=popup:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
pTitle:SetPoint("TOP",popup,"TOP",0,-25)
local pBody=popup:CreateFontString(nil,"OVERLAY","GameFontHighlight")
pBody:SetPoint("TOP",popup,"TOP",0,-57);pBody:SetWidth(498);pBody:SetHeight(53)
pBody:SetJustifyH("CENTER");pBody:SetJustifyV("MIDDLE")
local pClose=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
pClose:SetWidth(92);pClose:SetHeight(22)
pClose:SetPoint("BOTTOM",popup,"BOTTOM",0,14);pClose:SetText("知道了")
local activeAlert=nil
-- Two independent item columns: missing on the left, sufficient on the right.
-- Content starts immediately below the title when there is no pre-fight note.
local groupLists={{}, {}}
local groupOffsets={0,0}
local groupTitles={}
local groupRows={{},{}}
local groupNav={}
local GROUP_SIZE=5
local columnTop=-95
local function buildColumn(col)
    local baseX=(col==1) and 26 or 287
    local heading=popup:CreateFontString(nil,"OVERLAY","GameFontNormal")
    heading:SetPoint("TOPLEFT",popup,"TOPLEFT",baseX,-95)
    heading:SetWidth(235);heading:SetJustifyH("LEFT")
    groupTitles[col]=heading
    for i=1,GROUP_SIZE do
        local row=CreateFrame("Frame",nil,popup)
        row:SetPoint("TOPLEFT",popup,"TOPLEFT",baseX,-124-(i-1)*31)
        row:SetWidth(234);row:SetHeight(27)
        local icon=row:CreateTexture(nil,"ARTWORK")
        icon:SetPoint("LEFT",row,"LEFT",0,0);icon:SetWidth(23);icon:SetHeight(23)
        local name=row:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
        name:SetPoint("LEFT",row,"LEFT",29,0);name:SetWidth(137);name:SetJustifyH("LEFT")
        local number=row:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
        number:SetPoint("RIGHT",row,"RIGHT",0,0);number:SetWidth(65);number:SetJustifyH("RIGHT")
        groupRows[col][i]={frame=row,icon=icon,name=name,count=number}
    end
    local prev=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
    prev:SetWidth(54);prev:SetHeight(20)
    prev:SetPoint("BOTTOMLEFT",popup,"BOTTOMLEFT",baseX,73);prev:SetText("上翻")
    local page=popup:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    page:SetPoint("BOTTOMLEFT",popup,"BOTTOMLEFT",baseX+65,77);page:SetWidth(102);page:SetJustifyH("CENTER")
    local nextBtn=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
    nextBtn:SetWidth(54);nextBtn:SetHeight(20)
    nextBtn:SetPoint("BOTTOMLEFT",popup,"BOTTOMLEFT",baseX+180,73);nextBtn:SetText("下翻")
    groupNav[col]={prev=prev,nextBtn=nextBtn,page=page}
end
buildColumn(1);buildColumn(2)
local function itemTexture(id,stock)
    if stock[id] and stock[id].bag and type(GetContainerItemInfo)=="function" then
        local texture=GetContainerItemInfo(stock[id].bag,stock[id].slot)
        if texture then return texture end
    end
    if type(GetItemIcon)=="function" then return GetItemIcon(id) end
    if type(GetItemInfo)=="function" then local _,_,_,_,_,_,_,_,texture=GetItemInfo(id);return texture end
end
-- UI helpers intentionally live in the main file: no optional module/load dependency.
-- Only skip setter calls when the current value is identical.
local function setTextIfChanged(widget,value)
    if not widget then return end
    value=value or ""
    if widget:GetText()~=value then widget:SetText(value) end
end
local function setTextureIfChanged(texture,path)
    if not texture then return end
    if texture:GetTexture()~=path then texture:SetTexture(path) end
end
local function redrawStock()
    for col=1,2 do
        local list=groupLists[col]
        local count=table.getn(list)
        local maxOffset=math.max(0,count-GROUP_SIZE)
        if groupOffsets[col]>maxOffset then groupOffsets[col]=maxOffset end
        local title=groupTitles[col]
        title:ClearAllPoints()
        title:SetPoint("TOPLEFT",popup,"TOPLEFT",(col==1) and 26 or 287,columnTop)
        if col==1 then
            setTextIfChanged(title,"不足物品（"..count.."）")
            title:SetTextColor(1,0.25,0.25)
        else
            setTextIfChanged(title,"已达标物品（"..count.."）")
            title:SetTextColor(0.2,1,0.3)
        end
        for i=1,GROUP_SIZE do
            local r=groupRows[col][i]
            r.frame:ClearAllPoints()
            r.frame:SetPoint("TOPLEFT",popup,"TOPLEFT",(col==1) and 26 or 287,columnTop-29-(i-1)*31)
            local item=list[groupOffsets[col]+i]
            if item then
                setTextureIfChanged(r.icon,item.icon or "Interface\\Icons\\INV_Misc_QuestionMark")
                setTextIfChanged(r.name,item.name)
                if col==1 then r.name:SetTextColor(1,0.75,0.75);r.count:SetTextColor(1,0.25,0.25)
                else r.name:SetTextColor(1,1,1);r.count:SetTextColor(0.15,1,0.2) end
                setTextIfChanged(r.count,item.have.." / "..item.need)
                r.frame:Show()
            else r.frame:Hide() end
        end
        local nav=groupNav[col]
        if count>GROUP_SIZE then
            setTextIfChanged(nav.page,(groupOffsets[col]+1).."-"..math.min(count,groupOffsets[col]+GROUP_SIZE).." / "..count)
            nav.prev:Show();nav.nextBtn:Show();nav.page:Show()
        else nav.prev:Hide();nav.nextBtn:Hide();nav.page:Hide() end
    end
end
for col=1,2 do
    local c=col
    groupNav[col].prev:SetScript("OnClick",function() groupOffsets[c]=math.max(0,groupOffsets[c]-GROUP_SIZE);redrawStock() end)
    groupNav[col].nextBtn:SetScript("OnClick",function() groupOffsets[c]=math.min(math.max(0,table.getn(groupLists[c])-GROUP_SIZE),groupOffsets[c]+GROUP_SIZE);redrawStock() end)
end
popup:EnableMouseWheel(true)
popup:SetScript("OnMouseWheel",function()
    -- Use the cursor's column when possible; left column is the fallback.
    local col=1
    if type(GetCursorPosition)=="function" then
        local x=GetCursorPosition()
        local left=popup:GetLeft()
        local scale=popup:GetEffectiveScale()
        if x and left and scale and x/scale-left>popup:GetWidth()/2 then col=2 end
    end
    groupOffsets[col]=math.max(0,math.min(math.max(0,table.getn(groupLists[col])-GROUP_SIZE),groupOffsets[col]-arg1))
    redrawStock()
end)
local refreshActiveStock
local outfitHeading=popup:CreateFontString(nil,"OVERLAY","GameFontNormal")
outfitHeading:SetText("一键换装")
outfitHeading:SetPoint("BOTTOMLEFT",popup,"BOTTOMLEFT",25,146)
local alertSetPage=1
local alertSetButtons={}
local alertSetPager=popup:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
alertSetPager:SetPoint("BOTTOM",popup,"BOTTOM",0,64)
local function refreshAlertOutfits(available)
    local names=activeAlert and sortedSetNames(activeAlert.entry) or {}
    if table.getn(names)>0 and not available then
        local _,fresh=inventory(true)
        available=fresh or {}
    end
    local sets=activeAlert and outfitSets(activeAlert.entry) or nil
    local total=math.max(1,math.ceil(table.getn(names)/4))
    if alertSetPage>total then alertSetPage=total end
    for i=1,4 do
        local name=names[(alertSetPage-1)*4+i]
        local b=alertSetButtons[i]
        if name then
            local missing=setMissing(sets[name].slots,available)
            setTextIfChanged(b,name..(missing==0 and "（装备齐全）" or "（缺失"..missing.."件）"))
            b.setName=name
            b:Show()
        else b.setName=nil;b:Hide() end
    end
    alertSetPager:SetText(table.getn(names)>4 and (alertSetPage.." / "..total) or "")
    if table.getn(names)>0 then outfitHeading:Show() else outfitHeading:Hide() end
end
for i=1,4 do
    local n=i
    local b=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
    b:SetWidth(243);b:SetHeight(25)
    b:SetPoint("BOTTOMLEFT",popup,"BOTTOMLEFT",(i%2==1) and 25 or 281,(i<=2) and 111 or 78)
    b:SetScript("OnClick",function()
        if activeAlert and this.setName then
            local _,message=equipOutfit(activeAlert.entry,this.setName)
            say(message);refreshAlertOutfits()
        end
    end)
    alertSetButtons[n]=b
end
local alertPrev=button(popup,"<",26,-999,30,21,function() alertSetPage=math.max(1,alertSetPage-1);refreshAlertOutfits() end)
local alertNext=button(popup,">",487,-999,30,21,function() alertSetPage=alertSetPage+1;refreshAlertOutfits() end)
alertPrev:ClearAllPoints();alertPrev:SetPoint("BOTTOMLEFT",popup,"BOTTOMLEFT",22,62)
alertNext:ClearAllPoints();alertNext:SetPoint("BOTTOMRIGHT",popup,"BOTTOMRIGHT",-22,62)
local function updateAlertGear()
    local names=activeAlert and sortedSetNames(activeAlert.entry) or {}
    if table.getn(names)>0 then
        popup:SetHeight(activeAlert and trim(activeAlert.body or "")~="" and 522 or 466)
    end
    refreshAlertOutfits()
    if table.getn(names)>4 then alertPrev:Show();alertNext:Show() else alertPrev:Hide();alertNext:Hide() end
    if table.getn(names)==0 then
        popup:SetHeight(activeAlert and trim(activeAlert.body or "")~="" and 400 or 344)
    end
end

local queue={}
local alertRemaining=0
local QUEUE_LIMIT=20
local alertClose=CreateFrame("Button",nil,popup,"UIPanelCloseButton")
alertClose:SetPoint("TOPRIGHT",popup,"TOPRIGHT",-4,-4)
local queueKeys={}
local function playerIsDead()
    -- Vanilla WoW 1.12 native API; do not consume a reminder when dead/ghost.
    return type(UnitIsDeadOrGhost)=="function" and not not UnitIsDeadOrGhost("player")
end
local renderCurrentAlert
local function displayNext()
    -- Queued reminders must still refer to enabled, existing notes when shown.
    while table.getn(queue)>0 do
        local first=queue[1]
        local current=note(first.entry)
        if getDB().enabled and getDB().before and current and (trim(current.before)~="" or (prepData(first.entry) and next(prepData(first.entry).consumables)) or (prepData(first.entry) and table.getn(sortedSetNames(first.entry))>0)) then break end
        table.remove(queue,1)
        queueKeys[first.key]=nil
        if shown[first.guid] then shown[first.guid].before=nil end
    end
    if table.getn(queue)==0 then activeAlert=nil;popup:Hide();return end
    -- Do not display a stale reminder while the player is dead.
    -- Dropped pending reminders are NOT marked as delivered.
    while table.getn(queue)>0 and playerIsDead() do
        local discarded=table.remove(queue,1)
        queueKeys[discarded.key]=nil
        if shown[discarded.guid] then shown[discarded.guid].before=nil end
    end
    if table.getn(queue)==0 then activeAlert=nil;popup:Hide();return end
    activeAlert=table.remove(queue,1)
    alertRemaining=getDB().alertSeconds
    queueKeys[activeAlert.key]=nil
    renderCurrentAlert()
end
renderCurrentAlert=function()
    if not activeAlert then return end
    pTitle:SetText(activeAlert.title..(not activeAlert.preview and table.getn(queue)>0 and " (待处理 "..table.getn(queue)..")" or ""))
    local body=trim(activeAlert.body or "")
    if body=="" then
        pBody:Hide()
        popup:SetHeight(344)
        columnTop=-89
    else
        pBody:SetText(body)
        pBody:Show()
        popup:SetHeight(400)
        columnTop=-145
    end
    local d=prepData(activeAlert.entry)
    groupOffsets[1]=0;groupOffsets[2]=0
    refreshActiveStock()
    alertSetPage=1;updateAlertGear()
    popup:Show()
end
local function showBossPreparation(key)
    local b=bossInfo(key)
    local n=note(key)
    if not b or not n then return end
    -- Preview does not create a GUID, consume a reminder, or enter the queue.
    activeAlert={key="preview:"..tostring(key),entry=key,title="战前提醒 · "..(b.name or "Boss"),body=n.before or "",preview=true}
    alertRemaining=0
    renderCurrentAlert()
end
refreshActiveStock=function()
    groupLists[1]={};groupLists[2]={}
    if not activeAlert then return end
    local d=prepData(activeAlert.entry)
    local stock,available=inventory(true)
    if d then for key,need in pairs(d.consumables) do
        local id=tonumber(key)
        if id and type(need)=="number" then
            local have=stock[id] and stock[id].count or 0
            local item={id=id,name=itemName(id),have=have,need=need,icon=itemTexture(id,stock)}
            if have<need then table.insert(groupLists[1],item)
            else table.insert(groupLists[2],item) end
        end
    end end
    table.sort(groupLists[1],function(a,b)
        local gapA=a.need-a.have
        local gapB=b.need-b.have
        if gapA~=gapB then return gapA>gapB end
        return a.id<b.id
    end)
    table.sort(groupLists[2],function(a,b) return a.id<b.id end)
    redrawStock()
    if activeAlert then refreshAlertOutfits(available) end
end
pClose:SetScript("OnClick",function() activeAlert=nil;displayNext() end)
alertClose:SetScript("OnClick",function() activeAlert=nil;displayNext() end)
local alertTicker=CreateFrame("Frame",nil,popup)
alertTicker:SetScript("OnUpdate",function()
    if activeAlert and not activeAlert.preview and getDB().alertSeconds>0 then
        alertRemaining=alertRemaining-arg1
        if alertRemaining<=0 then activeAlert=nil;displayNext() end
    end
end)
popup:Hide()
local function alert(entry,guid)
    local d=getDB()
    if not d.enabled then return false end
    if not d.before or playerIsDead() then return false end
    local item=note(entry)
    if not item then return false end
    local content=trim(item.before)
    if content=="" and not (prepData(entry) and (next(prepData(entry).consumables) or table.getn(sortedSetNames(entry))>0)) then return false end
    local track=tracks[guid]
    local cycle=track and track.attempt or 1
    local key=tostring(guid)..":"..tostring(cycle)..":before"
    if queueKeys[key] or (activeAlert and activeAlert.key==key) then return true end
    if table.getn(queue)>=QUEUE_LIMIT then
        say("提醒队列已满（20 条），跳过最新提醒。")
        return false
    end
    local name=(item.name and item.name~="") and item.name or "Boss "..tostring(entry)
    local itemToShow={key=key,guid=guid,entry=entry,phase="before",title="战前提醒 · "..name,body=content}
    table.insert(queue,itemToShow);queueKeys[key]=true
    if not activeAlert then displayNext() end
    say("战前："..name)
    return true
end

-- V0.6.0-A: only session-local GUID tracking. SavedVariables hold notes/settings.
-- Track observed Boss combat state only; no background death polling.
local MAX_TRACKS=16
local TRACK_TTL_MS=300000
local function milliseconds() return math.floor(GetTime()*1000) end
local function countTracks()
    local n=0
    for guid,t in pairs(tracks) do n=n+1 end
    return n
end
local function resetSession()
    -- A map load starts a new local observation session. GUIDs may be reused
    -- by MaNGOS after an instance reset; never carry dedup across world loads.
    tracks={};shown={};timer=0
    pendingNewSelection=false
    -- Invalidate queued and currently displayed reminders from the old map.
    queue={};queueKeys={};activeAlert=nil;alertRemaining=0
    popup:Hide()

end
local function removeOldest()
    local oldestGuid,oldestTime=nil,nil
    for guid,t in pairs(tracks) do
        if not oldestTime or t.firstSeen<oldestTime then
            oldestGuid=guid;oldestTime=t.firstSeen
        end
    end
    if oldestGuid then tracks[oldestGuid]=nil end
end
local function addTrack(guid,entry)
    if type(guid)~="string" or not entry or not note(entry) then return end
    local t=tracks[guid]
    if t then t.lastSeen=milliseconds();t.invisibleSince=nil;return end
    if countTracks()>=MAX_TRACKS then removeOldest() end
    local now=milliseconds()
    tracks[guid]={entry=entry,firstSeen=now,lastSeen=now,invisibleSince=nil,attempt=1,wasCombat=false,offCount=0,onCount=0,resetReady=false}
end
local function trigger(guid,entry)
    if not note(entry) then return end
    -- A confirmed reset must not be mistaken for a new pull by stale M4 events.
    local track=tracks[guid]
    -- resetReady is consumed ONLY on a fresh, living target selection.
    if track and track.resetReady then return end
    if playerIsDead() then return end
    if not shown[guid] then shown[guid]={} end
    if shown[guid].before then return end
    if alert(entry,guid) then shown[guid].before=true end
end
-- Re-arm only from confirmed matched-target combat observations. Guid-only probes
-- generally have inCombatKnown=false, which must never count as a reset.
local function observeCombat(guid,entry,boss)
    local t=tracks[guid]
    local data=boss and boss.data
    if not t or not boss or not boss.ok or type(data)~="table" or data.inCombatKnown~=true or data.deadKnown~=true or data.dead~=false then return end
    if data.inCombat==true then
        t.offCount=0
        t.onCount=t.onCount+1
        -- Re-entering combat does not generate a pre-fight reminder: too late.
        -- A confirmed reset waits for a fresh PLAYER_TARGET_CHANGED selection.
        if t.onCount>=1 then t.wasCombat=true end
    elseif data.inCombat==false then
        t.onCount=0
        if t.wasCombat and getDB().realertAfterReset then
            t.offCount=t.offCount+1
            if t.offCount>=2 then t.resetReady=true end
        end
    end
end
local function matchingBossKey(name,zone,entry)
    local match=nil
    for key,boss in pairs(getAccount().bosses) do
        if type(boss)=="table" and normalizeBossName(boss.name)==normalizeBossName(name)
            and trim(boss.zone or "")==zone and note(key) then
            -- Entry helps resolve duplicate records, but is not mandatory.
            if entry and boss.entry and tonumber(boss.entry)==tonumber(entry) then return key end
            if match then return nil end
            match=key
        end
    end
    return match
end
local function observe()
    if not getDB().enabled then return end
    local newSelection=pendingNewSelection
    pendingNewSelection=false
    if type(UnitExists)~="function" or not UnitExists("target") then return end
    if type(UnitIsPlayer)~="function" or type(UnitPlayerControlled)~="function" then return end
    if UnitIsPlayer("target") or UnitPlayerControlled("target") then return end
    if type(UnitName)~="function" then return end
    local name=UnitName("target")
    if not name or name=="" then return end
    local identity=api("Unit.Identity","target")
    local data=identity and identity.ok and identity.data or nil
    local entry=data and data.entry
    local key=matchingBossKey(name,currentZoneName(),entry)
    if not key then return end
    local state=api("Boss.State","target")
    local st=state and state.ok and type(state.data)=="table" and state.data or nil
    -- Fail closed when the unit's alive/combat state cannot be determined.
    local isDead,combat
    if st and st.deadKnown==true then isDead=st.dead end
    if st and st.inCombatKnown==true then combat=st.inCombat end
    if isDead==nil and type(UnitIsDeadOrGhost)=="function" then isDead=not not UnitIsDeadOrGhost("target") end
    if combat==nil and type(UnitAffectingCombat)=="function" then combat=not not UnitAffectingCombat("target") end
    if isDead~=false or combat~=false or playerIsDead() then return end
    local guid=data and data.guid
    if type(guid)~="string" or guid=="" then
        -- In clients lacking GUID data, scope the fallback to the current map
        -- and target name; never use the numeric NPC Entry as a fake GUID.
        guid="name:"..currentZoneName()..":"..name
    end
    addTrack(guid,key)
    if st and st.guid==guid and st.deadKnown==true and st.inCombatKnown==true then
        observeCombat(guid,key,state)
        local tracked=tracks[guid]
        if newSelection and tracked and tracked.resetReady and getDB().realertAfterReset then
            tracked.attempt=tracked.attempt+1
            tracked.resetReady=false;tracked.wasCombat=false;tracked.offCount=0;tracked.onCount=0
            if shown[guid] then shown[guid].before=nil end
            trigger(guid,key)
        end
    end
    -- Both hostile and friendly NPCs are eligible; never wait until engage.
    trigger(guid,key)
end
-- Maintenance is independent of target matching: an empty/invalid target must
-- not prevent stale track eviction. Keep shown[] until a world-session reset,
-- otherwise evicting a track could cause duplicate pre-fight reminders.
local function maintainTracks()
    local now=milliseconds()
    for g,t in pairs(tracks) do
        if now-t.lastSeen>TRACK_TTL_MS then tracks[g]=nil end
    end
end
local mapController={}
do
-- Map-entry preparation summary; follows MoonMarker's area-event approach.
-- Only user-saved Boss.zone records are used; AtlasLoot is for edit-time suggestions.
local mapWindow=CreateFrame("Frame",nil,UIParent)
mapWindow:SetWidth(410);mapWindow:SetHeight(168)
mapWindow:SetPoint("CENTER",UIParent,"CENTER",0,115)
mapWindow:SetFrameStrata("DIALOG")
mapWindow:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
mapWindow:SetMovable(true);mapWindow:EnableMouse(true);mapWindow:RegisterForDrag("LeftButton")
mapWindow:SetScript("OnDragStart",function() this:StartMoving() end)
mapWindow:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
local mapHeading=label(mapWindow,16,-15,"地图 · 战前准备","GameFontNormalLarge",370)
-- Header labels use the same X offsets as the row columns; do not align with spaces.
local mapHeaderName=label(mapWindow,18,-42,"Boss 名称","GameFontHighlightSmall",171)
local mapHeaderPot=label(mapWindow,197,-42,"药剂","GameFontHighlightSmall",75)
local mapHeaderGear=label(mapWindow,287,-42,"装备","GameFontHighlightSmall",85)
local mapScroll=CreateFrame("ScrollFrame",nil,mapWindow)
mapScroll:SetPoint("TOPLEFT",mapWindow,"TOPLEFT",13,-65)
mapScroll:SetWidth(382);mapScroll:EnableMouseWheel(true)
local mapContent=CreateFrame("Frame",nil,mapScroll)
mapContent:SetWidth(373);mapContent:SetHeight(30)
mapScroll:SetScrollChild(mapContent)
local mapRows={}
local mapKeys={}
local mapSelected=nil
local mapCurrentZone=nil
local mapPending=false
local mapDelay=0
local mapWindowShownZone=nil
local function mapEntries(zone)
    local keys={}
    for key,boss in pairs(getAccount().bosses) do
        if type(boss)=="table" and trim(boss.zone or "")==zone and note(key) then
            table.insert(keys,key)
        end
    end
    table.sort(keys,function(a,b)
        local aa=bossInfo(a);local bb=bossInfo(b)
        return (aa and aa.name or "")<(bb and bb.name or "")
    end)
    return keys
end
local mapClose=button(mapWindow,"知道了",211,-999,172,25,function() mapWindow:Hide() end)
local mapOpen=button(mapWindow,"查看准备",24,-999,172,25,function()
    if not mapSelected then return end
    mapWindow:Hide()
    showBossPreparation(mapSelected)
end)
local function updateMapSelection()
    for j=1,table.getn(mapRows) do
        local r=mapRows[j]
        if r.key then
            local selected=(r.key==mapSelected)
            local boss=bossInfo(r.key)
            r.name:SetText((selected and "▶ " or "   ")..(boss and boss.name or "Boss"))
            if selected then r.highlight:Show();r.border:Show() else r.highlight:Hide();r.border:Hide() end
        end
    end
end
local function mapRow(i)
    if mapRows[i] then return mapRows[i] end
    local row=CreateFrame("Button",nil,mapContent)
    row:SetWidth(373);row:SetHeight(26)
    row:SetPoint("TOPLEFT",mapContent,"TOPLEFT",0,-(i-1)*27)
    local highlight=row:CreateTexture(nil,"BACKGROUND")
    highlight:SetAllPoints(row)
    highlight:SetTexture("Interface\\Tooltips\\UI-Tooltip-Background")
    highlight:SetVertexColor(0.85,0.65,0.1,0.23)
    highlight:Hide()
    -- Four 2px gold edges remain legible over dark dungeon backgrounds.
    local selectionBorder=CreateFrame("Frame",nil,row)
    selectionBorder:SetAllPoints(row)
    selectionBorder:SetFrameLevel(row:GetFrameLevel()+2)
    local function goldEdge(point1,point2,width,height)
        local edge=selectionBorder:CreateTexture(nil,"OVERLAY")
        edge:SetTexture("Interface\\Tooltips\\UI-Tooltip-Background")
        edge:SetVertexColor(1,0.78,0.12,1)
        edge:SetPoint(point1,selectionBorder,point1,0,0)
        if point2 then edge:SetPoint(point2,selectionBorder,point2,0,0) end
        if width then edge:SetWidth(width) end
        if height then edge:SetHeight(height) end
    end
    goldEdge("TOPLEFT","TOPRIGHT",nil,2)
    goldEdge("BOTTOMLEFT","BOTTOMRIGHT",nil,2)
    goldEdge("TOPLEFT","BOTTOMLEFT",2,nil)
    goldEdge("TOPRIGHT","BOTTOMRIGHT",2,nil)
    selectionBorder:Hide()
    local name=row:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    name:SetPoint("LEFT",row,"LEFT",5,0);name:SetWidth(171);name:SetJustifyH("LEFT")
    local pots=row:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    pots:SetPoint("LEFT",row,"LEFT",184,0);pots:SetWidth(75);pots:SetJustifyH("LEFT")
    local gear=row:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    gear:SetPoint("LEFT",row,"LEFT",274,0);gear:SetWidth(85);gear:SetJustifyH("LEFT")
    row:SetScript("OnClick",function()
        mapSelected=mapKeys[i]
        updateMapSelection()
    end)
    mapRows[i]={frame=row,name=name,pots=pots,gear=gear,highlight=highlight,border=selectionBorder}
    return mapRows[i]
end
local function showMapWindow(zone,keys)
    mapKeys=keys;mapSelected=keys[1]
    setTextIfChanged(mapHeading,zone.." · 战前准备（"..table.getn(keys).."）")
    -- Build one snapshot for all Boss rows and all named outfits.
    local stock,available=inventory(true)
    local count=table.getn(keys)
    for i=1,count do
        local key=keys[i]
        local d=prepData(key)
        local needs=0;local insufficient=false
        if d and type(d.consumables)=="table" then
            for id,required in pairs(d.consumables) do
                if type(required)=="number" and required>0 then
                    needs=needs+1
                    local item=stock[tonumber(id)]
                    if not item or item.count<required then insufficient=true end
                end
            end
        end
        local potionStatus=needs==0 and "未配置" or (insufficient and "不足" or "正常")
        local sets=outfitSets(key)
        local hasSet=false;local allComplete=true
        if sets then for _,set in pairs(sets) do
            if type(set)=="table" and type(set.slots)=="table" then
                hasSet=true
                if setMissing(set.slots,available)>0 then allComplete=false end
            end
        end end
        local gearStatus=not hasSet and "未配置" or (allComplete and "正常" or "缺失")
        local r=mapRow(i)
        r.key=key;r.name:SetText((i==1 and "▶ " or "  ")..(bossInfo(key).name or "Boss"))
        r.pots:SetText(potionStatus);r.gear:SetText(gearStatus)
        r.pots:SetTextColor(insufficient and 1 or 0.4,insufficient and 0.25 or 1,needs==0 and 0.5 or 0.4)
        r.gear:SetTextColor(hasSet and not allComplete and 1 or 0.4,hasSet and not allComplete and 0.25 or 1,hasSet and 0.4 or 0.5)
        r.frame:Show()
    end
    for i=count+1,table.getn(mapRows) do mapRows[i].key=nil;mapRows[i].frame:Hide() end
    updateMapSelection()
    local screenHeight=UIParent:GetHeight() or 768
    local visible=math.min(count*27,math.max(110,screenHeight-250))
    mapScroll:SetHeight(visible)
    mapContent:SetHeight(math.max(visible,count*27))
    mapScroll:SetVerticalScroll(0)
    mapWindow:SetHeight(visible+119)
    mapOpen:ClearAllPoints();mapOpen:SetPoint("BOTTOMLEFT",mapWindow,"BOTTOMLEFT",24,17)
    mapClose:ClearAllPoints();mapClose:SetPoint("BOTTOMRIGHT",mapWindow,"BOTTOMRIGHT",-24,17)
    mapWindow:Show()
end
mapScroll:SetScript("OnMouseWheel",function()
    local old=mapScroll:GetVerticalScroll()
    local maximum=math.max(0,mapContent:GetHeight()-mapScroll:GetHeight())
    local nextValue=math.max(0,math.min(maximum,old-arg1*54))
    mapScroll:SetVerticalScroll(nextValue)
end)
mapWindow:Hide()
local function scheduleMapReminder()
    if not getDB().mapReminder then mapPending=false;mapWindow:Hide();return end
    mapPending=true;mapDelay=0
end
local function checkMapReminder()
    local zone=currentZoneName()
    if zone=="" then return end
    if zone~=mapCurrentZone then
        mapCurrentZone=zone
        mapWindowShownZone=nil
        mapWindow:Hide()
    end
    if not getDB().mapReminder or mapWindowShownZone==zone then return end
    local keys=mapEntries(zone)
    if table.getn(keys)==0 then return end
    mapWindowShownZone=zone
    showMapWindow(zone,keys)
end
mapController.schedule=scheduleMapReminder
mapController.tick=function(dt)
    if mapPending then
        mapDelay=mapDelay+dt
        if mapDelay>=0.7 then mapPending=false;checkMapReminder() end
    end
end
mapController.reset=function()
    mapPending=false;mapCurrentZone=nil;mapWindowShownZone=nil;mapWindow:Hide()
end
mapController.disable=function()
    mapPending=false;mapWindow:Hide()
end
end -- isolate map UI locals from WoW Lua 5.0's 200-local function limit
local driver=CreateFrame("Frame")
driver:RegisterEvent("VARIABLES_LOADED")
driver:RegisterEvent("BAG_UPDATE")
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("PLAYER_LEAVING_WORLD")
driver:RegisterEvent("PLAYER_LOGOUT")
driver:RegisterEvent("PLAYER_TARGET_CHANGED")
driver:RegisterEvent("ZONE_CHANGED_NEW_AREA")
driver:RegisterEvent("ZONE_CHANGED")
driver:RegisterEvent("ZONE_CHANGED_INDOORS")
-- BAG_UPDATE can fire several times for one inventory action. Merge these
-- bursts into one redraw without delaying the target-change reminder.
local bagRefreshPending=false
local bagRefreshElapsed=0
local BAG_REFRESH_DELAY=0.15
local maintenanceElapsed=0
local MAINTENANCE_INTERVAL=10
local function clearPendingBagRefresh()
    bagRefreshPending=false
    bagRefreshElapsed=0
end
driver:SetScript("OnEvent",function()
    if event=="BAG_UPDATE" then
        if activeAlert then bagRefreshPending=true;bagRefreshElapsed=0 end
        return
    end
    if event=="VARIABLES_LOADED" then
        getDB()
        resetSession()
        clearPendingBagRefresh()
        maintenanceElapsed=0
    elseif event=="PLAYER_ENTERING_WORLD" or event=="PLAYER_LEAVING_WORLD" then
        -- PLAYER_ENTERING_WORLD runs after map/instance loading on WoW 1.12.
        -- The leaving event also prevents a previous map's alert from lingering.
        resetSession()
        clearPendingBagRefresh()
        maintenanceElapsed=0
        if event=="PLAYER_ENTERING_WORLD" then mapController.schedule()
        else mapController.reset() end
    elseif event=="ZONE_CHANGED_NEW_AREA" or event=="ZONE_CHANGED" or event=="ZONE_CHANGED_INDOORS" then
        mapController.schedule()
    elseif event=="PLAYER_TARGET_CHANGED" then
        pendingNewSelection=true
        -- Act on target selection promptly; do not wait for the next polling tick.
        observe()
    elseif event=="PLAYER_LOGOUT" then
        -- Never persist GUIDs, timestamps or pop-up dedup state.
        resetSession()
        clearPendingBagRefresh()
    end
end)
driver:SetScript("OnUpdate",function()
    mapController.tick(arg1)
    -- Keep maintenance independent from the active target and the global
    -- reminder toggle; saved notes and gear are never touched here.
    maintenanceElapsed=maintenanceElapsed+arg1
    if maintenanceElapsed>=MAINTENANCE_INTERVAL then
        maintenanceElapsed=maintenanceElapsed-MAINTENANCE_INTERVAL
        maintainTracks()
    end
    if bagRefreshPending then
        if not activeAlert then clearPendingBagRefresh()
        else
            bagRefreshElapsed=bagRefreshElapsed+arg1
            if bagRefreshElapsed>=BAG_REFRESH_DELAY then
                clearPendingBagRefresh()
                refreshActiveStock()
            end
        end
    end
    local cfg=getDB()
    if not cfg.enabled then return end
    timer=timer+arg1
    if timer>=cfg.pollInterval then
        -- Preserve the remaining fraction instead of accumulating drift.
        timer=timer-cfg.pollInterval
        observe()
    end
end)
-- All settings are in the Chinese UI; /bossmemo only opens the window.
local settings=CreateFrame("Frame",nil,frame)
settings:SetPoint("TOPLEFT",frame,"TOPLEFT",27,-68)
settings:SetWidth(700);settings:SetHeight(418)
settings:Hide()
label(settings,5,-8,"① 提醒控制","GameFontNormalLarge")
local function toggle(y,text,key)
    local c=CreateFrame("CheckButton",nil,settings,"UICheckButtonTemplate")
    c:SetPoint("TOPLEFT",settings,"TOPLEFT",8,y)
    label(settings,45,y-10,text,"GameFontHighlight")
    c:SetScript("OnClick",function() getDB()[key]=this:GetChecked() and true or false end)
    return c
end
local enabledAll=toggle(-49,"开启自动提醒","enabled")
local beforeAll=toggle(-90,"开启战前提醒","before")
local resetAll=toggle(-134,"Boss 重置后重新选中即可提醒（需确认脱战）","realertAfterReset")
local mapAll=toggle(-170,"开启地图战前提醒（进入已配置地图时弹窗）","mapReminder")
mapAll:SetScript("OnClick",function()
    getDB().mapReminder=this:GetChecked() and true or false
    if getDB().mapReminder then mapController.schedule()
    else mapController.disable() end
end)
label(settings,10,-216,"② Boss 状态检测间隔（秒）","GameFontNormalLarge")
local rates={0.2,0.5,1,2}
local rateButtons={}
local function updateRate()
    local d=getDB()
    for i=1,4 do
        rateButtons[i]:SetText((d.pollInterval==rates[i] and "● " or "○ ")..tostring(rates[i]).." 秒")
    end
end
for i=1,4 do
    local n=rates[i]
    rateButtons[i]=button(settings,"",10+(i-1)*112,-241,103,27,function()
        getDB().pollInterval=n;timer=0;updateRate()
    end)
end
label(settings,10,-291,"③ 提醒自动关闭","GameFontNormalLarge")
local durationButtons={}
local durations={0,5,10,15}
local function updateDuration()
    local current=getDB().alertSeconds
    for i=1,4 do
        durationButtons[i]:SetText((current==durations[i] and "● " or "○ ")..(durations[i]==0 and "不自动关闭" or durations[i].." 秒"))
    end
end
for i=1,4 do
    local n=durations[i]
    durationButtons[i]=button(settings,"",10+(i-1)*112,-315,103,25,function()
        getDB().alertSeconds=n;updateDuration()
    end)
end
label(settings,10,-351,"④ 运行状态（拖动提醒卡片会自动保存位置）","GameFontNormalLarge")
local trackerInfo=label(settings,10,-375,"","GameFontHighlight",600)

local function syncSettings()
    local d=getDB()
    enabledAll:SetChecked(d.enabled)
    beforeAll:SetChecked(d.before)
    resetAll:SetChecked(d.realertAfterReset)
    mapAll:SetChecked(d.mapReminder)
    updateRate()
    updateDuration()
    setTextIfChanged(trackerInfo,"DLL："..(type(TaiYangShenDian)=="function" and "已检测" or "未检测到").."   战斗状态缓存 "..countTracks().." 个 Boss")
end
local settingsTicker=CreateFrame("Frame",nil,settings)
local settingsElapsed=0
settingsTicker:SetScript("OnUpdate",function() settingsElapsed=settingsElapsed+arg1;if settingsElapsed>=1 then settingsElapsed=0;syncSettings() end end)
local openSettings=false
local tab=button(frame,"提醒设置",610,-48,116,27,function()
    openSettings=not openSettings
    if openSettings then prepOpen=false;prep:Hide();frame:SetHeight(565);manager:Hide();settings:Show();status:Hide();syncSettings();this:SetText("返回 Boss 管理")
    else settings:Hide();manager:Show();status:Show();refreshList();this:SetText("提醒设置") end
end)
frame:Hide()
-- Optional FuBar bridge: exposed only after the main UI has initialized.
TaiYangBossMemo_ToggleSettings=function()
    if frame:IsShown() then TaiYangBossMemoClose() else TaiYangBossMemoOpen() end
end
TaiYangBossMemo_GetFuBarSummary=function()
    local d=getDB()
    local enabledCount, allCount=0,0
    for key,boss in pairs(getAccount().bosses) do
        if type(boss)=="table" then
            allCount=allCount+1
            local n=d.notes[key]
            if type(n)=="table" and n.enabled~=false and trim(n.before)~="" then enabledCount=enabledCount+1 end
        end
    end
    return d.enabled and "开启" or "关闭", enabledCount,allCount,
        d.before==true,d.realertAfterReset==true,
        type(TaiYangShenDian)=="function",d.pollInterval
end
TaiYangBossMemoOpen=function()
    frame:Show()
    if openSettings then syncSettings() else refreshList() end
end
end
local initOK,initErr=pcall(TaiYangBossMemoInitialize)
if not initOK and DEFAULT_CHAT_FRAME then
    DEFAULT_CHAT_FRAME:AddMessage("|cffff5555[BossMemo] 初始化失败: "..tostring(initErr).."|r")
end