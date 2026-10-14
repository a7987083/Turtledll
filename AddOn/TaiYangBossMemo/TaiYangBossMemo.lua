-- TaiYangBossMemo 0.6.0-A | WoW 1.12 / Lua 5.0 | No external DLL dependencies.
-- Observational only: DEAD means unit death, not authoritative encounter victory.
local PREFIX="|cffffd100[太阳Boss备忘录]|r "
local db,shown = nil,{}
local tracks,lastEntry={},nil
local cursor=0
local timer=0
local function say(s) if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(PREFIX..s) end end
local function trim(s) return string.gsub(s or "^","^%s*(.-)%s*$","%1") end
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
    local d=getDB()
    return d.notes[tostring(entry)]
end

local frame=CreateFrame("Frame","TaiYangBossMemoWindow",UIParent)
frame:SetWidth(510);frame:SetHeight(465)
frame:SetPoint("CENTER",UIParent,"CENTER",0,35)
frame:SetFrameStrata("DIALOG")
frame:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=11,top=12,bottom=11}})
frame:SetMovable(true);frame:EnableMouse(true);frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart",function() this:StartMoving() end)
frame:SetScript("OnDragStop",function() this:StopMovingOrSizing() end)
local title=frame:CreateFontString(nil,"OVERLAY","GameFontNormalLarge")
title:SetPoint("TOP",frame,"TOP",0,-21);title:SetText("太阳神殿 · Boss 战斗备忘录")
local subtitle=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
subtitle:SetPoint("TOP",title,"BOTTOM",0,-8)
subtitle:SetText("以 NPC Entry 保存｜持续追踪 GUID｜间隔通过 /bossmemo interval 设置")
local function textAt(y,text)
    local x=frame:CreateFontString(nil,"OVERLAY","GameFontNormal")
    x:SetPoint("TOPLEFT",frame,"TOPLEFT",28,y);x:SetText(text)
    return x
end
textAt(-73,"NPC Entry（数字）")
textAt(-122,"Boss 名称（备注用）")
textAt(-170,"战前提醒内容")
textAt(-263,"死亡后提醒内容")
local function edit(y,h)
    local box=CreateFrame("EditBox",nil,frame)
    box:SetWidth(448);box:SetHeight(h)
    box:SetPoint("TOPLEFT",frame,"TOPLEFT",29,y)
    box:SetFontObject(GameFontHighlight)
    box:SetMultiLine(h>35);box:SetAutoFocus(false)
    local backdrop=CreateFrame("Frame",nil,frame)
    backdrop:SetFrameLevel(box:GetFrameLevel()-1)
    backdrop:SetPoint("TOPLEFT",box,"TOPLEFT",-5,6)
    backdrop:SetPoint("BOTTOMRIGHT",box,"BOTTOMRIGHT",5,-5)
    backdrop:SetBackdrop({bgFile="Interface\\Tooltips\\UI-Tooltip-Background",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",tile=true,tileSize=16,edgeSize=13,insets={left=3,right=3,top=3,bottom=3}})
    backdrop:SetBackdropColor(0.07,0.07,0.09,0.95)
    box:SetScript("OnEscapePressed",function() this:ClearFocus() end)
    return box
end
local entryBox=edit(-92,22)
local nameBox=edit(-140,22)
local beforeBox=edit(-190,64)
local afterBox=edit(-283,64)
local status=frame:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
status:SetPoint("BOTTOMLEFT",frame,"BOTTOMLEFT",27,60)
status:SetWidth(445);status:SetJustifyH("LEFT")
status:SetText("选中 Boss 后点击读取目标，或输入 Entry 并加载。")
local function setStatus(s) status:SetText(s) end
local function entryNumber()
    local str=trim(entryBox:GetText())
    if not string.find(str,"^%d+$") then return nil end
    local n=tonumber(str)
    if not n or n<1 or n>4294967295 then return nil end
    return n
end
local function loadEntry(n,label)
    if not n then setStatus("请输入有效的 NPC Entry 数字。");return end
    entryBox:SetText(tostring(n))
    local item=note(n)
    nameBox:SetText(item and item.name or label or "")
    beforeBox:SetText(item and item.before or "")
    afterBox:SetText(item and item.after or "")
    setStatus("已加载 Entry "..n.."。编辑完成后点击保存。")
    lastEntry=n
end
local function currentTarget()
    local id=api("Unit.Identity","target")
    if not id or not id.ok or not id.data or not id.data.entry then
        setStatus("当前目标不可用；也可以手动输入 Entry。");return
    end
    loadEntry(id.data.entry,id.data.name)
end
local function save()
    local n=entryNumber()
    if not n then setStatus("保存失败：Entry 必须为正整数。");return end
    local d=getDB()
    d.notes[tostring(n)]={name=trim(nameBox:GetText()),before=trim(beforeBox:GetText()),after=trim(afterBox:GetText())}
    lastEntry=n
    setStatus("已保存 Entry "..n.."。同一 Entry 的实例会共用备注。")
end
local function button(label,x,fn)
    local b=CreateFrame("Button",nil,frame,"UIPanelButtonTemplate")
    b:SetWidth(109);b:SetHeight(25);b:SetPoint("BOTTOMLEFT",frame,"BOTTOMLEFT",x,22)
    b:SetText(label);b:SetScript("OnClick",fn);return b
end
button("读取当前目标",25,currentTarget)
button("加载 Entry",140,function() loadEntry(entryNumber()) end)
button("保存备注",255,save)
button("关闭",370,function() frame:Hide() end)

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
frame:Hide()
SLASH_TAIYANGBOSSMEMO1="/bossmemo"
SlashCmdList["TAIYANGBOSSMEMO"]=function(msg)
    local cmd=trim(msg)
    if cmd=="off" then getDB().enabled=false;say("自动提醒已关闭。");return end
    if cmd=="status" then say("追踪 "..countTracks().."/"..MAX_TRACKS.." 个 Boss，查询间隔 "..getDB().pollInterval.." 秒。");return end
    local newInterval=string.match(cmd,"^interval%s+(%S+)$")
    if newInterval then
        local n=tonumber(newInterval)
        if n==0.2 or n==0.5 or n==1 or n==2 then
            getDB().pollInterval=n;timer=0;say("查询间隔已设为 "..n.." 秒。")
        else say("可设置的间隔：0.2、0.5、1、2 秒。例如 /bossmemo interval 1") end
        return
    end
    if cmd=="on" then getDB().enabled=true;say("自动提醒已开启。");return end
    if cmd=="target" then frame:Show();currentTarget();return end
    frame:Show()
    if lastEntry then loadEntry(lastEntry) else currentTarget() end
end
