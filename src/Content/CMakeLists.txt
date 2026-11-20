# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(Content DLL DEF Content.def
  SOURCES
    Content.cpp
    ContentDataPath.cpp
    ContentMessageRouter.cpp
    Support.cpp
    GameSupport.cpp
    SpaceSupport.cpp
    BaseSupport.cpp
    GenericScripts.cpp
    Voices.cpp
    PlayerState.cpp
    BaseDB.cpp
    BaseNpcRecords.cpp
    LootProps.cpp
    Populators.cpp
    Story.cpp
    StoryLauncher.cpp
    StoryMissions.cpp
    MissionObject.cpp
    MissionScript.cpp
    MissionCnd.cpp
    MissionAct.cpp
    RandomMissionDB.cpp
    RandomEncounters.cpp
    RandomMissionRuntime.cpp
    RandomMissionTriggers.cpp
    RandomMissionMachine.cpp
    RandomMissionVignette.cpp
    RandomMissionGen.cpp
    Encounters.cpp
    SystemInfo.cpp
    Zones.cpp
    PatrolPaths.cpp
    EncounterParams.cpp
    Guides.cpp
    PilotPersonalities.cpp
    NpcShips.cpp
    NpcBase.cpp
    BehaviorCmds.cpp
    NpcCreate.cpp
    NpcShip.cpp
    SystemConnections.cpp
    TradelaneRequests.cpp
    CombatReputation.cpp
    DestroyMission.cpp
    FactionProps.cpp
    DockQueues.cpp
    Cargo.cpp
    MissionObjList.cpp
    Gcs.cpp
    SpacePaths.cpp
    NpcNames.cpp
    GotoGuides.cpp
    RtcScripts.cpp
    MissionDialogs.cpp
    MissionSave.cpp
    SpawnPlacement.cpp
    PatrolGuides.cpp
)

fl_link(Content LIBS Server.lib Common.lib dacom.lib REFERENCES Server Common dacom)
