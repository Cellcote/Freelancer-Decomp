# CMake build of alchemy.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(alchemy DLL
  SOURCES
    alchemy.cpp
    alchemy_types.cpp
    alchemy_common.cpp
    alchemy_math.cpp
    alchemy_alchemy.cpp
    alchemy_fxruntime.cpp
    alchemy_effectarchetype.cpp
    alchemy_effectobject.cpp
    alchemy_float_forward.cpp
    alchemy_effectlibrary.cpp
    alchemy_genericfx.cpp
    alchemy_fields.cpp
    alchemy_emitters.cpp
    alchemy_appearances.cpp
    alchemy_ramp.cpp
    alchemy_nodelibrary.cpp
    alchemy_factory.cpp
    alchemy_material.cpp
    alchemy.rc
)


# x87 with /fp:fast and /Oi in every configuration: the retail float code (alchemy_math.cpp).
fl_item(alchemy_math.cpp
  BasicRuntimeChecks Default Optimization MaxSpeed IntrinsicFunctions true
  FloatingPointModel Fast EnableEnhancedInstructionSet NoExtensions SupportJustMyCode false
  DebugInformationFormat ProgramDatabase)
fl_item(alchemy_float_forward.cpp
  BasicRuntimeChecks Default Optimization MaxSpeed DebugInformationFormat ProgramDatabase
  SupportJustMyCode false EnableEnhancedInstructionSet StreamingSIMDExtensions2)

fl_link(alchemy LIBS dacom.lib REFERENCES dacom)
