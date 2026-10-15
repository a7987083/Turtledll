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
-- TaiYangBossMemo 0.9.1 | WoW 1.12.1 / Lua 5.0
-- SavedVariables: legacy {notes={ [entry]={name,before,after} }} stays valid.
local PREFIX="|cffffd100[太阳Boss备忘录]|r "
local db,shown=nil,{}
local tracks,lastEntry={},nil
local timer=0
local pendingNewSelection=false
local function say(s) if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(PREFIX..s) end end
local function trim(s) return string.gsub(s or "","^%s*(.-)%s*$","%1") end
local function getDB()
    if type(TaiYangBossMemoDB)~="table" then TaiYangBossMemoDB={} end
    db=TaiYangBossMemoDB
    if type(db.notes)~="table" then db.notes={} end
    if db.enabled==nil then db.enabled=true end
    if db.before==nil then db.before=true end
    if db.realertAfterReset==nil then db.realertAfterReset=false end
    if db.alertSeconds~=0 and db.alertSeconds~=5 and db.alertSeconds~=10 and db.alertSeconds~=15 then db.alertSeconds=0 end
    if type(db.alertPos)~="table" then db.alertPos=nil end
    if db.pollInterval~=0.2 and db.pollInterval~=0.5 and db.pollInterval~=1 and db.pollInterval~=2 then db.pollInterval=0.5 end
    return db
end
local function api(cmd,a,b)
    if type(TaiYangShenDian)~="function" then return nil end
    local ok,val
    if b~=nil then ok,val=pcall(TaiYangShenDian,cmd,a,b)
    elseif a~=nil then ok,val=pcall(TaiYangShenDian,cmd,a)
    else ok,val=pcall(TaiYangShenDian,cmd) end
    if ok and type(val)=="table" then return val end
end
local function note(entry)
    local item=getDB().notes[tostring(entry)]
    if type(item)~="table" or item.enabled==false then return nil end
    return item
end
local function anyNote(entry)
    local item=getDB().notes[tostring(entry)]
    if type(item)=="table" then return item end
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
title:SetPoint("TOP",frame,"TOP",0,-20);title:SetText("太阳神殿 · Boss 备忘录")
local subtitle=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
subtitle:SetPoint("TOP",title,"BOTTOM",0,-7)
subtitle:SetText("Boss 管理 / 提醒设置    ·    仅 /bossmemo 用于呼出")

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
    for key,item in pairs(getDB().notes) do
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
    entryBox:SetText(tostring(n))
    nameBox:SetText(item and item.name or name or "")
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
            local tag=(item and item.enabled==false) and "[停用] " or ""
            local name=(item and item.name and item.name~="") and item.name or ("Entry "..n)
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
beforeBox=edit(manager,235,-130,465,150)
for _,box in ipairs({entryBox,nameBox,beforeBox}) do
    box:SetScript("OnTextChanged",markDirty)
end
enabledCheck=CreateFrame("CheckButton",nil,manager,"UICheckButtonTemplate")
enabledCheck:SetPoint("TOPLEFT",manager,"TOPLEFT",234,-319)
label(manager,269,-328,"启用此 Boss 的提醒","GameFontHighlight")
enabledCheck:SetScript("OnClick",markDirty)
dirtyLabel=label(manager,590,-328,"已保存","GameFontHighlightSmall",114)
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
    local item={name=trim(nameBox:GetText()),before=trim(beforeBox:GetText()),after=oldItem and oldItem.after or "",enabled=enabledCheck:GetChecked() and true or false}
    getDB().notes[tostring(n)]=item
    -- Editing Entry is treated as add/save-as; never silently delete prior ID.
    loadEntry(n)
    setStatus("保存成功：Entry "..n..(item.enabled and "（已启用）" or "（已停用）"))
    refreshList()
end
local function deleteEntry()
    local n=entryNumber()
    if not n or not anyNote(n) then setStatus("当前 Entry 尚未保存，无法删除。");return end
    if confirmModal then
        local item=anyNote(n)
        local displayName=(item and item.name and item.name~="") and item.name or ("Entry "..n)
        confirmModal("确认删除 Boss", "删除「"..displayName.."」(Entry "..n..")？此操作无法撤销。",function()
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
button(manager,"读取当前目标",235,-365,112,25,target)
button(manager,"保存 Boss",354,-365,100,25,save)
button(manager,"删除",462,-365,80,25,deleteEntry)
button(manager,"新建",548,-365,80,25,function()
    askDiscard(function()
    suppressDirty=true
    selectedEntry=nil;entryBox:SetText("");nameBox:SetText("")
    beforeBox:SetText("");enabledCheck:SetChecked(true)
    setStatus("新建 Boss：输入 Entry、名称和提醒后保存。");refreshList()
    suppressDirty=false;captureSnapshot()
    end)
end)
refreshList()
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
popup:SetWidth(328);popup:SetHeight(176)
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
pBody:SetPoint("CENTER",popup,"CENTER",0,-3);pBody:SetWidth(282);pBody:SetHeight(90)
pBody:SetJustifyH("CENTER");pBody:SetJustifyV("MIDDLE")
local pClose=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
pClose:SetWidth(92);pClose:SetHeight(22)
pClose:SetPoint("BOTTOM",popup,"BOTTOM",0,16);pClose:SetText("知道了")
local queue={}
local activeAlert=nil
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
        if getDB().enabled and getDB().before and current and trim(current.before)~="" then break end
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
    pBody:SetText(activeAlert.body)
    popup:Show()
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
    if content=="" then return false end
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
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("PLAYER_LEAVING_WORLD")
driver:RegisterEvent("PLAYER_LOGOUT")
driver:RegisterEvent("PLAYER_TARGET_CHANGED")
driver:SetScript("OnEvent",function()
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
    if openSettings then manager:Hide();settings:Show();status:Hide();syncSettings();this:SetText("返回 Boss 管理")
    else settings:Hide();manager:Show();status:Show();refreshList();this:SetText("提醒设置") end
end)
frame:Hide()
TaiYangBossMemoOpen=function()
    frame:Show()
    if openSettings then syncSettings() else refreshList() end
end
end
local initOK,initErr=pcall(TaiYangBossMemoInitialize)
if not initOK and DEFAULT_CHAT_FRAME then
    DEFAULT_CHAT_FRAME:AddMessage("|cffff5555[BossMemo] 初始化失败: "..tostring(initErr).."|r")
end