# Freelancer Decomp: builds every module of the game from src\ into bin\ (bin_debug\,
# binasan\). See docs/building.md.
#
#   cmake --preset release
#   cmake --build --preset release

# 3.27: `cmake -E env --modify` (the DX8 include order, cmake/Freelancer.cmake).
cmake_minimum_required(VERSION 3.27)

project(FreelancerDecomp LANGUAGES C CXX RC)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Freelancer)

# Ordered so that every project is defined before the projects that link it.
set(FL_PROJECTS
  dacom zlib qhull DALib Common Server Content
  EngBase ReadFile alchemy Deformable2 x86math luaprofile system rpclocal Thorn
  SoundManager SoundStreamer RP8 RP9 RendComp Shading flmaterials ximage movie imeui
  freelancer RemoteClient RemoteServer
  Resources/resources Resources/InfoCards Resources/MiscText Resources/NameResources
  Resources/EquipResources Resources/OfferBribeResources Resources/MiscTextInfo2
  Resources/serverresources
  FLServer MemoryDiagnostics FLRegress)
foreach(_project IN LISTS FL_PROJECTS)
  add_subdirectory(src/${_project})
endforeach()
