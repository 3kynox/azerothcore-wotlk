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
#include "petition-api.h"
#include "player-interactions-api.h"
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

/* Online status notifications for in-process sessions (e.g. server-side
 * bots). Sessions that log in through a gateway already get these events
 * published by the gateway itself — only call these for sessions WITHOUT
 * a gateway connection, otherwise events are duplicated. The sidecar
 * fills RealmID and uses its servers-registry ID as GatewayID so that
 * charserver purges these entries when this game server dies. */
void TC9CharacterLoggedIn(uint64_t charGUID, const char* charName, uint8_t charRace, uint8_t charClass, uint8_t charGender, uint8_t charLevel, uint32_t charZone, uint32_t charMap, float charPosX, float charPosY, float charPosZ, uint32_t charGuildID, uint32_t accountID);
void TC9CharacterLoggedOut(uint64_t charGUID, const char* charName, uint32_t charGuildID, uint32_t accountID);

/* Post-login field updates for in-process sessions. Batched and merged
 * per character (same barrier semantics as the gateway) and published as
 * gw.char.chars-updates. Same rule as above: only call for sessions
 * WITHOUT a gateway connection. */
void TC9CharacterZoneChanged(uint64_t charGUID, uint32_t mapID, uint32_t areaID, uint32_t zoneID);
void TC9CharacterLevelChanged(uint64_t charGUID, uint8_t level);

/* Generic NATS pub/sub for in-process extensions. Subscribe callbacks run
 * on the thread that calls TC9ProcessEventsHooks. Call after TC9InitLib.
 * Both return 0 on success, -1 on error. */
typedef void (*TC9NatsMessageHandler)(const char* subject, const char* payload, int payloadLen);
int TC9NatsPublish(const char* subject, const char* payload, int payloadLen);
int TC9NatsSubscribe(const char* subject, TC9NatsMessageHandler handler);

/* Group operations for in-process sessions (no gateway to call the group
 * service on their behalf). Blocking gRPC call, do not call from map update
 * threads. Returns 0 on success, -1 on error. */
int TC9GroupAcceptInvite(uint64_t playerGUID);
int TC9GroupLeave(uint64_t playerGUID);

/* Guild creation through the guild service (it allocates the id, inserts
 * guild/ranks/leader rows, hydrates its cache and publishes guild.created).
 * Blocking gRPC call, do not call from map update threads. Returns 0 on
 * success and stores the created guild id in *guildID, -1 on error. */
int TC9GuildCreate(uint64_t leaderGUID, const char* name, uint64_t* guildID);
int TC9GuildAcceptInvite(uint64_t guid, const char* name, uint32_t lvl,
    uint32_t race, uint32_t classID, uint32_t gender, uint32_t areaID, uint64_t accountID);

/* Matchmaking notifications */
void TC9PlayerLeftBattleground(uint64_t playerGUID, uint32_t realmID, uint32_t instanceID);
void TC9BattlegroundStatusChanged(uint32_t instanceID, uint8_t status);

/* Query the queue slot assigned to an invited player (matchmaking owns the BG
 * queues cluster-wide; in-process sessions have no gateway to accept invites).
 * outIsAssignedToThisServer is 1 when the assigned battleground runs on THIS
 * worldserver. Blocking gRPC call, do not call from map update threads.
 * 0 on success, -1 on error or when no battleground is assigned yet. */
int TC9BattlegroundQueueDataForLocalPlayer(uint64_t playerGUID, uint32_t* outBgTypeID,
    uint32_t* outInstanceID, uint32_t* outMapID, int* outIsAssignedToThisServer);

/* Confirm to matchmaking that an in-process player entered the battleground
 * (the gateway does this for real players after AddPlayersToBattleground).
 * Blocking gRPC call, do not call from map update threads. 0 on success. */
int TC9PlayerJoinedBattleground(uint64_t playerGUID, uint32_t instanceID);

/* Enqueue a solo in-process player into a battleground queue — the same RPC
 * the gateway issues for real players (pvpTeamID: 1 alliance, 2 horde).
 * Blocking gRPC call, do not call from map update threads. 0 on success. */
int TC9EnqueueLocalPlayerToBattleground(uint64_t playerGUID, uint32_t playerLvl,
    uint32_t bgTypeID, uint32_t pvpTeamID);

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
void TC9SetOnGuildCreatedHook(OnGuildCreatedHook h);

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

void TC9SetCanTurnInGuildPetitionHandler(CanTurnInGuildPetitionHandler h);

#ifdef __cplusplus
}
#endif

#endif /* __LIBSIDECAR_H__ */
