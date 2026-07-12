# AzerothCore Playerbots on ToCloud9 (cluster mode)

This branch runs [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) inside [ToCloud9](https://github.com/walkline/ToCloud9) clustered worldservers: random bots live on every shard, are visible cluster-wide (`/who`, name queries, group frames) and real players can group with them across shards.

Status: experimental, tested on a 3-shard setup (Eastern Kingdoms / Kalimdor / Outland+Northrend). Discussion: `#playerbots-integration` on the ToCloud9 Discord.

## Repositories

| Repo | Branch | Contains |
|---|---|---|
| this repo | `Playerbot-cluster-nav5` | AC core: mod-playerbots core patches merged with walkline cluster-mode patches, plus sidecar wrappers used by the module (NATS publish/subscribe, group service client) |
| [3kynox/mod-playerbots](https://github.com/3kynox/mod-playerbots) | `integration/kp-nav5` | cluster awareness for bots: per-shard bot pool partition, cross-shard handoff, group invite accept, follow when grouped with a real player |
| [3kynox/ToCloud9](https://github.com/3kynox/ToCloud9) | `integration/kp-20260708` | ToCloud9 with the playerbots-specific parts not (yet) upstreamed: in-process session events in libsidecar, event envelope fix. Generic fixes and features are submitted upstream (merged so far: #39, #40, #41, #42, #44) |

The long-term goal is to shrink the ToCloud9 fork to zero as pieces get upstreamed. The AC and mod-playerbots forks are permanent: upstream mod-playerbots targets a standalone worldserver by design.

Note on the base: the mod-playerbots branch sits on top of the navigation/RPG overhaul from [mod-playerbots `feature/new_rpg_and_nav_5_bash`](https://github.com/mod-playerbots/mod-playerbots/tree/feature/new_rpg_and_nav_5_bash) (work by bash/hermensbas, not merged upstream yet), and the AC branch includes the matching mmaps core changes ([PR #204](https://github.com/mod-playerbots/azerothcore-wotlk/pull/204)). This is the combination we actually run and test. Once that work lands upstream, the branches will rebase onto plain master.

## How it works

- Bots are in-process world sessions, exactly like vanilla mod-playerbots. No extra service.
- libsidecar (native C++, ToCloud9 #38) gives the module NATS pub/sub and a group service client, so bot logins/logouts and character updates reach the cluster like real players do.
- Each shard only spawns bots for the maps it serves (partition follows the standard ToCloud9 `availableMaps` assignment). Teleporting a bot to a map served by another shard hands it off cleanly.
- Group invites from real players are accepted by the shard owning the bot (the invite event travels over NATS); grouped bots switch to follow like vanilla playerbots.

## Build

Build libsidecar first (from the ToCloud9 fork checkout):

```bash
cmake -S game-server/libsidecar-cpp -B build-sidecar \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
    -DUSE_SYSTEM_GRPC=OFF -DUSE_SYSTEM_PROTOBUF=OFF
cmake --build build-sidecar -j $(nproc)
```

Then AzerothCore (this repo), with the module cloned into `modules/` and the freshly built `libsidecar.so` dropped into `deps/libsidecar/` (headers are already vendored):

```bash
git clone -b integration/kp-nav5 https://github.com/3kynox/mod-playerbots.git modules/mod-playerbots
cp <tocloud9>/build-sidecar/libsidecar.so deps/libsidecar/libsidecar.so
cd bin
cmake .. -DUSE_REAL_LIBSIDECAR=ON <your usual AC flags>
make -j$(nproc)
```

Notes:
- the C++ libsidecar has SONAME `libsidecar.so.0` — install it under that name where the loader finds it (`/usr/lib/libsidecar.so.0`).
- without `-DUSE_REAL_LIBSIDECAR=ON` you get stubs: playerbots work, cluster mode off (plain vertical server).
- Go services: build them from the ToCloud9 fork branch (one source tree for services and libsidecar, no mixing).

## Setup

Follow the upstream ToCloud9 setup (docker-compose or manual) with these additions:

1. Run the worldservers built from this repo.
2. `dbimport` needs the `acore_playerbots` database (module SQL is under `modules/mod-playerbots/data/sql`); all shards share it, like the other AC databases.
3. Give each worldserver its map set through the usual ToCloud9 config (`availableMaps`, comma-separated). The bot pool partition follows it, nothing else to declare.
4. In `playerbots.conf`, on every playerbots-enabled shard, set the union of the maps served by those shards:

```ini
AiPlayerbot.ClusterBotMaps = "0,1"
```

(Example: bots enabled on the two continent shards only. A shard bench-logs-out any bot standing on a map outside its own assignment, so keep this list consistent with the per-shard `availableMaps`.)

5. Everything else in `playerbots.conf` is standard mod-playerbots (bot count, levels, etc.).
6. Standard extracted client data works; regenerating mmaps with this branch's generator (steep-slope marking, see PR #204) improves bot navigation but is optional.

## Known limitations

- Guild features are not supported yet (ToCloud9 itself doesn't implement guild creation; bot guilds are disabled territory for now).
- Dungeons, battlegrounds and raids with bots: untested in cluster mode.
- A grouped bot can only follow its master while both are on the same shard.
- Group member stats for bots don't flow through the gateway pipeline yet (bots have no gateway session).

## Reporting

- Bot behaviour in cluster mode → issues on [3kynox/mod-playerbots](https://github.com/3kynox/mod-playerbots/issues)
- Core merge / build issues → issues here
- Anything ToCloud9-generic → [upstream](https://github.com/walkline/ToCloud9/issues)
- Or just ask in `#playerbots-integration` on the ToCloud9 Discord.

## License

AGPL v3, inherited from AzerothCore and mod-playerbots.
