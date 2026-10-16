-- The Skills panel. Skills are plain data in CustomSkills.skills; CustomSkills:OnSkillClick is where real behaviour goes.
CustomSkills = {
    skills = {
        { name = "Fireball", icon = "Interface\\Icons\\Spell_Fire_FlameBolt", description = "Placeholder skill." },
        { name = "Healing Light", icon = "Interface\\Icons\\Spell_Holy_HolyBolt", description = "Placeholder skill." },
        { name = "Sprint", icon = "Interface\\Icons\\Ability_Rogue_Sprint", description = "Placeholder skill." },
    },
}

BINDING_HEADER_CUSTOMSKILLS = "Custom Skills"
BINDING_NAME_CUSTOMSKILLS_TOGGLE = "Toggle Skills Panel"

local ROW_HEIGHT = 44
local WIDTH = 360

function CustomSkills:OnSkillClick(skill)
    DEFAULT_CHAT_FRAME:AddMessage("Custom Skills: " .. skill.name .. " does nothing yet.")
end

local frame = CreateFrame("Frame", "CustomSkillsFrame", UIParent)
frame:SetWidth(WIDTH)
-- ponytail: fixed rows sized to the list, swap in a FauxScrollFrame once there are more skills than fit on screen
frame:SetHeight(60 + #CustomSkills.skills * ROW_HEIGHT)
frame:SetPoint("CENTER")
frame:SetToplevel(true)
frame:SetClampedToScreen(true)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
frame:EnableMouse(true)
frame:SetMovable(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetScript("OnShow", function() PlaySound("igCharacterInfoOpen") end)
frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose") end)
frame:Hide()
tinsert(UISpecialFrames, "CustomSkillsFrame") -- Escape closes it

local header = frame:CreateTexture(nil, "ARTWORK")
header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
header:SetWidth(256)
header:SetHeight(64)
header:SetPoint("TOP", 0, 12)

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", header, "TOP", 0, -14)
title:SetText("Skills")

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

for i, skill in ipairs(CustomSkills.skills) do
    local y = -36 - (i - 1) * ROW_HEIGHT
    local row = CreateFrame("Button", nil, frame)
    row:SetHeight(ROW_HEIGHT)
    row:SetPoint("TOPLEFT", 18, y)
    row:SetPoint("TOPRIGHT", -18, y)
    row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    row:SetScript("OnClick", function() CustomSkills:OnSkillClick(skill) end)

    local icon = row:CreateTexture(nil, "ARTWORK")
    icon:SetWidth(36)
    icon:SetHeight(36)
    icon:SetPoint("LEFT", 4, 0)
    icon:SetTexture(skill.icon)

    local name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    name:SetPoint("TOPLEFT", icon, "TOPRIGHT", 8, -2)
    name:SetText(skill.name)

    local description = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    description:SetPoint("TOPLEFT", name, "BOTTOMLEFT", 0, -4)
    description:SetWidth(WIDTH - 100)
    description:SetJustifyH("LEFT")
    description:SetText(skill.description)
end

function CustomSkills_Toggle()
    if frame:IsShown() then
        frame:Hide()
    else
        frame:Show()
    end
end

SLASH_CUSTOMSKILLS1 = "/skills"
SlashCmdList.CUSTOMSKILLS = CustomSkills_Toggle
