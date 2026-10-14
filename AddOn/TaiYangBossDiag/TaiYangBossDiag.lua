-- Diagnostic-only addon for M1; no polling or hooks.
SLASH_TAIYANGBOSSDIAG1 = "/bossdiag"
SlashCmdList["TAIYANGBOSSDIAG"] = function(msg)
    local unit = (msg and string.gsub(msg, "^%s*(.-)%s*$", "%1")) or ""
    if unit == "" then unit = "target" end
    local chat = DEFAULT_CHAT_FRAME
    if type(TaiYangShenDian) ~= "function" then
        chat:AddMessage("|cffff5555[BossDiag]|r DLL API missing")
        return
    end
    local called, result = pcall(TaiYangShenDian, "Unit.Identity", unit)
    if not called or type(result) ~= "table" then
        chat:AddMessage("|cffff5555[BossDiag]|r API error: " .. tostring(result))
        return
    end
    chat:AddMessage("[BossDiag] ok=" .. tostring(result.ok) .. " code=" .. tostring(result.code) .. " schema=" .. tostring(result.schemaVersion))
    local d = result.data or {}
    chat:AddMessage("unit=" .. tostring(d.unit) .. " guid=" .. tostring(d.guid) .. " entry=" .. tostring(d.entry) .. " type=" .. tostring(d.objectType))
    chat:AddMessage("name=" .. tostring(d.name) .. " level=" .. tostring(d.level) .. " classification=" .. tostring(d.classification))
    chat:AddMessage("known: guid=" .. tostring(d.guidKnown) .. " entry=" .. tostring(d.entryKnown) .. " name=" .. tostring(d.nameKnown) .. " level=" .. tostring(d.levelKnown) .. " class=" .. tostring(d.classificationKnown))
end
