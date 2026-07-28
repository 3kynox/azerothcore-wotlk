#ifndef __LIBSIDECAR_H__
#define __LIBSIDECAR_H__

#include <stdint.h>
#include <stdbool.h>

/* Include all API headers */
#include "battleground-api.h"
#include "events-group.h"
#include "events-guild.h"
#include "events-servers-registry.h"
#include "monitoring.h"
#include "player-interactions-api.h"
#include "petition-api.h"
#include "player-guild-api.h"
#include "player-items-api.h"
#include "player-money-api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Main library functions */
void TC9InitLib(uint16_t port, uint32_t realmID, uint8_t isCrossRealm, char* availableMaps, uint32_t** assignedMaps, int* assignedMapsSize);
void TC9GracefulShutdown();
void TC9ProcessGRPCOrHTTPRequests();
void TC9ProcessEventsHooks();

/* GUID generation */
uint64_t TC9GetNextAvailableCharacterGuid(int realmID);
uint64_t TC9GetNextAvailableItemGuid(int realmID);
uint64_t TC9GetNextAvailableInstanceGuid(int realmID);

/* Map loading notification */
void TC9ReadyToAcceptPlayersFromMaps(uint32_t* maps, int mapsLen);

/* Generic NATS pub/sub for in-process extensions. Subscribe callbacks run
 * on the thread that calls TC9ProcessEventsHooks. Call after TC9InitLib.
 * Both return 0 on success, -1 on error. */
typedef void (*TC9NatsMessageHandler)(const char* subject, const char* payload, int payloadLen);
int TC9NatsPublish(const char* subject, const char* payload, int payloadLen);
int TC9NatsSubscribe(const char* subject, TC9NatsMessageHandler handler);

/* Matchmaking notifications */
void TC9PlayerLeftBattleground(uint64_t playerGUID, uint32_t realmID, uint32_t instanceID);
void TC9BattlegroundStatusChanged(uint32_t instanceID, uint8_t status);

/* Event hooks registration */
void TC9SetOnGroupCreatedHook(OnGroupCreatedHook h);
void TC9SetOnGroupMemberAddedHook(OnGroupMemberAddedHook h);
void TC9SetOnGroupMemberRemovedHook(OnGroupMemberRemovedHook h);
void TC9SetOnGroupDisbandedHook(OnGroupDisbandedHook h);
void TC9SetOnGroupLootTypeChangedHook(OnGroupLootTypeChangedHook h);
void TC9SetOnGroupDungeonDifficultyChangedHook(OnGroupDungeonDifficultyChangedHook h);
void TC9SetOnGroupRaidDifficultyChangedHook(OnGroupRaidDifficultyChangedHook h);
void TC9SetOnGroupConvertedToRaidHook(OnGroupConvertedToRaidHook h);

void TC9SetOnGuildMemberAddedHook(OnGuildMemberAddedHook h);
void TC9SetOnGuildMemberRemovedHook(OnGuildMemberRemovedHook h);
void TC9SetOnGuildMemberLeftHook(OnGuildMemberLeftHook h);

void TC9SetOnMapsReassignedHook(OnMapsReassignedHook h);

/* Handler registration for gRPC requests */
void TC9SetBattlegroundStartHandler(BattlegroundStartHandler h);
void TC9SetBattlegroundAddPlayersHandler(BattlegroundAddPlayersHandler h);
void TC9SetCanPlayerJoinBattlegroundQueueHandler(CanPlayerJoinBattlegroundQueueHandler h);
void TC9SetCanPlayerTeleportToBattlegroundHandler(CanPlayerTeleportToBattlegroundHandler h);

void TC9SetMonitoringDataCollectorHandler(MonitoringDataCollectorHandler h);

void TC9SetCanPlayerInteractWithNPCAndFlagsHandler(CanPlayerInteractWithNPCAndFlagsHandler h);
void TC9SetCanPlayerInteractWithGOAndTypeHandler(CanPlayerInteractWithGOAndTypeHandler h);

void TC9SetGetPlayerItemsByGuidsHandler(GetPlayerItemsByGuidsHandler h);
void TC9SetRemoveItemsWithGuidsFromPlayerHandler(RemoveItemsWithGuidsFromPlayerHandler h);
void TC9SetAddExistingItemToPlayerHandler(AddExistingItemToPlayerHandler h);

void TC9SetGetMoneyForPlayerHandler(GetMoneyForPlayerHandler h);
void TC9SetModifyMoneyForPlayerHandler(ModifyMoneyForPlayerHandler h);

/* Kernel Panik extensions (in-process bots, guilds, guild bank, BG) */
void TC9CharacterLoggedIn(uint64_t charGUID, const char* charName, uint8_t charRace, uint8_t charClass, uint8_t charGender, uint8_t charLevel, uint32_t charZone, uint32_t charMap, float charPosX, float charPosY, float charPosZ, uint32_t charGuildID, uint32_t accountID);
void TC9CharacterLoggedOut(uint64_t charGUID, const char* charName, uint32_t charGuildID, uint32_t accountID);
void TC9CharacterZoneChanged(uint64_t charGUID, uint32_t mapID, uint32_t areaID, uint32_t zoneID);
void TC9CharacterLevelChanged(uint64_t charGUID, uint8_t level);
int TC9GroupAcceptInvite(uint64_t playerGUID);
int TC9GroupLeave(uint64_t playerGUID);
int TC9GuildCreate(uint64_t leaderGUID, const char* name, uint64_t* guildID);
int TC9GuildAcceptInvite(uint64_t guid, const char* name, uint32_t lvl,
    uint32_t race, uint32_t classID, uint32_t gender, uint32_t areaID, uint64_t accountID);
int TC9BattlegroundQueueDataForLocalPlayer(uint64_t playerGUID, uint32_t* outBgTypeID,
    uint32_t* outInstanceID, uint32_t* outMapID, int* outIsAssignedToThisServer);
int TC9PlayerJoinedBattleground(uint64_t playerGUID, uint32_t instanceID);
int TC9EnqueueLocalPlayerToBattleground(uint64_t playerGUID, uint32_t playerLvl,
    uint32_t bgTypeID, uint32_t pvpTeamID);
void TC9SetOnGuildCreatedHook(OnGuildCreatedHook h);
void TC9SetGetPlayerItemByPosHandler(GetPlayerItemByPosHandler h);
void TC9SetSetPlayerGuildFieldsHandler(SetPlayerGuildFieldsHandler h);
void TC9SetCanTurnInGuildPetitionHandler(CanTurnInGuildPetitionHandler h);

#ifdef __cplusplus
}
#endif

#endif /* __LIBSIDECAR_H__ */
