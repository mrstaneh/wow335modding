// wow335modding: the browser build has no Logitech G-series keyboard LCD, so CGLCD (LCD.cpp) is a no-op there.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "WoW/Source/Ui/UILCD/LCD.h"

CEzLcd* CGLCD::m_lcd;
int32_t CGLCD::m_chatAlertLines;
uint8_t CGLCD::initComplete;
CGPlayer_C* CGLCD::m_player;
uint8_t CGLCD::m_inWorld;
TSHashTable<LCDHANDLE, HASHKEY_CONSTSTRI> CGLCD::m_handles;
TSList<MAPDATA, TSGetLink<MAPDATA> > CGLCD::m_mapData;
TSList<POIIcon, TSGetLink<POIIcon> > CGLCD::m_POIIcons;

void LCDHANDLE::SetText(const char*) {}
void CGLCD::Logout() {}
void CGLCD::ClearWaitQueueScreen() {}
void CGLCD::IdleDisconnectWarning() {}
void CGLCD::Initialize() {}
void CGLCD::SetScreen(int32_t) {}
void CGLCD::ShowBattlefieldScreen() {}
void CGLCD::HideBattlefieldAlert() {}
void CGLCD::OnBattlefieldReady(const BattlefieldStatus*) {}
void CGLCD::OnBattlefieldActive() {}
void CGLCD::UpdateBattlefieldStats() {}
void CGLCD::Shutdown() {}
int32_t CGLCD::Update(const void*, void*) { return 1; }
void CGLCD::OnWhisper(const char*) {}
void CGLCD::AuctionOutbid(const char*) {}
void CGLCD::AuctionExpired(const char*) {}
void CGLCD::AuctionSold(const char*) {}
void CGLCD::AuctionWon(const char*) {}
void CGLCD::PushAlert(const char*, int32_t) {}
void CGLCD::Login() {}
void* CGLCD::GetHandle(const char*) { return nullptr; }
int32_t CGLCD::GetScreenFromName(const char*) { return 0; }
void CGLCD::CheckInput() {}
void CGLCD::UpdateMapPositions() {}
void CGLCD::SetStat(const char*, int32_t) {}
void CGLCD::SetStat(const char*, float, bool) {}
void CGLCD::SetStat(const char*, int32_t, int32_t) {}
void CGLCD::SetStat(const char*, const char*) {}
void CGLCD::SetHandleText(const char*, const char*) {}
void CGLCD::DisplayMeleeStats() {}
void CGLCD::DisplayRangedStats() {}
void CGLCD::DisplayDefenseStats() {}
void CGLCD::DisplaySpellStats() {}
void CGLCD::SetAlertText() {}
void CGLCD::PostInit() {}
bool CGLCD::LoadLCDMap(int32_t, XMLNode*, CStatus*, int32_t) { return false; }
bool CGLCD::LoadLCDText(int32_t, XMLNode*, CStatus*, int32_t) { return false; }
bool CGLCD::LoadLCDBitmap(int32_t, XMLNode*, CStatus*) { return false; }
bool CGLCD::LoadLCDElement(int32_t, XMLNode*, CStatus*, int32_t) { return false; }
bool CGLCD::LoadGlobals(XMLNode*, CStatus*) { return false; }
bool CGLCD::GeneratePOIs(XMLNode*) { return false; }
bool CGLCD::LoadScreen(XMLNode*, CStatus*) { return false; }
bool CGLCD::LoadLayout() { return false; }
void CGLCD::EnterWorld() {}
void CGLCD::LeaveWorld() {}

#endif
