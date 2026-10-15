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
-- TaiYangBossMemo 1.0.1 | WoW 1.12.1 / Lua 5.0
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
    if db.enabled==nil then db.enabled=true end
    if db.before==nil then db.before=true end
    if db.realertAfterReset==nil then db.realertAfterReset=false end
    if db.alertSeconds~=0 and db.alertSeconds~=5 and db.alertSeconds~=10 and db.alertSeconds~=15 then db.alertSeconds=0 end
    if type(db.alertPos)~="table" then db.alertPos=nil end
    if db.pollInterval~=0.2 and db.pollInterval~=0.5 and db.pollInterval~=1 and db.pollInterval~=2 then db.pollInterval=0.5 end
    return db
end
local function bossInfo(entry)
    local boss=getAccount().bosses[tostring(entry)]
    if type(boss)=="table" then return boss end
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
-- Inventory helpers: scan backpack and four bags only; bank is intentionally excluded.
local function itemID(link)
    if type(link)~="string" then return nil end
    local id=string.gsub(link,"^.-item:(%d+):?.*$","%1")
    return tonumber(id)
end
local function inventory()
    local result={}
    if type(GetContainerNumSlots)~="function" or type(GetContainerItemLink)~="function" then return result end
    for bag=0,4 do
        for slot=1,(GetContainerNumSlots(bag) or 0) do
            local link=GetContainerItemLink(bag,slot)
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
    return result
end
local function itemName(id)
    if type(GetItemInfo)=="function" then
        local name=GetItemInfo(id)
        if type(name)=="string" then return name end
    end
    return "物品 #"..tostring(id)
end
local function prepData(entry)
    local item=anyNote(entry)
    if not item then return nil end
    if type(item.consumables)~="table" then item.consumables={} end
    return item
end
-- Gear snapshots store exact inventory item links (enchant/instance identity) per slot.
-- Never auto-equip on targeting; a physical user click is required.
local EQUIP_SLOTS={1,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19}
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
local function setMissing(slots)
    local missing=0
    if type(slots)~="table" then return 0 end
    local available={}
    -- Count *instances*, not merely ItemID, and never read the bank.
    for i=1,table.getn(EQUIP_SLOTS) do
        local link=GetInventoryItemLink("player",EQUIP_SLOTS[i])
        if link then available[link]=(available[link] or 0)+1 end
    end
    for bag=0,4 do for slot=1,(GetContainerNumSlots(bag) or 0) do
        local link=GetContainerItemLink(bag,slot)
        if link then available[link]=(available[link] or 0)+1 end
    end end
    for i=1,table.getn(EQUIP_SLOTS) do
        local wanted=slots[tostring(EQUIP_SLOTS[i])]
        if wanted then
            if (available[wanted] or 0)>0 then available[wanted]=available[wanted]-1
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
                if GetInventoryItemLink("player",slot)==wanted then changed=changed+1 else missing=missing+1 end
            else missing=missing+1 end
        end
    end
    return missing==0,"「"..setName.."」换装：成功 "..changed.." 槽，未完成 "..missing.." 槽"
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
local nameBox,entryBox,beforeBox,enabledCheck
local dirtyLabel
local function updateDirtyLabel()
    if dirtyLabel then dirtyLabel:SetText(dirty and "● 未保存" or "已保存") end
end
local function editorSnapshot()
    if not entryBox then return nil end
    return {entry=entryBox:GetText() or "",name=nameBox:GetText() or "",before=beforeBox:GetText() or "",enabled=enabledCheck:GetChecked() and true or false}
end
local function captureSnapshot()
    savedSnapshot=editorSnapshot();dirty=false;updateDirtyLabel()
end
local function recomputeDirty()
    if suppressDirty or not savedSnapshot then return end
    local current=editorSnapshot()
    dirty=current.entry~=savedSnapshot.entry or current.name~=savedSnapshot.name or current.before~=savedSnapshot.before or current.enabled~=savedSnapshot.enabled
    updateDirtyLabel()
end
local confirmModal=nil
local function markDirty() recomputeDirty() end
local PAGE_SIZE=7
local function entryList()
    local entries={}
    local filter=string.lower(trim(searchBox:GetText() or ""))
    for key,item in pairs(getAccount().bosses) do
        if type(item)=="table" then
            local n=tonumber(key)
            local name=item.name or ""
            if n and (filter=="" or string.find(string.lower(name),filter,1,true) or string.find(key,filter,1,true)) then
                table.insert(entries,n)
            end
        end
    end
    table.sort(entries,function(a,b) return a<b end)
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
local function loadEntry(n,name)
    if not n or n<1 then setStatus("请输入有效的 NPC Entry。");return end
    suppressDirty=true
    selectedEntry=n
    local item=anyNote(n)
    local boss=bossInfo(n)
    entryBox:SetText(tostring(n))
    nameBox:SetText(boss and boss.name or name or "")
    beforeBox:SetText(item and item.before or "")
    enabledCheck:SetChecked(not item or item.enabled~=false)
    lastEntry=n
    setStatus("正在编辑 Entry "..n.."；修改后点击保存。")
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
            local name=(boss and boss.name and boss.name~="") and boss.name or ("Entry "..n)
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
label(manager,235,-106,"战前提醒")
beforeBox=edit(manager,235,-130,465,120)
for _,box in ipairs({entryBox,nameBox,beforeBox}) do
    box:SetScript("OnTextChanged",markDirty)
end
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
    if id and id.ok and id.data and id.data.entry then askDiscard(function() loadEntry(id.data.entry,id.data.name) end)
    else setStatus("当前没有可读取的目标，可手动输入 NPC Entry。") end
end
local function save()
    local n=entryNumber()
    if not n then setStatus("保存失败：NPC Entry 必须是正整数。");return end
    local oldItem=anyNote(n)
    -- Preserve legacy after text in SavedVariables for backward compatibility; never display or trigger it.
    local item={before=trim(beforeBox:GetText()),after=oldItem and oldItem.after or "",enabled=enabledCheck:GetChecked() and true or false,consumables=oldItem and oldItem.consumables or {},gear=oldItem and oldItem.gear or nil,gearSets=oldItem and oldItem.gearSets or nil,gearMigrated=oldItem and oldItem.gearMigrated or nil}
    getAccount().bosses[tostring(n)]={name=trim(nameBox:GetText())}
    getDB().notes[tostring(n)]=item
    -- Editing Entry is treated as add/save-as; never silently delete prior ID.
    loadEntry(n)
    setStatus("保存成功：Entry "..n..(item.enabled and "（已启用）" or "（已停用）"))
    refreshList()
end
local function deleteEntry()
    local n=entryNumber()
    if not n or not bossInfo(n) then setStatus("当前 Entry 尚未保存，无法删除。");return end
    if confirmModal then
        local item=bossInfo(n)
        local displayName=(item and item.name and item.name~="") and item.name or ("Entry "..n)
        confirmModal("确认删除 Boss", "删除「"..displayName.."」(Entry "..n..")？此操作无法撤销。",function()
            getAccount().bosses[tostring(n)]=nil
            getDB().notes[tostring(n)]=nil
            suppressDirty=true
            selectedEntry=nil;lastEntry=nil
    entryBox:SetText("");nameBox:SetText("");beforeBox:SetText("")
    enabledCheck:SetChecked(true);refreshList()
            suppressDirty=false;captureSnapshot()
            setStatus("已删除 Boss Entry "..n.."，不影响其他记录。")
        end)
    end
end
button(manager,"读取当前目标",235,-325,147,27,target)
button(manager,"保存 Boss",395,-325,145,27,save)
button(manager,"启用/停用",553,-325,150,27,function()
    local e=entryNumber();if not e or not bossInfo(e) then setStatus("请先选择已保存的 Boss") return end
    local data=getDB().notes[tostring(e)]
    if type(data)~="table" then data={before="",enabled=true};getDB().notes[tostring(e)]=data end
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
    selectedEntry=nil;entryBox:SetText("");nameBox:SetText("")
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
local prepHint=label(prep,8,-29,"先在 Boss 管理中保存 Entry，再配置药剂和装备。","GameFontHighlightSmall",670)
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
    local id=tonumber(trim(entryBox:GetText() or ""))
    if id and bossInfo(id) then return id end
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
label(outfitNameModal,24,-61,"方案名称（最多 30 个字符）","GameFontHighlightSmall",360)
local outfitNameInput=edit(outfitNameModal,28,-90,345,25)
button(outfitNameModal,"保存",55,-142,125,25,function()
    local e=currentPrepEntry()
    if not e then outfitNameModal:Hide();return end
    local name=trim(outfitNameInput:GetText() or "")
    if name=="" or string.len(name)>90 then prepStatus:SetText("请填写不超过 30 个汉字的方案名称");return end
    local slots=equipSnapshot()
    if not next(slots) then prepStatus:SetText("当前未读取到已穿戴装备，不能保存");return end
    local sets=outfitSets(e)
    local function commit()
        sets[name]={name=name,slots=slots}
        outfitNameModal:Hide()
        prepStatus:SetText("已保存装备方案："..name)
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
    outfitNameInput:SetText("");outfitNameModal:Show();outfitNameInput:SetFocus()
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
    prepOpen=not prepOpen
    if prepOpen then
        frame:SetHeight(715);manager:Hide();status:Hide();prep:Show();prepRefresh();refreshOutfits()
    else prep:Hide();frame:SetHeight(510);manager:Show();status:Show();refreshList() end
end
button(manager,"药剂与装备",235,-363,225,27,function() togglePrep() end)
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
            title:SetText("不足物品（"..count.."）")
            title:SetTextColor(1,0.25,0.25)
        else
            title:SetText("已达标物品（"..count.."）")
            title:SetTextColor(0.2,1,0.3)
        end
        for i=1,GROUP_SIZE do
            local r=groupRows[col][i]
            r.frame:ClearAllPoints()
            r.frame:SetPoint("TOPLEFT",popup,"TOPLEFT",(col==1) and 26 or 287,columnTop-29-(i-1)*31)
            local item=list[groupOffsets[col]+i]
            if item then
                r.icon:SetTexture(item.icon or "Interface\\Icons\\INV_Misc_QuestionMark")
                r.name:SetText(item.name)
                if col==1 then r.name:SetTextColor(1,0.75,0.75);r.count:SetTextColor(1,0.25,0.25)
                else r.name:SetTextColor(1,1,1);r.count:SetTextColor(0.15,1,0.2) end
                r.count:SetText(item.have.." / "..item.need)
                r.frame:Show()
            else r.frame:Hide() end
        end
        local nav=groupNav[col]
        if count>GROUP_SIZE then
            nav.page:SetText((groupOffsets[col]+1).."-"..math.min(count,groupOffsets[col]+GROUP_SIZE).." / "..count)
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
local function refreshAlertOutfits()
    local names=activeAlert and sortedSetNames(activeAlert.entry) or {}
    local total=math.max(1,math.ceil(table.getn(names)/4))
    if alertSetPage>total then alertSetPage=total end
    for i=1,4 do
        local name=names[(alertSetPage-1)*4+i]
        local b=alertSetButtons[i]
        if name then
            local sets=outfitSets(activeAlert.entry)
            local missing=setMissing(sets[name].slots)
            b:SetText(name..(missing==0 and "（装备齐全）" or "（缺失"..missing.."件）"))
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
    pTitle:SetText(activeAlert.title..(table.getn(queue)>0 and " (待处理 "..table.getn(queue)..")" or ""))
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
refreshActiveStock=function()
    groupLists[1]={};groupLists[2]={}
    if not activeAlert then return end
    local d=prepData(activeAlert.entry)
    local stock=inventory()
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
    if activeAlert then refreshAlertOutfits() end
end
pClose:SetScript("OnClick",function() activeAlert=nil;displayNext() end)
alertClose:SetScript("OnClick",function() activeAlert=nil;displayNext() end)
local alertTicker=CreateFrame("Frame",nil,popup)
alertTicker:SetScript("OnUpdate",function()
    if activeAlert and getDB().alertSeconds>0 then
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
    local name=(item.name and item.name~="") and item.name or "Entry "..entry
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
local function observe()
    if not getDB().enabled then return end
    local newSelection=pendingNewSelection
    pendingNewSelection=false
    local id=api("Unit.Identity","target")
    local targetGuid=nil
    if id and id.ok and id.data and id.data.guid and id.data.entry and note(id.data.entry) then
        targetGuid=id.data.guid
        addTrack(targetGuid,id.data.entry)
        -- Current-target state is needed for alive validation and wipe/re-pull detection.
        local boss=api("Boss.State","target")
        if boss and boss.ok and type(boss.data)=="table"
            and boss.data.guid==targetGuid and boss.data.entry==id.data.entry then
            observeCombat(targetGuid,id.data.entry,boss)
            local tracked=tracks[targetGuid]
            if newSelection and tracked and tracked.resetReady and getDB().realertAfterReset
                and boss.data and boss.data.deadKnown==true and boss.data.dead==false
                and boss.data.inCombatKnown==true and boss.data.inCombat==false
                and not playerIsDead() then
                tracked.attempt=tracked.attempt+1
                tracked.resetReady=false
                tracked.wasCombat=false
                tracked.offCount=0
                tracked.onCount=0
                if shown[targetGuid] then shown[targetGuid].before=nil end
                say("Boss 已重置，重新选中：Entry "..id.data.entry.."，第 "..tracked.attempt.." 轮。")
                trigger(targetGuid,id.data.entry)
            end
            if boss.state=="DETECTED" or boss.state=="ENGAGED" then
                trigger(targetGuid,id.data.entry)
            end
        end
    end
    -- Session-local eviction only. No GUID death probes or Boss event journal.
    local now=milliseconds()
    for guid,t in pairs(tracks) do
        -- Idle entries expire, but a continuously observed target stays tracked.
        -- Keep shown[guid] for the entire map session to prevent duplicate alerts.
        if now-t.lastSeen>TRACK_TTL_MS then tracks[guid]=nil end
    end
end
local driver=CreateFrame("Frame")
driver:RegisterEvent("VARIABLES_LOADED")
driver:RegisterEvent("BAG_UPDATE")
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("PLAYER_LEAVING_WORLD")
driver:RegisterEvent("PLAYER_LOGOUT")
driver:RegisterEvent("PLAYER_TARGET_CHANGED")
driver:SetScript("OnEvent",function()
    if event=="BAG_UPDATE" then if activeAlert then refreshActiveStock() end return end
    if event=="VARIABLES_LOADED" then
        getDB()
        resetSession()
    elseif event=="PLAYER_ENTERING_WORLD" or event=="PLAYER_LEAVING_WORLD" then
        -- PLAYER_ENTERING_WORLD runs after map/instance loading on WoW 1.12.
        -- The leaving event also prevents a previous map's alert from lingering.
        resetSession()
    elseif event=="PLAYER_TARGET_CHANGED" then
        pendingNewSelection=true
        -- Act on target selection promptly; do not wait for the next polling tick.
        observe()
    elseif event=="PLAYER_LOGOUT" then
        -- Never persist GUIDs, timestamps or pop-up dedup state.
        resetSession()
    end
end)
driver:SetScript("OnUpdate",function()
    if not getDB().enabled then return end
    timer=timer+arg1
    if timer>=getDB().pollInterval then timer=0;observe() end
end)
-- All settings are in the Chinese UI; /bossmemo only opens the window.
local settings=CreateFrame("Frame",nil,frame)
settings:SetPoint("TOPLEFT",frame,"TOPLEFT",27,-68)
settings:SetWidth(700);settings:SetHeight(368)
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
label(settings,10,-185,"② Boss 状态检测间隔（秒）","GameFontNormalLarge")
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
    rateButtons[i]=button(settings,"",10+(i-1)*112,-210,103,27,function()
        getDB().pollInterval=n;timer=0;updateRate()
    end)
end
label(settings,10,-260,"③ 提醒自动关闭","GameFontNormalLarge")
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
    durationButtons[i]=button(settings,"",10+(i-1)*112,-284,103,25,function()
        getDB().alertSeconds=n;updateDuration()
    end)
end
label(settings,10,-320,"④ 运行状态（拖动提醒卡片会自动保存位置）","GameFontNormalLarge")
local trackerInfo=label(settings,10,-344,"","GameFontHighlight",600)

local function syncSettings()
    local d=getDB()
    enabledAll:SetChecked(d.enabled)
    beforeAll:SetChecked(d.before)
    resetAll:SetChecked(d.realertAfterReset)
    updateRate()
    updateDuration()
    trackerInfo:SetText("DLL："..(type(TaiYangShenDian)=="function" and "已检测" or "未检测到").."   战斗状态缓存 "..countTracks().." 个 Boss")
end
local settingsTicker=CreateFrame("Frame",nil,settings)
local settingsElapsed=0
settingsTicker:SetScript("OnUpdate",function() settingsElapsed=settingsElapsed+arg1;if settingsElapsed>=1 then settingsElapsed=0;syncSettings() end end)
local openSettings=false
local tab=button(frame,"提醒设置",610,-48,116,27,function()
    openSettings=not openSettings
    if openSettings then prepOpen=false;prep:Hide();frame:SetHeight(510);manager:Hide();settings:Show();status:Hide();syncSettings();this:SetText("返回 Boss 管理")
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