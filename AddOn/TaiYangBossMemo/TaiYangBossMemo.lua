-- Register slash command before building UI; init failure remains diagnosable.
local TaiYangBossMemoOpen = nil
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
-- TaiYangBossMemo 0.7.0 | WoW 1.12.1 / Lua 5.0
-- SavedVariables: legacy {notes={ [entry]={name,before,after} }} stays valid.
local PREFIX="|cffffd100[太阳Boss备忘录]|r "
local db,shown=nil,{}
local tracks,lastEntry={},nil
local cursor,timer=0,0
local function say(s) if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(PREFIX..s) end end
local function trim(s) return string.gsub(s or "","^%s*(.-)%s*$","%1") end
local function getDB()
    if type(TaiYangBossMemoDB)~="table" then TaiYangBossMemoDB={} end
    db=TaiYangBossMemoDB
    if type(db.notes)~="table" then db.notes={} end
    if db.enabled==nil then db.enabled=true end
    if db.before==nil then db.before=true end
    if db.after==nil then db.after=true end
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
local nameBox,entryBox,beforeBox,afterBox,enabledCheck
local status=label(frame,29,-460,"请选择 Boss，或读取当前目标添加。","GameFontHighlightSmall",690)
local function setStatus(s) status:SetText(s) end
local refreshList
local function loadEntry(n,name)
    if not n or n<1 then setStatus("请输入有效的 NPC Entry。");return end
    selectedEntry=n
    local item=anyNote(n)
    entryBox:SetText(tostring(n))
    nameBox:SetText(item and item.name or name or "")
    beforeBox:SetText(item and item.before or "")
    afterBox:SetText(item and item.after or "")
    enabledCheck:SetChecked(not item or item.enabled~=false)
    lastEntry=n
    setStatus("正在编辑 Entry "..n.."；修改后点击保存。")
    if refreshList then refreshList() end
end
for i=1,PAGE_SIZE do
    local n=i
    local b=button(manager,"",10,-74-(i-1)*36,200,31,function()
        local entries=entryList()
        local id=entries[(page-1)*PAGE_SIZE+n]
        if id then loadEntry(id) end
    end)
    listRows[i]=b
end
refreshList=function()
    local entries=entryList()
    local total=math.max(1,math.ceil(table.getn(entries)/PAGE_SIZE))
    if page>total then page=total end
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
end
searchBox:SetScript("OnTextChanged",function() page=1;refreshList() end)
button(manager,"上一页",10,-335,93,25,function() if page>1 then page=page-1;refreshList() end end)
button(manager,"下一页",116,-335,93,25,function() page=page+1;refreshList() end end)
local pageInfo=label(manager,12,-369,"可通过名称或 Entry 搜索","GameFontHighlightSmall",200)
label(manager,235,-7,"Boss 详情 / 编辑","GameFontNormalLarge")
label(manager,236,-40,"NPC Entry")
entryBox=edit(manager,235,-61,120,22)
label(manager,375,-40,"Boss 名称")
nameBox=edit(manager,375,-61,325,22)
label(manager,235,-106,"战前提醒")
beforeBox=edit(manager,235,-130,465,70)
label(manager,235,-215,"死亡后提醒")
afterBox=edit(manager,235,-239,465,70)
enabledCheck=CreateFrame("CheckButton",nil,manager,"UICheckButtonTemplate")
enabledCheck:SetPoint("TOPLEFT",manager,"TOPLEFT",234,-319)
label(manager,269,-328,"启用此 Boss 的提醒","GameFontHighlight")
local function entryNumber()
    local value=trim(entryBox:GetText())
    if not string.find(value,"^%d+$") then return nil end
    local n=tonumber(value)
    if not n or n<1 or n>4294967295 then return nil end
    return n
end
local function target()
    local id=api("Unit.Identity","target")
    if id and id.ok and id.data and id.data.entry then loadEntry(id.data.entry,id.data.name)
    else setStatus("当前没有可读取的目标，可手动输入 NPC Entry。") end
end
local function save()
    local n=entryNumber()
    if not n then setStatus("保存失败：NPC Entry 必须是正整数。");return end
    local oldItem=anyNote(n)
    local item={name=trim(nameBox:GetText()),before=trim(beforeBox:GetText()),after=trim(afterBox:GetText()),enabled=enabledCheck:GetChecked() and true or false}
    getDB().notes[tostring(n)]=item
    -- Editing Entry is treated as add/save-as; never silently delete prior ID.
    loadEntry(n)
    setStatus("保存成功：Entry "..n..(item.enabled and "（已启用）" or "（已停用）"))
    refreshList()
end
local confirmEntry,confirmUntil=nil,0
local function deleteEntry()
    local n=entryNumber()
    if not n or not anyNote(n) then setStatus("当前 Entry 尚未保存，无法删除。");return end
    if confirmEntry~=n or GetTime()>confirmUntil then
        confirmEntry=n;confirmUntil=GetTime()+5
        setStatus("确认删除 Entry "..n.."？5 秒内再次点击「删除」确认。")
        return
    end
    getDB().notes[tostring(n)]=nil
    confirmEntry=nil;selectedEntry=nil;lastEntry=nil
    entryBox:SetText("");nameBox:SetText("");beforeBox:SetText("");afterBox:SetText("")
    enabledCheck:SetChecked(true);refreshList()
    setStatus("已删除 Boss Entry "..n.."，不影响其他记录。")
end
button(manager,"读取当前目标",235,-365,112,25,target)
button(manager,"保存 Boss",354,-365,100,25,save)
button(manager,"删除",462,-365,80,25,deleteEntry)
button(manager,"新建",548,-365,80,25,function()
    selectedEntry=nil;entryBox:SetText("");nameBox:SetText("")
    beforeBox:SetText("");afterBox:SetText("");enabledCheck:SetChecked(true)
    setStatus("新建 Boss：输入 Entry、名称和提醒后保存。");refreshList()
end)
refreshList()
local popup=CreateFrame("Frame","TaiYangBossMemoAlert",UIParent)
popup:SetWidth(440);popup:SetHeight(245)
popup:SetPoint("CENTER",UIParent,"CENTER",0,125)
popup:SetFrameStrata("FULLSCREEN_DIALOG")
popup:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
popup:SetMovable(true);popup:EnableMouse(true);popup:RegisterForDrag("LeftButton")
popup:SetScript("OnDragStart",function() this:StartMoving() end)
popup:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
local pTitle=popup:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
pTitle:SetPoint("TOP",popup,"TOP",0,-25)
local pBody=popup:CreateFontString(nil,"OVERLAY","GameFontHighlight")
pBody:SetPoint("TOPLEFT",popup,"TOPLEFT",30,-72);pBody:SetWidth(380);pBody:SetHeight(120)
pBody:SetJustifyH("LEFT");pBody:SetJustifyV("TOP")
local pClose=CreateFrame("Button",nil,popup,"UIPanelButtonTemplate")
pClose:SetWidth(110);pClose:SetHeight(25)
pClose:SetPoint("BOTTOM",popup,"BOTTOM",0,23);pClose:SetText("知道了")
pClose:SetScript("OnClick",function() popup:Hide() end)
popup:Hide()
local function alert(entry,phase)
    local d=getDB()
    if not d.enabled then return end
    if phase=="before" and not d.before then return end
    if phase=="after" and not d.after then return end
    local item=note(entry)
    if not item then return end
    local content=trim(item[phase])
    if content=="" then return end
    pTitle:SetText((phase=="before" and "战前提醒" or "检测到单位死亡").." · "..(item.name~="" and item.name or "Entry "..entry))
    pBody:SetText(content)
    popup:Show()
    say((phase=="before" and "战前：" or "死亡后：")..(item.name or tostring(entry)))
end

-- V0.6.0-A: only session-local GUID tracking. SavedVariables hold notes/settings.
-- GUID snapshots need the object to remain visible; invisibility is never interpreted as death.
local MAX_TRACKS=16
local TRACK_TTL_MS=300000
local TRACK_INVISIBLE_MS=60000
local function milliseconds() return math.floor(GetTime()*1000) end
local function countTracks()
    local n=0
    for guid,t in pairs(tracks) do n=n+1 end
    return n
end
local function resetSession()
    tracks={};shown={};cursor=0;timer=0
    local st=api("Boss.Status")
    if st and type(st.latestSeq)=="number" then cursor=st.latestSeq end
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
    tracks[guid]={entry=entry,firstSeen=now,lastSeen=now,invisibleSince=nil}
end
local function trigger(guid,entry,phase)
    if not note(entry) then return end
    if not shown[guid] then shown[guid]={} end
    if shown[guid][phase] then return end
    shown[guid][phase]=true
    alert(entry,phase)
end
local function observe()
    if not getDB().enabled then return end
    local id=api("Unit.Identity","target")
    local targetGuid=nil
    if id and id.ok and id.data and id.data.guid and id.data.entry and note(id.data.entry) then
        targetGuid=id.data.guid
        addTrack(targetGuid,id.data.entry)
        -- Boss.State generates M4 journal entries; call once while target is available.
        local boss=api("Boss.State","target")
        if boss and boss.ok then
            if boss.state=="DETECTED" or boss.state=="ENGAGED" then
                trigger(targetGuid,id.data.entry,"before")
            elseif boss.state=="DEAD" then
                trigger(targetGuid,id.data.entry,"after")
            end
        end
    end
    -- Once a GUID was seen, check its exact object even when target switches.
    -- Query limits: 16 tracked GUIDs, once per configured interval.
    local now=milliseconds()
    for guid,t in pairs(tracks) do
        if now-t.firstSeen>TRACK_TTL_MS then
            tracks[guid]=nil
        else
            local state=api("GroundProbe.UnitStateByGuid",guid)
            if state and state.visible and state.deadKnown then
                t.lastSeen=now;t.invisibleSince=nil
                if state.dead then trigger(guid,t.entry,"after") end
            elseif not state or not state.visible then
                if not t.invisibleSince then t.invisibleSince=now end
                if now-t.invisibleSince>TRACK_INVISIBLE_MS then tracks[guid]=nil end
            end
        end
    end
    -- Read any M4 transitions produced by Boss.State or other API clients.
    local ev=api("Boss.Event.GetAfter",cursor)
    if not ev then return end
    if ev.lost then
        cursor=ev.latestSeq or cursor
        return
    end
    local items=ev.events or {}
    for i=1,table.getn(items) do
        local v=items[i]
        if v and type(v.seq)=="number" and v.seq>cursor then
            cursor=v.seq
            if v.guid and v.entry and note(v.entry) then
                if v.state=="DETECTED" or v.state=="ENGAGED" then
                    trigger(v.guid,v.entry,"before")
                elseif v.state=="DEAD" then
                    trigger(v.guid,v.entry,"after")
                end
            end
        end
    end
    if type(ev.latestSeq)=="number" and ev.latestSeq>cursor then cursor=ev.latestSeq end
end
local driver=CreateFrame("Frame")
driver:RegisterEvent("VARIABLES_LOADED")
driver:RegisterEvent("PLAYER_LOGOUT")
driver:SetScript("OnEvent",function()
    if event=="VARIABLES_LOADED" then
        getDB()
        resetSession()
    elseif event=="PLAYER_LOGOUT" then
        -- Never persist GUIDs, timestamps, pop-up dedup state or event cursors.
        resetSession()
    end
end)
driver:SetScript("OnUpdate",function()
    local d=getDB()
    if not d.enabled then return end
    timer=timer+arg1
    if timer>=d.pollInterval then timer=0;observe() end
end)
-- All settings are in the Chinese UI; /bossmemo only opens the window.
local settings=CreateFrame("Frame",nil,frame)
settings:SetPoint("TOPLEFT",frame,"TOPLEFT",27,-79)
settings:SetWidth(700);settings:SetHeight(368)
settings:Hide()
label(settings,5,-8,"全局提醒设置","GameFontNormalLarge")
local function toggle(y,text,key)
    local c=CreateFrame("CheckButton",nil,settings,"UICheckButtonTemplate")
    c:SetPoint("TOPLEFT",settings,"TOPLEFT",8,y)
    label(settings,45,y-10,text,"GameFontHighlight")
    c:SetScript("OnClick",function() getDB()[key]=this:GetChecked() and true or false end)
    return c
end
local enabledAll=toggle(-49,"开启自动提醒","enabled")
local beforeAll=toggle(-90,"开启战前提醒","before")
local afterAll=toggle(-131,"开启死亡后提醒","after")
label(settings,10,-190,"GUID 检测间隔（秒）")
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
    rateButtons[i]=button(settings,"",10+(i-1)*112,-214,103,27,function()
        getDB().pollInterval=n;timer=0;updateRate()
    end)
end
local trackerInfo=label(settings,10,-270,"","GameFontHighlight",600)
label(settings,10,-312,"最多追踪 16 个 GUID；不可见不会判定死亡。退出游戏清空临时状态，保留配置。","GameFontHighlightSmall",650)
local function syncSettings()
    local d=getDB()
    enabledAll:SetChecked(d.enabled)
    beforeAll:SetChecked(d.before)
    afterAll:SetChecked(d.after)
    updateRate()
    trackerInfo:SetText("当前正在追踪 "..countTracks().."/16 个 Boss    ·    每 "..d.pollInterval.." 秒查询")
end
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
