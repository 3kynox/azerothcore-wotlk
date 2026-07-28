#include "player-guild-api.h"
#include "libsidecar.h"

extern void panicWithTC9Unavailable(const char* message);

void SetSetPlayerGuildFieldsHandler(SetPlayerGuildFieldsHandler h) { panicWithTC9Unavailable("SetSetPlayerGuildFieldsHandler"); }

SetPlayerGuildFieldsResponse CallSetPlayerGuildFieldsHandler(uint64_t player_guid, uint32_t guild_id, uint32_t rank) {
    panicWithTC9Unavailable("CallSetPlayerGuildFieldsHandler");
    SetPlayerGuildFieldsResponse r = { PlayerGuildErrorCodeNoHandler, false };
    return r;
}
